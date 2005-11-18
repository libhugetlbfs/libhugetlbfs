#define _LARGEFILE64_SOURCE /* Need this for statfs64 */

#include <features.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/vfs.h>
#include <sys/statfs.h>

#include "hugetlbfs.h"

/********************************************************************/
/* Internal functions                                               */
/********************************************************************/

#define ERROR(...) fprintf(stderr, __VA_ARGS__)

#define stringify_1(x)	#x
#define stringify(x)	stringify_1(x)

#define BUF_SZ 256

static int read_meminfo(const char *format)
{
	int val;
	FILE *f;
	int retcode;
	char buff[BUF_SZ];

	f = fopen("/proc/meminfo", "r");
	if (!f) {
		ERROR("Failed to open /proc/meminfo for reading\n");
		return -1;
	}

	while (!feof(f)) {
		fgets(buff, BUF_SZ, f);
		retcode = sscanf(buff, format, &val);
		if (retcode == 1) {
			fclose(f);
			return val;
		}
	}
	fclose(f);
	ERROR("Could not find \"%s\" in /proc/meminfo\n", format);
	return -1;
}

/********************************************************************/
/* Library user visible functions                                   */
/********************************************************************/

int gethugepagesize(void)
{
	static int hpage_size = 0;
	int hpage_kb;

	if (hpage_size)
		return hpage_size;

	hpage_kb = read_meminfo("Hugepagesize: %d ");
	if (hpage_kb < 0)
		hpage_size = -1;
	else
		/* convert from kb to bytes */
		hpage_size = 1024 * hpage_kb;

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

const char *hugetlbfs_find_path(void)
{
	static char htlb_mount[PATH_MAX+1];
	int err;
	char *tmp;
	FILE *f;

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
	f = fopen("/proc/mounts", "r");
	if (!f) {
		f = fopen("/etc/mtab", "r");
		if (!f) {
			ERROR("Couldn't open either /proc/mounts or /etc/mtab\n");
			return NULL;
		}
	}

	while (1) {
		char buf[sizeof(htlb_mount) + BUF_SZ];

		tmp = fgets(buf, BUF_SZ, f);
		if (! tmp)
			break;

		err = sscanf(buf,
			     "%*s %" stringify(sizeof(htlb_mount))
			     "s hugetlbfs ",
			     htlb_mount);
		if ((err == 1) && (hugetlbfs_test_path(htlb_mount) == 1)) {
			fclose(f);
			return htlb_mount;
		}

		memset(htlb_mount, 0, sizeof(htlb_mount));
	}

	fclose(f);
	return NULL;
}

int hugetlbfs_tempfile(int unlink_after_open)
{
	char name[PATH_MAX+1];
	int fd;

	name[sizeof(name)-1] = '\0';
	strcpy(name, hugetlbfs_find_path());
	strncat(name, "/XXXXXX", sizeof(name)-1);
	/* FIXME: deal with overflows */

	fd = mkstemp(name);

	if (unlink_after_open)
		unlink(name);

	return fd;
}

#if 0

int get_sysctl(char *file)
{
	FILE* f;
	char buff[BUF_SZ];

	f = fopen(file, "r");
	if (!f) {
		printf("Failed to open %s for reading\n", file);
		return(-1);
	}
	fgets(buff, BUF_SZ, f);
	fclose(f);
	return atoi(buff);
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
	char buff[BUF_SZ];

	snprintf(buff, BUF_SZ*sizeof(char), "%i", val);
	return set_sysctl_str(file, buff);	
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

int get_nr_free_huge_pages()
{
	return read_meminfo("HugePages_Free: %d ");
}

int get_nr_total_huge_pages()
{
	return read_meminfo("HugePages_Total: %d ");
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
	char buff[BUF_SZ];
	int hpage_size = get_hugepage_size();
	unsigned long bytes = nr_hugepages * hpage_size * 1024;
	
	snprintf(buff, sizeof(char)*BUF_SZ, "%lu", bytes);

	if (get_sysctl("/proc/sys/kernel/shmall") < bytes)
		ret = set_sysctl_str("/proc/sys/kernel/shmall", buff);
	if (get_sysctl("/proc/sys/kernel/shmmax") < bytes)
		ret += set_sysctl_str("/proc/sys/kernel/shmmax", buff);
	if (ret)
		return -1;
	else
		return 0;
}

#endif
