#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hugetests.h"

/* Override the working version from libhugetlbfs */
int hugetlbfs_unlinked_fd(void)
{
	return -1;
}

int main(int argc, char *argv[])
{
	test_init(argc, argv);

	/* All we're testing is that we survive the library attempting
	 * and failing to remap us into hugepages */

	PASS();
}
