/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2008 Adam Litke, IBM Corporation.
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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <hugetlbfs.h>

#include "hugetests.h"

#define REAL_SYSFS_DIR	"/sys/kernel/mm/hugepages/"
char fake_sysfs[] = "/tmp/sysfs-XXXXXX";
DIR *(*real_opendir)(const char *name);
int cleanup_dir = 0;

long (*real_read_meminfo)(const char *tag);
enum {
	OVERRIDE_OFF,		/* Pass-through to real function */
	OVERRIDE_ON,		/* Ovewrride with local function */
	OVERRIDE_MISSING,	/* Emulate missing support */
};
int meminfo_state = OVERRIDE_OFF;
int sysfs_state = OVERRIDE_OFF;

/*
 * Override opendir so we'll open the fake sysfs dir if intended
 */
DIR *opendir(const char *name)
{
	if (!real_opendir)
		real_opendir = dlsym(RTLD_NEXT, "opendir");

	/* Only override calls to the sysfs dir */
	if (strcmp(name, REAL_SYSFS_DIR))
		return real_opendir(name);

	switch (sysfs_state) {
	case OVERRIDE_OFF:
		return real_opendir(name);
	case OVERRIDE_ON:
		/* Only safe to override of fake_sysfs was set up */
		if (cleanup_dir)
			return real_opendir(fake_sysfs);
		else
			FAIL("Trying to override opendir before initializing "
				"fake_sysfs directory\n");
	default:
		errno = ENOENT;
		return NULL;
	}
}

#define HPAGE_SIZE (16 * 1024)

/*
 * Override read_meminfo to simulate various conditions
 */
long read_meminfo(const char *tag)
{
	if (!real_read_meminfo)
		real_read_meminfo = dlsym(RTLD_NEXT, "read_meminfo");

	/* Only override calls that check the page size */
	if (strcmp(tag, "Hugepagesize:"))
		return real_read_meminfo(tag);

	switch (meminfo_state) {
		case OVERRIDE_OFF:	return real_read_meminfo(tag);
		case OVERRIDE_ON:	return HPAGE_SIZE;
		default:		return -1;
	}
}

void cleanup_fake_sysfs(void)
{
	DIR *dir;
	struct dirent *ent;
	char fname[PATH_MAX+1];

	cleanup_dir = 0;
	dir = real_opendir(fake_sysfs);
	if (!dir)
		FAIL("opendir %s: %s", fake_sysfs, strerror(errno));

	while ((ent = readdir(dir))) {
		if (strncmp(ent->d_name, "hugepages-", 10))
			continue;
		snprintf(fname, PATH_MAX, "%s/%s", fake_sysfs,
			ent->d_name);
		if (rmdir(fname))
			FAIL("rmdir %s: %s", fake_sysfs, strerror(errno));
	}
	closedir(dir);
	if (rmdir(fake_sysfs))
		FAIL("rmdir %s: %s", fake_sysfs, strerror(errno));
}

void setup_fake_sysfs(long sizes[], int n_elem)
{
	int i;
	char fname[PATH_MAX+1];

	if (cleanup_dir)
		cleanup_fake_sysfs();

	if (!mkdtemp(fake_sysfs))
		FAIL("mkdtemp: %s", strerror(errno));
	cleanup_dir = 1;

	for (i = 0; i < n_elem; i++) {
		snprintf(fname, PATH_MAX, "%s/hugepages-%lukB", fake_sysfs,
				sizes[i] / 1024);
		if (mkdir(fname, 0700))
			FAIL("mkdir %s: %s", fname, strerror(errno));
	}
}

void cleanup(void)
{
	if (cleanup_dir)
		cleanup_fake_sysfs();
}

void validate_sizes(int line, long actual_sizes[], int actual, int max_actual,
			long expected_sizes[], int expected)
{
	int i, j;
	if (expected != actual)
		FAIL("Line %i: Wrong number of sizes returned -- expected %i "
			"got %i", line, expected, actual);

	for (i = 0; i < expected; i++) {
		for (j = 0; j < actual; j++)
			if (expected_sizes[i] == actual_sizes[j])
				break;
		if (j >= actual)
			FAIL("Line %i: Expected size %li not found in actual "
				"results", line, expected_sizes[i]);
	}

	for (i = expected; i < max_actual; i++)
		if (actual_sizes[i] != 42)
			FAIL("Line %i: Wrote past official limit at %i",
				line, i);
}

