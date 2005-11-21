#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/mman.h>
#include <errno.h>

#include "hugetlbfs.h"

#include "libhugetlbfs_internal.h"

int __hugetlbfs_verbose = 1;

static void __attribute__ ((constructor)) setup_debug(void)
{
	char *env;

	env = getenv("HUGETLB_VERBOSE");
	if (env)
		__hugetlbfs_verbose = atoi(env);
}
