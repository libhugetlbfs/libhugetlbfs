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

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "hugetests.h"

#define HUGETLBFS_MAGIC	0x958458f6
#define BUF_SZ 1024
#define MEMINFO_SZ 2048

int verbose_test = 1;
char *test_name;

void check_free_huge_pages(int nr_pages_needed)
{
	int freepages = get_pool_counter(HUGEPAGES_FREE, 0);
	if (freepages < nr_pages_needed)
		CONFIG("Must have at least %i free hugepages", nr_pages_needed);
}

void check_must_be_root(void)
{
	uid_t uid = getuid();
	if (uid != 0)
		CONFIG("Must be root");
}

void check_hugetlb_shm_group(void)
{
	int fd;
	ssize_t ret;
	char gid_buffer[64] = {0};
	gid_t hugetlb_shm_group;
	gid_t gid = getgid();
	uid_t uid = getuid();

	/* root is an exception */
	if (uid == 0)
		return;

	fd = open("/proc/sys/vm/hugetlb_shm_group", O_RDONLY);
	if (fd < 0)
		ERROR("Unable to open /proc/sys/vm/hugetlb_shm_group: %s",
							strerror(errno));
	ret = read(fd, &gid_buffer, sizeof(gid_buffer));
	if (ret < 0)
		ERROR("Unable to read /proc/sys/vm/hugetlb_shm_group: %s",
							strerror(errno));
	hugetlb_shm_group = atoi(gid_buffer);
	close(fd);
	if (hugetlb_shm_group != gid)
		CONFIG("Do not have permission to use SHM_HUGETLB");
}

void  __attribute__((weak)) cleanup(void)
{
}

#if 0
static void segv_handler(int signum, siginfo_t *si, void *uc)
{
	FAIL("Segmentation fault");
}
#endif

static void sigint_handler(int signum, siginfo_t *si, void *uc)
{
	cleanup();
	fprintf(stderr, "%s: %s (pid=%d)\n", test_name,
		strsignal(signum), getpid());
	exit(RC_BUG);
}

void test_init(int argc, char *argv[])
{
	int err;
	struct sigaction sa_int = {
		.sa_sigaction = sigint_handler,
	};

	test_name = argv[0];

	err = sigaction(SIGINT, &sa_int, NULL);
	if (err)
		FAIL("Can't install SIGINT handler: %s", strerror(errno));

	if (getenv("QUIET_TEST"))
		verbose_test = 0;

	verbose_printf("Starting testcase \"%s\", pid %d\n",
		       test_name, getpid());
}

#define MAPS_BUF_SZ 4096

static int read_maps(unsigned long addr, char *buf)
{
	FILE *f;
	char line[MAPS_BUF_SZ];
	char *tmp;

	f = fopen("/proc/self/maps", "r");
	if (!f) {
		ERROR("Failed to open /proc/self/maps: %s\n", strerror(errno));
		return -1;
	}

	while (1) {
		unsigned long start, end, off, ino;
		int ret;

		tmp = fgets(line, MAPS_BUF_SZ, f);
		if (!tmp)
			break;

		buf[0] = '\0';
		ret = sscanf(line, "%lx-%lx %*s %lx %*s %ld %255s",
			     &start, &end, &off, &ino,
			     buf);
		if ((ret < 4) || (ret > 5)) {
			ERROR("Couldn't parse /proc/self/maps line: %s\n",
			      line);
			fclose(f);
			return -1;
		}

		if ((start <= addr) && (addr < end)) {
			fclose(f);
			return 1;
		}
	}

	fclose(f);
	return 0;
}

/* We define this function standalone, rather than in terms of
 * hugetlbfs_test_path() so that we can use it without -lhugetlbfs for
 * testing PRELOAD */
int test_addr_huge(void *p)
{
	char name[256];
	char *dirend;
	int ret;
	struct statfs64 sb;

	ret = read_maps((unsigned long)p, name);
	if (ret < 0)
		return ret;
	if (ret == 0) {
		verbose_printf("Couldn't find address %p in /proc/self/maps\n",
			       p);
		return -1;
	}

	/* looks like a filename? */
	if (name[0] != '/')
		return 0;

	/* Truncate the filename portion */

	dirend = strrchr(name, '/');
	if (dirend && dirend > name) {
		*dirend = '\0';
	}

	ret = statfs64(name, &sb);
	if (ret)
		return -1;

	return (sb.f_type == HUGETLBFS_MAGIC);
}