#define MAX 16
#define EXPECT_SIZES(func, max, count, expected)			\
({									\
	long __a[MAX] = { [0 ... MAX-1] = 42 };				\
	int __na;							\
	int __l = (count < max) ? count : max;				\
									\
	__na = func(__a, max);						\
									\
	validate_sizes(__LINE__, __a, __na, MAX, expected, __l);	\
									\
	__na;								\
})

#define INIT_LIST(a, values...)						\
({									\
	long __e[] = { values };					\
	memcpy(a, __e, sizeof(__e));					\
})

int main(int argc, char *argv[])
{
	long expected_sizes[MAX], actual_sizes[MAX], meminfo_size;
	long base_size = sysconf(_SC_PAGESIZE);

	test_init(argc, argv);

	/*
	 * ===
	 * Argment error checking tests
	 * ===
	 */
	meminfo_state = OVERRIDE_OFF;
	sysfs_state = OVERRIDE_OFF;

	if (gethugepagesizes(actual_sizes, -1) != -1 || errno != EINVAL)
		FAIL("Mishandled params (n_elem < 0)");
	if (gethugepagesizes(NULL, 1) != -1 || errno != EINVAL)
		FAIL("Mishandled params (pagesizes == NULL, n_elem > 0)");

	if (getpagesizes(actual_sizes, -1) != -1 || errno != EINVAL)
		FAIL("Mishandled params (n_elem < 0)");
	if (getpagesizes(NULL, 1) != -1 || errno != EINVAL)
		FAIL("Mishandled params (pagesizes == NULL, n_elem > 0)");

	/*
	 * ===
	 * Test some corner cases using a fake system configuration
	 * ===
	 */

	/*
	 * Check handling when /proc/meminfo indicates no huge page support
	 */
	meminfo_state = OVERRIDE_MISSING;

	EXPECT_SIZES(gethugepagesizes, MAX, 0, expected_sizes);

	INIT_LIST(expected_sizes, base_size);
	EXPECT_SIZES(getpagesizes, MAX, 1, expected_sizes);

	/*
	 * When the sysfs heirarchy is not present ...
	 */
	sysfs_state = OVERRIDE_MISSING;

	/* ... only the meminfo size is returned. */
	meminfo_state = OVERRIDE_ON;
	meminfo_size = read_meminfo("Hugepagesize:") * 1024;

	INIT_LIST(expected_sizes, meminfo_size);
	EXPECT_SIZES(gethugepagesizes, MAX, 1, expected_sizes);

	INIT_LIST(expected_sizes, base_size, meminfo_size);
	EXPECT_SIZES(getpagesizes, MAX, 2, expected_sizes);

	/*
	 * When sysfs defines additional sizes ...
	 */
	INIT_LIST(expected_sizes, meminfo_size, 1024 * 1024, 2048 * 1024);

	setup_fake_sysfs(expected_sizes, 3);
	sysfs_state = OVERRIDE_ON;

	/* ... make sure all sizes are returned without duplicates */
	/* ... while making sure we do not overstep our limit */
	EXPECT_SIZES(gethugepagesizes, MAX, 3, expected_sizes);
	EXPECT_SIZES(gethugepagesizes, 1, 3, expected_sizes);
	EXPECT_SIZES(gethugepagesizes, 2, 3, expected_sizes);
	EXPECT_SIZES(gethugepagesizes, 3, 3, expected_sizes);
	EXPECT_SIZES(gethugepagesizes, 4, 3, expected_sizes);

	INIT_LIST(expected_sizes,
			base_size, meminfo_size, 1024 * 1024, 2048 * 1024);
	EXPECT_SIZES(getpagesizes, MAX, 4, expected_sizes);
	EXPECT_SIZES(getpagesizes, 1, 4, expected_sizes);
	EXPECT_SIZES(getpagesizes, 2, 4, expected_sizes);
	EXPECT_SIZES(getpagesizes, 3, 4, expected_sizes);
	EXPECT_SIZES(getpagesizes, 4, 4, expected_sizes);
	EXPECT_SIZES(getpagesizes, 5, 4, expected_sizes);

	/* ... we can check how many sizes are supported. */
	if (gethugepagesizes(NULL, 0) != 3)
		FAIL("Unable to check the number of supported sizes");

	if (getpagesizes(NULL, 0) != 4)
		FAIL("Unable to check the number of supported sizes");

	PASS();
}
