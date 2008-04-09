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

#include <elf.h>
#include <link.h>

#ifndef __LIBHUGETLBFS__
#error This header should not be included by library users.
#endif /* __LIBHUGETLBFS__ */

#define stringify_1(x)	#x
#define stringify(x)	stringify_1(x)

#define ALIGN(x, a)	(((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_UP(x,a)	(((x) + (a)) & ~((a) - 1))
#define ALIGN_DOWN(x,a) ((x) & ~((a) - 1))

#if defined(__powerpc64__) || defined (__powerpc__)
#define SLICE_LOW_SHIFT		28
#define SLICE_HIGH_SHIFT	40
#elif defined(__ia64__)
#define SLICE_HIGH_SHIFT	63
#endif

extern int __hugetlbfs_verbose;
extern int __hugetlbfs_debug;
extern int __hugetlbfs_prefault;
extern void __hugetlbfs_setup_elflink();
extern void __hugetlbfs_setup_morecore();
extern void __hugetlbfs_setup_debug();
extern char __hugetlbfs_hostname[];

#define ERROR(format, ...) \
	do { \
		if (__hugetlbfs_debug || __hugetlbfs_verbose >= 1) { \
			fprintf(stderr, "libhugetlbfs [%s:%d]: ERROR: " format, __hugetlbfs_hostname, getpid(), ##__VA_ARGS__); \
			fflush(stderr); \
		} \
	} while (0)

#define WARNING(format, ...) \
	do { \
		if (__hugetlbfs_debug || __hugetlbfs_verbose >= 2) { \
			fprintf(stderr, "libhugetlbfs [%s:%d]: WARNING: " format, __hugetlbfs_hostname, getpid(), ##__VA_ARGS__); \
			fflush(stderr); \
		} \
	} while (0)

#define DEBUG(format, ...) \
	do { \
		if (__hugetlbfs_debug || __hugetlbfs_verbose >= 3) { \
			fprintf(stderr, "libhugetlbfs [%s:%d]: " format, __hugetlbfs_hostname, getpid(), ##__VA_ARGS__); \
			fflush(stderr); \
		} \
	} while (0)

#define DEBUG_CONT(...) \
	do { \
		if (__hugetlbfs_debug || __hugetlbfs_verbose >= 3) { \
			fprintf(stderr, __VA_ARGS__); \
			fflush(stderr); \
		} \
	} while (0)

#if defined(__powerpc64__) && !defined(__LP64__)
/* Older binutils fail to provide this symbol */
#define __LP64__
#endif

/* Arch-specific callbacks */
extern int direct_syscall(int sysnum, ...);
extern ElfW(Word) plt_extrasz(ElfW(Dyn) *dyntab);

#endif /* _LIBHUGETLBFS_INTERNAL_H */
