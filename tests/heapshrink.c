/*
 * Test heap shrinking for libhugetlbfs.
 * Copyright 2007 Cray Inc.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, 5th Floor, Boston, MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "hugetests.h"

/*
 * We cannot test mapping size against huge page size because we are not linked
 * against libhugetlbfs so gethugepagesize() won't work.  So instead we define
 * our MIN_PAGE_SIZE as 64 kB (the largest base page available) and make sure
 * the mapping page size is larger than this.
 */
#define MIN_PAGE_SIZE 65536
#define MAX(a, b) a > b ? a : b

int main(int argc, char **argv)
{
	int is_huge, have_env, shrink_ok, have_helper, tcache_enabled;
	unsigned long long mapping_size;
	void *p;
	long size = MAX(32*1024*1024, kernel_default_hugepage_size());

	test_init(argc, argv);

	have_env = getenv("HUGETLB_MORECORE") != NULL;
	shrink_ok = getenv("HUGETLB_MORECORE_SHRINK") != NULL;
	p = getenv("LD_PRELOAD");
	have_helper = p != NULL && strstr(p, "heapshrink") != NULL;

	/*
	 * After upstream commit: (glibc-2.25.90-688-gd5c3fafc43) glibc has a
	 * new per-thread caching mechanism that will NOT allow this test to
	 * successfully measure if heap has shrunk or not due to the fact that
	 * heap won't have its sized reduced right away.
	 *
	 * In order to disable it you need to have the tunable GLIBC in place.
	 * Unfortunately, it requires to be set before program is loaded, as an
	 * environment variable, since we can't re-initialize malloc() from the
	 * program context (not even with a constructor function), and the
	 * tunable is only evaluated during malloc() initialization.
	 *
	 * GLIBC_TUNABLES=glibc.malloc.tcache_count=0
	 */
	p = getenv("GLIBC_TUNABLES");
	tcache_enabled = p != NULL && strstr(p, "malloc.tcache_count=0");

	p = malloc(size);
	if (!p) {
		if (shrink_ok && have_helper) {
			/* Hitting unexpected behavior in malloc() */
			PASS_INCONCLUSIVE();
		} else
			FAIL("malloc(%ld) failed\n", size);
	}
	memset(p, 0, size);
	mapping_size = get_mapping_page_size(p);
	is_huge = (mapping_size > MIN_PAGE_SIZE);
	if (have_env && !is_huge) {
		if (shrink_ok && have_helper) {
			/* Hitting unexpected behavior in malloc() */
			PASS_INCONCLUSIVE();
		} else
			FAIL("Heap not on hugepages");
	}
	if (!have_env && is_huge)
		FAIL("Heap unexpectedly on hugepages");

	free(p);
	mapping_size = get_mapping_page_size(p+size-1);
	if (shrink_ok && mapping_size > MIN_PAGE_SIZE) {
		if (tcache_enabled)
			FAIL("Heap did not shrink");
		else
			FAIL("Heap didn't shrink. Check malloc.tcache_count=0");
	}

	PASS();
}
