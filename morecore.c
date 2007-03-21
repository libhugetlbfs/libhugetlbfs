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
#include <malloc.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <dlfcn.h>
#include <string.h>

#include "hugetlbfs.h"

#include "libhugetlbfs_internal.h"

static int heap_fd;
static long blocksize;

static void *heapbase;
static void *heaptop;
static long mapsize;

static long hugetlbfs_next_addr(long addr)
{
#if defined(__powerpc64__)
	return ALIGN(addr, 1L << 40);
#elif defined(__powerpc__)
	return ALIGN(addr, 1L << 28);
#elif defined(__ia64__)
	if (addr < (1UL << 63))
		return ALIGN(addr, 1UL << 63);
	else
		return ALIGN(addr, gethugepagesize());
#else
	return ALIGN(addr, gethugepagesize());
#endif
}

/*
 * Our plan is to ask for pages 'roughly' at the BASE.  We expect and
 * require the kernel to offer us sequential pages from wherever it
 * first gave us a page.  If it does not do so, we return the page and
 * pretend there are none this covers us for the case where another
 * map is in the way.  This is required because 'morecore' must have
 * 'sbrk' semantics, ie. return sequential, contigious memory blocks.
 * Luckily, if it does not do so and we error out malloc will happily
 * go back to small pages and use mmap to get them.  Hurrah.
 */

static void *hugetlbfs_morecore(ptrdiff_t increment)
{
	int ret;
	void *p;
	long newsize = 0;

	DEBUG("hugetlbfs_morecore(%ld) = ...\n", (long)increment);

	/*
	 * how much to grow the heap by =
	 * 	(size of heap) + malloc request - mmap'd space
	 */
	newsize = (heaptop-heapbase) + increment - mapsize;

	DEBUG("heapbase = %p, heaptop = %p, mapsize = %lx, newsize=%ld\n",
	      heapbase, heaptop, mapsize, newsize);

	/* growing the heap */
	if (newsize > 0) {
		/*
		 * convert our request to a multiple of hugepages
		 * we will have more space allocated then used, basically
		 */
		newsize = ALIGN(newsize, blocksize);

		DEBUG("Attempting to map %ld bytes\n", newsize);

		/* map in (extend) more of the file at the end of our last map */
		p = mmap(heapbase + mapsize, newsize, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE, heap_fd, mapsize);
		if (p == MAP_FAILED) {
			WARNING("Mapping failed in hugetlbfs_morecore()\n");
			return NULL;
		}

		/* if this is the first map */
		if (! mapsize) {
			if (heapbase && (heapbase != p))
				WARNING("Heap originates at %p instead of %p\n",
					p, heapbase);
			/* then setup the heap variables */
			heapbase = heaptop = p;
		} else if (p != (heapbase + mapsize)) {
			/* Couldn't get the mapping where we wanted */
			munmap(p, newsize);
			WARNING("Mapped at %p instead of %p in hugetlbfs_morecore()\n",
			      p, heapbase + mapsize);
			return NULL;
		}

		/* Use of mlock was reintroduced in libhugetlbfs 1.1,
		 * as the NUMA issues have been fixed in-kernel. The
		 * NUMA users of libhugetlbfs' malloc feature are
		 * expected to use the numactl program to specify an
		 * appropriate policy for hugepage allocation */

		/* Use mlock to guarantee these pages to the process */
		ret = mlock(p, newsize);
		if (ret) {
			WARNING("Failed to reserve huge pages in "
					"hugetlbfs_morecore(): %s\n",
					strerror(errno));
		} else {
			munlock(p, newsize);
		}

		/* we now have mmap'd further */
		mapsize += newsize;
	}

	/* heap is continuous */
	p = heaptop;
	/* and we now have added this much more space to the heap */
	heaptop = heaptop + increment;

	DEBUG("... = %p\n", p);
	return p;
}

static void __attribute__((constructor)) setup_morecore(void)
{
	char *env, *ep;
	unsigned long heapaddr;

	env = getenv("HUGETLB_MORECORE");
	if (! env)
		return;
	if (strcasecmp(env, "no") == 0) {
		DEBUG("HUGETLB_MORECORE=%s, not setting up morecore\n",
								env);
		return;
	}

	blocksize = gethugepagesize();
	if (! blocksize) {
		ERROR("Hugepages unavailable\n");
		return;
	}

	heap_fd = hugetlbfs_unlinked_fd();
	if (heap_fd < 0) {
		ERROR("Couldn't open hugetlbfs file for morecore\n");
		return;
	}

	env = getenv("HUGETLB_MORECORE_HEAPBASE");
	if (env) {
		heapaddr = strtoul(env, &ep, 16);
		if (*ep != '\0') {
			ERROR("Can't parse HUGETLB_MORECORE_HEAPBASE: %s\n",
			      env);
			return;
		}
	} else {
		heapaddr = (unsigned long)sbrk(0);
		heapaddr = hugetlbfs_next_addr(heapaddr);
	}

	DEBUG("setup_morecore(): heapaddr = 0x%lx\n", heapaddr);

	heaptop = heapbase = (void *)heapaddr;
	__morecore = &hugetlbfs_morecore;

	/* Set some allocator options more appropriate for hugepages */
	
	/* XXX: This morecore implementation does not support trimming!
	 * If we are forced to change the heapaddr from the original brk()
	 * value we have violated brk semantics (which we are not supposed to
	 * do).  This shouldn't pose a problem until glibc tries to trim the
	 * heap to an address lower than what we aligned heapaddr to.  At that
	 * point the alignment "gap" causes heap corruption.
	 *
	 * So, for now, disable heap trimming.
	 */
	mallopt(M_TRIM_THRESHOLD, -1);
	mallopt(M_TOP_PAD, blocksize / 2);
	/* we always want to use our morecore, not ordinary mmap().
	 * This doesn't appear to prohibit malloc() from falling back
	 * to mmap() if we run out of hugepages. */
	mallopt(M_MMAP_MAX, 0);
}
