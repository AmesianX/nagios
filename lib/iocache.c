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

/**
 * Attempts to move data from the end to the beginning
 * of the ioc_buf, expelling old data to make more room
 * for new reads.
 */
static inline void iocache_move_data(iocache *ioc)
{
	unsigned long available;

	if (!ioc->ioc_offset)
		return; /* nothing to do */

	available = iocache_available(ioc);
	memmove(ioc->ioc_buf, ioc->ioc_buf + ioc->ioc_offset, available);
	ioc->ioc_offset = 0;
	ioc->ioc_buflen = available;
}

int iocache_resize(iocache *ioc, unsigned long new_size)
{
	char *buf;

	if (!ioc)
		return -1;

	iocache_move_data(ioc);

	buf = realloc(ioc->ioc_buf, new_size);
	if (!buf)
		return -1;
	return 0;
}

unsigned long iocache_capacity(iocache *ioc)
{
	if (!ioc || !ioc->ioc_buf || !ioc->ioc_bufsize)
		return 0;

	iocache_move_data(ioc);

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
		ptr = memchr(buf, *delim, remains - (delim_len - 1));
		if (!ptr) {
			return NULL;
		}
		if (delim_len == 1 || !memcmp(ptr, delim, delim_len)) {
			unsigned long ioc_start;

			/* ptr must point *after* the delimiter */
			ptr += delim_len;
			ioc_start = (unsigned long)ioc->ioc_buf + ioc->ioc_offset;
			*size = (unsigned long)ptr - ioc_start;

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
		ioc->ioc_buf = calloc(1, size);
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
	int to_read, bytes_read;

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

	/* we make sure we've got as much room as possible */
	iocache_move_data(ioc);

	/* calculate the size we should read */
	to_read = ioc->ioc_bufsize - ioc->ioc_buflen;

	bytes_read = read(fd, ioc->ioc_buf + ioc->ioc_buflen, to_read);
	if (bytes_read > 0) {
		ioc->ioc_buflen += bytes_read;
	}

	return bytes_read;
}

#ifdef TEST_IOCACHE
#include <stdio.h>
#include <stdarg.h>
struct strcode {
	char *str;
	unsigned int len;
};
#define ADDSTR(str) { str, sizeof(str) - 1 }
static int test_delimiter(const char *delim, unsigned int delim_len)
{
	struct strcode sc[] = {
		ADDSTR("Charlie Chaplin"),
		ADDSTR("Madonna Something something"),
		ADDSTR("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nulla turpis augue, laoreet eleifend ultricies et, tincidunt non felis. Suspendisse vitae accumsan dolor. Vivamus posuere venenatis dictum. Integer hendrerit est eget turpis scelerisque porttitor. Donec ullamcorper sodales purus, sed bibendum odio porttitor sit amet. Donec pretium sem ac sapien iaculis feugiat. Quisque commodo consequat quam, ac cursus est sodales euismod. Sed nec massa felis, sit amet varius dui. Morbi fermentum varius tellus, eget tempus felis imperdiet quis. Praesent congue auctor ligula, a tempor ipsum malesuada at. Proin pharetra tempor adipiscing. Aenean egestas tellus vitae arcu sagittis non ultrices turpis cursus."),
		ADDSTR("Emma Blomqvist"),
		ADDSTR("Random message"),
		ADDSTR("Random\0message\0with\0nuls\0embedded"),
		{ NULL, 0, },
	};
	int i;
	iocache *ioc;

	ioc = iocache_create(65536);

	for (i = 0; sc[i].str; i++) {
		memcpy(&ioc->ioc_buf[ioc->ioc_buflen], sc[i].str, sc[i].len);
		ioc->ioc_buflen += sc[i].len;
		memcpy(ioc->ioc_buf + ioc->ioc_buflen, delim, delim_len);
		ioc->ioc_buflen += delim_len;
	}

	for (i = 0; sc[i].str; i++) {
		char *ptr;
		unsigned long len;
		int error = 0;
		ptr = iocache_use_delim(ioc, delim, delim_len, &len);
		if (!ptr) {
			printf("Null pointer. What weird shit is this??\n");
			exit(1);
		}
		if (len != sc[i].len + delim_len) {
			printf("########## len error\n");
			error = 1;
		} else if (memcmp(ptr, sc[i].str, len - delim_len)) {
			printf("########## memcmp() error\n");
			error = 2;
		}

		if (error) {
			printf("delim_len: %d. i: %d; len: %lu; sc[i].len: %d\n",
				   delim_len, i, len, sc[i].len);
			printf("sc[i].str: %s\n", sc[i].str);
			printf("ptr      : %s\n", ptr);
			printf("strlen(sc[i].str): %lu\n", strlen(sc[i].str));
			printf("strlen(ptr)      : %lu\n", strlen(ptr));
			exit(1);
		}
	}
	iocache_destroy(ioc);
	return 0;
}

int main(int argc, char **argv)
{
	int i;
	struct strcode sc[] = {
		ADDSTR("\n"),
		ADDSTR("\0\0"),
		ADDSTR("XXXxXXX"),
		ADDSTR("LALALALALALALAKALASBALLE\n"),
		{ NULL, 0 },
	};

	for (i = 0; sc[i].str; i++) {
		printf("   Testing delimiter of len %d\n", sc[i].len);
		test_delimiter(sc[i].str, sc[i].len);
	}

	printf("All tests passed\n");
	return 0;
}
#endif /* TEST_IOCACHE */
