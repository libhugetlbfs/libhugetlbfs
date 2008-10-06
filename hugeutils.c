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

#define _LARGEFILE64_SOURCE /* Need this for statfs64 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <features.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <linux/types.h>
#include <linux/unistd.h>

#include "libhugetlbfs_internal.h"
#include "hugetlbfs.h"

static int hugepagesize_errno; /* = 0 */

#define MAX_HPAGE_SIZES 10
struct hpage_size hpage_sizes[MAX_HPAGE_SIZES];
static int nr_hpage_sizes;
static int hpage_sizes_default_idx = -1;

/********************************************************************/
/* Internal functions                                               */
/********************************************************************/

#define BUF_SZ 256
#define MEMINFO_SIZE	2048

/*
 * Convert a quantity in a given unit to the next smallest unit by
 * multiplying the quantity by 1024 (eg. convert 1MB to 1024kB).
 * If the conversion would overflow the variable, return LONG_MAX to signify
 * the error.
 */
static inline long size_to_smaller_unit(long size)
{
	if (size == LONG_MAX || size * 1024 < size)
		return LONG_MAX;
	else
		return size * 1024;
}

/*
 * Convert a page size string with an optional unit suffix into a page size
 * in bytes.
 *
 * On error, -1 is returned and errno is set appropriately:
 * 	EINVAL		- str could not be parsed or was not greater than zero
 *	EOVERFLOW	- Overflow when converting from the specified units
 */
long __lh_parse_page_size(const char *str)
{
	char *pos;
	long size;

	errno = 0;
	size = strtol(str, &pos, 0);
	/* Catch strtoul errors and sizes that overflow the native word size */
	if (errno || str == pos || size <= 0) {
		if (errno == ERANGE)
			errno = EOVERFLOW;
		else
			errno = EINVAL;
		return -1;
	}

	switch (*pos) {
	case 'G':
	case 'g':
		size = size_to_smaller_unit(size);
	case 'M':
	case 'm':
		size = size_to_smaller_unit(size);
	case 'K':
	case 'k':
		size = size_to_smaller_unit(size);
	}

	if (size == LONG_MAX) {
		errno = EOVERFLOW;
		return -1;
	} else
		return size;
}

static long read_meminfo(const char *tag)
{
	int fd;
	char buf[MEMINFO_SIZE];
	int len, readerr;
	char *p, *q;
	long val;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0) {
		ERROR("Couldn't open /proc/meminfo (%s)\n", strerror(errno));
		return -1;
	}

	len = read(fd, buf, sizeof(buf));
	readerr = errno;
	close(fd);
	if (len < 0) {
		ERROR("Error reading /proc/meminfo (%s)\n", strerror(readerr));
		return -1;
	}
	if (len == sizeof(buf)) {
		ERROR("/proc/meminfo is too large\n");
		return -1;
	}
	buf[len] = '\0';

	p = strstr(buf, tag);
	if (!p)
		return -1; /* looks like the line we want isn't there */

	p += strlen(tag);
	errno = 0;
	val = strtol(p, &q, 0);
	if (errno != 0) {
		if (errno == ERANGE && val == LONG_MAX)
			ERROR("Value of %s in /proc/meminfo overflows long\n", tag);
		else
			ERROR("strtol() failed (%s)\n", strerror(errno));
		return -1;
	}
	if (! isspace(*q)) {
		ERROR("Couldn't parse /proc/meminfo value\n");
		return -1;
	}

	return val;
}

static int hpage_size_to_index(unsigned long size)
{
	int i;

	for (i = 0; i < nr_hpage_sizes; i++)
		if (hpage_sizes[i].pagesize == size)
			return i;
	return -1;
}

static void probe_default_hpage_size(void)
{
	char *env;
	long size;
	int index;

	if (nr_hpage_sizes == 0) {
		DEBUG("No configured huge page sizes\n");
		hpage_sizes_default_idx = -1;
		return;
	}

	/*
	 * Check if the user specified a default size, otherwise use the
	 * system default size as reported by /proc/meminfo.
	 */
	env = getenv("HUGETLB_DEFAULT_PAGE_SIZE");
	if (env && strlen(env) > 0)
		size = __lh_parse_page_size(env);
	else {
		size = read_meminfo("Hugepagesize:");
		size *= 1024; /* convert from kB to B */
	}

	if (size >= 0) {
		index = hpage_size_to_index(size);
		if (index >= 0)
			hpage_sizes_default_idx = index;
		else {
			DEBUG("No mount point found for default huge page "
				"size. Using first available mount point.\n");
			hpage_sizes_default_idx = 0;
		}
	} else {
		DEBUG("Unable to determine default huge page size\n");
		hpage_sizes_default_idx = -1;
	}
}

