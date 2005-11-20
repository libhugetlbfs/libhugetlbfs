#include <stdlib.h>
#include <malloc.h>
#include <sys/mman.h>
#include <errno.h>

#include "hugetlbfs.h"

#if 0
#ifdef SMALL
#define BASE ((void *)(0x09000000000UL))
#else 
#define BASE ((void *)(0x11000000000UL))
#endif
#define BLOCK (16384*1024 * 4)

static void *current = 0;
static void *highest = 0;

#endif

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

static void *(*orig_morecore)(ptrdiff_t);

static void *hugetlbfs_morecore(ptrdiff_t increment)
{
	return (*orig_morecore)(increment);
#if 0
	void *b;
	if ((current + increment) >= highest) {
		b = mmap(BASE, BLOCK, PROT_READ|PROT_WRITE,
			MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
		if (b == MAP_FAILED)
			return(0);
		if (highest != 0) {
			 if (b != highest) { 
/* printf("APW: it failed ... b<%p>\n", b); */
				munmap(b, BLOCK);
				return(0);
			}
		} else
			current = b;
		highest = b + BLOCK;
	}
	b = current;
	current += increment;

	return b;
#endif
}

void __attribute__ ((constructor)) setup_morecore(void)
{
	char *env;

	orig_morecore = __morecore;

	env = getenv("HUGETLB_MORECORE");
	if (env) {
		__morecore = &hugetlbfs_morecore;
		/* we always want to use our morecore, not mmap() */
		mallopt(M_MMAP_MAX, 0);
	}
}
