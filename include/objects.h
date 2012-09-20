/*****************************************************************************
 *
 * OBJECTS.H - Header file for object addition/search functions
 *
 * License:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/


#ifndef _OBJECTS_H
#define _OBJECTS_H

#include "common.h"

NAGIOS_BEGIN_DECL


/*************** CURRENT OBJECT REVISION **************/

#define CURRENT_OBJECT_STRUCTURE_VERSION        402     /* increment when changes are made to data structures... */
/* Nagios 3 starts at 300, Nagios 4 at 400, etc. */



/***************** OBJECT SIZE LIMITS *****************/

#define MAX_STATE_HISTORY_ENTRIES		21	/* max number of old states to keep track of for flap detection */
#define MAX_CONTACT_ADDRESSES                   6       /* max number of custom addresses a contact can have */



/***************** SKIP LISTS ****************/

#define NUM_OBJECT_SKIPLISTS                   12
#define NUM_HASHED_OBJECT_TYPES                8

#define HOST_SKIPLIST                          0
#define SERVICE_SKIPLIST                       1
#define COMMAND_SKIPLIST                       2
#define TIMEPERIOD_SKIPLIST                    3
#define CONTACT_SKIPLIST                       4
#define CONTACTGROUP_SKIPLIST                  5
#define HOSTGROUP_SKIPLIST                     6
#define SERVICEGROUP_SKIPLIST                  7
#define HOSTDEPENDENCY_SKIPLIST                8
#define SERVICEDEPENDENCY_SKIPLIST             9
#define HOSTESCALATION_SKIPLIST                10
#define SERVICEESCALATION_SKIPLIST             11


/***************** DATE RANGE TYPES *******************/

#define DATERANGE_CALENDAR_DATE  0  /* 2008-12-25 */
#define DATERANGE_MONTH_DATE     1  /* july 4 (specific month) */
#define DATERANGE_MONTH_DAY      2  /* day 21 (generic month) */
#define DATERANGE_MONTH_WEEK_DAY 3  /* 3rd thursday (specific month) */
#define DATERANGE_WEEK_DAY       4  /* 3rd thursday (generic month) */
#define DATERANGE_TYPES          5


/*
 * flags for notification_options, flapping_options and other similar
 * flags. They overlap (hosts and services), so we can't use enum's.
 */
#define OPT_NOTHING       0 /* no options selected */
#define OPT_ALL           (~0) /* everything selected, so all bits set */
#define OPT_DOWN          (1 << HOST_DOWN)
#define OPT_UP            (1 << HOST_UP)
#define OPT_UNREACHABLE   (1 << HOST_UNREACHABLE)
#define OPT_OK            (1 << STATE_OK)
#define OPT_WARNING       (1 << STATE_WARNING)
#define OPT_CRITICAL      (1 << STATE_CRITICAL)
#define OPT_UNKNOWN       (1 << STATE_UNKNOWN)
#define OPT_RECOVERY      OPT_OK
/* and now the "unreal" states... */
#define OPT_PENDING       (1 << 10)
#define OPT_FLAPPING      (1 << 11)
#define OPT_DOWNTIME      (1 << 12)
#define OPT_DISABLED      (1 << 15) /* will denote disabled checks some day */

/* macros useful with both hosts and services */
#define flag_set(c, flag)    (c |= (flag))
#define flag_get(c, flag)    (c & (flag))
#define flag_isset(c, flag)  (flag_get((c), (flag)) == (flag))
#define flag_unset(c, flag)  (c &= ~(flag))
#define should_stalk(o) flag_isset(o->stalking_options, 1 << o->current_state)
#define should_flap_detect(o) flag_isset(o->flap_detection_options, 1 << o->current_state)
#define should_notify(o) flag_isset(o->notification_options, 1 << o->current_state)
#define add_notified_on(o, f) (o->notified_on |= (1 << f))


/****************** DATA STRUCTURES *******************/

typedef struct host_struct host;
typedef struct service_struct service;
typedef struct contact_struct contact;

