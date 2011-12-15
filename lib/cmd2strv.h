#ifndef INCLUDE_cmd2strv_h__
#define INCLUDE_cmd2strv_h__

#define CMD_HAS_REDIR (1 << 0) /* I/O redirection */
#define CMD_HAS_SUBCOMMAND  (1 << 1) /* subcommands present */
#define CMD_HAS_PAREN (1 << 2) /* parentheses present in command */
#define CMD_HAS_JOBCONTROL (1 << 3) /* job control stuff present */
#define CMD_HAS_UBSQ (1 << 4) /* unbalanced single quotes */
#define CMD_HAS_UBDQ (1 << 5) /* unbalanced double quotes */
#define CMD_HAS_WILDCARD (1 << 6) /* wildcards present */

/**
 * Parses a regular string into a string vector much the same
 * way a shell would have done it.
 * @param str The string to parse
 * @param out_argc Number of strings in out vector
 * @param out_argv Out-vector
 * @return 0 on success and 100% shell-compatible parsing. < 0 as a
 *         bitmask on errors.
 */
extern int cmd2strv(const char *str, int *out_argc, char **out_argv);
#endif
