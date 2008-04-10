/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2005-2007 David Gibson & Adam Litke, IBM Corporation.
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
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <hugetlbfs.h>
#include "hugetests.h"

/*
 * Test Rationale:
 *
 * The hugetlb pool maintains 4 global counters to track pages as they
 * transition between various states.  Due to the complex relationships between
 * the counters, regressions are likely to occur in the future.  This test
 * performs operations that change the counters in known ways.  It emulates the
 * expected kernel behavior and compares the expected result to the actual
 * values after each operation.
 */

extern int errno;

/* Global test configuration */
#define DYNAMIC_SYSCTL "/proc/sys/vm/nr_overcommit_hugepages"
static long saved_nr_hugepages;
static long hpage_size;

/* State arrays for our mmaps */
#define NR_SLOTS	2
#define SL_SETUP	0
#define SL_TEST		1
static int map_fd[NR_SLOTS];
static char *map_addr[NR_SLOTS];
static unsigned long map_size[NR_SLOTS];
static unsigned int touched[NR_SLOTS];

/* Keep track of expected counter values */
static long prev_total;
static long prev_free;
static long prev_resv;
static long prev_surp;

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

/* Restore original nr_hugepages */
void cleanup(void) {
	int fd;
	char buf[10];

	snprintf(buf, 10, "%li", saved_nr_hugepages);
	fd = open("/proc/sys/vm/nr_hugepages", O_WRONLY);
	if (fd > 0)
		write(fd, buf, 10);
	close(fd);
}

void verify_dynamic_pool_support(void)
{
	int fd;
	char value;

	fd = open(DYNAMIC_SYSCTL, O_RDONLY);
	if (fd < 0)
		CONFIG("Unable to find %s: No dynamic hugetlb pool support?",
				DYNAMIC_SYSCTL);

	if (read(fd, &value, 1) != 1)
		FAIL("Unable to read %s", DYNAMIC_SYSCTL);
	if (value == '0')
		CONFIG("Dynamic hugetlb pool support present, but disabled");
	close(fd);
}

void bad_value(int line, const char *name, long expect, long actual)
{
	if (actual == -1)
		ERROR("%s not found in /proc/meminfo", name);
	else
		FAIL("Line %i: Bad %s: expected %li, actual %li\n",
			line, name, expect, actual);
}

void verify_counters(int line, long et, long ef, long er, long es)
{
	long t, f, r, s;

	t = read_meminfo("HugePages_Total:");
	f = read_meminfo("HugePages_Free:");
	r = read_meminfo("HugePages_Rsvd:");
	s = read_meminfo("HugePages_Surp:");

	/* Invariant checks */
	if (t < 0 || f < 0 || r < 0 || s < 0)
		ERROR("Negative counter value");
	if (f < r)
		ERROR("HugePages_Free < HugePages_Rsvd");

	/* Check actual values against expected values */
	if (t != et)
		bad_value(line, "HugePages_Total", et, t);

	if (f != ef)
		bad_value(line, "HugePages_Free", ef, f);

	if (r != er)
		bad_value(line, "HugePages_Rsvd", er, r);

	if (s != es)
		bad_value(line, "HugePages_Surp", es, s);

	/* Everything's good.  Update counters */
	prev_total = t;
	prev_free = f;
	prev_resv = r;
	prev_surp = s;
}

/* Memory operations:
 * Each of these has a predefined effect on the counters
 */
#define persistent_huge_pages (et - es)
void _set_nr_hugepages(unsigned long count, int line)
{
	FILE *f;
	long min_size;
	long et, ef, er, es;

	f = fopen("/proc/sys/vm/nr_hugepages", "w");
	if (!f)
		CONFIG("Cannot open /proc/sys/vm/nr_hugepages for writing");

	fprintf(f, "%lu", count);
	fclose(f);

	/* The code below is based on set_max_huge_pages in mm/hugetlb.c */
	es = prev_surp;
	et = prev_total;
	ef = prev_free;
	er = prev_resv;

	/*
	 * Increase the pool size
	 * First take pages out of surplus state.  Then make up the
	 * remaining difference by allocating fresh huge pages.
	 */
	while (es && count > persistent_huge_pages)
		es--;
	while (count > persistent_huge_pages) {
		et++;
		ef++;
	}
	if (count >= persistent_huge_pages)
		goto out;

	/*
	 * Decrease the pool size
	 * First return free pages to the buddy allocator (being careful
	 * to keep enough around to satisfy reservations).  Then place
	 * pages into surplus state as needed so the pool will shrink
	 * to the desired size as pages become free.
	 */
	min_size = max(count, er + et - ef);
	while (min_size < persistent_huge_pages) {
		ef--;
		et--;
	}
	while (count < persistent_huge_pages) {
		es++;
	}

out:
	verify_counters(line, et, ef, er, es);
}
#define set_nr_hugepages(c) _set_nr_hugepages(c, __LINE__)

