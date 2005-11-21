#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hugetests.h"

int block_sizes[] = {
	sizeof(int), 1024, 128*1024, 1024*1024, 16*1024*1024,
};
#define NUM_SIZES	(sizeof(block_sizes) / sizeof(block_sizes[0]))

int main(int argc, char *argv[])
{
	int i, j;
	char *env;
	int expect_hugepage = 0;
	char *p;

	test_init(argc, argv);
	
	env = getenv("HUGETLB_MORECORE");
	verbose_printf("HUGETLB_MORECORE=%s\n", env);
	if (env)
		expect_hugepage = 1;

	for (i = 0; i < NUM_SIZES; i++) {
		int size = block_sizes[i];

		p = malloc(size);
		if (! p)
			FAIL("malloc()");

		verbose_printf("malloc(%d) = %p\n", size, p);

		for (j = 0; j < size; j++)
			p[j] = j % 256;


		for (j = 0; j < size; j++)
			if (p[j] != (j % 256))
				FAIL("Mismatch");

		free(p);
	}

	PASS();
}