struct hugetlb_pool_counter_info_t hugetlb_counter_info[] = {
	[HUGEPAGES_TOTAL] = {
		.meminfo_key	= "HugePages_Total:",
		.sysfs_file	= "nr_hugepages",
	},
	[HUGEPAGES_FREE] = {
		.meminfo_key	= "HugePages_Free:",
		.sysfs_file	= "free_hugepages",
	},
	[HUGEPAGES_RSVD] = {
		.meminfo_key	= "HugePages_Rsvd:",
		.sysfs_file	= "resv_hugepages",
	},
	[HUGEPAGES_SURP] = {
		.meminfo_key	= "HugePages_Surp:",
		.sysfs_file	= "surplus_hugepages",
	},
	[HUGEPAGES_OC] = {
		.meminfo_key	= NULL,
		.sysfs_file	= "nr_overcommit_hugepages"
	},
};

long file_read_ulong(char *file, const char *tag)
{
	int fd;
	char buf[MEMINFO_SZ];
	int len, readerr;
	char *p, *q;
	long val;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		ERROR("Couldn't open %s: %s\n", file, strerror(errno));
		return -1;
	}

	len = read(fd, buf, sizeof(buf));
	readerr = errno;
	close(fd);
	if (len < 0) {
		ERROR("Error reading %s: %s\n", file, strerror(errno));
		return -1;
	}
	if (len == sizeof(buf)) {
		ERROR("%s is too large\n", file);
		return -1;
	}
	buf[len] = '\0';

	/* Search for a tag if provided */
	if (tag) {
		p = strstr(buf, tag);
		if (!p)
			return -1; /* looks like the line we want isn't there */
		p += strlen(tag);
	} else
		p = buf;

	val = strtol(p, &q, 0);
	if (! isspace(*q)) {
		ERROR("Couldn't parse %s value\n", file);
		return -1;
	}

	return val;
}

int file_write_ulong(char *file, unsigned long val)
{
	FILE *f;
	int ret;

	f = fopen(file, "w");
	if (!f) {
		ERROR("Couldn't open %s: %s\n", file, strerror(errno));
		return -1;
	}

	ret = fprintf(f, "%lu", val);
	fclose(f);
	return ret > 0 ? 0 : -1;
}

int select_pool_counter(unsigned int counter, unsigned long pagesize_kb,
				char *filename, char **key)
{
	long default_size;
	char *meminfo_key;
	char *sysfs_file;

	if (counter >= HUGEPAGES_MAX_COUNTERS) {
		ERROR("Invalid counter specified\n");
		return -1;
	}

	meminfo_key = hugetlb_counter_info[counter].meminfo_key;
	sysfs_file = hugetlb_counter_info[counter].sysfs_file;
	if (key)
		*key = NULL;

	/* 
	 * Get the meminfo page size.
	 * This could be made more efficient if utility functions were shared
	 * between libhugetlbfs and the test suite.  For now we will just
	 * read /proc/meminfo. 
	 */
	default_size = file_read_ulong("/proc/meminfo", "Hugepagesize:");
	if (default_size < 0) {
		ERROR("Cannot determine the default page size\n");
		return -1;
	}

	/* Convert a pagesize_kb of 0 to the libhugetlbfs default size */
	if (pagesize_kb == 0)
		pagesize_kb = gethugepagesize() / 1024;

	/* If the user is dealing in the default page size, we can use /proc */
	if (pagesize_kb == default_size) {
		if (meminfo_key && key) {
			strcpy(filename, "/proc/meminfo");
			*key = meminfo_key;
		} else
			sprintf(filename, "/proc/sys/vm/%s", sysfs_file);
	} else /* Use the sysfs interface */
		sprintf(filename, "/sys/kernel/mm/hugepages/hugepages-%lukB/%s",
			pagesize_kb, sysfs_file);
	return 0;
}

long get_pool_counter(unsigned int counter, unsigned long pagesize_kb)
{
	char file[PATH_MAX+1];
	char *key;

	if (select_pool_counter(counter, pagesize_kb, file, &key))
		return -1;

	return file_read_ulong(file, key);
}

