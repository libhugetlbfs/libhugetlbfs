#include <stdio.h>
#include <stdlib.h>

#include <hugetlbfs.h>

#include "hugetests.h"

int main(int argc, char *argv[])
{
	int hpage_size;

	test_init(argc, argv);

	hpage_size = gethugepagesize();

	if (hpage_size > 0) {
		verbose_printf("Huge page size is %d bytes\n", hpage_size);
		PASS();
	}

	if (hpage_size < 0)
		CONFIG("No hugepage kernel support");

	FAIL("");
}

