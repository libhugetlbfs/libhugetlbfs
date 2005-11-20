#ifndef _HUGETLBFS_H
#define _HUGETLBFS_H

#define HUGETLBFS_MAGIC	0x958458f6

int gethugepagesize(void);
int hugetlbfs_test_path(const char *mount);
const char *hugetlbfs_find_path(void);
int hugetlbfs_unlinked_fd(void);

/* Debugging functions */
int hugetlbfs_test_addr(void *p);

#endif /* _HUGETLBFS_H */
