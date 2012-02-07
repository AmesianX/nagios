#ifndef INCLUDE_workers_h__
#define INCLUDE_workers_h__
#include "../lib/libnagios.h"
extern void free_worker_memory(void);
extern int init_workers(int desired_workers);
extern int wproc_run_check(check_result *cr, char *cmd, nagios_macros *mac);
extern void wproc_poll(int timeout_ms);
#endif