/* TIMED_EVENT structure */
typedef struct timed_event_struct {
	int event_type;
	time_t run_time;
	int recurring;
	unsigned long event_interval;
	int compensate_for_time_change;
	void *timing_func;
	void *event_data;
	void *event_args;
	int event_options;
	unsigned int priority; /* 0 is auto, 1 is highest. n+1 < n */
	struct squeue_event *sq_event;
	} timed_event;


/* NOTIFY_LIST structure */
typedef struct notify_list_struct {
	contact *contact;
	struct notify_list_struct *next;
	} notification;


/*
 * *name can be "Nagios Core", "Merlin", "mod_gearman" or "DNX", fe.
 * source_name gets passed the 'source' pointer from check_result
 * and must return a non-free()'able string useful for printing what
 * we need to determine exactly where the check was received from,
 * such as "mod_gearman worker@10.11.12.13", or "Nagios Core command
 * file worker" (for passive checks submitted locally), which will be
 * stashed with hosts and services and used as the "CHECKSOURCE" macro.
 */
struct check_engine {
	char *name;         /* "Nagios Core", "Merlin", "Mod Gearman" fe */
	const char *(*source_name)(void *);
	void (*clean_result)(void *);
};

/* CHECK_RESULT structure */
typedef struct check_result_struct {
	int object_check_type;                          /* is this a service or a host check? */
	char *host_name;                                /* host name */
	char *service_description;                      /* service description */
	int check_type;					/* was this an active or passive service check? */
	int check_options;
	int scheduled_check;                            /* was this a scheduled or an on-demand check? */
	int reschedule_check;                           /* should we reschedule the next check */
	char *output_file;                              /* what file is the output stored in? */
	FILE *output_file_fp;
	double latency;
	struct timeval start_time;			/* time the service check was initiated */
	struct timeval finish_time;			/* time the service check was completed */
	int early_timeout;                              /* did the service check timeout? */
	int exited_ok;					/* did the plugin check return okay? */
	int return_code;				/* plugin return code */
	char *output;	                                /* plugin output */
	struct rusage rusage;			/* resource usage by this check */
	struct check_engine *engine;	/* where did we get this check from? */
	void *source;					/* engine handles this */
	} check_result;


/* SCHED_INFO structure */
typedef struct sched_info_struct {
	int total_services;
	int total_scheduled_services;
	int total_hosts;
	int total_scheduled_hosts;
	double average_services_per_host;
	double average_scheduled_services_per_host;
	unsigned long service_check_interval_total;
	unsigned long host_check_interval_total;
	double average_service_execution_time;
	double average_service_check_interval;
	double average_host_check_interval;
	double average_service_inter_check_delay;
	double average_host_inter_check_delay;
	double service_inter_check_delay;
	double host_inter_check_delay;
	int service_interleave_factor;
	int max_service_check_spread;
	int max_host_check_spread;
	time_t first_service_check;
	time_t last_service_check;
	time_t first_host_check;
	time_t last_host_check;
	} sched_info;


/* DBUF structure - dynamic string storage */
typedef struct dbuf_struct {
	char *buf;
	unsigned long used_size;
	unsigned long allocated_size;
	unsigned long chunk_size;
	} dbuf;


#define CHECK_STATS_BUCKETS                  15

/* used for tracking host and service check statistics */
typedef struct check_stats_struct {
	int current_bucket;
	int bucket[CHECK_STATS_BUCKETS];
	int overflow_bucket;
	int minute_stats[3];
	time_t last_update;
	} check_stats;



/* OBJECT LIST STRUCTURE */
typedef struct objectlist_struct {
	void      *object_ptr;
	struct objectlist_struct *next;
	} objectlist;


/* TIMERANGE structure */
typedef struct timerange_struct {
	unsigned long range_start;
	unsigned long range_end;
	struct timerange_struct *next;
	} timerange;


/* DATERANGE structure */
typedef struct daterange_struct {
	int type;
	int syear;          /* start year */
	int smon;           /* start month */
	int smday;          /* start day of month (may 3rd, last day in feb) */
	int swday;          /* start day of week (thursday) */
	int swday_offset;   /* start weekday offset (3rd thursday, last monday in jan) */
	int eyear;
	int emon;
	int emday;
	int ewday;
	int ewday_offset;
	int skip_interval;
	timerange *times;
	struct daterange_struct *next;
	} daterange;


