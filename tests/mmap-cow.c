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
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <hugetlbfs.h>
#include "hugetests.h"

extern int errno;

#define P "mmap-cow"
#define DESC \
	"* Tests copy-on-write semantics of large pages where a number     *\n"\
	"* of threads map the same file with the MAP_PRIVATE flag.  The    *\n"\
	"* threads then write into their copy of the mapping and recheck   *\n"\
	"* the contents to ensure they were not corrupted by the other     *\n"\
	"* threads.                                                        *"\

#define HTLB_FILE "mmap-cow"
#define BUF_SZ 256
#define MAX_PROCS 100
#define PRIVATE 0
#define SHARED 1

/* Setup Configuration */
int nr_hugepages;		/* Number of huge pages to allocate */
unsigned int threads;		/* Number of threads to run */
char mountpoint[BUF_SZ];	/* Location of mounted hugetlbfs */

int mmap_file(char *mount, char **addr, int *fh, size_t size, int shared)
{
	int flags = 0;

	/* First, open the file */
	*fh = hugetlbfs_unlinked_fd();
	if (*fh < 0) {
		PERROR("Unable to open temp file in hugetlbfs");
		CONFIG();
	}

	(shared) ? (flags |= MAP_SHARED) : (flags |= MAP_PRIVATE);
	*addr = mmap(NULL, size, PROT_READ|PROT_WRITE, flags, *fh, 0);
	if (addr == MAP_FAILED) {
		PERROR("Failed to mmap the hugetlb file");
		CONFIG();
	}
	return 0;
}

int do_work(int thread, size_t size) {
	char *addr;
	int fh;
	size_t i;
	char pattern = thread+65;

	if (mmap_file(mountpoint, &addr, &fh, size, PRIVATE)) {
		ERROR("mmap failed\n");
		return -1;
	}
	verbose_printf("%i: Mapped at address %p\n", thread, addr);

	/* Write to the mapping with a distinct pattern */
	verbose_printf("%i: Writing %c to the mapping\n", thread, pattern);
	for (i = 0; i < size; i++) {
		memcpy((char *)addr+i, &pattern, 1);
	}

	if (msync(addr, size, MS_SYNC)) {
		PERROR("msync");
		return -1;
	}

	/* Verify the pattern */
	for (i = 0; i < size; i++) {
		if (addr[i] != pattern) {
			ERROR("%i: Memory corruption at %p: Got %c, Expected %c\n",
				thread, &addr[i], addr[i], pattern);
			return -1;
		}
	}
	verbose_printf("%i: Pattern verified\n", thread);

	/* Munmap the area */
	munmap(addr, size);
	close(fh);
	return 0;
}

int main(int argc, char ** argv)
{
	char *addr;
	size_t hpage_size, size;
	int i, pid, status, fh;
	int wait_list[MAX_PROCS];
	int passfail = 0;

	test_init(argc, argv);

	if (argc < 3) {
		ERROR("Usage: mmap-cow <# threads> <# pages>\n");
		exit(RC_CONFIG);
	}
	nr_hugepages = atoi(argv[2]);
	threads = atoi(argv[1]);

	hpage_size = gethugepagesize();
	size = (nr_hugepages/threads) * hpage_size;
	verbose_printf("hpage_size is %zx, Size is %zu, threads: %u\n",
		       hpage_size, size, threads);

	/* First, mmap the file with MAP_SHARED and fill with data
	 * If this is not done, then the fault handler will not be
	 * called in the kernel since private mappings will be
	 * created for the children at prefault time.
	 */
	if (mmap_file(mountpoint, &addr, &fh, size, SHARED)) {
		PERROR("Failed to create shared mapping");
		CONFIG();
	}
	for (i = 0; i < size; i += 8) {
		memcpy(addr+i, "deadbeef", 8);
	}

	for (i=0; i<threads; i++) {
		if ((pid = fork()) < 0) {
			PERROR("fork");
			FAIL();
		}
		if (pid == 0) {
			exit(do_work(i, size));
		}
		wait_list[i] = pid;
	}
	for (i=0; i<threads; i++) {
		waitpid(wait_list[i], &status, 0);
		if (WEXITSTATUS(status) != 0)
			passfail = 1;
	}

	munmap(addr, size);
	close(fh);

	if (!passfail)
		PASS();
	else
		FAIL();
}