void _map(int s, int hpages, int flags, int line)
{
	long et, ef, er, es;

	map_fd[s] = hugetlbfs_unlinked_fd();
	if (map_fd[s] < 0)
		CONFIG("Unable to open hugetlbfs file: %s", strerror(errno));
	map_size[s] = hpages * hpage_size;
	map_addr[s] = mmap(NULL, map_size[s], PROT_READ|PROT_WRITE, flags,
				map_fd[s], 0);
	if (map_addr[s] == MAP_FAILED)
		FAIL("mmap failed: %s", strerror(errno));
	touched[s] = 0;

	et = prev_total;
	ef = prev_free;
	er = prev_resv;
	es = prev_surp;

	/*
	 * When using MAP_SHARED, a reservation will be created to guarantee
	 * pages to the process.  If not enough pages are available to
	 * satisfy the reservation, surplus pages are added to the pool.
	 * NOTE: This code assumes that the whole mapping needs to be
	 * reserved and hence, will not work with partial reservations.
	 */
	if (flags & MAP_SHARED) {
		unsigned long shortfall = 0;
		if (hpages + prev_resv > prev_free)
			shortfall = hpages - prev_free + prev_resv;
		et += shortfall;
		ef = prev_free + shortfall;
		er = prev_resv + hpages;
		es = prev_surp + shortfall;
	}
	/* MAP_PRIVATE mappings do not alter the counters in any way. */

	verify_counters(line, et, ef, er, es);
}
#define map(s, h, f) _map(s, h, f, __LINE__)

void _unmap(int s, int hpages, int flags, int line)
{
	long et, ef, er, es;
	unsigned long i;

	munmap(map_addr[s], map_size[s]);
	close(map_fd[s]);
	map_fd[s] = -1;
	map_addr[s] = NULL;
	map_size[s] = 0;

	et = prev_total;
	ef = prev_free;
	er = prev_resv;
	es = prev_surp;

	/*
	 * When a VMA is unmapped, the instantiated (touched) pages are
	 * freed.  If the pool is in a surplus state, pages are freed to the
	 * buddy allocator, otherwise they go back into the hugetlb pool.
	 * NOTE: This code assumes touched pages have only one user.
	 */
	for (i = 0; i < touched[s]; i++) {
		if (es) {
			et--;
			es--;
		} else
			ef++;
	}

	/*
	 * mmap may have created some surplus pages to accomodate a
	 * reservation.  If those pages were not touched, then they will
	 * not have been freed by the code above.  Free them here.
	 */
	if (flags & MAP_SHARED) {
		int unused_surplus = min(hpages - touched[s], es);
		et -= unused_surplus;
		ef -= unused_surplus;
		er -= hpages - touched[s];
		es -= unused_surplus;
	}

	verify_counters(line, et, ef, er, es);
}
#define unmap(s, h, f) _unmap(s, h, f, __LINE__)

