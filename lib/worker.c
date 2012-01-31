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
	struct child_process *prev_cp, *next_cp;
} child_process;

static iobroker_set *iobs;
static child_process *first_cp, *last_cp;
static unsigned int started, running_jobs;
static int master_sd;

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
	len = vsnprintf(&lmsg[4], sizeof(lmsg) - 8, fmt, ap);
	va_end(ap);
	if (len < 0 || len >= sizeof(lmsg) - 4)
		return;
	len += 4;
	/* double null termination */
	lmsg[len++] = 0; lmsg[len++] = 0;
	write(master_sd, lmsg, len);
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
	kvvb = kvvec2buf(kvv, '=', '\0', 2);
	if (!kvvb) {
		/*
		 * XXX: do *something* sensible here to let the
		 * master know we failed, although the most likely
		 * reason is OOM, in which case the OOM-slayer will
		 * probably kill us sooner or later.
		 */
		return;
	}

	/*
	 * use "bufsize" rather than buflen here, as the latter gets
	 * us the two delimiting nul's
	 */
	ret = write(sd, kvvb->buf, kvvb->bufsize);
	if (ret < 0) {
		/* XXX: do something sensible here */
	}
	free(kvvb->buf);
	free(kvvb);
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

static int check_completion(child_process *cp, int flags)
{
	int result, status;

	wlog("checking completion for command '%s' with pid %d", cp->cmd, cp->pid);
	if (!cp || !cp->pid) {
		return 0;
	}

	result = wait4(cp->pid, &status, flags, &cp->rusage);
	if (result == -1) {
		/* XXX: handle this better */
	}

	if (result == cp->pid || errno == ECHILD) {
		struct kvvec *resp;
		struct rusage *ru = &cp->rusage;
		char *buf;
		child_process *prev, *next;

		resp = kvvec_init(12); /* how many key/value pairs do we need? */

		gettimeofday(&cp->stop, NULL);

		/* get rid of child's filedescriptors */
		iobroker_close(iobs, cp->outstd.fd);
		iobroker_close(iobs, cp->outerr.fd);
		cp->outstd.fd = -1;
		cp->outerr.fd = -1;

		cp->runtime = tv_delta_f(&cp->start, &cp->stop);
		cp->ret = status;
		cp->pid = 0;

		/* now build the return message */
		kvvec_addkv(resp, "job_id", (char *)mkstr("%u", cp->id));
		kvvec_addkv(resp, "wait_status", (char *)mkstr("%d", cp->ret));
		kvvec_addkv_wlen(resp, "stdout", 6, cp->outstd.buf, cp->outstd.len);
		kvvec_addkv_wlen(resp, "stderr", 6, cp->outerr.buf, cp->outerr.len);
		kvvec_add_tv(resp, "start", cp->start);
		kvvec_add_tv(resp, "stop", cp->stop);
		buf = (char *)mkstr("%f", cp->runtime);
		kvvec_addkv(resp, "runtime", buf);
		kvvec_add_tv(resp, "ru_utime", ru->ru_utime);
		kvvec_add_tv(resp, "ru_stime", ru->ru_stime);
		kvvec_add_long(resp, "ru_minflt", ru->ru_minflt);
		kvvec_add_long(resp, "ru_majflt", ru->ru_majflt);
		kvvec_add_long(resp, "ru_nswap", ru->ru_nswap);
		kvvec_add_long(resp, "ru_inblock", ru->ru_inblock);
		kvvec_add_long(resp, "ru_oublock", ru->ru_oublock);
		kvvec_add_long(resp, "ru_nsignals", ru->ru_nsignals);
		send_kvvec(master_sd, resp);

		/*
		 * we mustn't free() the key/value pairs here, as they're all
		 * stack-allocated
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

		/* now we remove this check from the list of running ones */
		next = cp->next_cp;
		prev = cp->prev_cp;
		if (next) {
			next->prev_cp = prev;
		} else {
			last_cp = prev;
		}
		if (prev) {
			prev->next_cp = next;
		} else {
			first_cp = next;
		}
		free(cp->cmd);
		free(cp);
	}

	return result;
}

static void gather_output(child_process *cp, iobuf *io)
{
	iobuf *other_io;

	other_io = io == &cp->outstd ? &cp->outerr : &cp->outstd;

	wlog("Gathering output from '%s' with pid %d", cp->cmd, cp->pid);
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
	if (last_cp) {
		last_cp->prev_cp = cp;
	}
	cp->next_cp = last_cp;
	last_cp = cp;

	/*
	 * we must copy from the vector, since it points to data
	 * found in the iocache where we read the command, which will
	 * be overwritten when we receive one next
	 */
	wlog("parsing %d key/value pairs\n", kvv->kv_pairs);
	for (i = 0; i < kvv->kv_pairs; i++) {
		char *key = kvv->kv[i]->key;
		char *value = kvv->kv[i]->value;
		char *endptr;
		wlog("parsing '%s=%s'\n", key ? key : "(null)", value ? value : "(null)");
		if (!strcmp(key, "command")) {
			cp->cmd = strdup(value);
			wlog("Found command: '%s'\n", cp->cmd);
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
		wlog("unknown key when parsing command: '%s=%s'\n", key, value);
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

	wlog("Parsing command");
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
	wlog("Successfully started '%s'. Started: %d; Running: %d",
		 cp->cmd, started, running_jobs);
}

static void exit_worker(void)
{
	/*
	 * XXX: check to make sure we have no children running. If
	 * we do, kill 'em all before we go home.
	 */
	exit(EXIT_SUCCESS);
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

	wlog("iocache_read() returned %d", ioc_ret);
	ioc->ioc_buf[ioc->ioc_buflen + ioc_ret] = 0;

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
	while ((buf = iocache_use_delim(ioc, "\0\0", 2, &size))) {
		kvvec *kvv;
		kvv = buf2kvvec(buf, size, '=', '\0');
		spawn_job(kvv);
		wlog("Destroying kvvec struct");
		kvvec_destroy(kvv, 0);
		wlog("Done destroying kvvec struct. Parsing next check");
	}
	wlog("Done parsing input from master");

	return 0;
}


static void enter_worker(int sd)
{
	/* created with socketpair(), usually */
	master_sd = sd;

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
		wlog("Polling iobroker socket set");
		iobroker_poll(iobs, -1);
	}

	/* not reached. we exit when the master closes our socket */
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
