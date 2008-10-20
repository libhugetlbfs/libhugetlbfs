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

/*
 * This file should only contain definitions of functions, data types, and
 * constants which are part of the published libhugetlfs API.  Functions
 * exported here must also be listed in version.lds.
 */

#ifndef _HUGETLBFS_H
#define _HUGETLBFS_H

#define HUGETLBFS_MAGIC	0x958458f6

long gethugepagesize(void);
int gethugepagesizes(long pagesizes[], int n_elem);
int getpagesizes(long pagesizes[], int n_elem);
int hugetlbfs_test_path(const char *mount);
const char *hugetlbfs_find_path(void);
const char *hugetlbfs_find_path_for_size(long page_size);
int hugetlbfs_unlinked_fd(void);
int hugetlbfs_unlinked_fd_for_size(long page_size);

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

#endif /* _HUGETLBFS_H */
