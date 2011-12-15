#include "iocache.h"
#include <unistd.h>
#include <stdlib.h>

iocache *iocache_create(unsigned long size)
{
	iocache *ioc;

	ioc = calloc(1, sizeof(*ioc));
	if (ioc && size) {
		ioc->ioc_buf = malloc(size);
		if (!ioc->ioc_buf) {
			free(ioc);
			return NULL;
		}
		ioc->ioc_bufsize = size;
	}

	return ioc;
}

int iocache_read(iocache *ioc, int fd)
{
	size_t to_read, bytes_read;

	if (!ioc || !ioc->ioc_buf || fd < 0)
		return -1;

	/*
	 * Check if we've managed to read our fill and the caller
	 * has parsed all data. Otherwise we might end up in a state
	 * where we can't read anything but there's still new data
	 * queued on the socket
	 */
	if (ioc->ioc_offset >= ioc->ioc_buflen)
		ioc->ioc_offset = ioc->ioc_buflen = 0;

	/* calculate the size we should read */
	to_read = ioc->ioc_bufsize - ioc->ioc_buflen;

	bytes_read = read(fd, ioc->ioc_buf + ioc->ioc_offset, to_read);
	if (bytes_read > 0) {
		ioc->ioc_buflen += bytes_read;
	}

	return bytes_read;
}
