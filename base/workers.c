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

/* different jobtypes. We add more as needed */
#define JOBTYPE_CHECK   0

static worker_job *create_job(int type, void *arg, time_t timeout, const char *command)
{
	worker_job *job;

	job = calloc(1, sizeof(*job));
	if (!job)
		return NULL;

	job->type = type;
	job->arg = arg;
	job->timeout = timeout;
	job->command = strdup(command);

	return job;
}

static int get_job_id(worker_process *wp)
{
	return wp->job_index++ % wp->max_jobs;

}

static worker_job *get_job(worker_process *wp, int job_id)
{
	/*
	 * XXX FIXME check job->id against job_id and do something if
	 * they don't match
	 */
	return wp->jobs[job_id % wp->max_jobs];
}

static void destroy_job(worker_job *job)
{
	if (!job)
		return;

	my_free(job->command);

	switch (job->type) {
	case JOBTYPE_CHECK:
		free_check_result(job->arg);
		free(job->arg);
		break;
	default:
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Workers: Unknown job type: %d\n", job->type);
		break;
	}
}

/*
 * This gets called from both parent and worker process, so
 * we must take care not to blindly shut down everything here
 */
void free_worker_memory(void)
{
	unsigned int i;

	for (i = 0; i < num_workers; i++) {
		if (!workers[i])
			continue;

		iocache_destroy(workers[i]->ioc);
		close(workers[i]->sd);
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

static int str2timeval(char *str, struct timeval *tv)
{
	char *ptr, *ptr2;

	tv->tv_sec = strtoul(str, &ptr, 10);
	if (ptr == str) {
		tv->tv_sec = tv->tv_usec = 0;
		return -1;
	}
	if (*ptr == '.' || *ptr == ',') {
		ptr2 = ptr + 1;
		tv->tv_usec = strtoul(ptr2, &ptr, 10);
	}
	return 0;
}

static int handle_check_result(check_result *cr)
{
	if (!cr->host_name)
		return -1;

	if (cr->service_description) {
		service *svc = find_service(cr->host_name, cr->service_description);
		if (!svc) {
			logit(NSLOG_RUNTIME_WARNING, TRUE,
			      "Checkresult from worker for unknown service '%s' on host '%s'\n",
			      cr->service_description, cr->host_name);
			return -1;
		}
		return handle_async_service_check_result(svc, cr);
	} else {
		host *hst = find_host(cr->host_name);
		if (!hst) {
			logit(NSLOG_RUNTIME_WARNING, TRUE,
			      "Checkresult from worker for unknown host '%s'\n",
			      cr->host_name);
			return -1;
		}
		return handle_async_host_check_result_3x(hst, cr);
	}

	return -1;
}

static int handle_worker_result(int sd, int events, void *arg)
{
	worker_process *wp = (worker_process *)arg;
	char *buf;
	unsigned long size;
	int ret;

	ret = iocache_read(wp->ioc, wp->sd);
	while ((buf = iocache_use_delim(wp->ioc, MSG_DELIM, MSG_DELIM_LEN, &size))) {
		kvvec *kvv;
		int i, is_check = 0;
		check_result cr;
		char *err_output = NULL;

		kvv = buf2kvvec(buf, size, '=', '\0');
		if (!kvv) {
			continue;
		}
		memset(&cr, 0, sizeof(cr));
		cr.object_check_type = HOST_CHECK;
		cr.check_type = HOST_CHECK_ACTIVE;
		cr.reschedule_check = TRUE;
		for (i = 0; i < kvv->kv_pairs; i++) {
			unsigned int job_id;
			char *endptr, *key, *value;
			int val;

			key = kvv->kv[i]->key;
			value = kvv->kv[i]->value;

			/*
			 * This if() else if() thing should be replaced
			 * with a list using binary lookups to make it
			 * go a bit faster, since we expect to run this
			 * routine several hundred thousand times per
			 * minute.
			 */
			if (!strcmp(key, "type")) {
				if (strcmp(value, "check")) {
					printf("Type isn't 'check'. Ignoring worker result\n");
					break;
				}
				is_check = 1;
			} else if (!strcmp(key, "job_id")) {
				job_id = strtoul(value, &endptr, 10);
			} else if (!strcmp(key, "host_name")) {
				cr.host_name = value;
			} else if (!strcmp(key, "service_description")) {
				cr.service_description = value;
				cr.object_check_type = SERVICE_CHECK;
				cr.check_type = SERVICE_CHECK_ACTIVE;
			} else if (!strcmp(key, "start")) {
				str2timeval(value, &cr.start_time);
			} else if (!strcmp(key, "stop")) {
				str2timeval(value, &cr.finish_time);
			} else if (!strcmp(key, "error")) {
				int val = atoi(value);
				if (val == ETIME) {
					cr.early_timeout = 1;
				}
			} else if (!strcmp(key, "stdout")) {
				cr.output = value;
			} else if (!strcmp(key, "stderr")) {
				err_output = value;
			} else if (!strcmp(key, "wait_status")) {
				val = atoi(value);
				cr.exited_ok = WIFEXITED(val);
				if (cr.exited_ok) {
					cr.return_code = WEXITSTATUS(val);
				}
			} else if (!strcmp(key, "command")) {
				/* ignored */
				;
			} else if (!strcmp(key, "runtime")) {
				/* ignored */
				;
			} else if (!strcmp(key, "ru_utime")) {
				str2timeval(value, &cr.rusage.ru_utime);
			} else if (!strcmp(key, "ru_stime")) {
				str2timeval(value, &cr.rusage.ru_stime);
			} else if (!strcmp(key, "ru_minflt")) {
				cr.rusage.ru_minflt = atoi(value);
			} else if (!strcmp(key, "ru_majflt")) {
				cr.rusage.ru_majflt = atoi(value);
			} else if (!strcmp(key, "ru_nswap")) {
				cr.rusage.ru_nswap = atoi(value);
			} else if (!strcmp(key, "ru_inblock")) {
				cr.rusage.ru_inblock = atoi(value);
			} else if (!strcmp(key, "ru_oublock")) {
				cr.rusage.ru_oublock = atoi(value);
			} else if (!strcmp(key, "ru_nsignals")) {
				cr.rusage.ru_nsignals = atoi(value);
			} else if (!strcmp(key, "log")) {
				logit(NSLOG_INFO_MESSAGE, TRUE, "worker %d: %s\n", wp->pid, value);
			} else {
				printf("Unrecognized check result variable: %s=%s\n", key, value);
			}
		}
		if (is_check) {
			/* FIXME: this could be handled better... */
			if (!cr.output && err_output)
				cr.output = err_output;
			handle_check_result(&cr);
		}
		kvvec_destroy(kvv, KVVEC_FREE_ALL);
	}

	return 0;
}

void wproc_poll(int ms)
{
	iobroker_poll(nagios_iobs, ms);
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

	if (desired_workers <= 0) {
		desired_workers = 4;
	}

	/* can't shrink the number of workers (yet) */
	if (desired_workers < num_workers)
		return -1;

	wps = calloc(desired_workers, sizeof(worker_process *));
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
