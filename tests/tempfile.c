#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

int main(int argc, char *argv[])
{
	int fd;
	void *p;
	int err;

	test_init(argc, argv);

	fd = hugetlbfs_tempfile(1);
	if (fd < 0)
		CONFIG();

	p = mmap(NULL, gethugepagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
		 fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap");

	err = munmap(p, gethugepagesize());
	if (err != 0)
		FAIL("munmap");

	if (close(fd))
		FAIL("close");

	PASS();
}

