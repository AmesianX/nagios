/*
 * Simple test-program to try multiplexing running other programs
 * through the worker process layer.
 */

#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include "worker.h"

static int sigreceived;
static iobroker_set *iobs;

static void die(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

static void sighandler(int sig)
{
	sigreceived = sig;
	printf("%d: caught sig %d (%s)\n", getpid(), sig, strsignal(sig));
}

static int print_input(int sd, int events, void *wp_)
{
	int ret, pkt = 0;
	worker_process *wp = (worker_process *)wp_;
	kvvec *kvv;
	char *buf;
	unsigned long old_offset, tot_bytes = 0, size;

	ret = iocache_read(wp->ioc, sd);
	if (!ret) {
		printf("main: Worker with pid %d seems to have crashed. Exiting\n", wp->pid);
		exit(1);
	}
	printf("main: read %d bytes from worker with pid %d::\n",
		   ret, wp->pid);
	old_offset = wp->ioc->ioc_offset;
	while ((buf = iocache_use_delim(wp->ioc, MSG_DELIM, MSG_DELIM_LEN, &size))) {
		int i;
		tot_bytes += size;
		kvv = buf2kvvec(buf, (unsigned int)size, '=', 0);
		if (!kvv) {
			fprintf(stderr, "main: Failed to parse buffer to key/value vector");
			continue;
		}
		for (i = 0; i < kvv->kv_pairs; i++) {
			struct key_value *kv = kvv->kv[i];
			printf("%2d.%02d: %s=%s\n", pkt, i, kv->key, kv->value);
		}
		pkt++;
	}
	if (tot_bytes != ret)
		printf("tot_bytes: %ld; size: %d\n", tot_bytes, ret);

	return 0;
}

#define NWPS 7
static worker_process *wps[NWPS];
static int wp_index;

static int send_command(int sd, int events, void *discard)
{
	char buf[8192];
	int ret;
	worker_process *wp;
	struct kvvec *kvv;

	ret = read(sd, buf, sizeof(buf));
	if (ret == 0) {
		iobroker_close(iobs, sd);
		return 0;
	}
	if (ret < 0) {
		printf("main: Failed to read() from fd %d: %s",
			   sd, strerror(errno));
	}

	/* this happens when we're reading from stdin */
	buf[--ret] = 0;

	kvv = kvvec_init(1);
	kvvec_addkv_wlen(kvv, "command", sizeof("command") - 1, buf, ret);
	wp = wps[wp_index++ % NWPS];
	send_kvvec(wp->sd, kvv);
	kvvec_destroy(kvv, 0);
	return 0;
}

void print_some_crap(void *arg)
{
	char *str = (char *)arg;

	printf("%d: Argument passed: %s\n", getpid(), str);
}

int main(int argc, char **argv)
{
	struct worker_process *wp;
	int i;

	signal(SIGINT, sighandler);
	signal(SIGPIPE, sighandler);

	iobs = iobroker_create();
	if (!iobs)
		die("Failed to create io broker set");

	for (i = 0; i < NWPS; i++) {
		wp = spawn_worker(print_some_crap, "lalala");
		if (!wp) {
			die("Failed to spawn worker(s)\n");
		}
		wps[i] = wp;
		printf("Registering worker sd %d with io broker\n", wp->sd);
		iobroker_register(iobs, wp->sd, wp, print_input);
	}

	iobroker_register(iobs, fileno(stdin), NULL, send_command);

	/* get to work */
	while (!sigreceived && iobroker_get_num_fds(iobs)) {
		iobroker_poll(iobs, -1);
	}

	for (i = 0; i < NWPS; i++) {
		kill(wps[i]->pid, SIGKILL);
	}

	return 0;
}
