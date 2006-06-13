/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2006 Nishanth Aravamudan, IBM Corporation
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
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>

#include "hugetests.h"

#define BLOCK_SIZE	16384
#define CONST	0xdeadbeef
#define SHM_KEY 0xdeadcab

#define barrier()	asm volatile ("" : : : "memory" )

#define BIG_INIT	{ \
	[0] = CONST, [17] = CONST, [BLOCK_SIZE-1] = CONST, \
}
static int small_data = 1;
static int big_data[BLOCK_SIZE] = BIG_INIT;

static int small_bss;
static int big_bss[BLOCK_SIZE];

const int small_const = CONST;
const int big_const[BLOCK_SIZE] = BIG_INIT;

static int static_func(int x)
{
	return x;
}

int global_func(int x)
{
	return x;
}

struct test_entry {
	const char *name;
	void *data;
	int size;
	char linkchar;
	int writable, execable;
	int is_huge;
} testtab[] = {
#define RWENT(name, linkchar)	{ #name, &name, sizeof(name), linkchar, 1, 0, }
#define ROENT(name, linkchar)	{ #name, (void *)&name, sizeof(name), linkchar, 0, 0, }
#define RXENT(name, linkchar)	{ #name, &name, sizeof(name), linkchar, 0, 1, }
	RWENT(small_data, 'D'),
	RWENT(big_data, 'D'),
	RWENT(small_bss, 'B'),
	RWENT(big_bss, 'B'),
	ROENT(small_const, 'T'),
	ROENT(big_const, 'T'),
	RXENT(static_func, 'T'),
	RXENT(global_func, 'T'),
};

#define NUM_TESTS	(sizeof(testtab) / sizeof(testtab[0]))

static char link_string[32];

static void get_link_string(const char *argv0)
{
	const char *p, *q;

	/* Find program basename */
	p = strrchr(argv0, '/');
	if (p)
		p++;
	else
		p = argv0;

	if (*p != 'x')
		return; /* just a plain ordinary link */

	q = strchr(p, '.');
	if (!q)
		/* ERROR? */
		return;

	memcpy(link_string, p, q-p);
}

static ino_t do_test(struct test_entry *te)
{
	int i;
	volatile int *p = te->data;

	if (te->writable) {
		for (i = 0; i < (te->size / sizeof(*p)); i++)
			p[i] = CONST ^ i;

		barrier();

		for (i = 0; i < (te->size / sizeof(*p)); i++)
			if (p[i] != (CONST ^ i))
				FAIL("mismatch on %s", te->name);
	} else if (te->execable) {
		int (*pf)(int) = te->data;

		if ((*pf)(CONST) != CONST)
			FAIL("%s returns incorrect results", te->name);
	} else {
		/* Otherwise just read touch it */
		for (i = 0; i < (te->size / sizeof(*p)); i++)
			p[i];
	}

	te->is_huge = (test_addr_huge(te->data) == 1);

	return get_addr_inode(te->data);
}

