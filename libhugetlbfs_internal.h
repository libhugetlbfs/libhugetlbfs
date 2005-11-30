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

#endif /* _LIBHUGETLBFS_INTERNAL_H */
