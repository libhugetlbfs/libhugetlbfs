#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hugetests.h"

#define ALLOC_SIZE	(128)
#define NUM_ALLOCS	(262144)

int main(int argc, char *argv[])
{
	int i, j;
	char *env;
	char *p;
	int expect_hugepage = 0;

	test_init(argc, argv);

	env = getenv("HUGETLB_MORECORE");
	verbose_printf("HUGETLB_MORECORE=%s\n", env);
	if (env)
		expect_hugepage = 1;

	for (i = 0; i < NUM_ALLOCS; i++) {
		p = malloc(ALLOC_SIZE);
		if (! p)
			FAIL("malloc()");

		if (i < 16)
			verbose_printf("p = %p\n", p);

		memset(p, 0, ALLOC_SIZE);

		if ((i % 157) == 0) {
			/* With this many allocs, testing every one
			 * takes forever */
			if (expect_hugepage && (test_addr_huge(p) != 1))
				FAIL("Address is not hugepage");
			if (!expect_hugepage && (test_addr_huge(p) == 1))
				FAIL("Address is unexpectedly huge");
		}
	}

	PASS();
}

