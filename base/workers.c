/*
 * This file holds all nagios<->libnagios integration stuff, so that
 * libnagios itself is usable as a standalone library for addon
 * writers to use as they see fit.
 *
 * This means apis inside libnagios can be tested without compiling
 * all of Nagios into it, and that they can remain general-purpose
 * code that can be reused for other things later.
 */
#include "../include/nagios.h"
iobroker_set *nagios_iobs = NULL;
static worker_process **workers;
static unsigned int num_workers;
static unsigned int worker_index;

/*
 * This gets called from both parent and worker process, so
 * we must take care not to blindly shut down everything here
 */
void free_worker_memory(void)
{
	unsigned int i;

	for (i = 0; i < num_workers; i++) {
		my_free(workers[i]);
	}
	iobroker_destroy(nagios_iobs, 0);
	nagios_iobs = NULL;
}

/*
 * function workers call as soon as they come alive
 */
static void worker_init_func(void *arg)
{
	/*
	 * we pass 'arg' here to safeguard against
	 * changes in it since the worker spawned
	 */
	free_memory((nagios_macros *)arg);
}

static int handle_worker_result(int sd, int events, void *arg)
{
	worker_process *wp = (worker_process *)arg;

	iocache_read(wp->ioc, wp->sd);

	/* XXX FIXME! */

	return 0;
}

static int init_iobroker(void)
{
	if (!nagios_iobs)
		nagios_iobs = iobroker_create();

	if (nagios_iobs)
		return 0;
	return -1;
}

int init_workers(int desired_workers)
{
	worker_process **wps;
	int i;

	if (desired_workers == 0) {
		desired_workers = 4;
	}

	/* can't shrink the number of workers (yet) */
	if (desired_workers < num_workers)
		return -1;

	wps = malloc(sizeof(worker_process *) * desired_workers);
	if (!wps)
		return -1;

	if (workers) {
		if (num_workers < desired_workers) {
			for (i = 0; i < num_workers; i++) {
				wps[i] = workers[i];
			}
		}

		free(workers);
	}
	for (i = num_workers; i < desired_workers; i++) {
		worker_process *wp;


		wp = spawn_worker(worker_init_func, (void *)get_global_macros());
		if (!wp) {
			/* XXX: what to do? */
		} else {
			wps[i] = wp;
		}
	}

	init_iobroker();

	/*
	 * second pass. We do this in two rounds to avoid letting
	 * lately spawned workers inherit a lot of allocated memory
	 * used to set up their previously spawned brethren.
	 * XXX: Later we'll have to deal with that, to let workers
	 * respawn if they leak memory (embedded perl, anyone?),
	 * but for now that will have to wait.
	 */
	for (i = num_workers; i < desired_workers; i++) {
		worker_process *wp = wps[i];
		iobroker_register(nagios_iobs, wp->sd, wp, handle_worker_result);
		wp->ioc = iocache_create(128 * 1024);
	}
	num_workers = desired_workers;
	workers = wps;
	return 0;
}

static worker_process *get_worker(void)
{
	worker_process *wp = NULL;
	int i;

	if (!workers) {
		return NULL;
	}

	return workers[worker_index++ % num_workers];

	/* dead code below. for now */
	for (i = 0; i < num_workers; i++) {
		wp = workers[worker_index++ % num_workers];
		if (wp) {
			/*
			 * XXX: check worker flags so we don't ship checks to a
			 * worker that's about to reincarnate.
			 */
			return wp;
		}

		worker_index++;
		if (wp)
			return wp;
	}
}

int wproc_run_check(char *hname, char *sdesc, char *cmd, nagios_macros *mac)
{
	kvvec *kvv;
	worker_process *wp;

	kvv = kvvec_init(4);
	if (!kvv)
		return -1;

	kvvec_addkv(kvv, "type", "check");
	kvvec_addkv(kvv, "host_name", hname);
	if (sdesc) {
		kvvec_addkv(kvv, "service_description", sdesc);
	}

	kvvec_addkv(kvv, "command", cmd);

	/* should also add support for environment macros here */

	wp = get_worker();
	send_kvvec(wp->sd, kvv);
	kvvec_destroy(kvv, 0);
	return 0;
}
