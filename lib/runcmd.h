#ifndef INCLUDE_runcmd_h__
#define INCLUDE_runcmd_h__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define _(x) x

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

#endif /* INCLUDE_runcmd_h__ */