void _touch(int s, int hpages, int flags, int line)
{
	long et, ef, er, es;
	int nr;
	char *c;

	for (c = map_addr[s], nr = hpages;
			hpages && c < map_addr[s] + map_size[s];
			c += hpage_size, nr--)
		*c = (char) (nr % 2);
	/*
	 * Keep track of how many pages were touched since we can't easily
	 * detect that from user space.
	 * NOTE: Calling this function more than once for a mmap may yield
	 * results you don't expect.  Be careful :)
	 */
	touched[s] = max(touched[s], hpages);

	/*
	 * Shared mappings consume resv pages that were previously
	 * allocated.  Also deduct them from the free count.
	 *
	 * Private mappings may need to allocate surplus pages to
	 * satisfy the fault.  The surplus pages become part of the pool
	 * which could elevate total, free, and surplus counts.  resv is
	 * unchanged but free must be decreased.
	 */
	if (flags & MAP_SHARED) {
		et = prev_total;
		ef = prev_free - hpages;
		er = prev_resv - hpages;
		es = prev_surp;
	} else {
		if (hpages + prev_resv > prev_free)
			et = prev_total + (hpages - prev_free + prev_resv);
		else
			et = prev_total;
		er = prev_resv;
		es = prev_surp + et - prev_total;
		ef = prev_free - hpages + et - prev_total;
	}
	verify_counters(line, et, ef, er, es);
}
#define touch(s, h, f) _touch(s, h, f, __LINE__)

void run_test(char *desc, int base_nr)
{
	verbose_printf("%s...\n", desc);
	set_nr_hugepages(base_nr);

	/* untouched, shared mmap */
	map(SL_TEST, 1, MAP_SHARED);
	unmap(SL_TEST, 1, MAP_SHARED);

	/* untouched, private mmap */
	map(SL_TEST, 1, MAP_PRIVATE);
	unmap(SL_TEST, 1, MAP_PRIVATE);

	/* touched, shared mmap */
	map(SL_TEST, 1, MAP_SHARED);
	touch(SL_TEST, 1, MAP_SHARED);
	unmap(SL_TEST, 1, MAP_SHARED);

	/* touched, private mmap */
	map(SL_TEST, 1, MAP_PRIVATE);
	touch(SL_TEST, 1, MAP_PRIVATE);
	unmap(SL_TEST, 1, MAP_PRIVATE);

	/* Explicit resizing during outstanding surplus */
	/* Consume surplus when growing pool */
	map(SL_TEST, 2, MAP_SHARED);
	set_nr_hugepages(max(base_nr, 1));

	/* Add pages once surplus is consumed */
	set_nr_hugepages(max(base_nr, 3));

	/* Release free huge pages first */
	set_nr_hugepages(max(base_nr, 2));

	/* When shrinking beyond committed level, increase surplus */
	set_nr_hugepages(base_nr);

	/* Upon releasing the reservation, reduce surplus counts */
	unmap(SL_TEST, 2, MAP_SHARED);

	verbose_printf("OK.\n");
}

int main(int argc, char ** argv)
{
	int base_nr;

	test_init(argc, argv);
	check_must_be_root();
	saved_nr_hugepages = read_meminfo("HugePages_Total:");
	verify_dynamic_pool_support();
	hpage_size = check_hugepagesize();

	/*
	 * This test case should require a maximum of 3 huge pages.
	 * Run through the battery of tests multiple times, with an increasing
	 * base pool size.  This alters the circumstances under which surplus
	 * pages need to be allocated and increases the corner cases tested.
	 */
	for (base_nr = 0; base_nr <= 3; base_nr++) {
		verbose_printf("Base pool size: %i\n", base_nr);
		/* Run the tests with a clean slate */
		run_test("Clean", base_nr);

		/* Now with a pre-existing untouched, shared mmap */
		map(SL_SETUP, 1, MAP_SHARED);
		run_test("Untouched, shared", base_nr);
		unmap(SL_SETUP, 1, MAP_SHARED);

		/* Now with a pre-existing untouched, private mmap */
		map(SL_SETUP, 1, MAP_PRIVATE);
		run_test("Untouched, private", base_nr);
		unmap(SL_SETUP, 1, MAP_PRIVATE);

		/* Now with a pre-existing touched, shared mmap */
		map(SL_SETUP, 1, MAP_SHARED);
		touch(SL_SETUP, 1, MAP_SHARED);
		run_test("Touched, shared", base_nr);
		unmap(SL_SETUP, 1, MAP_SHARED);

		/* Now with a pre-existing touched, private mmap */
		map(SL_SETUP, 1, MAP_PRIVATE);
		touch(SL_SETUP, 1, MAP_PRIVATE);
		run_test("Touched, private", base_nr);
		unmap(SL_SETUP, 1, MAP_PRIVATE);
	}

	PASS();
}
