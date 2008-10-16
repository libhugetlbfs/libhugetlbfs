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
#ifndef _HUGETLBFS_H
#define _HUGETLBFS_H

#define HUGETLBFS_MAGIC	0x958458f6

long gethugepagesize(void);
int gethugepagesizes(long pagesizes[], int n_elem);
int getpagesizes(long pagesizes[], int n_elem);
int hugetlbfs_test_path(const char *mount);
long hugetlbfs_test_pagesize(const char *mount);
const char *hugetlbfs_find_path(void);
const char *hugetlbfs_find_path_for_size(long page_size);
int hugetlbfs_unlinked_fd(void);
int hugetlbfs_unlinked_fd_for_size(long page_size);

/* Diagnoses/debugging only functions */
#define dump_proc_pid_maps __lh_dump_proc_pid_maps
long dump_proc_pid_maps(void);

#define PF_LINUX_HUGETLB	0x100000

/*
 * Direct alloc flags and types
 *
 * GHP_DEFAULT - Use a combination of flags deemed to be a sensible default
 * 		by the current implementation of the library
 * GHP_FALLBACK - Use the default hugepage size if possible but fallback to
 * 		smaller pages if necessary
 */
typedef unsigned long ghp_t;
#define GHP_FALLBACK	(0x01UL)
#define GHP_DEFAULT	(0)

/* Direct alloc functions */
void *get_huge_pages(size_t len, ghp_t flags);
void free_huge_pages(void *ptr);

/* Kernel feature testing */
/* This enum defines the bits in a feature bitmask */
enum {
	/* Reservations are created for private mappings */
	HUGETLB_FEATURE_PRIVATE_RESV,
	HUGETLB_FEATURE_NR,
};
int hugetlbfs_test_feature(int feature_code);

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
long get_huge_page_counter(long pagesize, unsigned int counter);
int set_huge_page_counter(long pagesize, unsigned int counter,
							unsigned long val);
int set_nr_hugepages(long pagesize, unsigned long val);
int set_nr_overcommit_hugepages(long pagesize, unsigned long val);
long read_meminfo(const char *tag);
#endif /* _HUGETLBFS_H */
