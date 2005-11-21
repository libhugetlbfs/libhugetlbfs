#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hugetests.h"

#define ALLOC_SIZE	(sizeof(long))
#define NUM_ALLOCS	(1024*1024)

int main(int argc, char *argv[])
{
	int i, j;
	char *p;

	test_init(argc, argv);

	for (i = 0; i < NUM_ALLOCS; i++) {
		p = malloc(ALLOC_SIZE);
		if (! p)
			FAIL("malloc()");

		memset(p, 0, ALLOC_SIZE);

		for (j = 0; j < ALLOC_SIZE; j++)
			p[j] = j % 256;

		for (j = 0; j < ALLOC_SIZE; j++)
			if (p[j] != (j % 256))
				FAIL("Mismatch");

	}

	PASS();
}

