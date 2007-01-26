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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <hugetlbfs.h>
#include "hugetests.h"

/*
 * Test rationale:
 *
 * On PowerPC, the address space is divided into segments.  These segments can
 * contain either huge pages or normal pages, but not both.  All segments are
 * initially set up to map normal pages.  When a huge page mapping is created
 * within a set of empty segments, they are "enabled" for huge pages at that
 * time.  Once enabled for huge pages, they can not be used again for normal
 * pages for the remaining lifetime of the process.
 *
 * If the segment immediately preceeding the segment containing the stack is
 * converted to huge pages and the stack is made to grow into the this
 * preceeding segment, some kernels may attempt to map normal pages into the
 * huge page-only segment -- resulting in bugs.
 */

void do_child()
{
	while (1) {
		volatile int *x;
		x = alloca(16*1024*1024);
		*x = 1;
	}
}

int main(int argc, char *argv[])
{
	int fd, pid, s, ret;
	struct rlimit r;
	char *b;
	unsigned long hpage_size = gethugepagesize();

	test_init(argc, argv);

	ret = getrlimit(RLIMIT_STACK, &r);
	if (ret)
		CONFIG("getrlimit failed");

	if (r.rlim_cur != RLIM_INFINITY)
		CONFIG("Stack rlimit must be 'unlimited'");

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		CONFIG("Couldn't get hugepage fd");

	b = mmap(0, hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (b != MAP_FAILED)
		munmap(b, hpage_size);
	else
		FAIL("mmap");

	if ((pid = fork()) < 0)
		FAIL("fork");

	if (pid == 0) {
		do_child();
		exit(0);
	}

	ret = waitpid(pid, &s, 0);
	if (ret == -1)
		FAIL("waitpid");

	/*
	 * The child grows its stack until a failure occurs.  We expect
	 * this to result in a SIGSEGV.  If any other signal is
	 * delivered (ie. SIGTRAP) or no signal is sent at all, we
	 * determine the kernel has not behaved correctly and trigger a
	 * test failure.
	 */
	if (WIFSIGNALED(s)) {
		int sig = WTERMSIG(s);

		if (sig == SIGSEGV) {
			PASS();
		} else {
			FAIL("Got unexpected signal: %s", strsignal(sig));
		}
	}
	FAIL("Child not signalled");
}
