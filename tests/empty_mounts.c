#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdarg.h>

#include <hugetlbfs.h>

#include "hugetests.h"

/* We override the normal open, so libhugetlbfs gets an apparently
 * empty /proc/mounts or /etc/mtab */
int open(const char *path, int flags, ...)
{
	int (*old_open)(const char *, int, ...);
	int fd;

	if ((strcmp(path, "/proc/mounts") == 0)
	    || (strcmp(path, "/etc/mtab") == 0))
		path = "/dev/null";

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
	int fd;
	int err;

	test_init(argc, argv);

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG();

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		PASS();

	FAIL("Mysteriously found a mount");
}

