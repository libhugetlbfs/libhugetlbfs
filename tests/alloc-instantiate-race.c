/*
 * Test rationale:
 *
 * This test is designed to detect a kernel allocation race introduced
 * with hugepage demand-faulting.  The problem is that no lock is held
 * between allocating a hugepage and instantiating it in the
 * pagetables or page cache index.  In between the two, the (huge)
 * page is cleared, so there's substantial time.  Thus two processes
 * can race instantiating the (same) last available hugepage - one
 * will fail on the allocation, and thus cause an OOM fault even
 * though the page it actually wants is being instantiated by the
 * other racing process.
 *
 *
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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <sys/wait.h>

#include <hugetlbfs.h>

#include "hugetests.h"

int hpage_size;
pid_t child1, child2;

void cleanup(void)
{
	if (child1)
		kill(child1, SIGKILL);
	if (child2)
		kill(child2, SIGKILL);
}


void one_racer(void *p, int cpu,
	       volatile int *mytrigger, volatile int *othertrigger)
{
	volatile int *pi = p;
	cpu_set_t cpuset;
	int err;

	/* Split onto different cpus to encourage the race */
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	err = sched_setaffinity(getpid(), CPU_SETSIZE/8, &cpuset);
	if (err != 0)
		CONFIG("sched_setaffinity(cpu%d): %s", cpu, strerror(errno));

	/* Ready.. */
	*mytrigger = 1;
	/* Set.. */
	while (! *othertrigger)
		;

	/* Instantiate! */
	*pi = 1;

	exit(0);
}

void run_race(void *syncarea)
{
	volatile int *trigger1, *trigger2;
	int fd;
	void *p;
	int status1, status2;
	pid_t ret;

	memset(syncarea, 0, sizeof(*trigger1) + sizeof(*trigger2));
	trigger1 = syncarea;
	trigger2 = trigger1 + 1;

	/* Get a new file for the final page */
	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	verbose_printf("Mapping final page.. ");
	p = mmap(NULL, hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap(): %s", strerror(errno));
	verbose_printf("%p\n", p);
	
	child1 = fork();
	if (child1 < 0)
		FAIL("fork(): %s", strerror(errno));
	if (child1 == 0)
		one_racer(p, 0, trigger1, trigger2);

	child2 = fork();
	if (child2 < 0)
		FAIL("fork(): %s", strerror(errno));
	if (child2 == 0)
		one_racer(p, 1, trigger2, trigger1);

	/* wait() calls */
	ret = waitpid(child1, &status1, 0);
	if (ret < 0)
		FAIL("waitpid() child 1: %s", strerror(errno));
	verbose_printf("Child 1 status: %x\n", status1);


	ret = waitpid(child2, &status2, 0);
	if (ret < 0)
		FAIL("waitpid() child 2: %s", strerror(errno));
	verbose_printf("Child 2 status: %x\n", status2);

	if (WIFSIGNALED(status1))
		FAIL("Child 1 killed by signal %s",
		     strsignal(WTERMSIG(status1)));
	if (WIFSIGNALED(status2))
		FAIL("Child 2 killed by signal %s",
		     strsignal(WTERMSIG(status2)));

	if (WEXITSTATUS(status1) != 0)
		FAIL("Child 1 terminated with code %d", WEXITSTATUS(status1));

	if (WEXITSTATUS(status2) != 0)
		FAIL("Child 2 terminated with code %d", WEXITSTATUS(status2));
}

int main(int argc, char *argv[])
{
	unsigned long totpages;
	int fd;
	void *p, *q;
	unsigned long i;

	test_init(argc, argv);

	if (argc != 2)
		CONFIG("Usage: alloc-instantiate-race <# total available hugepages>");

	totpages = atoi(argv[1]);

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG("No hugepage kernel support");

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	/* Get a shared normal page for synchronization */
	verbose_printf("Mapping synchronization area..");
	q = mmap(NULL, getpagesize(), PROT_READ|PROT_WRITE,
		 MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (q == MAP_FAILED)
		FAIL("mmap() sync area: %s", strerror(errno));
	verbose_printf("done\n");

	verbose_printf("Mapping %ld/%ld pages.. ", totpages-1, totpages);
	p = mmap(NULL, (totpages-1)*hpage_size, PROT_READ|PROT_WRITE,
		 MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap() 1: %s", strerror(errno));

	/* Allocate all save one of the pages up front */
	verbose_printf("instantiating.. ");
	for (i = 0; i < (totpages - 1); i++)
		memset(p + (i * hpage_size), 0, sizeof(int));
	verbose_printf("done\n");

	run_race(q);

	PASS();
}

