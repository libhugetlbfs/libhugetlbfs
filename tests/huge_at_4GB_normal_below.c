#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

/* Designed to pick up a bug on ppc64 where
 * touches_hugepage_high_range() falsely reported true for ranges
 * reaching below 4GB */

int main(int argc, char *argv[])
{
	int page_size, hpage_size;
	int fd;
	void *p, *q;
	unsigned long lowaddr;
	int err;

	test_init(argc, argv);

	page_size = getpagesize();

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG("No hugepage kernel support");

	if (sizeof(void *) <= 4)
		IRRELEVANT();

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	p = mmap((void *)0x100000000UL, hpage_size, PROT_READ|PROT_WRITE,
		 MAP_SHARED | MAP_FIXED, fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap() huge");
	if (p != (void *)0x100000000UL)
		FAIL("Wrong address with MAP_FIXED huge");

	verbose_printf("Mapped hugetlb at %p\n", p);

	memset(p, 0, hpage_size);
	
	err = test_addr_huge(p);
	if (err != 1)
		FAIL("Mapped address is not hugepage");

	/* Test just below 4GB to check for off-by-one errors */
	lowaddr = 0x100000000UL - page_size;
	q = mmap((void *)lowaddr, page_size, PROT_READ|PROT_WRITE,
		 MAP_SHARED|MAP_FIXED|MAP_ANONYMOUS, 0, 0);
	if (q == MAP_FAILED)
		FAIL("mmap() normal");
	if (q != (void *)lowaddr)
		FAIL("Wrong address with MAP_FIXED normal");

	memset(q, 0, page_size);

	PASS();
}

