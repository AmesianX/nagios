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
#include "../include/workers.h"

extern int service_check_timeout, host_check_timeout;
extern int notification_timeout;

iobroker_set *nagios_iobs = NULL;
static worker_process **workers;
static unsigned int num_workers;
static unsigned int worker_index;

/* different jobtypes. We add more as needed */
#define JOBTYPE_CHECK   0
#define JOBTYPE_NOTIFY  1

typedef struct wproc_notify_job {
	char *contact_name;
	char *host_name;
	char *service_description;
} wproc_notify_job;

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
	case JOBTYPE_NOTIFY:
		{
			wproc_notify_job *nj = (wproc_notify_job *)job->arg;
			free(nj->contact_name);
			free(nj->host_name);
			if (nj->service_description)
				free(nj->service_description);
		}
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

static int handle_worker_check(kvvec *kvv, worker_process *wp, worker_job *job)
{
	int i, result = ERROR;
	check_result *cr = (check_result *)job->arg;
	char *err_output;

	/* kvv->kv[0] has "job_id"="xxx" if we end up here, so start at 1 */
	for (i = 1; i < kvv->kv_pairs; i++) {
		char *key, *value;
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
			/*
			 * XXX FIXME check that type is indeed JOBTYPE_CHECK
			 * ignored for now though
			 */
		} else if (!strcmp(key, "timeout")) {
			/*
			 * XXX FIXME check for timeouts and set early_timeout
			 * in case it did (since workers don't handle such
			 * things all too well yet).
			 * ignored for now though
			 */
		} else if (!strcmp(key, "start")) {
			str2timeval(value, &cr->start_time);
		} else if (!strcmp(key, "stop")) {
			str2timeval(value, &cr->finish_time);
		} else if (!strcmp(key, "error")) {
			int val = atoi(value);
			if (val == ETIME) {
				cr->early_timeout = 1;
			}
		} else if (!strcmp(key, "stdout")) {
			cr->output = strdup(value);
		} else if (!strcmp(key, "stderr")) {
			err_output = value; /* mustn't copy this one */
		} else if (!strcmp(key, "wait_status")) {
			val = atoi(value);
			cr->exited_ok = WIFEXITED(val);
			if (cr->exited_ok) {
				cr->return_code = WEXITSTATUS(val);
			}
		} else if (!strcmp(key, "command")) {
			/* ignored */
			;
		} else if (!strcmp(key, "runtime")) {
			/* ignored */
			;
		} else if (!strcmp(key, "ru_utime")) {
			str2timeval(value, &cr->rusage.ru_utime);
		} else if (!strcmp(key, "ru_stime")) {
			str2timeval(value, &cr->rusage.ru_stime);
		} else if (!strcmp(key, "ru_minflt")) {
			cr->rusage.ru_minflt = atoi(value);
		} else if (!strcmp(key, "ru_majflt")) {
			cr->rusage.ru_majflt = atoi(value);
		} else if (!strcmp(key, "ru_nswap")) {
			cr->rusage.ru_nswap = atoi(value);
		} else if (!strcmp(key, "ru_inblock")) {
			cr->rusage.ru_inblock = atoi(value);
		} else if (!strcmp(key, "ru_oublock")) {
			cr->rusage.ru_oublock = atoi(value);
		} else if (!strcmp(key, "ru_nsignals")) {
			cr->rusage.ru_nsignals = atoi(value);
		} else {
			logit(NSLOG_RUNTIME_WARNING, TRUE, "Unrecognized check result variable: (i=%d) %s=%s\n", i, key, value);
		}
	}

	/*
	 * XXX FIXME: this could be handled better so nagios actually
	 * cares what plugins write on stderr in case of errors
	 */
	if (!cr->output && err_output)
		cr->output = strdup(err_output);

	if (cr->service_description) {
		service *svc = find_service(cr->host_name, cr->service_description);
		if (svc)
			result = handle_async_service_check_result(svc, cr);
	} else {
		host *hst = find_host(cr->host_name);
		if (hst)
			result = handle_async_host_check_result_3x(hst, cr);
	}
	free_check_result(cr);

	return result;
}

static int handle_worker_notification(kvvec *kvv, worker_process *wp, worker_job *job)
{
	/* XXX FIXME check if notification failed and log it */
	return 0;
}

