#include <stdio.h>
#include <stdlib.h>

#include <hugetlbfs.h>

#include "hugetests.h"

int main(int argc, char *argv[])
{
	int val;

	test_init(argc, argv);

	val = hugetlbfs_test_path("/");

	if (val)
		FAIL("/ reports as hugetlbfs");

	PASS();
}

