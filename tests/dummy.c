#include <stdio.h>
#include <stdlib.h>

#include "hugetests.h"

int main(int argc, char *argv[])
{
	test_init(argc, argv);

	/* If we're even able to load, that's enough */
	PASS();
}

