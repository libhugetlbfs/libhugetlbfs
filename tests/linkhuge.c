#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hugetests.h"

#define BLOCK_SIZE	65536
#define SMALL_CONST	0xdeadbeef

#define barrier()	asm volatile ("" : : : "memory" )

static int small_data = 1;
static unsigned char big_data[BLOCK_SIZE] = "dummy";

static int small_bss;
static unsigned char big_bss[BLOCK_SIZE];

struct test_entry {
	const char *name;
	int *small;
	unsigned char *big;
	char linkchar;
	int small_huge, big_huge;
} testtab[] = {
	{ "DATA", &small_data, big_data, 'D' },
	{ "BSS", &small_bss, big_bss, 'B' },
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

	*(te->small) = SMALL_CONST;
	barrier();
	if (*(te->small) != SMALL_CONST)
		FAIL("small mismatch (%s)\n", te->name);

	te->small_huge = (test_addr_huge(te->small) == 1);

	for (i = 0; i < BLOCK_SIZE; i++)
		te->big[i] = (i & 0xff);
	barrier();
	for (i = 0; i < BLOCK_SIZE; i++)
		if (te->big[i] != (i & 0xff))
			FAIL("big mismatch (%s)\n", te->name);

	te->big_huge = (test_addr_huge(te->big) == 1);
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

	verbose_printf("Small data huge for:");
	for (i = 0; i < NUM_TESTS; i++)
		if (testtab[i].small_huge)
			verbose_printf(" %s", testtab[i].name);
	verbose_printf("\n");

	verbose_printf("Big data huge for:");
	for (i = 0; i < NUM_TESTS; i++)
		if (testtab[i].big_huge)
			verbose_printf(" %s", testtab[i].name);
	verbose_printf("\n");

	for (i = 0; i < NUM_TESTS; i++) {
		char linkchar = testtab[i].linkchar;

		if (linkchar && strchr(link_string, linkchar)) {
			if (! testtab[i].small_huge)
				FAIL("Small %s not hugepage\n",
				     testtab[i].name);
			if (! testtab[i].big_huge)
				FAIL("Big %s not hugepage\n",
				     testtab[i].name);
		}
	}
	PASS();
}
