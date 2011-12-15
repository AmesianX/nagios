#ifndef INCLUDE_iocache_h__
#define INCLUDE_iocache_h__
#include <stdlib.h>
#include <limits.h>
struct iocache {
	char *ioc_buf; /* the data */
	unsigned long ioc_offset; /* where we're reading in the buffer */
	unsigned long ioc_buflen; /* the amount of data read into the buffer */
	unsigned long ioc_bufsize; /* size of the buffer */
};
typedef struct iocache iocache;

static inline unsigned long iocache_used(iocache *ioc)
{
	if (!ioc)
		return 0;
	return ioc->ioc_buflen - ioc->ioc_offset;
}

static inline unsigned long iocache_free(iocache *ioc)
{
	if (!ioc)
		return 0;
	return ioc->ioc_bufsize - ioc->ioc_buflen;
}

static inline int iocache_grow(iocache *ioc, unsigned long add_size)
{
	if (!ioc)
		return -1;
	ioc->ioc_bufsize += add_size;
	ioc->ioc_buf = realloc(ioc->ioc_buf, ioc->ioc_bufsize);
	if (ioc->ioc_buf)
		return 0;

	return -1;
}

/**
 * Creates the iocache object, initializing it with the given size
 * @param size Initial size of the iocache buffer
 * @return Pointer to a valid iocache object
 */
extern iocache *iocache_create(unsigned long size);

/**
 * Read data into the iocache buffer
 * @param ioc The io cache we should read into
 * @param fd The filedescriptor we should read from
 * @return The number of bytes read on success. < 0 on errors
 */
extern int iocache_read(iocache *ioc, int fd);

#endif /* INCLUDE_iocache_h__ */