/* TIMEPERIODEXCLUSION structure */
typedef struct timeperiodexclusion_struct {
	char  *timeperiod_name;
	struct timeperiod_struct *timeperiod_ptr;
	struct timeperiodexclusion_struct *next;
	} timeperiodexclusion;


/* TIMEPERIOD structure */
typedef struct timeperiod_struct {
	unsigned int id;
	char    *name;
	char    *alias;
	timerange *days[7];
	daterange *exceptions[DATERANGE_TYPES];
	timeperiodexclusion *exclusions;
	struct 	timeperiod_struct *next;
	} timeperiod;


/* CONTACTSMEMBER structure */
typedef struct contactsmember_struct {
	char    *contact_name;
#ifdef NSCORE
	contact *contact_ptr;
#endif
	struct  contactsmember_struct *next;
	} contactsmember;


/* CONTACTGROUP structure */
typedef struct contactgroup_struct {
	unsigned int id;
	char	*group_name;
	char    *alias;
	contactsmember *members;
	struct	contactgroup_struct *next;
	} contactgroup;


/* CONTACTGROUPSMEMBER structure */
typedef struct contactgroupsmember_struct {
	char    *group_name;
#ifdef NSCORE
	contactgroup *group_ptr;
#endif
	struct contactgroupsmember_struct *next;
	} contactgroupsmember;


/* CUSTOMVARIABLESMEMBER structure */
typedef struct customvariablesmember_struct {
	char    *variable_name;
	char    *variable_value;
	int     has_been_modified;
	struct customvariablesmember_struct *next;
	} customvariablesmember;


/* COMMAND structure */
typedef struct command_struct {
	unsigned int id;
	char    *name;
	char    *command_line;
	struct command_struct *next;
	} command;


/* COMMANDSMEMBER structure */
typedef struct commandsmember_struct {
	char	*command;
#ifdef NSCORE
	command *command_ptr;
#endif
	struct	commandsmember_struct *next;
	} commandsmember;


/* CONTACT structure */
struct contact_struct {
	unsigned int id;
	char	*name;
	char	*alias;
	char	*email;
	char	*pager;
	char    *address[MAX_CONTACT_ADDRESSES];
	commandsmember *host_notification_commands;
	commandsmember *service_notification_commands;
	unsigned int host_notification_options;
	unsigned int service_notification_options;
	unsigned int minimum_value;
	char	*host_notification_period;
	char	*service_notification_period;
	int     host_notifications_enabled;
	int     service_notifications_enabled;
	int     can_submit_commands;
	int     retain_status_information;
	int     retain_nonstatus_information;
	customvariablesmember *custom_variables;
#ifdef NSCORE
	time_t  last_host_notification;
	time_t  last_service_notification;
	unsigned long modified_attributes;
	unsigned long modified_host_attributes;
	unsigned long modified_service_attributes;

	timeperiod *host_notification_period_ptr;
	timeperiod *service_notification_period_ptr;
	objectlist *contactgroups_ptr;
#endif
	struct	contact_struct *next;
	};


/* SERVICESMEMBER structure */
typedef struct servicesmember_struct {
	char    *host_name;
	char    *service_description;
#ifdef NSCORE
	service *service_ptr;
#endif
	struct servicesmember_struct *next;
	} servicesmember;


/* HOSTSMEMBER structure */
typedef struct hostsmember_struct {
	char    *host_name;
#ifdef NSCORE
	host    *host_ptr;
#endif
	struct hostsmember_struct *next;
	} hostsmember;


/* HOSTGROUP structure */
typedef struct hostgroup_struct {
	unsigned int id;
	char 	*group_name;
	char    *alias;
	hostsmember *members;
	char    *notes;
	char    *notes_url;
	char    *action_url;
	struct	hostgroup_struct *next;
	} hostgroup;


