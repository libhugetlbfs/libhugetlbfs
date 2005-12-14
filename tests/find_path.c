#include <stdio.h>
#include <stdlib.h>

#include <hugetlbfs.h>

#include "hugetests.h"

int main(int argc, char *argv[])
{
	const char *dir;

	test_init(argc, argv);

	dir = hugetlbfs_find_path();

	if (! dir)
		CONFIG("No hugepage mount");

	verbose_printf("Found hugetlbfs path at %s\n", dir);

	if (hugetlbfs_test_path(dir) == 1)
		PASS();

	FAIL("");
}