static void add_hugetlbfs_mount(char *path, int user_mount)
{
	int idx;
	long size;

	if (strlen(path) > PATH_MAX)
		return;

	if (!hugetlbfs_test_path(path)) {
		WARNING("%s is not a hugetlbfs mount point, ignoring\n", path);
		return;
	}

	size = hugetlbfs_test_pagesize(path);
	if (size < 0) {
		DEBUG("Unable to detect page size for path %s\n", path);
		return;
	}

	idx = hpage_size_to_index(size);
	if (idx < 0) {
		if (nr_hpage_sizes >= MAX_HPAGE_SIZES) {
			WARNING("Maximum number of huge page sizes exceeded, "
				"ignoring %lukB page size\n", size);
			return;
		}

		idx = nr_hpage_sizes;
		hpage_sizes[nr_hpage_sizes++].pagesize = size;
	}

	if (strlen(hpage_sizes[idx].mount)) {
		if (user_mount)
			WARNING("Mount point already defined for size %li, "
				"ignoring %s\n", size, path);
		return;
	}

	strcpy(hpage_sizes[idx].mount, path);
}

static void debug_show_page_sizes(void)
{
	int i;

	DEBUG("Detected page sizes:\n");
	for (i = 0; i < nr_hpage_sizes; i++)
		DEBUG("   Size: %li kB %s  Mount: %s\n",
			hpage_sizes[i].pagesize / 1024,
			i == hpage_sizes_default_idx ? "(default)" : "",
			hpage_sizes[i].mount); 
}

#define LINE_MAXLEN	2048
static void find_mounts(void)
{
	int fd;
	char path[PATH_MAX+1];
	char line[LINE_MAXLEN + 1];
	char *eol;
	int bytes, err;
	off_t offset;

	fd = open("/proc/mounts", O_RDONLY);
	if (fd < 0) {
		fd = open("/etc/mtab", O_RDONLY);
		if (fd < 0) {
			ERROR("Couldn't open /proc/mounts or /etc/mtab (%s)\n",
				strerror(errno));
			return;
		}
	}

	while ((bytes = read(fd, line, LINE_MAXLEN)) > 0) {
		line[LINE_MAXLEN] = '\0';
		eol = strchr(line, '\n');
		if (!eol) {
			ERROR("Line too long when parsing mounts\n");
			break;
		}

		/*
		 * Truncate the string to just one line and reset the file
		 * to begin reading at the start of the next line.
		 */
		*eol = '\0';
		offset = bytes - (eol + 1 - line);
		lseek(fd, -offset, SEEK_CUR);

		err = sscanf(line, "%*s %" stringify(PATH_MAX) "s hugetlbfs ",
			path);
		if ((err == 1) && (hugetlbfs_test_path(path) == 1))
			add_hugetlbfs_mount(path, 0);
	}
	close(fd);
}

void __lh_setup_mounts(void)
{
	char *env;
	int do_scan = 1;

	/* If HUGETLB_PATH is set, only add mounts specified there */
	env = getenv("HUGETLB_PATH");
	while (env) {
		char path[PATH_MAX + 1];
		char *next = strchrnul(env, ':');

		do_scan = 0;
		if (next - env > PATH_MAX) {
			ERROR("Path too long in HUGETLB_PATH -- "
				"ignoring environment\n");
			break;
		}

		strncpy(path, env, next - env);
		path[next - env] = '\0';
		add_hugetlbfs_mount(path, 1);

		/* skip the ':' token */
		env = *next == '\0' ? NULL : next + 1;
	}

	/* Then probe all mounted filesystems */
	if (do_scan)
		find_mounts();

	probe_default_hpage_size();
	if (__hugetlbfs_debug)
		debug_show_page_sizes();
}

/********************************************************************/
/* Library user visible functions                                   */
/********************************************************************/

/*
 * NOTE: This function uses data that is initialized by
 * __lh_setup_mounts() which is called during libhugetlbfs initialization.
 *
 * returns:
 *   on success, size of a huge page in number of bytes
 *   on failure, -1
 *	errno set to ENOSYS if huge pages are not supported
 *	errno set to EOVERFLOW if huge page size would overflow return type
 */
long gethugepagesize(void)
{
	long hpage_size;

	/* Are huge pages available and have they been initialized? */
	if (hpage_sizes_default_idx == -1) {
		errno = hugepagesize_errno = ENOSYS;
		return -1;
	}

	errno = 0;
	hpage_size = hpage_sizes[hpage_sizes_default_idx].pagesize;
	return hpage_size;
}