/* HOST structure */
struct host_struct {
	unsigned int id;
	char    *name;
	char    *display_name;
	char	*alias;
	char    *address;
	hostsmember *parent_hosts;
	hostsmember *child_hosts;
	servicesmember *services;
	char    *check_command;
	int     initial_state;
	double  check_interval;
	double  retry_interval;
	int     max_attempts;
	char    *event_handler;
	contactgroupsmember *contact_groups;
	contactsmember *contacts;
	double  notification_interval;
	double  first_notification_delay;
	unsigned int notification_options;
	unsigned int hourly_value;
	char	*notification_period;
	char    *check_period;
	int     flap_detection_enabled;
	double  low_flap_threshold;
	double  high_flap_threshold;
	int     flap_detection_options;
	unsigned int stalking_options;
	int     check_freshness;
	int     freshness_threshold;
	int     process_performance_data;
	int     checks_enabled;
	const char *check_source;
	int     accept_passive_checks;
	int     event_handler_enabled;
	int     retain_status_information;
	int     retain_nonstatus_information;
	int     obsess;
	char    *notes;
	char    *notes_url;
	char    *action_url;
	char    *icon_image;
	char    *icon_image_alt;
	char    *statusmap_image; /* used by lots of graphing tools */
/* #ifdef NSCGI */
	/*
	 * these are kept in ancillary storage for the daemon and
	 * thrown out as soon as we've created the object cache.
	 * The CGI's still attach them though, since they are the
	 * only users of this utter crap.
	 */
	char    *vrml_image;
	int     have_2d_coords;
	int     x_2d;
	int     y_2d;
	int     have_3d_coords;
	double  x_3d;
	double  y_3d;
	double  z_3d;
	int     should_be_drawn;
/* #endif */
	customvariablesmember *custom_variables;
#ifdef NSCORE
	int     problem_has_been_acknowledged;
	int     acknowledgement_type;
	int     check_type;
	int     current_state;
	int     last_state;
	int     last_hard_state;
	char	*plugin_output;
	char    *long_plugin_output;
	char    *perf_data;
	char	*saved_data;
	int     state_type;
	int     current_attempt;
	unsigned long current_event_id;
	unsigned long last_event_id;
	unsigned long current_problem_id;
	unsigned long last_problem_id;
	double  latency;
	double  execution_time;
	int     is_executing;
	int     check_options;
	int     notifications_enabled;
	time_t  last_notification;
	time_t  next_notification;
	time_t  next_check;
	int     should_be_scheduled;
	time_t  last_check;
	time_t	last_state_change;
	time_t	last_hard_state_change;
	time_t  last_time_up;
	time_t  last_time_down;
	time_t  last_time_unreachable;
	int     has_been_checked;
	int     is_being_freshened;
	int     notified_on;
	int     current_notification_number;
	int     no_more_notifications;
	unsigned long current_notification_id;
	int     check_flapping_recovery_notification;
	int     scheduled_downtime_depth;
	int     pending_flex_downtime;
	int     state_history[MAX_STATE_HISTORY_ENTRIES];    /* flap detection */
	int     state_history_index;
	time_t  last_state_history_update;
	int     is_flapping;
	unsigned long flapping_comment_id;
	double  percent_state_change;
	int     total_services;
	unsigned long total_service_check_interval;
	unsigned long modified_attributes;

	command *event_handler_ptr;
	command *check_command_ptr;
	timeperiod *check_period_ptr;
	timeperiod *notification_period_ptr;
	objectlist *hostgroups_ptr;
#endif
	/* objects we depend upon */
	objectlist *exec_deps, *notify_deps;
	objectlist *escalation_list;
	struct  host_struct *next;
	void *next_check_event;
	};


/* SERVICEGROUP structure */
typedef struct servicegroup_struct {
	unsigned int id;
	char 	*group_name;
	char    *alias;
	servicesmember *members;
	char    *notes;
	char    *notes_url;
	char    *action_url;
	struct	servicegroup_struct *next;
	} servicegroup;


