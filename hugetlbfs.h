#ifndef _HUGETLBFS_H
#define _HUGETLBFS_H

#define HUGETLBFS_MAGIC	0x958458f6

long gethugepagesize(void);
long hugetlbfs_vaddr_granularity(void);
int hugetlbfs_test_path(const char *mount);
const char *hugetlbfs_find_path(void);
int hugetlbfs_unlinked_fd(void);

#define PF_LINUX_HUGETLB	0x100000

#endif /* _HUGETLBFS_H */
