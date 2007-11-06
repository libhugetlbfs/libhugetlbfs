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

#define	SIZE	(32 * 1024 * 1024)

int main(int argc, char **argv)
{
	int is_huge, have_env;
	void *p;

	test_init(argc, argv);

	have_env = getenv("HUGETLB_MORECORE") != NULL;

	p = malloc(SIZE);
	memset(p, 0, SIZE);
	is_huge = test_addr_huge(p+SIZE-1) == 1;
	if (have_env && !is_huge)
		FAIL("Heap not on hugepages");
	if (!have_env && is_huge)
		FAIL("Heap unexpectedly on hugepages");

	free(p);
	if (test_addr_huge(p+SIZE-1) == 1)
		FAIL("Heap did not shrink");
	PASS();
}