/* SERVICE structure */
struct service_struct {
	unsigned int id;
	char	*host_name;
	char	*description;
	char    *display_name;
	servicesmember *parents;
	servicesmember *children;
	char    *check_command;
	char    *event_handler;
	int     initial_state;
	double	check_interval;
	double  retry_interval;
	int	max_attempts;
	int     parallelize;
	contactgroupsmember *contact_groups;
	contactsmember *contacts;
	double	notification_interval;
	double  first_notification_delay;
	unsigned int notification_options;
	unsigned int stalking_options;
	unsigned int hourly_value;
	int     is_volatile;
	char	*notification_period;
	char	*check_period;
	int     flap_detection_enabled;
	double  low_flap_threshold;
	double  high_flap_threshold;
	unsigned int flap_detection_options;
	int     process_performance_data;
	int     check_freshness;
	int     freshness_threshold;
	int     accept_passive_checks;
	int     event_handler_enabled;
	int	checks_enabled;
	const char *check_source;
	int     retain_status_information;
	int     retain_nonstatus_information;
	int     notifications_enabled;
	int     obsess;
	char    *notes;
	char    *notes_url;
	char    *action_url;
	char    *icon_image;
	char    *icon_image_alt;
	customvariablesmember *custom_variables;
#ifdef NSCORE
	int     problem_has_been_acknowledged;
	int     acknowledgement_type;
	int     host_problem_at_last_check;
	int     check_type;
	int	current_state;
	int	last_state;
	int	last_hard_state;
	char	*plugin_output;
	char    *long_plugin_output;
	char    *perf_data;
	char	*saved_data;
	int     state_type;
	time_t	next_check;
	int     should_be_scheduled;
	time_t	last_check;
	int	current_attempt;
	unsigned long current_event_id;
	unsigned long last_event_id;
	unsigned long current_problem_id;
	unsigned long last_problem_id;
	time_t	last_notification;
	time_t  next_notification;
	int     no_more_notifications;
	int     check_flapping_recovery_notification;
	time_t	last_state_change;
	time_t	last_hard_state_change;
	time_t  last_time_ok;
	time_t  last_time_warning;
	time_t  last_time_unknown;
	time_t  last_time_critical;
	int     has_been_checked;
	int     is_being_freshened;
	unsigned int notified_on;
	int     current_notification_number;
	unsigned long current_notification_id;
	double  latency;
	double  execution_time;
	int     is_executing;
	int     check_options;
	int     scheduled_downtime_depth;
	int     pending_flex_downtime;
	int     state_history[MAX_STATE_HISTORY_ENTRIES];    /* flap detection */
	int     state_history_index;
	int     is_flapping;
	unsigned long flapping_comment_id;
	double  percent_state_change;
	unsigned long modified_attributes;

	host *host_ptr;
	command *event_handler_ptr;
	char *event_handler_args;
	command *check_command_ptr;
	char *check_command_args;
	timeperiod *check_period_ptr;
	timeperiod *notification_period_ptr;
	objectlist *servicegroups_ptr;
#endif
	objectlist *exec_deps, *notify_deps;
	objectlist *escalation_list;
	struct service_struct *next;
	void *next_check_event;
	};


/* SERVICE ESCALATION structure */
typedef struct serviceescalation_struct {
	unsigned int id;
	char    *host_name;
	char    *description;
	int     first_notification;
	int     last_notification;
	double  notification_interval;
	char    *escalation_period;
	int     escalation_options;
	contactgroupsmember *contact_groups;
	contactsmember *contacts;
#ifdef NSCORE
	service *service_ptr;
	timeperiod *escalation_period_ptr;
#endif
	} serviceescalation;


/* SERVICE DEPENDENCY structure */
typedef struct servicedependency_struct {
	unsigned int id;
	int     dependency_type;
	char    *dependent_host_name;
	char    *dependent_service_description;
	char    *host_name;
	char    *service_description;
	char    *dependency_period;
	int     inherits_parent;
	int     failure_options;
#ifdef NSCORE
	service *master_service_ptr;
	service *dependent_service_ptr;
	timeperiod *dependency_period_ptr;
#endif
	} servicedependency;


/* HOST ESCALATION structure */
typedef struct hostescalation_struct {
	unsigned int id;
	char    *host_name;
	int     first_notification;
	int     last_notification;
	double  notification_interval;
	char    *escalation_period;
	int     escalation_options;
	contactgroupsmember *contact_groups;
	contactsmember *contacts;
#ifdef NSCORE
	host    *host_ptr;
	timeperiod *escalation_period_ptr;
#endif
	} hostescalation;


