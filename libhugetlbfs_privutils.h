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
#ifndef _LIBHUGETLBFS_PRIVUTILS_H
#define _LIBHUGETLBFS_PRIVUTILS_H

/* Hugetlb pool counter operations */
/* Keys for reading hugetlb pool counters */
enum {			 /* The number of pages of a given size that ... */
	HUGEPAGES_TOTAL, /*  are allocated to the pool */
	HUGEPAGES_FREE,  /*  are not in use */
	HUGEPAGES_RSVD,  /*  are reserved for possible future use */
	HUGEPAGES_SURP,  /*  are allocated to the pool on demand */
	HUGEPAGES_OC,    /*  can be allocated on demand - maximum */
	HUGEPAGES_MAX_COUNTERS,
};
#define get_huge_page_counter __pu_get_huge_page_counter
long get_huge_page_counter(long pagesize, unsigned int counter);
#define set_huge_page_counter __pu_set_huge_page_counter
int set_huge_page_counter(long pagesize, unsigned int counter,
							unsigned long val);
#define set_nr_hugepages __pu_set_nr_hugepages
int set_nr_hugepages(long pagesize, unsigned long val);
#define set_nr_overcommit_hugepages __pu_set_nr_overcommit_hugepages
int set_nr_overcommit_hugepages(long pagesize, unsigned long val);

#define read_meminfo __pu_read_meminfo
long read_meminfo(const char *tag);

/* Kernel feature testing */
/* This enum defines the bits in a feature bitmask */
enum {
	/* Reservations are created for private mappings */
	HUGETLB_FEATURE_PRIVATE_RESV,
	HUGETLB_FEATURE_NR,
};
#define hugetlbfs_test_feature __pu_hugetlbfs_test_feature
int hugetlbfs_test_feature(int feature_code);

#endif /* _LIBHUGETLBFS_PRIVUTILS_H */