int hugetlbfs_test_path(const char *mount)
{
	struct statfs64 sb;
	int err;

	/* Bugs in the 32<->64 translation code in pre-2.6.15 kernels
	 * mean that plain statfs() returns bogus errors on hugetlbfs
	 * filesystems.  Use statfs64() to work around. */
	err = statfs64(mount, &sb);
	if (err)
		return -1;

	return (sb.f_type == HUGETLBFS_MAGIC);
}

/* Return the page size for the given mount point in bytes */
long hugetlbfs_test_pagesize(const char *mount)
{
	struct statfs64 sb;
	int err;

	err = statfs64(mount, &sb);
	if (err)
		return -1;

	if ((sb.f_bsize <= 0) || (sb.f_bsize > LONG_MAX))
		return -1;

	return sb.f_bsize;
}

const char *hugetlbfs_find_path_for_size(long page_size)
{
	char *path;
	int idx;

	if (page_size == 0)
		idx = hpage_sizes_default_idx;
	else
		idx = hpage_size_to_index(page_size);

	if (idx >= 0) {
		path = hpage_sizes[idx].mount;
		if (strlen(path))
			return path;
	}
	return NULL;
}

const char *hugetlbfs_find_path(void)
{
	return hugetlbfs_find_path_for_size(0);
}

int hugetlbfs_unlinked_fd_for_size(long page_size)
{
	const char *path;
	char name[PATH_MAX+1];
	int fd;

	path = hugetlbfs_find_path_for_size(page_size);
	if (!path)
		return -1;

	name[sizeof(name)-1] = '\0';

	strcpy(name, path);
	strncat(name, "/libhugetlbfs.tmp.XXXXXX", sizeof(name)-1);
	/* FIXME: deal with overflows */

	fd = mkstemp64(name);

	if (fd < 0) {
		ERROR("mkstemp() failed: %s\n", strerror(errno));
		return -1;
	}

	unlink(name);

	return fd;
}

int hugetlbfs_unlinked_fd(void)
{
	return hugetlbfs_unlinked_fd_for_size(0);
}

#define IOV_LEN 64
int __lh_hugetlbfs_prefault(int fd, void *addr, size_t length)
{
	/*
	 * The NUMA users of libhugetlbfs' malloc feature are
	 * expected to use the numactl program to specify an
	 * appropriate policy for hugepage allocation
	 *
	 * Use readv(2) to instantiate the hugepages unless HUGETLB_NO_PREFAULT
	 * is set. If we instead returned a hugepage mapping with insufficient
	 * hugepages, the VM system would kill the process when the
	 * process tried to access the missing memory.
	 *
	 * The value of this environment variable is read during library
	 * initialisation and sets __hugetlbfs_prefault accordingly. If 
	 * prefaulting is enabled and we can't get all that were requested,
	 * -ENOMEM is returned. The caller is expected to release the entire
	 * mapping and optionally it may recover by mapping base pages instead.
	 */
	if (__hugetlbfs_prefault) {
		int i;
		size_t offset;
		struct iovec iov[IOV_LEN];
		int ret;

		for (offset = 0; offset < length; ) {
			for (i = 0; i < IOV_LEN && offset < length; i++) {
				iov[i].iov_base = addr + offset;
				iov[i].iov_len = 1;
				offset += gethugepagesize();
			}
			ret = readv(fd, iov, i);
			if (ret != i) {
				DEBUG("Got %d of %d requested; err=%d\n", ret,
						i, ret < 0 ? errno : 0);
				WARNING("Failed to reserve %ld huge pages "
						"for new region\n",
						length / gethugepagesize());
				return -ENOMEM;
			}
		}
	}

	return 0;
}

/********************************************************************/
/* Library user visible DIAGNOSES/DEBUGGING ONLY functions          */
/********************************************************************/

#define MAPS_BUF_SZ 4096
long __lh_dump_proc_pid_maps()
{
	FILE *f;
	char line[MAPS_BUF_SZ];
	size_t ret;

	f = fopen("/proc/self/maps", "r");
	if (!f) {
		ERROR("Failed to open /proc/self/maps\n");
		return -1;
	}

	while (1) {
		ret = fread(line, sizeof(char), MAPS_BUF_SZ, f);
		if (ret < 0) {
			ERROR("Failed to read /proc/self/maps\n");
			return -1;
		}
		if (ret == 0)
			break;
		ret = fwrite(line, sizeof(char), ret, stderr);
		if (ret < 0) {
			ERROR("Failed to write /proc/self/maps to stderr\n");
			return -1;
		}
	}

	fclose(f);
	return 0;
}
