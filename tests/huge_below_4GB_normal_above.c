#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

/* Designed to pick up a bug on ppc64 where
 * touches_hugepage_low_range() could give false positives because of
 * the peculiar (undefined) behaviour of << for large shifts */

int main(int argc, char *argv[])
{
	int page_size, hpage_size;
	int fd;
	void *p, *q;
	unsigned long lowaddr, highaddr;
	int err;

	test_init(argc, argv);

	page_size = getpagesize();

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG();

	if (sizeof(void *) <= 4)
		IRRELEVANT();

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");



	/* We use a low address right below 4GB so we can test for
	 * off-by-one errors */
	lowaddr = 0x100000000UL - hpage_size;
	verbose_printf("Mapping hugepage at at %lx...", lowaddr);
	p = mmap((void *)lowaddr, hpage_size, PROT_READ|PROT_WRITE,
		 MAP_SHARED|MAP_FIXED, fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap() huge");
	if (p != (void *)lowaddr)
		FAIL("Wrong address with MAP_FIXED huge");
	verbose_printf("done\n");

	memset(p, 0, hpage_size);
	
	err = test_addr_huge(p);
	if (err != 1)
		FAIL("Mapped address is not hugepage");

	/* Test for off by one errors */
	highaddr = 0x100000000UL;
	verbose_printf("Mapping normal page at %lx...", highaddr);
	q = mmap((void *)highaddr, page_size, PROT_READ|PROT_WRITE,
		 MAP_SHARED|MAP_FIXED|MAP_ANONYMOUS, 0, 0);
	if (q == MAP_FAILED)
		FAIL("mmap() normal 1");
	if (q != (void *)highaddr)
		FAIL("Wrong address with MAP_FIXED normal 2");
	verbose_printf("done\n");

	memset(q, 0, page_size);

	/* Why this address?  Well on ppc64, we're working with 256MB
	 * segment numbers, hence >>28.  In practice the shift
	 * instructions only start wrapping around with shifts 128 or
	 * greater. */
	highaddr = ((lowaddr >> 28) + 128) << 28;
	verbose_printf("Mapping normal page at %lx...", highaddr);
	q = mmap((void *)highaddr, page_size, PROT_READ|PROT_WRITE,
		 MAP_SHARED|MAP_FIXED|MAP_ANONYMOUS, 0, 0);
	if (q == MAP_FAILED)
		FAIL("mmap() normal 2");
	if (q != (void *)highaddr)
		FAIL("Wrong address with MAP_FIXED normal 2");
	verbose_printf("done\n");

	memset(q, 0, page_size);

	PASS();
}

