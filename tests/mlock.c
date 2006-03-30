/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2005-2006 David Gibson & Adam Litke, IBM Corporation.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

#include <hugetlbfs.h>
#include "hugetests.h"

void test_simple_mlock(unsigned long size, unsigned long map_flags)
{
	char *a;
	int ret;
	int fd = hugetlbfs_unlinked_fd();

	a = mmap(0, size, PROT_READ|PROT_WRITE, map_flags, fd, 0);
	if (a == MAP_FAILED)
		FAIL("mmap() failed: %s", strerror(errno));

	if (map_flags & MAP_LOCKED) {
		ret = mlock(a, size);
		if (ret)
			FAIL("mlock() failed: %s", strerror(errno));

		ret = munlock(a, size);
		if (ret)
			 FAIL("munlock() failed: %s", strerror(errno));
	}

	ret = munmap(a, size);
	if (ret)
		FAIL("munmap() failed: %s", strerror(errno));

	close(fd);
}

int main(int argc, char *argv[])
{
	unsigned long tot_pages, size;

	if (argc != 2)
		CONFIG("Usage: mlock <# total available hugepages>");

	tot_pages = atoi(argv[1]);
	size = tot_pages * gethugepagesize();
	
	test_simple_mlock(size, MAP_PRIVATE);
	test_simple_mlock(size, MAP_SHARED);
	test_simple_mlock(size, MAP_PRIVATE|MAP_LOCKED);
	test_simple_mlock(size, MAP_SHARED|MAP_LOCKED);
	PASS();
}
