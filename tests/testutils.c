#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <sys/vfs.h>

#include "hugetests.h"

#define HUGETLBFS_MAGIC	0x958458f6

int verbose_test = 1;
char *test_name;

void  __attribute__((weak)) cleanup(void)
{
}

#if 0
static void segv_handler(int signum, siginfo_t *si, void *uc)
{
	FAIL("Segmentation fault");
}
#endif

void test_init(int argc, char *argv[])
{
#if 0
	int err;
	struct sigaction sa = {
		.sa_sigaction = segv_handler,
		.sa_flags = SA_SIGINFO,
	};

	test_name = argv[0];

	err = sigaction(SIGSEGV, &sa, NULL);
	if (err)
		FAIL("Can't install SEGV handler");
#endif

	if (getenv("QUIET_TEST"))
		verbose_test = 0;
}

#define MAPS_BUF_SZ 4096

static int read_maps(unsigned long addr, char *buf)
{
	FILE *f;
	char line[MAPS_BUF_SZ];
	char *tmp;

	f = fopen("/proc/self/maps", "r");
	if (!f) {
		ERROR("Failed to open /proc/self/maps\n");
		return -1;
	}

	while (1) {
		unsigned long start, end, off, ino;
		int ret;
		
		tmp = fgets(line, MAPS_BUF_SZ, f);
		if (!tmp)
			break;
		
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
		ERROR("Couldn't find addres %p in /proc/self/maps\n", p);
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
