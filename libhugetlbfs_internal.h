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
#ifndef _LIBHUGETLBFS_INTERNAL_H
#define _LIBHUGETLBFS_INTERNAL_H

#ifndef __LIBHUGETLBFS__
#error This header should not be included by library users.
#endif /* __LIBHUGETLBFS__ */

#define stringify_1(x)	#x
#define stringify(x)	stringify_1(x)

#define ALIGN(x, a)	(((x) + (a) - 1) & ~((a) - 1))

extern int __hugetlbfs_verbose;

#define ERROR(...) \
	if (__hugetlbfs_verbose >= 1) \
		fprintf(stderr, "libhugetlbfs: ERROR: " __VA_ARGS__)

#define WARNING(...) \
	if (__hugetlbfs_verbose >= 2) \
		fprintf(stderr, "libhugetlbfs: WARNING: " __VA_ARGS__)

#define DEBUG(...) \
	if (__hugetlbfs_verbose >= 3) \
		fprintf(stderr, "libhugetlbfs: " __VA_ARGS__)

#define DEBUG_CONT(...) \
	if (__hugetlbfs_verbose >= 3) \
		fprintf(stderr, __VA_ARGS__)

extern void __hugetlbfs_init_debug(void);

struct seg_info {
	void *vaddr;
	unsigned long filesz, memsz;
	int prot;
	int fd;
};

#if defined(__powerpc64__) && !defined(__LP64__)
/* Older binutils fail to provide this symbol */
#define __LP64__
#endif

#endif /* _LIBHUGETLBFS_INTERNAL_H */
