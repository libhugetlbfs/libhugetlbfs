#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <dlfcn.h>

#include "hugetlbfs.h"

#include "libhugetlbfs_internal.h"

static int heap_fd;
static long blocksize = (16 * 1024 * 1024);

static void *heapbase;
static void *heaptop;
static long mapsize;


/*
 * Our plan is to ask for pages 'roughly' at the BASE.  We expect nee require
 * the kernel to offer us sequential pages from wherever it first gave us a
 * page.  If it does not do so, we return the page and pretend there are none
 * this covers us for the case where another map is in the way.  This is 
 * required because 'morecore' must have 'sbrk' semantics, ie. return
 * sequential, contigious memory blocks.  Luckily, if it does not do so
 * and we error out malloc will happily go back to small pages and use mmap
 * to get them.  Hurrah.
 */

static void *hugetlbfs_morecore(ptrdiff_t increment)
{
	void *p;
	long newsize = 0;

	DEBUG("hugetlbfs_morecore(%d) = ...\n", increment);

	newsize = (heaptop-heapbase) + increment - mapsize;

	DEBUG("heapbase = %p, heaptop = %p, mapsize = %lx, newsize=%ld\n",
	      heapbase, heaptop, mapsize, newsize);

	if (newsize > 0) {
		newsize = ALIGN(newsize, blocksize);

		DEBUG("Attempting to map %ld bytes\n", newsize);

		p = mmap(heapbase + mapsize, newsize, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE | MAP_FIXED, heap_fd, mapsize);
		if (p == MAP_FAILED) {
			ERROR("Mapping failed in hugetlbfs_morecore()\n");
			return NULL;
		}

		if (! heapbase) {
			heapbase = heaptop = p;
		} else if (p != (heapbase + mapsize)) {
			/* Couldn't get the mapping where we wanted */
			munmap(p, newsize);
			ERROR("Mapped at %p instead of %p in hugetlbfs_morecore()\n",
			      p, heapbase + mapsize);
			return NULL;
		}

		mapsize += newsize;
	}

	p = heaptop;
	heaptop = heaptop + increment;

	DEBUG("... = %p\n", p);
	return p;
}

static void __attribute__((constructor)) setup_morecore(void)
{
	char *env, *ep;
	unsigned long heapaddr = 0;

	DEBUG("setup_morecore()\n");

	env = getenv("HUGETLB_MORECORE");
	if (! env)
		return;

	heap_fd = hugetlbfs_unlinked_fd();
	if (heap_fd < 0) {
		ERROR("Couldn't open hugetlbfs file for morecore\n");
		return;
	}

	env = getenv("HUGETLB_MORECORE_HEAPBASE");
	if (env) {
		heapaddr = strtol(env, &ep, 16);
		if (*ep != '\0') {
			ERROR("Can't parse HUGETLB_MORECORE_HEAPBASE: %s\n",
			      env);
			return;
		}
	}

	if (! heapaddr) {
		heapaddr = (unsigned long)sbrk(0);
		heapaddr = ALIGN(heapaddr, hugetlbfs_vaddr_granularity());
	}

	DEBUG("Placing hugepage morecore heap at 0x%lx\n", heapaddr);
		
	heaptop = heapbase = (void *)heapaddr;
	__morecore = &hugetlbfs_morecore;

	/* we always want to use our morecore, not ordinary mmap() */
	mallopt(M_MMAP_MAX, 0);
}
