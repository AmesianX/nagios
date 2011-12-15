#include <stdlib.h>
#include <string.h>
#include "cmd2strv.h"

#define STATE_NONE  0
#define STATE_WHITE (1 << 0)
#define STATE_INARG (1 << 1)
#define STATE_INSQ  (1 << 2)
#define STATE_INDQ  (1 << 3)
#define STATE_SPECIAL (1 << 4)
#define in_quotes (state & (STATE_INSQ | STATE_INDQ))
#define is_state(s) (state == s)
#define set_state(s) (state = s)
#define has_state(s) ((state & s) == s)
#define have_state(s) has_state(s)
#define add_state(s) (state |= s)
#define del_state(s) (state &= ~s)

#define add_ret(r) (ret |= r)

/*
 * Simple command parser which is still tolerably accurate
 * for our simple needs.
 *
 * It's up to the caller to handle output redirection, job control,
 * conditional statements, variable substitution, nested commands and
 * function execution. We do mark such occasions with the return code
 * though, which is to be interpreted as a bitfield with potentially
 * multiple flags set.
 */
int cmd2strv(const char *str, int *out_argc, char **out_argv)
{
	int arg = 0, i, a = 0;
	int state, ret = 0;
	size_t len;
	char *argz;

	set_state(STATE_NONE);
	len = strlen(str);
	argz = malloc(len + 10);
	for (i = 0; i < len; i++) {
		const char *p = &str[i];

		switch (*p) {
		case 0:
			return ret;

		case ' ': case '\t': case '\r': case '\n':
			if (is_state(STATE_INARG)) {
				set_state(STATE_NONE);
				argz[a++] = 0;
				continue;
			}
			if (!in_quotes)
				continue;

			break;

		case '\\':
			i++;
			break;

		case '\'':
			if (have_state(STATE_INDQ))
				break;
			if (have_state(STATE_INSQ)) {
				del_state(STATE_INSQ);
				continue;
			}

			/*
			 * quotes can come inside arguments or
			 * at the start of them
			 */
			if (is_state(STATE_NONE) || is_state(STATE_INARG)) {
				if (is_state(STATE_NONE)) {
					/* starting a new argument */
					out_argv[arg++] = &argz[a];
				}
				set_state(STATE_INSQ | STATE_INARG);
				continue;
			}
		case '"':
			if (have_state(STATE_INSQ))
				break;
			if (has_state(STATE_INDQ)) {
				del_state(STATE_INDQ);
				continue;
			}
			if (is_state(STATE_NONE) || is_state(STATE_INARG)) {
				if (is_state(STATE_NONE)) {
					out_argv[arg++] = &argz[a];
				}
				set_state(STATE_INDQ | STATE_INARG);
				continue;
			}
			break;

		case '|':
			if (!in_quotes) {
				add_ret(CMD_HAS_REDIR);
			}
			break;
		case '&': case ';':
			if (!in_quotes) {
				set_state(STATE_SPECIAL);
				add_ret(CMD_HAS_JOBCONTROL);
				if (i && str[i - 1] != *p) {
					argz[a++] = 0;
					out_argv[arg++] = &argz[a];
				}
			}
			break;

		case '`':
			if (!in_quotes) {
				add_ret(CMD_HAS_SUBCOMMAND);
			}
			break;

		case '(':
			if (!in_quotes) {
				add_ret(CMD_HAS_PAREN);
			}
			break;

		case '*': case '?':
			if (!in_quotes) {
				add_ret(CMD_HAS_WILDCARD);
			}

			/* fallthrough */

		default:
			break;
		}

		if (is_state(STATE_NONE)) {
			set_state(STATE_INARG);
			out_argv[arg++] = &argz[a];
		}

		/* by default we simply copy the byte */
		argz[a++] = str[i];
	}

	/* make sure we nul-terminate the last argument */
	argz[a++] = 0;

	if (have_state(STATE_INSQ))
		add_ret(CMD_HAS_UBSQ);
	if (have_state(STATE_INDQ))
		add_ret(CMD_HAS_UBDQ);

	*out_argc = arg;

	return ret;
}