/* HOST DEPENDENCY structure */
typedef struct hostdependency_struct {
	unsigned int id;
	int     dependency_type;
	char    *dependent_host_name;
	char    *host_name;
	char    *dependency_period;
	int     inherits_parent;
	int     failure_options;
#ifdef NSCORE
	host    *master_host_ptr;
	host    *dependent_host_ptr;
	timeperiod *dependency_period_ptr;
#endif
	} hostdependency;

extern command *command_list;
extern timeperiod *timeperiod_list;
extern host *host_list;
extern service *service_list;
extern contact *contact_list;
extern hostgroup *hostgroup_list;
extern servicegroup *servicegroup_list;
extern contactgroup *contactgroup_list;
extern hostescalation *hostescalation_list;
extern hostdependency *hostdependency_list;
extern serviceescalation *serviceescalation_list;
extern servicedependency *servicedependency_list;


/********************* FUNCTIONS **********************/

/**** Top-level input functions ****/
int read_object_config_data(char *, int);     /* reads all external configuration data of specific types */


/**** Object Creation Functions ****/
contact *add_contact(char *name, char *alias, char *email, char *pager, char **addresses, char *svc_notification_period, char *host_notification_period, int service_notification_options, int host_notification_options, int service_notifications_enabled, int host_notifications_enabled, int can_submit_commands, int retain_status_information, int retain_nonstatus_information, unsigned int minimum_value);
commandsmember *add_service_notification_command_to_contact(contact *, char *);				/* adds a service notification command to a contact definition */
commandsmember *add_host_notification_command_to_contact(contact *, char *);				/* adds a host notification command to a contact definition */
customvariablesmember *add_custom_variable_to_contact(contact *, char *, char *);                       /* adds a custom variable to a service definition */
host *add_host(char *name, char *display_name, char *alias, char *address, char *check_period, int initial_state, double check_interval, double retry_interval, int max_attempts, int notification_options, double notification_interval, double first_notification_delay, char *notification_period, int notifications_enabled, char *check_command, int checks_enabled, int accept_passive_checks, char *event_handler, int event_handler_enabled, int flap_detection_enabled, double low_flap_threshold, double high_flap_threshold, int flap_detection_options, int stalking_options, int process_perfdata, int check_freshness, int freshness_threshold, char *notes, char *notes_url, char *action_url, char *icon_image, char *icon_image_alt, char *vrml_image, char *statusmap_image, int x_2d, int y_2d, int have_2d_coords, double x_3d, double y_3d, double z_3d, int have_3d_coords, int should_be_drawn, int retain_status_information, int retain_nonstatus_information, int obsess_over_host, unsigned int hourly_value);
hostsmember *add_parent_host_to_host(host *, char *);							/* adds a parent host to a host definition */
servicesmember *add_parent_service_to_service(service *, char *host_name, char *description);
hostsmember *add_child_link_to_host(host *, host *);						       /* adds a child host to a host definition */
contactgroupsmember *add_contactgroup_to_host(host *, char *);					       /* adds a contactgroup to a host definition */
contactsmember *add_contact_to_host(host *, char *);                                                    /* adds a contact to a host definition */
customvariablesmember *add_custom_variable_to_host(host *, char *, char *);                             /* adds a custom variable to a host definition */
timeperiod *add_timeperiod(char *, char *);								/* adds a timeperiod definition */
timeperiodexclusion *add_exclusion_to_timeperiod(timeperiod *, char *);                                 /* adds an exclusion to a timeperiod */
timerange *add_timerange_to_timeperiod(timeperiod *, int, unsigned long, unsigned long);			/* adds a timerange to a timeperiod definition */
daterange *add_exception_to_timeperiod(timeperiod *, int, int, int, int, int, int, int, int, int, int, int, int);
timerange *add_timerange_to_daterange(daterange *, unsigned long, unsigned long);
hostgroup *add_hostgroup(char *, char *, char *, char *, char *);						/* adds a hostgroup definition */
hostsmember *add_host_to_hostgroup(hostgroup *, char *);						/* adds a host to a hostgroup definition */
servicegroup *add_servicegroup(char *, char *, char *, char *, char *);                                 /* adds a servicegroup definition */
servicesmember *add_service_to_servicegroup(servicegroup *, char *, char *);                            /* adds a service to a servicegroup definition */
contactgroup *add_contactgroup(char *, char *);								/* adds a contactgroup definition */
contactsmember *add_contact_to_contactgroup(contactgroup *, char *);					/* adds a contact to a contact group definition */
command *add_command(char *, char *);									/* adds a command definition */
service *add_service(char *host_name, char *description, char *display_name, char *check_period, int initial_state, int max_attempts, int parallelize, int accept_passive_checks, double check_interval, double retry_interval, double notification_interval, double first_notification_delay, char *notification_period, int notification_options, int notifications_enabled, int is_volatile, char *event_handler, int event_handler_enabled, char *check_command, int checks_enabled, int flap_detection_enabled, double low_flap_threshold, double high_flap_threshold, int flap_detection_options, int stalking_options, int process_perfdata, int check_freshness, int freshness_threshold, char *notes, char *notes_url, char *action_url, char *icon_image, char *icon_image_alt, int retain_status_information, int retain_nonstatus_information, int obsess_over_service, unsigned int hourly_value);
contactgroupsmember *add_contactgroup_to_service(service *, char *);					/* adds a contact group to a service definition */
contactsmember *add_contact_to_service(service *, char *);                                              /* adds a contact to a host definition */
serviceescalation *add_serviceescalation(char *host_name, char *description, int first_notification, int last_notification, double notification_interval, char *escalation_period, int escalation_options);
contactgroupsmember *add_contactgroup_to_serviceescalation(serviceescalation *, char *);                /* adds a contact group to a service escalation definition */
contactsmember *add_contact_to_serviceescalation(serviceescalation *, char *);                          /* adds a contact to a service escalation definition */
customvariablesmember *add_custom_variable_to_service(service *, char *, char *);                       /* adds a custom variable to a service definition */
servicedependency *add_service_dependency(char *dependent_host_name, char *dependent_service_description, char *host_name, char *service_description, int dependency_type, int inherits_parent, int failure_options, char *dependency_period);
hostdependency *add_host_dependency(char *dependent_host_name, char *host_name, int dependency_type, int inherits_parent, int failure_options, char *dependency_period);
hostescalation *add_hostescalation(char *host_name, int first_notification, int last_notification, double notification_interval, char *escalation_period, int escalation_options);
contactsmember *add_contact_to_hostescalation(hostescalation *, char *);                                /* adds a contact to a host escalation definition */
contactgroupsmember *add_contactgroup_to_hostescalation(hostescalation *, char *);                      /* adds a contact group to a host escalation definition */

