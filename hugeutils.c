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

#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/file.h>

#include "libhugetlbfs_internal.h"
#include "hugetlbfs.h"

static long hpage_size; /* = 0 */
static char htlb_mount[PATH_MAX+1]; /* = 0 */
static int hugepagesize_errno; /* = 0 */

/********************************************************************/
/* Internal functions                                               */
/********************************************************************/

#define BUF_SZ 256
#define MEMINFO_SIZE	2048

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

/********************************************************************/
/* Library user visible functions                                   */
/********************************************************************/

/*
 * returns:
 *   on success, size of a huge page in number of bytes
 *   on failure, -1
 *	errno set to ENOSYS if huge pages are not supported
 *	errno set to EOVERFLOW if huge page size would overflow return type
 */
long gethugepagesize(void)
{
	long hpage_kb;
	long max_hpage_kb = LONG_MAX / 1024;

	if (hpage_size) {
		errno = hugepagesize_errno;
		return hpage_size;
	}
	errno = 0;

	hpage_kb = read_meminfo("Hugepagesize:");
	if (hpage_kb < 0) {
		hpage_size = -1;
		errno = hugepagesize_errno = ENOSYS;
	} else {
		if (hpage_kb > max_hpage_kb) {
			/* would overflow if converted to bytes */
			hpage_size = -1;
			errno = hugepagesize_errno = EOVERFLOW;
		}
		else
			/* convert from kb to bytes */
			hpage_size = 1024 * hpage_kb;
	}

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

#define MOUNTS_SZ	4096

const char *hugetlbfs_find_path(void)
{
	int err, readerr;
	char *tmp;
	int fd, len;
	char buf[MOUNTS_SZ];

	/* Have we already located a mount? */
	if (*htlb_mount)
		return htlb_mount;

	/* No?  Let's see if we've been told where to look */
	tmp = getenv("HUGETLB_PATH");
	if (tmp) {
		err = hugetlbfs_test_path(tmp);
		if (err < 0) {
			ERROR("Can't statfs() \"%s\" (%s)\n",
			      tmp, strerror(errno));
			return NULL;
		} else if (err == 0) {
			ERROR("\"%s\" is not a hugetlbfs mount\n", tmp);
			return NULL;
		}
		strncpy(htlb_mount, tmp, sizeof(htlb_mount)-1);
		return htlb_mount;
	}

	/* Oh well, let's go searching for a mountpoint */
	fd = open("/proc/mounts", O_RDONLY);
	if (fd < 0) {
		fd = open("/etc/mtab", O_RDONLY);
		if (fd < 0) {
			ERROR("Couldn't open /proc/mounts or /etc/mtab (%s)\n",
			      strerror(errno));
			return NULL;
		}
	}

	len = read(fd, buf, sizeof(buf));
	readerr = errno;
	close(fd);
	if (len < 0) {
		ERROR("Error reading mounts (%s)\n", strerror(errno));
		return NULL;
	}
	if (len >= sizeof(buf)) {
		ERROR("/proc/mounts is too long\n");
		return NULL;
	}
	buf[sizeof(buf)-1] = '\0';

	tmp = buf;
	while (tmp) {
		err = sscanf(tmp,
			     "%*s %" stringify(PATH_MAX)
			     "s hugetlbfs ",
			     htlb_mount);
		if ((err == 1) && (hugetlbfs_test_path(htlb_mount) == 1))
			return htlb_mount;

		memset(htlb_mount, 0, sizeof(htlb_mount));

		tmp = strchr(tmp, '\n');
		if (tmp)
			tmp++;
	}

	WARNING("Could not find hugetlbfs mount point in /proc/mounts. "
			"Is it mounted?\n");

	return NULL;
}

int hugetlbfs_unlinked_fd(void)
{
	const char *path;
	char name[PATH_MAX+1];
	int fd;

	path = hugetlbfs_find_path();
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

/********************************************************************/
/* Library user visible DIAGNOSES/DEBUGGING ONLY functions          */
/********************************************************************/

long hugetlbfs_num_free_pages(void)
{
	return read_meminfo("HugePages_Free:");
}

long hugetlbfs_num_pages(void)
{
	return read_meminfo("HugePages_Total:");
}

#define MAPS_BUF_SZ 4096
long dump_proc_pid_maps()
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
