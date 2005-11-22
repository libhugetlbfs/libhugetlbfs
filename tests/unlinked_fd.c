#include <stdio.h>
#include <stdlib.h>
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

	test_init(argc, argv);

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG();

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	p = mmap(NULL, hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		 fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap()");

	err = test_addr_huge(p);
	if (err != 1)
		FAIL("Mapped address is not hugepage");

	err = munmap(p, hpage_size);
	if (err != 0)
		FAIL("munmap()");

	if (close(fd))
		FAIL("close()");

	PASS();
}

