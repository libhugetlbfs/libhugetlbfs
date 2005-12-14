#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

#define RANDOM_CONSTANT	0x1234ABCD

int main(int argc, char *argv[])
{
	int hpage_size;
	int fd;
	void *p, *q;
	unsigned int *pl, *ql;
	int i;

	test_init(argc, argv);

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG("No hugepage kernel support");

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	p = mmap(NULL, hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		 fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap() 1");

	q = mmap(NULL, hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		 fd, 0);
	if (q == MAP_FAILED)
		FAIL("mmap() 2");

	pl = p;
	for (i = 0; i < (hpage_size / sizeof(*pl)); i++) {
		pl[i] = RANDOM_CONSTANT ^ i;
	}

	ql = q;
	for (i = 0; i < (hpage_size / sizeof(*ql)); i++) {
		if (ql[i] != (RANDOM_CONSTANT ^ i))
			FAIL("Mismatch");
	}

	PASS();
}

