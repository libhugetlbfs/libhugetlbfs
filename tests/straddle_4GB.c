#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

int main(int argc, char *argv[])
{
	int hpage_size;
	int fd;
	void *p;
	int err;
	unsigned long straddle_addr;

	test_init(argc, argv);

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG();

	if (sizeof(void *) <= 4)
		IRRELEVANT();

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	straddle_addr = (1UL << 32) - hpage_size;

	/* We first try to get the mapping without MAP_FIXED */
	verbose_printf("Mapping without MAP_FIXED at %lx...", straddle_addr);
	p = mmap((void *)straddle_addr, 2*hpage_size, PROT_READ|PROT_WRITE,
		 MAP_SHARED, fd, 0);
	if (p == (void *)straddle_addr) {
		/* These tests irrelevant if we didn't get the
		 * straddle address */
		verbose_printf("done\n");

		if (test_addr_huge(p) != 1)
			FAIL("Mapped address is not hugepage");
		
		if (test_addr_huge(p + hpage_size) != 1)
			FAIL("Mapped address is not hugepage");

		verbose_printf("Clearing below 4GB...");
		memset(p, 0, hpage_size);
		verbose_printf("done\n");
	
		verbose_printf("Clearing above 4GB...");
		memset(p + hpage_size, 0, hpage_size);
		verbose_printf("done\n");
	} else {
		verbose_printf("got %p instead, never mind\n", p);
		munmap(p, 2*hpage_size);
	}

	verbose_printf("Mapping with MAP_FIXED at %lx...", straddle_addr);
	p = mmap((void *)straddle_addr, 2*hpage_size, PROT_READ|PROT_WRITE,
		 MAP_SHARED|MAP_FIXED, fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap() FIXED");
	if (p != (void *)straddle_addr) {
		verbose_printf("got %p instead\n", p);
		FAIL("Wrong address with MAP_FIXED");
	}
	verbose_printf("done\n", p);

	if (test_addr_huge(p) != 1)
		FAIL("Mapped address is not hugepage");

	if (test_addr_huge(p + hpage_size) != 1)
		FAIL("Mapped address is not hugepage");

	verbose_printf("Clearing below 4GB...");
	memset(p, 0, hpage_size);
	verbose_printf("done\n");
	
	verbose_printf("Clearing above 4GB...");
	memset(p + hpage_size, 0, hpage_size);
	verbose_printf("done\n");

	verbose_printf("Tested above 4GB\n");

	PASS();
}

