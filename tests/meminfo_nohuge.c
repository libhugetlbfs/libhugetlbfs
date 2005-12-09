#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdarg.h>

#include <hugetlbfs.h>

#include "hugetests.h"

/* We override the normal open, so libhugetlbfs gets a /proc/meminfo
 * which doesn't contain any hugepage information */
int open(const char *path, int flags, ...)
{
	int (*old_open)(const char *, int, ...);
	int fd;

	if (strcmp(path, "/proc/meminfo") == 0) {
		FILE *f;

		f = popen("/bin/grep -vi ^hugepage /proc/meminfo", "r");
		return fileno(f);
	}

	old_open = dlsym(RTLD_NEXT, "open");
	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		fd = (*old_open)(path, flags, va_arg(ap, mode_t));
		va_end(ap);
		return fd;
	} else {
		return (*old_open)(path, flags);
	}
}

int main(int argc, char *argv[])
{
	int hpage_size;

	test_init(argc, argv);

	hpage_size = gethugepagesize();
	if (hpage_size == -1)
		PASS();

	FAIL("Mysteriously found a hugepage size of %d\n", hpage_size);
}

