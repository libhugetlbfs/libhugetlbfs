#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

#define RANDOM_CONSTANT	0x1234ABCD

void sigbus_handler(int signum, siginfo_t *si, void *uc)
{
	PASS();
}

int main(int argc, char *argv[])
{
	int hpage_size;
	int fd;
	void *p;
	volatile unsigned int *q;
	int i;
	int err;
	struct sigaction sa = {
		.sa_sigaction = sigbus_handler,
		.sa_flags = SA_SIGINFO,
	};

	test_init(argc, argv);

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG();

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	p = mmap(NULL, hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		 fd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap()");

	q = p;

	/* Touch the memory */
	*q = 0;

	err = sigaction(SIGBUS, &sa, NULL);
	if (err)
		FAIL("sigaction()");


	err = ftruncate(fd, 0);
	if (err)
		FAIL("ftruncate()");

	*q;

	/* Should have SIGBUSed above */
	FAIL("Didn't SIGBUS");
}

