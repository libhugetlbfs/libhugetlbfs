/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2005-2007 David Gibson & Adam Litke, IBM Corporation.
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
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <hugetlbfs.h>
#include <sys/vfs.h>
#include "hugetests.h"

/*
 * Test Rationale:
 *
 * The number of global huge pages available to a mounted hugetlbfs filesystem
 * can be limited using a fs quota mechanism by setting the size attribute at
 * mount time.  Older kernels did not properly handle quota accounting in a
 * number of cases (eg. for MAP_PRIVATE pages, and wrt MAP_SHARED reservation.
 *
 * This test replays some scenarios on a privately mounted filesystem to check
 * for regressions in hugetlbfs quota accounting.
 */

extern int errno;

#define BUF_SZ 1024

/* Global test configuration */
static long hpage_size;
static char mountpoint[17];

/* Testlet results */
#define GOOD 0
#define BAD_SIG  1
#define BAD_EXIT 2

char result_str[3][10] = { "pass", "killed", "fail" };

void cleanup(void)
{
	if (umount(mountpoint) == 0)
		rmdir(mountpoint);
}

/*
 * Debugging function:  Verify the counters in the hugetlbfs superblock that
 * are used to implement the filesystem quotas.
 */
void verify_stat(int line, long tot, long free, long avail)
{
	struct statfs s;
	statfs(mountpoint, &s);

	if (s.f_blocks != tot || s.f_bfree != free || s.f_bavail != avail)
		printf("Bad quota counters at line %i: total: %li free: %li"
		       "avail: %li\n", line, s.f_blocks, s.f_bfree, s.f_bavail);
}

void get_quota_fs(unsigned long size)
{
	char size_str[20];

	snprintf(size_str, 20, "size=%luK", size/1024);

	sprintf(mountpoint, "/tmp/huge-XXXXXX");
	if (!mkdtemp(mountpoint))
		FAIL("Cannot create directory for mountpoint");

	if (mount("none", mountpoint, "hugetlbfs", 0, size_str)) {
		perror("mount");
		FAIL();
	}

	/*
	 * Set HUGETLB_PATH so future calls to hugetlbfs_unlinked_fd()
	 * will use this mountpoint.
	 */
	if (setenv("HUGETLB_PATH", mountpoint, 1))
		FAIL("Cannot set HUGETLB_PATH environment variable");

	verbose_printf("Using %s as temporary mount point.\n", mountpoint);
}

void map(unsigned long size, int flags, int cow)
{
	int fd;
	char *a, *b, *c;

	fd = hugetlbfs_unlinked_fd();
	if (!fd) {
		verbose_printf("hugetlbfs_unlinked_fd () failed");
		exit(1);
	}

	a = mmap(0, size, PROT_READ|PROT_WRITE, flags, fd, 0);
	if (a == MAP_FAILED) {
		verbose_printf("mmap failed\n");
		exit(1);
	}


	for (b = a; b < a + size; b += hpage_size)
		*(b) = 1;

	if (cow) {
		c = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
		if ((*c) !=  1) {
			verbose_printf("Data mismatch when setting up COW");
			exit(1);
		}
		if (c == MAP_FAILED) {
			verbose_printf("Creating COW mapping failed");
			exit(1);
		}
		(*c) = 0;
		munmap(c, size);
	}

	munmap(a, size);
	close(fd);
}

void do_unexpected_result(int line, int expected, int actual)
{
	FAIL("Unexpected result on line %i: expected %s, actual %s",
		line, result_str[expected], result_str[actual]);
}

void _spawn(int l, int expected_result, unsigned long size, int flags,
							int cow)
{
	pid_t pid;
	int status;
	int actual_result;

	pid = fork();
	if (pid == 0) {
		map(size, flags, cow);
		exit(0);
	} else if (pid < 0) {
		FAIL("fork()");
	} else {
		waitpid(pid, &status, 0);
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == 0)
				actual_result = GOOD;
			else
				actual_result = BAD_EXIT;
		} else {
			actual_result = BAD_SIG;
		}

		if (actual_result != expected_result)
			do_unexpected_result(l, expected_result, actual_result);
	}
}
#define spawn(e,s,f,c) _spawn(__LINE__, e, s, f, c)

int main(int argc, char ** argv)
{
	test_init(argc, argv);
	check_must_be_root();
	mountpoint[0] = '\0';
	hpage_size = check_hugepagesize();

	get_quota_fs(hpage_size);

	/*
	 * Check that simple page instantiation works within quota limits
	 * for private and shared mappings.
	 */
	spawn(GOOD, hpage_size, MAP_PRIVATE, 0);
	spawn(GOOD, hpage_size, MAP_SHARED, 0);

	/*
	 * Page instantiation should be refused if doing so puts the fs
	 * over quota.
	 */
	spawn(BAD_EXIT, 2 * hpage_size, MAP_SHARED, 0);
	spawn(BAD_SIG, 2 * hpage_size, MAP_PRIVATE, 0);

	/*
	 * COW should not be allowed if doing so puts the fs over quota.
	 */
	spawn(BAD_SIG, hpage_size, MAP_SHARED, 1);
	spawn(BAD_SIG, hpage_size, MAP_PRIVATE, 1);

	/*
	 * Make sure that operations within the quota will succeed after
	 * some failures.
	 */
	spawn(GOOD, hpage_size, MAP_SHARED, 0);
	spawn(GOOD, hpage_size, MAP_PRIVATE, 0);

	PASS();
}
