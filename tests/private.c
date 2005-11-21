#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

#define RANDOM_CONSTANT	0x1234ABCD
#define OTHER_CONSTANT  0xFEDC9876

int main(int argc, char *argv[])
{
	int hpage_size;
	int fd;
	void *p, *q;
	unsigned int *pl, *ql;
	int i;
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
		FAIL("mmap() SHARED");

	pl = p;
	for (i = 0; i < (hpage_size / sizeof(*pl)); i++) {
		pl[i] = RANDOM_CONSTANT ^ i;
	}

	q = mmap(NULL, hpage_size, PROT_READ|PROT_WRITE, MAP_PRIVATE,
		 fd, 0);
	if (q == MAP_FAILED)
		FAIL("mmap() PRIVATE");

	ql = q;
	for (i = 0; i < (hpage_size / sizeof(*ql)); i++) {
		if (ql[i] != (RANDOM_CONSTANT ^ i))
			FAIL("Mismatch");
	}

	for (i = 0; i < (hpage_size / sizeof(*ql)); i++) {
		ql[i] = OTHER_CONSTANT ^ i;
	}

	for (i = 0; i < (hpage_size / sizeof(*ql)); i++) {
		if (ql[i] != (OTHER_CONSTANT ^ i))
			FAIL("PRIVATE mismatch");
	}

	for (i = 0; i < (hpage_size / sizeof(*pl)); i++) {
		if (pl[i] != (RANDOM_CONSTANT ^ i))
			FAIL("SHARED map contaminated");
	}

	memset(p, 0, hpage_size);

	for (i = 0; i < (hpage_size / sizeof(*ql)); i++) {
		if (ql[i] != (OTHER_CONSTANT ^ i))
			FAIL("PRIVATE map contaminated");
	}

	PASS();
}