int set_pool_counter(unsigned int counter, unsigned long val,
			unsigned long pagesize_kb)
{
	char file[PATH_MAX+1];

	if (select_pool_counter(counter, pagesize_kb, file, NULL))
		return -1;

	return file_write_ulong(file, val);
}

long read_meminfo(const char *tag)
{
	return file_read_ulong("/proc/meminfo", tag);
}

ino_t get_addr_inode(void *p)
{
	char name[256];
	int ret;
	struct stat sb;

	ret = read_maps((unsigned long)p, name);
	if (ret < 0)
		return ret;
	if (ret == 0) {
		ERROR("Couldn't find address %p in /proc/self/maps\n", p);
		return -1;
	}

	/* Don't care about non-filenames */
	if (name[0] != '/')
		return 0;

	/* Truncate the filename portion */

	ret = stat(name, &sb);
	if (ret < 0) {
		/* Don't care about unlinked files */
		if (errno == ENOENT)
			return 0;
		ERROR("stat failed: %s\n", strerror(errno));
		return -1;
	}

	return sb.st_ino;
}

int remove_shmid(int shmid)
{
	if (shmid >= 0) {
		if (shmctl(shmid, IPC_RMID, NULL) != 0) {
			ERROR("shmctl(%x, IPC_RMID) failed (%s)\n",
			      shmid, strerror(errno));
			return -1;
		}
	}
	return 0;
}

/* WARNING: This function relies on the hugetlb pool counters in a way that
 * is known to be racy.  Due to the expected usage of hugetlbfs test cases, the
 * risk of a race is acceptible.  This function should NOT be used for real
 * applications.
 */
int kernel_has_private_reservations(int fd)
{
	long t, f, r, s;
	long nt, nf, nr, ns;
	void *map;

	/* Read pool counters */
	t = get_pool_counter(HUGEPAGES_TOTAL, 0);
	f = get_pool_counter(HUGEPAGES_FREE, 0);
	r = get_pool_counter(HUGEPAGES_RSVD, 0);
	s = get_pool_counter(HUGEPAGES_SURP, 0);


	if (fd < 0) {
		ERROR("kernel_has_private_reservations: hugetlbfs_unlinked_fd: "
			"%s\n", strerror(errno));
		return -1;
	}
	map = mmap(NULL, gethugepagesize(), PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) {
		ERROR("kernel_has_private_reservations: mmap: %s\n",
			strerror(errno));
		return -1;
	}

	/* Recheck the counters */
	nt = get_pool_counter(HUGEPAGES_TOTAL, 0);
	nf = get_pool_counter(HUGEPAGES_FREE, 0);
	nr = get_pool_counter(HUGEPAGES_RSVD, 0);
	ns = get_pool_counter(HUGEPAGES_SURP, 0);

	munmap(map, gethugepagesize());

	/*
	 * There are only three valid cases:
	 * 1) If a surplus page was allocated to create a reservation, all
	 *    four pool counters increment
	 * 2) All counters remain the same except for Hugepages_Rsvd, then
	 *    a reservation was created using an existing pool page.
	 * 3) All counters remain the same, indicates that no reservation has
	 *    been created
	 */
	if ((nt == t + 1) && (nf == f + 1) && (ns == s + 1) && (nr == r + 1)) {
		return 1;
	} else if ((nt == t) && (nf == f) && (ns == s)) {
		if (nr == r + 1)
			return 1;
		else if (nr == r)
			return 0;
	} else {
		ERROR("kernel_has_private_reservations: bad counter state - "
		      "T:%li F:%li R:%li S:%li -> T:%li F:%li R:%li S:%li\n",
			t, f, r, s, nt, nf, nr, ns);
	}
	return -1;
}

int using_system_hpage_size(const char *mount)
{
	struct statfs64 sb;
	int err;
	long meminfo_size, mount_size;

	if (!mount)
		FAIL("using_system_hpage_size: hugetlbfs is not mounted\n");

	err = statfs64(mount, &sb);
	if (err)
		FAIL("statfs64: %s\n", strerror(errno));

	meminfo_size = file_read_ulong("/proc/meminfo", "Hugepagesize:");
	if (meminfo_size < 0)
		FAIL("using_system_hpage_size: Failed to read /proc/meminfo\n");

	mount_size = sb.f_bsize / 1024; /* Compare to meminfo in kB */
	if (mount_size == meminfo_size)
		return 1;
	else
		return 0;
}
