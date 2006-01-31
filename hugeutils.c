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
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/statfs.h>

#include "hugetlbfs.h"

#include "libhugetlbfs_internal.h"

static int hpage_size; /* = 0 */
static char htlb_mount[PATH_MAX+1]; /* = 0 */

/********************************************************************/
/* Internal functions                                               */
/********************************************************************/

#define BUF_SZ 256
#if 0

struct linebuf {
	char buf[LINE_SZ];
};

static char *get_line(int fd, struct linebuf *buf)
{
}

static int get_line(int fd, char *buf, int bufsize, int tailoffset)
{
	int tailsize = bufsize - tailoffset;
	int datalen;
	int rc;

	if (tailsize < 0) {
		/* Chew remainders */
	}

	if (tailoffset)
		memmove(buf, buf + lastline, bufsize - lastline);

	rc = read(fd, buf + tailsize, bufsize - tailsize);

	if (tail) {
		
	}
}

#endif

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
	val = strtol(p, &q, 0);
	if (! isspace(*q)) {
		ERROR("Couldn't parse /proc/meminfo value\n");
		return -1;
	}

	return val;
}

/********************************************************************/
/* Library user visible functions                                   */
/********************************************************************/

long gethugepagesize(void)
{
	int hpage_kb;

	if (hpage_size)
		return hpage_size;

	hpage_kb = read_meminfo("Hugepagesize:");
	if (hpage_kb < 0)
		hpage_size = -1;
	else
		/* convert from kb to bytes */
		hpage_size = 1024 * hpage_kb;

	return hpage_size;
}

long hugetlbfs_vaddr_granularity(void)
{
#if defined(__powerpc64__)
	return (1L << 40);
#elif defined(__powerpc__)
	return (1L << 28);
#else
	return gethugepagesize();
#endif
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

	return NULL;
}

int hugetlbfs_unlinked_fd(void)
{
	const char *path;
	char name[PATH_MAX+1];
	int fd;

	name[sizeof(name)-1] = '\0';
	path  = hugetlbfs_find_path();
	if (! path)
		return -1;

	strcpy(name, hugetlbfs_find_path());
	strncat(name, "/libhugetlbfs.tmp.XXXXXX", sizeof(name)-1);
	/* FIXME: deal with overflows */

	fd = mkstemp(name);

	if (fd >= 0)
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

#if 0

int get_sysctl(char *file)
{
	FILE* f;
	char buf[BUF_SZ];

	f = fopen(file, "r");
	if (!f) {
		printf("Failed to open %s for reading\n", file);
		return(-1);
	}
	fgets(buf, BUF_SZ, f);
	fclose(f);
	return atoi(buf);
}

int set_sysctl_str(char *file, char *str)
{
	FILE* f;

	f = fopen(file, "w");
	if (!f) {
		printf("Unable to open %s for writing\n", file);
		return(-1);
	}
	fputs(str, f);
	fclose(f);
	return(0);
}

int set_sysctl(char *file, int val)
{
	char buf[BUF_SZ];

	snprintf(buf, BUF_SZ*sizeof(char), "%i", val);
	return set_sysctl_str(file, buf);	
}

int mount_hugetlbfs(char *mountpoint, size_t strsz)
{
	char *cmd;
	size_t cmdsz;

	if (strsz > BUF_SZ || strsz < 20)
		return -1;

	cmdsz = (30 + strsz) * sizeof(char);
	cmd = malloc(cmdsz);
	if (!cmd)
		return -1;

//	snprintf(mountpoint, strsz, "/tmp/hugetlbfs");

	mkdir(mountpoint, 0777);
	
	snprintf(cmd, cmdsz, "mount -t hugetlbfs hugetlbfs %s", mountpoint); 
	return (system(cmd));
}

int find_mount_hugetlbfs(char *mountpoint, size_t strsz)
{
	int ret;

	ret = find_hugetlbfs_mount(mountpoint, strsz);

	switch (ret) {
		case 1:
			return mount_hugetlbfs(mountpoint, strsz);
		default:
			return ret;
	}
}

int set_nr_hugepages(int num)
{
	return set_sysctl("/proc/sys/vm/nr_hugepages", num);
}

int alloc_hugepages(int num)
{
	int cur_free = get_nr_free_huge_pages();
	int cur_total = get_nr_total_huge_pages();
	int cur_used = cur_total - cur_free;
	int ret;

	if (cur_free < num) {
		ret = set_nr_hugepages(cur_used + num);
		if (ret == -1)
			goto error;
		if (get_nr_free_huge_pages() < num)
			goto error;
	}
	return 0;
error:
	printf("Failed to allocate enough huge pages\n");
	return -1;
}

int shmem_setup(int nr_hugepages)
{
	int ret = 0;
	char buf[BUF_SZ];
	int hpage_size = get_hugepage_size();
	unsigned long bytes = nr_hugepages * hpage_size * 1024;
	
	snprintf(buf, sizeof(char)*BUF_SZ, "%lu", bytes);

	if (get_sysctl("/proc/sys/kernel/shmall") < bytes)
		ret = set_sysctl_str("/proc/sys/kernel/shmall", buf);
	if (get_sysctl("/proc/sys/kernel/shmmax") < bytes)
		ret += set_sysctl_str("/proc/sys/kernel/shmmax", buf);
	if (ret)
		return -1;
	else
		return 0;
}
#endif
