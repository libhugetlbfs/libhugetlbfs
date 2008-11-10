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
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "hugetlbfs.h"
#include "libhugetlbfs_internal.h"

/* Allocate base pages if huge page allocation fails */
static void *fallback_base_pages(size_t len, ghp_t flags)
{
	int fd;
	void *buf;
	DEBUG("get_hugepage_region: Falling back to base pages\n");

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
 * len: Size of the region to allocate, must be hugepage-aligned
 * flags: Flags specifying the behaviour of the function
 *
 * This function allocates a region of memory that is backed by huge pages
 * and hugepage-aligned. This is not a suitable drop-in for malloc() but a
 * a malloc library could use this function to create a new fixed-size heap
 * similar in principal to what morecore does for glibc malloc.
 */
void *get_huge_pages(size_t len, ghp_t flags)
{
	void *buf;
	int heap_fd;

	/* Catch an altogether-too easy typo */
	if (flags & GHR_MASK)
		ERROR("Improper use of GHR_* in get_huge_pages()\n");

	/* Create a file descriptor for the new region */
	heap_fd = hugetlbfs_unlinked_fd();
	if (heap_fd < 0) {
		ERROR("Couldn't open hugetlbfs file for %zd-sized heap\n", len);
		return NULL;
	}

	/* Map the requested region */
	buf = mmap(NULL, len, PROT_READ|PROT_WRITE,
		 MAP_PRIVATE, heap_fd, 0);
	if (buf == MAP_FAILED) {
		close(heap_fd);

		WARNING("get_huge_pages: New region mapping failed (flags: 0x%lX): %s\n",
			flags, strerror(errno));
		return NULL;
	}

	/* Fault the region to ensure accesses succeed */
	if (hugetlbfs_prefault(heap_fd, buf, len) != 0) {
		munmap(buf, len);
		close(heap_fd);

		WARNING("get_huge_pages: Prefaulting failed (flags: 0x%lX): %s\n",
			flags, strerror(errno));
		return NULL;
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

/*
 * Offset the buffer using bytes wasted due to alignment to avoid using the
 * same cache lines for the start of every buffer returned by
 * get_huge_pages(). A small effort is made to select a random cacheline
 * rather than sequential lines to give decent behaviour on average.
 */
void *cachecolor(void *buf, size_t len, size_t color_bytes)
{
	static long cacheline_size = 0;
	static int linemod = 0;
	char *bytebuf = (char *)buf;
	int numlines;
	int line = 0;

	/* Lookup our cacheline size once */
	if (cacheline_size == 0) {
		cacheline_size = sysconf(_SC_LEVEL2_CACHE_LINESIZE);
		linemod = time(NULL);
	}

	numlines = color_bytes / cacheline_size;
	DEBUG("%d lines of cacheline size %ld due to %zd wastage\n",
		numlines, cacheline_size, color_bytes);
	if (numlines) {
		line = linemod % numlines;
		bytebuf += cacheline_size * line;

		/* Pseudo-ish random line selection */
		linemod += len % numlines;
	}
	DEBUG("Using line offset %d from start\n", line);

	return bytebuf;
}

/**
 * get_hugepage_region - Allocate an amount of memory backed by huge pages
 *
 * len: Size of the region to allocate
 * flags: Flags specifying the behaviour of the function
 *
 * This function allocates a region of memory backed by huge pages. Care should
 * be taken when using this function as a drop-in replacement for malloc() as
 * memory can be wasted if the length is not hugepage-aligned. This function
 * is more relaxed than get_huge_pages() in that it allows fallback to small
 * pages when requested.
 */
void *get_hugepage_region(size_t len, ghr_t flags)
{
	size_t aligned_len, wastage;
	void *buf;

	/* Catch an altogether-too easy typo */
	if (flags & GHP_MASK)
		ERROR("Improper use of GHP_* in get_hugepage_region()\n");

	/* Align the len parameter to a hugepage boundary and allocate */
	aligned_len = ALIGN(len, gethugepagesize());
	buf = get_huge_pages(aligned_len, GHP_DEFAULT);
	if (buf == NULL && (flags & GHR_FALLBACK)) {
		aligned_len = ALIGN(len, getpagesize());
		buf = fallback_base_pages(len, flags);
	}

	/* Calculate wastage for coloring */
	wastage = aligned_len - len;
	if (wastage != 0 && !(flags & GHR_COLOR))
		DEBUG("get_hugepage_region: Wasted %zd bytes due to alignment\n",
			wastage);

	/* Only colour if requested */
	if (flags & GHR_COLOR)
		buf = cachecolor(buf, len, wastage);

	return buf;
}

/**
 * free_hugepage_region - Free a region allocated by get_hugepage_region
 * ptr - The pointer to the buffer returned by get_hugepage_region
 *
 * This function finds a region to free based on the contents of
 * /proc/pid/maps. The assumption is made that the ptr is the start of
 * a hugepage region allocated with get_hugepage_region. No checking is made
 * that the pointer is to a hugepage backed region.
 */
void free_hugepage_region(void *ptr)
{
	/* Buffers may be offset for cache line coloring */
	DEBUG("free_hugepage_region(%p) unaligned\n", ptr);
	ptr = (void *)ALIGN_DOWN((unsigned long)ptr, gethugepagesize());
	DEBUG("free_hugepage_region(%p) aligned\n", ptr);

	free_huge_pages(ptr);
}
