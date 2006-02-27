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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

#define RANDOM_CONSTANT	0x1234ABCD

int main(int argc, char *argv[])
{
	int hpage_size;
	unsigned long totpages, chunk1, chunk2;
	int fd;
	void *p, *q;

	test_init(argc, argv);

	if (argc != 2)
		CONFIG("Usage: overcommit <# total available hugepages>");

	totpages = atoi(argv[1]);

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG("No hugepage kernel support");

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	chunk1 = (totpages / 2) + 1;
	chunk2 = totpages - chunk1 + 1;

	verbose_printf("overcommit: %ld hugepages available: "
		       "chunk1=%ld chunk2=%ld\n", totpages, chunk1, chunk2);

	p = mmap(NULL, chunk1*hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		 fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap() chunk1: %s", strerror(errno));

	q = mmap(NULL, chunk2*hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		 fd, chunk1*hpage_size);
	if (q == MAP_FAILED) {
		if (errno != ENOMEM)
			FAIL("mmap() chunk2: %s", strerror(errno));
		else
			PASS();
	}

	FAIL("Overcommitted");
}

