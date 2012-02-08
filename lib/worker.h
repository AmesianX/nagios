#ifndef INCLUDE_worker_h__
#define INCLUDE_worker_h__
#include <errno.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "iobroker.h"
#include "kvvec.h"
#include "iocache.h"

#define MSG_DELIM "\0\0"
#define MSG_DELIM_LEN (sizeof(MSG_DELIM) - 1)

typedef struct worker_job {
	int id;         /* job id */
	int type;
	time_t timeout; /* timeout, in absolute time */
	char *command;
	void *arg;      /* any random argument */
} worker_job;

typedef struct worker_process {
	int sd;
	pid_t pid; /* pid of this worker */
	int max_jobs, jobs_running, jobs_started;
	struct timeval start;
	iocache *ioc;
	worker_job **jobs;
	int job_index; /* this will wrap around, but that's ok */
	struct worker_process *prev_wp, *next_wp;
} worker_process;

extern worker_process *spawn_worker(void (init_func)(void *), void *init_arg);
extern void send_kvvec(int sd, kvvec *kvv);
extern const char *mkstr(const char *fmt, ...);
#endif /* INCLUDE_worker_h__ */
