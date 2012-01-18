/*
 * Simple test-program to try multiplexing the running of external commands
 * Note that signal(7) claims SIGCHLD to be set to SIG_IGN by default, but
 * when set so explicitly this program runs into all sorts of weird errors.
 *
 * A sighandler which just returns might be a way out, although it doesn't
 * seem to be.
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
	char buf[65536];
	int ret;
	worker_process *wp = (worker_process *)wp_;

	ret = read(sd, buf, sizeof(buf));
	buf[ret] = 0;
	printf("main: read %d bytes from worker with pid %d::\n",
		   ret, wp->pid);
	write(fileno(stdout), buf, ret);
	putchar('\n');
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
	struct kvvec_buf *kvvb;

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
	kvvb = kvvec2buf(kvv, '=', '\0', 2);
	kvvec_destroy(kvv, 0);
	wp = wps[wp_index++ % NWPS];
	write(wp->sd, kvvb->buf, kvvb->bufsize);
	return 0;
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
		wp = spawn_worker();
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
