VERS_1.0 {
	global:
		gethugepagesize;
		hugetlbfs_test_path;
		hugetlbfs_find_path;
		hugetlbfs_unlinked_fd;
	local:
		__lh_*;
};

HTLBFS_2.0 {
	global:
		get_huge_pages;
		free_huge_pages;
};
