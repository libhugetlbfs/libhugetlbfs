#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hugetests.h"

#define BLOCK_SIZE	16384
#define CONST	0xdeadbeef

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

static void do_test(struct test_entry *te)
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
}

int main(int argc, char *argv[])
{
	int i;

	test_init(argc, argv);

	get_link_string(argv[0]);

	verbose_printf("Link string is [%s]\n", link_string);

	for (i = 0; i < NUM_TESTS; i++) {
		do_test(testtab + i);
	}

	verbose_printf("Hugepages used for:");
	for (i = 0; i < NUM_TESTS; i++)
		if (testtab[i].is_huge)
			verbose_printf(" %s", testtab[i].name);
	verbose_printf("\n");

	for (i = 0; i < NUM_TESTS; i++) {
		char linkchar = testtab[i].linkchar;

		if (linkchar && strchr(link_string, linkchar)) {
			if (! testtab[i].is_huge)
				FAIL("%s is not hugepage\n", testtab[i].name);
		}
	}
	PASS();
}
