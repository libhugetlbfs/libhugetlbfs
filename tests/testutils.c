#include <stdio.h>
#include <stdlib.h>

#include "hugetests.h"

int verbose_test = 1;
char *test_name;

void  __attribute__((weak)) cleanup(void)
{
}

void test_init(int argc, char *argv[])
{
	test_name = argv[0];

	if (getenv("QUIET_TEST"))
		verbose_test = 0;
}
