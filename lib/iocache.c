#include "iocache.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void iocache_destroy(iocache *ioc)
{
	if (!ioc)
		return;

	if (ioc->ioc_buf)
		free(ioc->ioc_buf);

	free(ioc);
}

unsigned long iocache_capacity(iocache *ioc)
{
	if (!ioc || !ioc->ioc_buf || !ioc->ioc_bufsize)
		return 0;

	return ioc->ioc_bufsize - ioc->ioc_buflen;
}

unsigned long iocache_available(iocache *ioc)
{
	if (!ioc || !ioc->ioc_buf || !ioc->ioc_bufsize || !ioc->ioc_buflen)
		return 0;

	return ioc->ioc_buflen - ioc->ioc_offset;
}

char *iocache_use_size(iocache *ioc, unsigned long size)
{
	char *ret;

	if (!ioc || !ioc->ioc_buf)
		return NULL;
	if (ioc->ioc_bufsize < size || iocache_available(ioc) < size)
		return NULL;

	ret = ioc->ioc_buf + ioc->ioc_offset;
	ioc->ioc_offset += size;
	return ret;
}

char *iocache_use_delim(iocache *ioc, const char *delim, size_t delim_len, unsigned long *size)
{
	char *ptr = NULL;
	char *buf;
	unsigned long remains;

	if (!ioc || !ioc->ioc_buf || !ioc->ioc_bufsize || !ioc->ioc_buflen)
		return NULL;

	buf = &ioc->ioc_buf[ioc->ioc_offset];
	remains = iocache_available(ioc);
	while (remains >= delim_len) {
		unsigned long jump;
		ptr = memchr(buf, *delim, iocache_available(ioc) - delim_len);
		if (!ptr) {
			return NULL;
		}
		if (ptr && !memcmp(ptr, delim, delim_len)) {
			ptr += delim_len;
			*size = (unsigned long)ptr - ((unsigned long)ioc->ioc_buf) - ioc->ioc_offset;

			return iocache_use_size(ioc, *size);
		}
		jump = 1 + (unsigned long)ptr - (unsigned long)buf;
		remains -= jump;
		buf += jump;
	}
	return NULL;
}

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