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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <hugetlbfs.h>
#include "hugetests.h"

#define P "shm-fork"
#define DESC \
	"* Test shared memory behavior when multiple threads are attached  *\n"\
	"* to a segment.  A segment is created and then children are       *\n"\
	"* spawned which attach, write, read (verify), and detach from the *\n"\
	"* shared memory segment.                                          *"

extern int errno;

/* Global Configuration */
int nr_hugepages;
int numprocs;
int shmid;

#define MAX_PROCS 200
#define BUF_SZ 256

void cleanup(void)
{
	remove_shmid(shmid);
}
	
int main(int argc, char ** argv)
{
	unsigned int size;
	unsigned int hpage_size;
	int pid, status;
	int i;
	volatile char *shmaddr;
	struct shmid_ds shmbuf;
	int wait_list[MAX_PROCS];
	int passfail = 0;

	test_init(argc, argv);

	if (argc < 3) {
		ERROR("Usage:  %s <# procs> <# pages>\n", argv[0]);
		CONFIG();
	}

	numprocs = atoi(argv[1]);
	nr_hugepages = atoi(argv[2]);
	
	hpage_size = gethugepagesize();
	if (numprocs > MAX_PROCS) {
		ERROR("Cannot spawn more than %i processes\n", MAX_PROCS);
		CONFIG();
	}
        size = hpage_size * nr_hugepages;
	verbose_printf("Requesting %u bytes\n", size);
	if ((shmid = shmget(2, size, SHM_HUGETLB|IPC_CREAT|SHM_R|SHM_W )) < 0) {
		PERROR("shmget:");
		FAIL();
	}
	verbose_printf("shmid: %i\n", shmid);
			
	verbose_printf("Spawning children:\n");
	for (i=0; i<numprocs; i++) {
		if ((pid = fork()) < 0) {
			PERROR("fork:");
			FAIL();
		}
		if (pid == 0) {
			int j, k;
			verbose_printf(".");
			for (j=0; j<5; j++) {
				shmaddr = shmat(shmid, 0, SHM_RND);
				if (shmaddr == MAP_FAILED)
					FAIL("shmat failed: %s (PID=%d)\n",
					     strerror(errno), getpid());

				for (k=0;k<size;k++)
					shmaddr[k] = (char) (k);
				for (k=0;k<size;k++)
					if (shmaddr[k] != (char)k) {
						ERROR("Index %d mismatch.", k);
						exit(1);
					}
				if (shmdt((const void *)shmaddr) != 0) {
					PERROR("shmdt:");
					exit(1);
				}
			}
			exit(0);
		}
		wait_list[i] = pid;
	}
	for (i=0; i<numprocs; i++) {
		waitpid(wait_list[i], &status, 0);
		if (WEXITSTATUS(status) != 0)
			passfail = 1;
		if (WIFSIGNALED(status)) {
			ERROR("pid %i received unhandled signal\n", wait_list[i]);
			passfail = 1;
		}
	}
	
	if (shmctl(shmid, IPC_RMID, &shmbuf)) {
		PERROR("Destroy failure:");
		FAIL();
	}
	shmid = 0;

	verbose_printf("\n");
	if (!passfail)
		PASS();
	else
		FAIL();
}

