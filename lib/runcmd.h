#ifndef INCLUDE_runcmd_h__
#define INCLUDE_runcmd_h__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/** types **/
struct output {
	char *buf;     /* output buffer */
	size_t buflen; /* output buffer content length */
	char **line;   /* array of lines (points to buf) */
	size_t *lens;  /* string lengths */
	size_t lines;  /* lines of output */
};

typedef struct output output;

/** prototypes **/
pid_t runcmd_pid(int fd);

int np_runcmd_open(const char *cmdstring, int *pfd, int *pfderr, char **env)
	__attribute__((__nonnull__(1, 2, 3)));

int np_fetch_output(int fd, output *op, int flags)
	__attribute__((__nonnull__(2)));

int np_runcmd_close(int fd);

int np_runcmd(const char *cmd, output *out, output *err, int flags);

/* only multi-threaded plugins need to bother with this */
void np_runcmd_init(void);

#define NP_RUNCMD_INIT np_runcmd_init()

/* possible flags for np_runcmd()'s fourth argument */
#define RUNCMD_NO_ARRAYS 0x01 /* don't populate arrays at all */
#define RUNCMD_NO_ASSOC 0x02  /* output.line won't point to buf */

/* For the semi-internal string-to-argv splitter */
#define CMD_HAS_REDIR (1 << 0) /* I/O redirection */
#define CMD_HAS_SUBCOMMAND  (1 << 1) /* subcommands present */
#define CMD_HAS_PAREN (1 << 2) /* parentheses present in command */
#define CMD_HAS_JOBCONTROL (1 << 3) /* job control stuff present */
#define CMD_HAS_UBSQ (1 << 4) /* unbalanced single quotes */
#define CMD_HAS_UBDQ (1 << 5) /* unbalanced double quotes */
#define CMD_HAS_WILDCARD (1 << 6) /* wildcards present */

extern int cmd2strv(const char *str, int *out_argc, char **out_argv);

#endif /* INCLUDE_runcmd_h__ */
