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
int hugetlbfs_test_path(const char *mount);
const char *hugetlbfs_find_path(void);
int hugetlbfs_unlinked_fd(void);

/* Diagnoses/debugging only functions */
long hugetlbfs_num_free_pages(void);
long hugetlbfs_num_pages(void);
long dump_proc_pid_maps(void);

#define PF_LINUX_HUGETLB	0x100000

#endif /* _HUGETLBFS_H */
