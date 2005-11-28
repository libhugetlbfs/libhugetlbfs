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
	void *p;
	unsigned int *q;
	int i;

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

	q = p;
	for (i = 0; i < (hpage_size / sizeof(*q)); i++) {
		q[i] = RANDOM_CONSTANT ^ i;
	}

	for (i = 0; i < (hpage_size / sizeof(*q)); i++) {
		if (q[i] != (RANDOM_CONSTANT ^ i))
			FAIL("Mismatch");
	}

	PASS();
}

