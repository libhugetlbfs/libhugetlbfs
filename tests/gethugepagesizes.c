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
		case OVERRIDE_ON:	return 16 * 1024;
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

#define INIT_SIZES(a, v1, v2, v3) {a[0] = v1; a[1] = v2; a[2] = v3; a[3] = -1;}

void expect_sizes(int line, int expected, int actual,
			long expected_sizes[], long actual_sizes[])
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
}

int main(int argc, char *argv[])
{
	long expected_sizes[4], actual_sizes[4], meminfo_size;
	int nr_sizes;

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

	/*
	 * ===
	 * Test some corner cases using a fake system configuration
	 * ===
	 */

	/*
	 * Check handling when /proc/meminfo indicates no huge page support
	 */
	meminfo_state = OVERRIDE_MISSING;
	if (gethugepagesizes(actual_sizes, 1) != 0 || errno != 0)
		FAIL("Incorrect handling when huge page support is missing");

	/*
	 * When the sysfs heirarchy is not present ...
	 */
	sysfs_state = OVERRIDE_MISSING;

	/* ... only the meminfo size is returned. */
	meminfo_state = OVERRIDE_ON;
	meminfo_size = read_meminfo("Hugepagesize:") * 1024;
	INIT_SIZES(expected_sizes, meminfo_size, -1, -1);

	/* Use 2 to give the function the chance to return too many sizes */
	nr_sizes = gethugepagesizes(actual_sizes, 2);
	expect_sizes(__LINE__, 1, nr_sizes, expected_sizes, actual_sizes);

	/*
	 * When sysfs defines additional sizes ...
	 */
	INIT_SIZES(expected_sizes, meminfo_size, 1024, 2048);
	setup_fake_sysfs(expected_sizes, 3);
	sysfs_state = OVERRIDE_ON;

	/* ... make sure all sizes are returned without duplicates */
	nr_sizes = gethugepagesizes(actual_sizes, 4);
	expect_sizes(__LINE__, 3, nr_sizes, expected_sizes, actual_sizes);

	/* ... we can check how many sizes are supported. */
	if (gethugepagesizes(NULL, 0) != 3)
		FAIL("Unable to check the number of supported sizes");

	PASS();
}
