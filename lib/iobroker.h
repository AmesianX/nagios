#ifndef INCLUDE_iobroker_h__
#define INCLUDE_iobroker_h__

#if (_POSIX_C_SOURCE - 0) >= 200112L
#include <poll.h>
# define IOBROKER_POLLIN POLLIN
# define IOBROKER_POLLPRI POLLPRI
# define IOBROKER_POLLOUT POLLOUT

# define IOBROKER_POLLERR POLLERR
# define IOBROKER_POLLHUP POLLHUP
# define IOBROKER_POLLNVAL POLLNVAL
#else
# define IOBROKER_POLLIN   0x001 /* there is data to read */
# define IOBROKER_POLLPRI  0x002 /* there is urgent data to read */
# define IOBROKER_POLLOUT  0x004 /* writing now will not block */

# define IOBROKER_POLLERR  0x008 /* error condition */
# define IOBROKER_POLLHUP  0x010 /* hung up */
# define IOBROKER_POLLNVAL 0x020 /* invalid polling request */
#endif

/** return codes */
#define IOBROKER_SUCCESS 0
#define IOBROKER_ENOSET -1
#define IOBROKER_ENOINIT -2
#define IOBROKER_ELIB -3
#define IOBROKER_EINVAL -EINVAL

/* Opaque type. Callers needn't worry about this */
struct iobroker_set;
typedef struct iobroker_set iobroker_set;

/**
 * Get a string describing the error in the last iobroker call.
 * The returned string must not be free()'d.
 * @param error The error code
 * @return A string describing the meaning of the error code
 */
extern const char *iobroker_strerror(int error);

/**
 * Write an io broker set to the named filedescriptor
 * @param fd The filedescriptor to write to (using dprintf())
 * @param iobs The io broker set to print
 */
extern void iobroker_print_set(int fd, iobroker_set *iobs);

/**
 * Create a new socket set
 * @return An iobroker_set on success. NULL on errors.
 */
extern iobroker_set *iobroker_create(void);

/**
 * Published utility function used to determine the max number of
 * file descriptors this process can keep open at any one time.
 * @return Max number of filedescriptors we can keep open
 */
extern int iobroker_max_usable_fds(void);

/**
 * Register a socket for input polling with the broker.
 *
 * @param iobs The socket set to add the socket to.
 * @param sd The socket descriptor to add
 * @param arg Argument passed to input handler on available input
 * @param input_handler The callback function to call when input is available
 *
 * @return 0 on succes. < 0 on errors.
 */
extern int iobroker_register(iobroker_set *iobs, int sd, void *arg, int (*handler)(int, int, void *));


/**
 * Getter function for number of file descriptors registered in
 * the set specified.
 * @param iobs The io broker set to query
 * @return Number of file descriptors registered in the set
 */
extern int iobroker_get_num_fds(iobroker_set *iobs);

/**
 * Getter function for the maximum amount of file descriptors this
 * set can handle.
 * @param iobs The io broker set to query
 * @return Max file descriptor capacity for the set
 */
extern int iobroker_get_max_fds(iobroker_set *iobs);

/**
 * Unregister a socket for input polling with the broker.
 *
 * @param iobs The socket set to remove the socket from
 * @param sd The socket descriptor to remove
 * @return 0 on succes. < 0 on errors.
 */
extern int iobroker_unregister(iobroker_set *iobs, int sd);

/**
 * Deregister a socket for input polling with the broker
 * (this is identical to iobroker_unregister())
 * @param iobs The socket set to remove the socket from
 * @param sd The socket descriptor to remove
 * @return 0 on success. < 0 on errors.
 */
extern int iobroker_deregister(iobroker_set *iobs, int sd);

/**
 * Unregister and close(2) a socket registered for input with the
 * broker. This is a convenience function which exists only to avoid
 * doing multiple calls when read() returns 0, as closed sockets must
 * always be removed from the socket set to avoid consuming tons of
 * cpu power from iterating "too fast" over the file descriptors.
 *
 * @param iobs The socket set to remove the socket from
 * @param sd The socket descriptor to remove and close
 * @return 0 on success. < 0 on errors
 */
extern int iobroker_close(iobroker_set *iobs, int sd);

/**
 * Destroy a socket set as created by iobroker_create
 * @param iobs The socket set to destroy
 * @param close close(2) all available sockets
 */
extern void iobroker_destroy(iobroker_set *iobs);

/**
 * Wait for input on any of the registered sockets.
 * @param iobs The socket set to wait for.
 * @param timeout Timeout in milliseconds. -1 is "wait indefinitely"
 */
extern int iobroker_poll(iobroker_set *iobs, int timeout);
#endif /* INCLUDE_iobroker_h__ */