contactsmember *add_contact_to_object(contactsmember **, char *);                                       /* adds a contact to an object */
customvariablesmember *add_custom_variable_to_object(customvariablesmember **, char *, char *);         /* adds a custom variable to an object */


servicesmember *add_service_link_to_host(host *, service *);


int skiplist_compare_text(const char *val1a, const char *val1b, const char *val2a, const char *val2b);
int get_host_count(void);
int get_service_count(void);


int create_object_tables(unsigned int *);

/**** Object Search Functions ****/
timeperiod *find_timeperiod(const char *);						                /* finds a timeperiod object */
host *find_host(const char *);									/* finds a host object */
hostgroup *find_hostgroup(const char *);						                /* finds a hostgroup object */
servicegroup *find_servicegroup(const char *);					                /* finds a servicegroup object */
contact *find_contact(const char *);							                /* finds a contact object */
contactgroup *find_contactgroup(const char *);					                /* finds a contactgroup object */
command *find_command(const char *);							                /* finds a command object */
service *find_service(const char *, const char *);								/* finds a service object */


#define OBJECTLIST_DUPE 1
int add_object_to_objectlist(objectlist **, void *);
int prepend_object_to_objectlist(objectlist **, void *);
int prepend_unique_object_to_objectlist(objectlist **, void *, size_t size);
int free_objectlist(objectlist **);


