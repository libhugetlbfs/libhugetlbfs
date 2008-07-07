/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2008 Eric Munson, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdarg.h>

#include <hugetlbfs.h>

#include "hugetests.h"

#define BUF_SIZE 4096
#define FILLER "tmpfs /var/run tmpfs rw,nosuid,nodev,noexec,mode=755 0 0\n"

int mountsfd; /* = 0; */
int readcount; /* = 0; */
int filler_sz; /* = 0; */

/*
 * We override the normal open, so we can remember the fd for the
 * mounts file
 */
int open(const char *path, int flags, ...)
{
	int (*old_open)(const char *, int, ...);
	int fd;

	old_open = dlsym(RTLD_NEXT, "open");
	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		fd = (*old_open)(path, flags, va_arg(ap, mode_t));
		va_end(ap);
		return fd;
	} else {
		fd = (*old_open)(path, flags);
		if (strcmp(path, "/proc/mounts") == 0)
			mountsfd = fd;
		return fd;
	}
}

/*
 * We override read so that we can pad the mounts file to ensure that the
 * hugetlb mount point is beyond 4kb.
 */
ssize_t read(int fd, void *buf, size_t count)
{
	int (*old_read)(int fd, void *buf, size_t count);
	int out = 0;
	int num_filler;

	old_read = dlsym(RTLD_NEXT, "read");
	/*
	 * Pass through to libc read if we aren't looking at /proc/mounts or
	 * if we have padded the mounts file with enough stuff to ensure that
	 * the hugetlb mount is beyond 4kb.
	 */
	if (fd != mountsfd || readcount >= BUF_SIZE - filler_sz)
		return (*old_read)(fd, buf, count);

	num_filler = (count < BUF_SIZE - readcount ?
			count : BUF_SIZE - readcount) / filler_sz;

	for (; num_filler > 0; num_filler--) {
		memcpy(buf + out, FILLER, filler_sz);
		out += filler_sz;
		readcount += filler_sz;
	}
	return (ssize_t)out;
}

int main(int argc, char *argv[])
{
	int fd;

	test_init(argc, argv);
	filler_sz = strlen(FILLER);
	check_hugepagesize();

	fd = hugetlbfs_unlinked_fd();

	if (fd < 0)
		FAIL("Unable to find mount point\n");

	PASS();
}
