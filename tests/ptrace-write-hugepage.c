#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <hugetlbfs.h>

#include "hugetests.h"

#define CONST	0xdeadbeefL

int hpage_size;
volatile int ready_to_trace = 0;

static void sigchld_handler(int signum, siginfo_t *si, void *uc)
{
	int status;
	pid_t pid;

	pid = wait(&status);
	if (WIFEXITED(status))
		exit(WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		exit(status);

	ready_to_trace = 1;
}

static void child(int hugefd, int pipefd)
{
	void *p;
	int err;

	p = mmap(NULL, hpage_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		 hugefd, 0);
	if (p == MAP_FAILED)
		FAIL("mmap(): %s", strerror(errno));

	memset(p, 0, hpage_size);

	verbose_printf("Child mapped data at %p\n", p);

	err = write(pipefd, &p, sizeof(p));
	if (err != sizeof(p))
		FAIL("Writing to pipe");

	pause();
}

static void do_poke(pid_t pid, void *p)
{
	long err;

	verbose_printf("Poking...");
	err = ptrace(PTRACE_POKEDATA, pid, p, (void *)CONST);
	if (err)
		FAIL("ptrace(POKEDATA): %s", strerror(errno));
	verbose_printf("done\n");

	verbose_printf("Peeking...");
	err = ptrace(PTRACE_PEEKDATA, pid, p, NULL);
	if (err == -1)
		FAIL("ptrace(PEEKDATA): %s", strerror(errno));

	if (err != CONST)
		FAIL("mismatch (%lx instead of %lx)", err, CONST);
	verbose_printf("done\n");
}

int main(int argc, char *argv[])
{
	int fd;
	int pipefd[2];
	long err;
	pid_t cpid;
	void *p;
	struct sigaction sa = {
		.sa_sigaction = sigchld_handler,
		.sa_flags = SA_SIGINFO,
	};


	test_init(argc, argv);

	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG();

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	err = sigaction(SIGCHLD, &sa, NULL);
	if (err)
		FAIL("Can't install SIGCHLD handler");
	
	err = pipe(pipefd);
	if (err)
		FAIL("pipe()");

	cpid = fork();
	if (cpid < 0)
		FAIL("fork()\n");


	if (cpid == 0) {
		child(fd, pipefd[1]);
		exit(0);
	}

	/* Parent */
	err = read(pipefd[0], &p, sizeof(p));
	if (err != sizeof(p))
		FAIL("Reading pipe");

	verbose_printf("Parent received address %p\n", p);

	err = ptrace(PTRACE_ATTACH, cpid, NULL, NULL);
	if (err)
		FAIL("ptrace(ATTACH): %s", strerror(errno));

	while (! ready_to_trace)
		;

	do_poke(cpid, p);
	do_poke(cpid, p + getpagesize());

	ptrace(PTRACE_KILL, cpid, NULL, NULL);
	
	PASS();
}

