/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2005-2006 David Gibson & Adam Litke, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

void test_get_huge_pages(int num_hugepages)
{
	int err;
	long hpage_size = check_hugepagesize();
	void *p = get_huge_pages(num_hugepages * hpage_size, GHP_DEFAULT);
	if (p == NULL)
		FAIL("get_huge_pages() for %d hugepages", num_hugepages);

	memset(p, 1, hpage_size);

	err = test_addr_huge(p + (num_hugepages -1) * hpage_size);
	if (err != 1)
		FAIL("Returned page is not hugepage");

	free_huge_pages(p);
	err = test_addr_huge(p);
	if (err == 1)
		FAIL("hugepage was not correctly freed");
}

int main(int argc, char *argv[])
{
	test_init(argc, argv);
	check_free_huge_pages(4);
	test_get_huge_pages(1);
	test_get_huge_pages(4);

	PASS();
}