int main(int argc, char *argv[], char *envp[])
{
	int i;
	int shmid;
	ino_t *shm;
	int num_sharings;

	test_init(argc, argv);

	if (argc == 2) {
		/*
		 * first process
		 * arg1 = num_sharings
		 */
		char *env;
		pid_t *children;
		int ret, j;
		/* both default to 0 */
		int sharing = 0, elfmap_off = 0;

		env = getenv("HUGETLB_ELFMAP");
		if (env && (strcasecmp(env, "no") == 0)) {
			verbose_printf("Segment remapping disabled\n");
			elfmap_off = 1;
		} else {
			env = getenv("HUGETLB_SHARE");
			if (env)
				sharing = atoi(env);
			verbose_printf("Segment remapping enabled, sharing = %d\n", sharing);
		}

		num_sharings = atoi(argv[1]);
		if (num_sharings > 99999)
			FAIL("Too many sharings requested (max = 99999)");

		children = (pid_t *)malloc(num_sharings * sizeof(pid_t));
		if (!children)
			FAIL("malloc failed: %s", strerror(errno));

		shmid = shmget(SHM_KEY, num_sharings * NUM_TESTS * sizeof(ino_t), IPC_CREAT | IPC_EXCL | 0666);
		if (shmid < 0)
			FAIL("Parent's shmget failed: %s", strerror(errno));

		shm = shmat(shmid, NULL, 0);
		if (shm == (void *)-1)
			FAIL("shmat failed: %s", strerror(errno));

		for (i = 0; i < num_sharings; i++) {
			char execarg1[5], execarg2[5];
			ret = snprintf(execarg1, 5, "%d", num_sharings);
			if (ret < 0)
				FAIL("snprintf failed: %s", strerror(errno));

			ret = snprintf(execarg2, 5, "%d", i);
			if (ret < 0)
				FAIL("snprintf failed: %s", strerror(errno));

			ret = fork();
			if (ret) {
				if (ret < 0)
					FAIL("fork failed: %s", strerror(errno));
				children[i] = ret;
			} else {
				ret = execlp(argv[0], argv[0], execarg1, execarg2, NULL);
				if (ret) {
					shmctl(shmid, IPC_RMID, NULL);
					shmdt(shm);
					FAIL("execl(%s, %s, %s, %s failed: %s",
						argv[0], argv[0], execarg1, execarg2, strerror(errno));
				}
			}
		}
		for (i = 0; i < num_sharings; i++) {
			ret = waitpid(children[i], NULL, 0);
			if (ret < 0) {
				shmctl(shmid, IPC_RMID, NULL);
				shmdt(shm);
				FAIL("waitpid failed: %s", strerror(errno));
			}
		}
		for (i = 0; i < NUM_TESTS; i++) {
			ino_t base = shm[i];
			for (j = 1; j < num_sharings; j++) {
				ino_t comp = shm[j * NUM_TESTS + i];
				if (base != comp) {
					/* we care if we mismatch if either
 					 * sharing all segments or sharing only read-only segments and this is one */
					if ((sharing == 2) || 
							((sharing == 1) && (testtab[i].writable == 0))) {
						shmctl(shmid, IPC_RMID, NULL);
						shmdt(shm);
						FAIL("Inodes do not match (%u != %u)", (int)base, (int)comp);
					}
				} else {
					/* we care if we match if either
 					 * not remapping or not sharing or sharing only read-only segments and this is not one */
					if ((elfmap_off == 1) || (sharing == 0) || ((sharing == 1) && (testtab[i].writable == 1) && (base == -1))) {
						shmctl(shmid, IPC_RMID, NULL);
						shmdt(shm);
						if ((sharing == 1) && (testtab[i].writable == 1))
							verbose_printf("sharing a writable segment...\n");
						FAIL("Inodes match, but we should not be sharing this segment (%d == %d)", (int)base, (int)comp);
					}
				}
			}
		}
		shmctl(shmid, IPC_RMID, NULL);
		shmdt(shm);
		PASS();
	} else if (argc == 3) {
		/*
		 * child process
		 * arg1 = num_sharings
		 * arg2 = index + 1 into shared memory array
		 */
		ino_t i1;
		int index;

		num_sharings = atoi(argv[1]);
		index = atoi(argv[2]);

		get_link_string(argv[0]);

		shmid = shmget(SHM_KEY, num_sharings * NUM_TESTS * sizeof(ino_t), 0666);
		if (shmid < 0)
			FAIL("Child's shmget failed: %s", strerror(errno));

		shm = shmat(shmid, NULL, 0);
		if (shm == (void *)-1)
			FAIL("shmat failed: %s", strerror(errno));

		for (i = 0; i < NUM_TESTS; i++) {
			i1 = do_test(testtab + i);
			if (((int)i1) < 0)
				shmdt(shm);
			shm[index * NUM_TESTS + i] = i1;
		}
		shmdt(shm);
		exit(0);
	} else
		FAIL("Incorrect arguments\n");
}
