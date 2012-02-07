#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include "runcmd.h"
#include "kvvec.h"
#include "iobroker.h"
#include "iocache.h"
#include "worker.h"

typedef struct iobuf
{
	int fd;
	unsigned int len;
	char *buf;
} iobuf;

typedef struct child_process {
	unsigned int id, timeout;
	char *cmd;
	pid_t pid;
	int ret;
	struct timeval start;
	struct timeval stop;
	float runtime;
	struct rusage rusage;
	iobuf outstd;
	iobuf outerr;
	kvvec *request;
} child_process;

static iobroker_set *iobs;
static unsigned int started, running_jobs;
static int master_sd;
static int parent_pid;

static void worker_die(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

/*
 * contains all information sent in a particular request
 */
struct request {
	char *cmd;
	int when;
	char **env;
	struct kvvec *request, *response;
};

static void exit_worker(void)
{
	/*
	 * XXX: check to make sure we have no children running. If
	 * we do, kill 'em all before we go home.
	 */
	exit(EXIT_SUCCESS);
}

/*
 * write a log message to master. This can almost certainly be
 * improved, but it works and we're not terribly concerned with
 * optimizing surrounding infrastructure just now.
 */
static void wlog(const char *fmt, ...)
{
	va_list ap;
	static char lmsg[8192] = "log=";
	int len;

	va_start(ap, fmt);
	len = vsnprintf(&lmsg[4], sizeof(lmsg) - (4 + MSG_DELIM_LEN), fmt, ap);
	va_end(ap);
	if (len < 0 || len >= sizeof(lmsg) - (4 + MSG_DELIM_LEN))
		return;

	len += 4; /* log= */

	/* add delimiter and send it. 1 extra as kv pair separator */
	memset(&lmsg[len], 0, MSG_DELIM_LEN + 1);
	if (write(master_sd, lmsg, len + MSG_DELIM_LEN + 1) < 0) {
		if (errno == EPIPE) {
			/* master has died or abandoned us, so exit */
			exit_worker();
		}
	}
}

static void job_error(child_process *cp, kvvec *kvv, const char *fmt, ...)
{
	char msg[4096];
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
	va_end(ap);
	kvvec_addkv_wlen(kvv, "error", 5, msg, len);
	send_kvvec(master_sd, kvv);
	kvvec_destroy(kvv, 0);
}

static float tv_delta_f(const struct timeval *start, const struct timeval *stop)
{
#define DIVIDER 1000000
	float ret;
	unsigned long usecs, stop_usec;

	ret = stop->tv_sec - start->tv_sec;
	stop_usec = stop->tv_usec;
	if (stop_usec < start->tv_usec) {
		ret -= 1.0;
		stop_usec += DIVIDER;
	}
	usecs = stop_usec - start->tv_usec;

	ret += (float)((float)usecs / DIVIDER);
	return ret;
}

#define MKSTR_BUFS 256 /* should be plenty */
const char *mkstr(const char *fmt, ...)
{
	static char buf[MKSTR_BUFS][32]; /* 8k statically on the stack */
	static int slot = 0;
	char *ret;
	va_list ap;

	ret = buf[slot++ & (MKSTR_BUFS - 1)];
	va_start(ap, fmt);
	vsnprintf(ret, sizeof(buf[0]), fmt, ap);
	va_end(ap);
	return ret;
}

void send_kvvec(int sd, struct kvvec *kvv)
{
	int ret;
	struct kvvec_buf *kvvb;

	/*
	 * key=value, separated by nul bytes and two nul's
	 * delimit one message from another
	 */
	kvvb = kvvec2buf(kvv, '=', '\0', MSG_DELIM_LEN);
	if (!kvvb) {
		/*
		 * XXX: do *something* sensible here to let the
		 * master know we failed, although the most likely
		 * reason is OOM, in which case the OOM-slayer will
		 * probably kill us sooner or later.
		 */
		return;
	}

	/* use bufsize here, as it gets us the nul string delimiter */
	ret = write(sd, kvvb->buf, kvvb->bufsize);
	free(kvvb->buf);
	free(kvvb);
	if (ret < 0) {
		if (errno == EPIPE) {
			/*
			 * master has crashed or abandoned us, so we die
			 * with what grace we can.
			 */
			exit_worker();
		}
	}
}

#define kvvec_add_long(kvv, key, value) \
	do { \
		char *buf = (char *)mkstr("%ld", value); \
		kvvec_addkv_wlen(kvv, key, sizeof(key) - 1, buf, strlen(buf)); \
	} while (0)

#define kvvec_add_tv(kvv, key, value) \
	do { \
		char *buf = (char *)mkstr("%ld.%06ld", value.tv_sec, value.tv_usec); \
		kvvec_addkv_wlen(kvv, key, sizeof(key) - 1, buf, strlen(buf)); \
	} while (0)

static int finish_job(child_process *cp, int reason)
{
	struct kvvec *resp;
	struct rusage *ru = &cp->rusage;
	int i;

	resp = kvvec_init(12 + cp->request->kv_pairs); /* how many key/value pairs do we need? */

	gettimeofday(&cp->stop, NULL);

	/* get rid of still open filedescriptors */
	if (cp->outstd.fd != -1)
		iobroker_close(iobs, cp->outstd.fd);
	if (cp->outerr.fd != -1)
		iobroker_close(iobs, cp->outerr.fd);

	cp->runtime = tv_delta_f(&cp->start, &cp->stop);

	/*
	 * Now build the return message.
	 * First comes the request, minus environment variables
	 */
	for (i = 0; i < cp->request->kv_pairs; i++) {
		struct key_value *kv = cp->request->kv[i];
		/* skip environment macros */
		if (kv->key_len == 3 && !strcmp(kv->key, "env")) {
			continue;
		}
		kvvec_addkv_wlen(resp, kv->key, kv->key_len, kv->value, kv->value_len);
	}
	kvvec_addkv(resp, "wait_status", (char *)mkstr("%d", cp->ret));
	kvvec_addkv_wlen(resp, "stdout", 6, cp->outstd.buf, cp->outstd.len);
	kvvec_addkv_wlen(resp, "stderr", 6, cp->outerr.buf, cp->outerr.len);
	kvvec_add_tv(resp, "start", cp->start);
	kvvec_add_tv(resp, "stop", cp->stop);
	kvvec_addkv(resp, "runtime", (char *)mkstr("%f", cp->runtime));
	if (!reason) {
		/* child exited */
		kvvec_add_tv(resp, "ru_utime", ru->ru_utime);
		kvvec_add_tv(resp, "ru_stime", ru->ru_stime);
		kvvec_add_long(resp, "ru_minflt", ru->ru_minflt);
		kvvec_add_long(resp, "ru_majflt", ru->ru_majflt);
		kvvec_add_long(resp, "ru_nswap", ru->ru_nswap);
		kvvec_add_long(resp, "ru_inblock", ru->ru_inblock);
		kvvec_add_long(resp, "ru_oublock", ru->ru_oublock);
		kvvec_add_long(resp, "ru_nsignals", ru->ru_nsignals);
	} else {
		kvvec_addkv(resp, "reason", (char *)mkstr("%d", reason));
	}
	send_kvvec(master_sd, resp);

	/*
	 * we mustn't free() the key/value pairs here, as they're all
	 * stack-allocated or located in the request thing
	 */
	kvvec_destroy(resp, 0);

	running_jobs--;
	if (cp->outstd.buf) {
		free(cp->outstd.buf);
		cp->outstd.buf = NULL;
	}
	if (cp->outerr.buf) {
		free(cp->outerr.buf);
		cp->outerr.buf = NULL;
	}

	kvvec_destroy(cp->request, KVVEC_FREE_ALL);
	free(cp->cmd);
	free(cp);

	return 0;
}

static int check_completion(child_process *cp, int flags)
{
	int result, status;

	if (!cp || !cp->pid) {
		return 0;
	}

	result = wait4(cp->pid, &status, flags, &cp->rusage);
	if (result == -1) {
		/* XXX: handle this better */
	}

	if (result == cp->pid || errno == ECHILD) {
		cp->ret = status;
		finish_job(cp, 0);
	}

	return result;
}

static void gather_output(child_process *cp, iobuf *io)
{
	iobuf *other_io;

	other_io = io == &cp->outstd ? &cp->outerr : &cp->outstd;

	for (;;) {
		char buf[4096];
		int rd;

		rd = read(io->fd, buf, sizeof(buf));
		if (rd < 0) {
			if (errno == EINTR)
				continue;
			/* XXX: handle the error somehow */
			check_completion(cp, WNOHANG);
		}

		if (rd) {
			/* we read some data */
			io->buf = realloc(io->buf, rd + io->len + 1);
			memcpy(&io->buf[io->len], buf, rd);
			io->len += rd;
			io->buf[io->len] = '\0';
		} else {
			iobroker_close(iobs, io->fd);
			io->fd = -1;
			if (other_io->fd < 0) {
				check_completion(cp, 0);
			} else {
				check_completion(cp, WNOHANG);
			}
		}
		break;
	}
}


static int stderr_handler(int fd, int events, void *cp_)
{
	child_process *cp = (child_process *)cp_;
	gather_output(cp, &cp->outerr);
	return 0;
}

static int stdout_handler(int fd, int events, void *cp_)
{
	child_process *cp = (child_process *)cp_;
	gather_output(cp, &cp->outstd);
	return 0;
}

static int fd_start_cmd(child_process *cp)
{
	int pfd[2], pfderr[2];

	cp->outstd.fd = np_runcmd_open(cp->cmd, pfd, pfderr, NULL);
	if (cp->outstd.fd == -1) {
		return -1;
	}
	gettimeofday(&cp->start, NULL);

	cp->outerr.fd = pfderr[0];
	cp->pid = runcmd_pid(cp->outstd.fd);
	iobroker_register(iobs, cp->outstd.fd, cp, stdout_handler);
	iobroker_register(iobs, cp->outerr.fd, cp, stderr_handler);

	return 0;
}

static iocache *ioc;

child_process *parse_command_kvvec(struct kvvec *kvv)
{
	int i;
	child_process *cp;

	/* get this command's struct and insert it at the top of the list */
	cp = calloc(1, sizeof(*cp));
	if (!cp) {
		wlog("Failed to calloc() a child_process struct");
		return NULL;
	}

	/*
	 * we must copy from the vector, since it points to data
	 * found in the iocache where we read the command, which will
	 * be overwritten when we receive one next
	 */
	for (i = 0; i < kvv->kv_pairs; i++) {
		char *key = kvv->kv[i]->key;
		char *value = kvv->kv[i]->value;
		char *endptr;

		if (!strcmp(key, "command")) {
			cp->cmd = strdup(value);
			continue;
		}
		if (!strcmp(key, "job_id")) {
			cp->id = (unsigned int)strtoul(value, &endptr, 0);
			continue;
		}
		if (!strcmp(key, "timeout")) {
			cp->timeout = (unsigned int)strtoul(value, &endptr, 0);
			continue;
		}
	}

	/* jobs without a timeout get a default of 300 seconds. */
	if (!cp->timeout) {
		cp->timeout = time(NULL) + 300;
	} else {
		/* timeout is passed in duration, but kept in absolute */
		cp->timeout += time(NULL) + 1;
	}
	return cp;
}

static void spawn_job(struct kvvec *kvv)
{
	int result;
	child_process *cp;

	cp = parse_command_kvvec(kvv);
	if (!cp) {
		job_error(NULL, kvv, "Failed to parse worker-command");
		return;
	}
	if (!cp->cmd) {
		job_error(cp, kvv, "Failed to parse commandline. Ignoring job %u", cp->id);
		return;
	}

	result = fd_start_cmd(cp);
	if (result < 0) {
		job_error(cp, kvv, "Failed to start child");
		return;
	}

	started++;
	running_jobs++;
	cp->request = kvv;
}

static int receive_command(int sd, int events, void *discard)
{
	int ioc_ret;
	char *buf;
	unsigned long size;

	if (!ioc) {
		ioc = iocache_create(65536);
	}
	ioc_ret = iocache_read(ioc, sd);

	/* master closed the connection, so we exit */
	if (ioc_ret == 0) {
		iobroker_close(iobs, sd);
		exit_worker();
	}
	if (ioc_ret < 0) {
		/* XXX: handle this somehow */
	}

	/*
	 * now loop over all inbound messages in the iocache,
	 * separated by double NUL's.
	 */
	while ((buf = iocache_use_delim(ioc, MSG_DELIM, MSG_DELIM_LEN, &size))) {
		kvvec *kvv;
		kvv = buf2kvvec(buf, size, '=', '\0');
		spawn_job(kvv);
	}

	return 0;
}


static void enter_worker(int sd)
{
	/* created with socketpair(), usually */
	master_sd = sd;
	parent_pid = getppid();

	if (setpgid(0, 0)) {
		/* XXX: handle error somehow, or maybe just ignore it */
	}

	fcntl(fileno(stdout), F_SETFD, FD_CLOEXEC);
	fcntl(fileno(stderr), F_SETFD, FD_CLOEXEC);
	fcntl(master_sd, F_SETFD, FD_CLOEXEC);
	iobs = iobroker_create();
	if (!iobs) {
		/* XXX: handle this a bit better */
		worker_die("Worker failed to create io broker socket set");
	}
	iobroker_register(iobs, master_sd, NULL, receive_command);
	while (iobroker_get_num_fds(iobs)) {
		iobroker_poll(iobs, -1);
		/*
		 * if our parent goes away we can't really do anything
		 * sensible at all, so let's just break out and exit
		 */
		if (kill(parent_pid, 0) < 0 && errno == ESRCH) {
			break;
		}
	}

	/* we exit when the master shuts us down */
	exit(EXIT_SUCCESS);
}

struct worker_process *spawn_worker(void (*init_func)(void *), void *init_arg)
{
	int sv[2];
	int pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		return NULL;

	pid = fork();
	if (pid < 0)
		return NULL;

	/* parent leaves the child */
	if (pid) {
		worker_process *worker = calloc(1, sizeof(worker_process));
		if (!worker) {
			kill(SIGKILL, pid);
			return NULL;
		}
		worker->sd = sv[0];
		worker->pid = pid;
		worker->ioc = iocache_create(65536);

		/* 1 socket for master, 2 fd's for each child */
		worker->max_jobs = (iobroker_max_usable_fds() - 1) / 2;
		worker->jobs = calloc(worker->max_jobs, sizeof(worker_job *));

		return worker;
	}

	if (init_func) {
		init_func(init_arg);
	}
	/* child closes parent's end of socket and gets busy */
	close(sv[0]);
	enter_worker(sv[1]);

	/* not reached, ever */
	exit(EXIT_FAILURE);
}