static int handle_worker_result(int sd, int events, void *arg)
{
	worker_process *wp = (worker_process *)arg;
	char *buf;
	unsigned long size;
	int ret;

	ret = iocache_read(wp->ioc, wp->sd);
	if (ret < 0) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "iocache_read() from worker %d returned %d: %s\n",
			  wp->pid, ret, strerror(errno));
		return 0;
	} else if (ret == 0) {
		/*
		 * XXX FIXME worker exited. spawn a new on to replace it
		 * and distribute all unfinished jobs from this one to others
		 */
		return 0;
	}

	while ((buf = iocache_use_delim(wp->ioc, MSG_DELIM, MSG_DELIM_LEN, &size))) {
		kvvec *kvv;
		int job_id = -1;
		char *endptr, *key, *value;
		worker_job *job;

		kvv = buf2kvvec(buf, size, '=', '\0');
		if (!kvv) {
			/* XXX FIXME log an error */
			continue;
		}

		key = kvv->kv[0]->key;
		value = kvv->kv[0]->value;

		/* log messages are handled first */
		if (kvv->kv_pairs == 1 && !strcmp(key, "log")) {
			logit(NSLOG_INFO_MESSAGE, TRUE, "worker %d: %s\n", wp->pid, value);
			continue;
		}

		/*
		 * All others are real jobs.
		 * Entry order must be:
		 *   job_id
		 *   timeout
		 *   type
		 *   command
		 * since that's what we send and the workers have to copy our
		 * request in their response
		 */
		/* min 6 for our 4 vars + output + wait_status */
		if (kvv->kv_pairs < 6) {
			logit(NSLOG_RUNTIME_WARNING, TRUE, "Insufficient key/value pairs (%d) in response from worker %d\n",
				 kvv->kv_pairs, wp->pid);
			continue;
		}
		if (strcmp("job_id", key)) {
			/* worker is loony. Disregard this message */
			logit(NSLOG_RUNTIME_WARNING, TRUE, "First key/value pair of worker response is '%s=%s', not 'job_id=<int>'. Ignoring.\n",
				  key, value);
			continue;
		}
		job_id = (int)strtol(kvv->kv[0]->value, &endptr, 10);
		job = get_job(wp, job_id);
		if (!job) {
			logit(NSLOG_RUNTIME_WARNING, TRUE, "Worker job with id '%d' doesn't exist on worker %d.\n",
				  job_id, wp->pid);
			continue;
		}

		switch (job->type) {
		case JOBTYPE_CHECK:
			ret = handle_worker_check(kvv, wp, job);
			break;
		case JOBTYPE_NOTIFY:
			ret = handle_worker_notification(kvv, wp, job);
			break;
		default:
			logit(NSLOG_RUNTIME_WARNING, TRUE, "Worker %d: Unknown jobtype: %d\n", wp->pid, job->type);
			break;
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

static worker_process *get_worker(worker_job *job)
{
	worker_process *wp = NULL;
	int i;

	if (!workers) {
		return NULL;
	}

	wp = workers[worker_index++ % num_workers];
	job->id = get_job_id(wp);

	if (job->id < 0) {
		/* XXX FIXME Fiddle with finding a new, less busy, worker here */
	}
	wp->jobs[job->id % wp->max_jobs] = job;
	return wp;

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

	return NULL;
}

/*
 * Handles adding the command and macros to the kvvec,
 * as well as shipping the command off to a designated
 * worker
 */
static int wproc_run_job(worker_job *job, nagios_macros *mac)
{
	kvvec *kvv;
	worker_process *wp;

	/*
	 * get_worker() also adds job to the workers list
	 * and sets job_id
	 */
	wp = get_worker(job);
	if (!wp || job->id < 0)
		return ERROR;

	/*
	 * XXX FIXME: add environment macros as
	 *  kvvec_addkv(kvv, "env", "NAGIOS_LALAMACRO=VALUE");
	 *  kvvec_addkv(kvv, "env", "NAGIOS_LALAMACRO2=VALUE");
	 * so workers know to add them to environment. For now,
	 * we don't support that though.
	 */
	kvv = kvvec_init(4); /* job_id, type, command and timeout */
	if (!kvv)
		return ERROR;

	kvvec_addkv(kvv, "job_id", (char *)mkstr("%d", job->id));
	kvvec_addkv(kvv, "type", (char *)mkstr("%d", job->type));
	kvvec_addkv(kvv, "command", job->command);
	kvvec_addkv(kvv, "timeout", (char *)mkstr("%u", job->timeout));
	send_kvvec(wp->sd, kvv);
	kvvec_destroy(kvv, 0);

	return 0;
}

int wproc_notify(char *cname, char *hname, char *sdesc, char *cmd, nagios_macros *mac)
{
	worker_job *job;
	wproc_notify_job *notify;

	notify = calloc(1, sizeof(*notify));
	notify->contact_name = strdup(cname);
	notify->host_name = strdup(hname);
	if (sdesc) {
		notify->service_description = strdup(sdesc);
	}
	job = create_job(JOBTYPE_NOTIFY, notify, notification_timeout, cmd);

	return wproc_run_job(job, mac);
}

int wproc_run_check(check_result *cr, char *cmd, nagios_macros *mac)
{
	worker_job *job;
	time_t timeout;

	if (cr->service_description)
		timeout = service_check_timeout;
	else
		timeout = host_check_timeout;

	job = create_job(JOBTYPE_CHECK, cr, timeout, cmd);
	return wproc_run_job(job, mac);
}
