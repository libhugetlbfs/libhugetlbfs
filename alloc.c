/*
 * libhugetlbfs - Easy use of Linux hugepages
 * alloc.c - Simple allocator of regions backed by hugepages
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
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "hugetlbfs.h"
#include "libhugetlbfs_internal.h"

/* Allocate base pages if huge page allocation fails */
static void *fallback_base_pages(size_t len, ghp_t flags)
{
	int fd;
	void *buf;
	DEBUG("get_huge_pages: Falling back to base pages\n");

	/*
	 * Map /dev/zero instead of MAP_ANONYMOUS avoid VMA mergings. Freeing
	 * pages depends on /proc/pid/maps to find lengths of allocations.
	 * This is a bit lazy and if found to be costly due to either the
	 * extra open() or virtual address space usage, we could track active
	 * mappings in a lock-protected list instead.
	 */
	fd = open("/dev/zero", O_RDWR);
	if (fd == -1) {
		ERROR("get_huge_pages: Failed to open /dev/zero for fallback");
		return NULL;
	}

	buf = mmap(NULL, len,
			PROT_READ|PROT_WRITE,
			MAP_PRIVATE,
			fd, 0);
	if (buf == MAP_FAILED) {
		WARNING("Base page fallback failed: %s\n", strerror(errno));
		buf = NULL;
	}
	close(fd);

	return buf;
}

/**
 * get_huge_pages - Allocate an amount of memory backed by huge pages
 * len: Size of the region to allocate
 * flags: Flags specifying the behaviour of the function
 *
 * This function allocates a region of memory backed by huge pages and
 * at least hugepage-aligned. This is not a suitable drop-in for malloc()
 * and is only suitable in the event the length is expected to be
 * hugepage-aligned. However, a malloc-like library could use this function
 * to create additional heap similar in principal to what morecore does for
 * glibc malloc.
 */
void *get_huge_pages(size_t len, ghp_t flags)
{
	void *buf;
	int heap_fd;

	/* Create a file descriptor for the new region */
	heap_fd = hugetlbfs_unlinked_fd();
	if (heap_fd < 0) {
		ERROR("Couldn't open hugetlbfs file for %zd-sized heap\n", len);
		return NULL;
	}

	/* Map the requested region */
	buf = mmap(NULL, len, PROT_READ|PROT_WRITE,
		 MAP_PRIVATE, heap_fd, len);
	if (buf == MAP_FAILED) {
		close(heap_fd);

		/* Try falling back to base pages if allowed */
		if (flags & GHP_FALLBACK)
			return fallback_base_pages(len, flags);

		WARNING("get_huge_pages: New region mapping failed (flags: 0x%lX): %s\n",
			flags, strerror(errno));
		return NULL;
	}

	/* Fault the region to ensure accesses succeed */
	if (hugetlbfs_prefault(heap_fd, buf, len) != 0) {
		munmap(buf, len);
		close(heap_fd);

		/* Try falling back to base pages if allowed */
		if (flags & GHP_FALLBACK)
			return fallback_base_pages(len, flags);
	}

	/* Close the file so we do not have to track the descriptor */
	if (close(heap_fd) != 0) {
		WARNING("Failed to close new heap fd: %s\n", strerror(errno));
		munmap(buf, len);
		return NULL;
	}

	/* woo, new buffer of shiny */
	return buf;
}

#define MAPS_BUF_SZ 4096
/**
 * free_huge_pages - Free a region allocated that was backed by large pages
 * ptr - The pointer to the buffer returned by get_huge_pages()
 *
 * This function finds a region to free based on the contents of
 * /proc/pid/maps. The assumption is made that the ptr is the start of
 * a hugepage region allocated with free_huge_pages. No checking is made
 * that the pointer is to a hugepage backed region.
 */
void free_huge_pages(void *ptr)
{
	FILE *fd;
	char line[MAPS_BUF_SZ];
	unsigned long start = 0, end = 0;

	/*
	 * /proc/self/maps is used to determine the length of the original
	 * allocation. As mappings are based on different files, we can
	 * assume that maps will not merge. If the hugepages were truly
	 * anonymous, this assumption would be broken.
	 */
	fd = fopen("/proc/self/maps", "r");
	if (!fd) {
		ERROR("Failed to open /proc/self/maps\n");
		return;
	}

	/* Parse /proc/maps for address ranges line by line */
	while (!feof(fd)) {
		char *bufptr;
		char *saveptr = NULL;

		/* Read a line of input */
		if (fgets(line, MAPS_BUF_SZ, fd) == NULL)
			break;

		/* Parse the line to get the start and end of each mapping */
		bufptr = strtok_r(line, " ", &saveptr);
		bufptr = strtok_r(bufptr, "-", &saveptr);
		start = strtoull(bufptr, NULL, 16);
		bufptr = strtok_r(NULL, "-", &saveptr);

		/* If the correct mapping is found, remove it */
		if (start == (unsigned long)ptr) {
			end = strtoull(bufptr, NULL, 16);
			munmap(ptr, end - start);
			break;
		}
	}

	/* Print a warning if the ptr appeared to point nowhere */
	if (end == 0)
		ERROR("hugepages_free using invalid or double free\n");

	fclose(fd);
}