/**** Object Query Functions ****/
unsigned int host_services_value(host *h);
int is_host_immediate_child_of_host(host *, host *);	               /* checks if a host is an immediate child of another host */
int is_host_primary_immediate_child_of_host(host *, host *);            /* checks if a host is an immediate child (and primary child) of another host */
int is_host_immediate_parent_of_host(host *, host *);	               /* checks if a host is an immediate child of another host */
int is_host_member_of_hostgroup(hostgroup *, host *);		       /* tests whether or not a host is a member of a specific hostgroup */
int is_host_member_of_servicegroup(servicegroup *, host *);	       /* tests whether or not a service is a member of a specific servicegroup */
int is_service_member_of_servicegroup(servicegroup *, service *);	/* tests whether or not a service is a member of a specific servicegroup */
int is_contact_member_of_contactgroup(contactgroup *, contact *);	/* tests whether or not a contact is a member of a specific contact group */
int is_contact_for_host(host *, contact *);			       /* tests whether or not a contact is a contact member for a specific host */
int is_escalated_contact_for_host(host *, contact *);                   /* checks whether or not a contact is an escalated contact for a specific host */
int is_contact_for_service(service *, contact *);		       /* tests whether or not a contact is a contact member for a specific service */
int is_escalated_contact_for_service(service *, contact *);             /* checks whether or not a contact is an escalated contact for a specific service */
int is_host_immediate_parent_of_host(host *, host *);		       /* tests whether or not a host is an immediate parent of another host */

int number_of_immediate_child_hosts(host *);		                /* counts the number of immediate child hosts for a particular host */
int number_of_total_child_hosts(host *);				/* counts the number of total child hosts for a particular host */
int number_of_immediate_parent_hosts(host *);				/* counts the number of immediate parents hosts for a particular host */

#ifdef NSCORE
void fcache_contactlist(FILE *fp, const char *prefix, contactsmember *list);
void fcache_contactgrouplist(FILE *fp, const char *prefix, contactgroupsmember *list);
void fcache_hostlist(FILE *fp, const char *prefix, hostsmember *list);
void fcache_customvars(FILE *fp, customvariablesmember *cvlist);
void fcache_timeperiod(FILE *fp, timeperiod *temp_timeperiod);
void fcache_command(FILE *fp, command *temp_command);
void fcache_contactgroup(FILE *fp, contactgroup *temp_contactgroup);
void fcache_hostgroup(FILE *fp, hostgroup *temp_hostgroup);
void fcache_servicegroup(FILE *fp, servicegroup *temp_servicegroup);
void fcache_contact(FILE *fp, contact *temp_contact);
void fcache_host(FILE *fp, host *temp_host);
void fcache_service(FILE *fp, service *temp_service);
void fcache_servicedependency(FILE *fp, servicedependency *temp_servicedependency);
void fcache_serviceescalation(FILE *fp, serviceescalation *temp_serviceescalation);
void fcache_hostdependency(FILE *fp, hostdependency *temp_hostdependency);
void fcache_hostescalation(FILE *fp, hostescalation *temp_hostescalation);
void fcache_contactlist(FILE *fp, const char *prefix, contactsmember *list);
void fcache_contactgrouplist(FILE *fp, const char *prefix, contactgroupsmember *list);
void fcache_hostlist(FILE *fp, const char *prefix, hostsmember *list);
void fcache_customvars(FILE *fp, customvariablesmember *cvlist);
void fcache_timeperiod(FILE *fp, timeperiod *temp_timeperiod);
void fcache_command(FILE *fp, command *temp_command);
void fcache_contactgroup(FILE *fp, contactgroup *temp_contactgroup);
void fcache_hostgroup(FILE *fp, hostgroup *temp_hostgroup);
void fcache_servicegroup(FILE *fp, servicegroup *temp_servicegroup);
void fcache_contact(FILE *fp, contact *temp_contact);
void fcache_host(FILE *fp, host *temp_host);
void fcache_service(FILE *fp, service *temp_service);
void fcache_servicedependency(FILE *fp, servicedependency *temp_servicedependency);
void fcache_serviceescalation(FILE *fp, serviceescalation *temp_serviceescalation);
void fcache_hostdependency(FILE *fp, hostdependency *temp_hostdependency);
void fcache_hostescalation(FILE *fp, hostescalation *temp_hostescalation);
int fcache_objects(char *cache_file);
#endif


/**** Object Cleanup Functions ****/
int free_object_data(void);                             /* frees all allocated memory for the object definitions */


NAGIOS_END_DECL
#endif
