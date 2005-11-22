#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/mman.h>
#include <errno.h>

#include "hugetlbfs.h"

#include "libhugetlbfs_internal.h"

int __hugetlbfs_verbose = 1;

static int initialized;

void __hugetlbfs_init_debug(void)
{
	char *env;

	if (initialized)
		return;

	env = getenv("HUGETLB_VERBOSE");
	if (env)
		__hugetlbfs_verbose = atoi(env);
}

static void __attribute__ ((constructor)) setup_debug(void)
{
	__hugetlbfs_init_debug();
}

