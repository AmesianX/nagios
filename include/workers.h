#ifndef INCLUDE_workers_h__
#define INCLUDE_workers_h__
#include "../lib/libnagios.h"

/* different jobtypes. We add more as needed */
#define WPJOB_CHECK   0
#define WPJOB_NOTIFY  1
extern void free_worker_memory(void);
extern int init_workers(int desired_workers);
extern int wproc_run_check(check_result *cr, char *cmd, nagios_macros *mac);
extern int wproc_notify(char *cname, char *hname, char *sdesc, char *cmd, nagios_macros *mac);
extern void wproc_poll(int timeout_ms);
extern int wproc_run(int job_type, char *cmd, int timeout, nagios_macros *mac);
#endif
