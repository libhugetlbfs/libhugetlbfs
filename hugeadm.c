/***************************************************************************
 *   User front end for using huge pages Copyright (C) 2008, IBM           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the Lesser GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2.1 of the  *
 *   License, or at your option) any later version.                        *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Lesser General Public License for more details.                   *
 *                                                                         *
 *   You should have received a copy of the Lesser GNU General Public      *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

/*
 * hugeadm is designed to make an administrators life simpler, to automate
 * and simplify basic system configuration as it relates to hugepages.  It
 * is designed to help with pool and mount configuration.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#define _GNU_SOURCE /* for getopt_long */
#include <unistd.h>
#include <getopt.h>

#define REPORT_UTIL "hugeadm"
#include "libhugetlbfs_internal.h"
#include "hugetlbfs.h"

extern int optind;
extern char *optarg;

#define OPTION(opts, text)	fprintf(stderr, " %-25s  %s\n", opts, text)
#define CONT(text) 		fprintf(stderr, " %-25s  %s\n", "", text)

void print_usage()
{
	fprintf(stderr, "hugectl [options] target\n");
	fprintf(stderr, "options:\n");

	OPTION("--pool-list", "List all pools");
	OPTION("--pool-pages-min <size>:[+|-]<count>", "");
	CONT("Adjust pool 'size' lower bound");
	OPTION("--pool-pages-max <size>:[+|-]<count>", "");
	CONT("Adjust pool 'size' upper bound");

	OPTION("--page-sizes", "Display page sizes that a configured pool");
	OPTION("--page-sizes-all",
			"Display page sizes support by the hardware");

	OPTION("--help, -h", "Prints this message");
}

int opt_dry_run = 0;

/*
 * getopts return values for options which are long only.
 */
#define LONG_POOL		('p' << 8)
#define LONG_POOL_LIST		(LONG_POOL|'l')
#define LONG_POOL_MIN_ADJ	(LONG_POOL|'m')
#define LONG_POOL_MAX_ADJ	(LONG_POOL|'M')

#define LONG_PAGE	('P' << 8)
#define LONG_PAGE_SIZES	(LONG_PAGE|'s')
#define LONG_PAGE_AVAIL	(LONG_PAGE|'a')

#define MAX_POOLS	32

static int cmpsizes(const void *p1, const void *p2)
{
	return ((struct hpage_pool *)p1)->pagesize >
			((struct hpage_pool *)p2)->pagesize;
}

void pool_list(void)
{
	struct hpage_pool pools[MAX_POOLS];
	int pos;
	int cnt;

	cnt = hpool_sizes(pools, MAX_POOLS);
	if (cnt < 0) {
		ERROR("unable to obtain pools list");
		exit(EXIT_FAILURE);
	}
	qsort(pools, cnt, sizeof(pools[0]), cmpsizes);

	printf("%10s %8s %8s %8s %8s\n",
		"Size", "Minimum", "Current", "Maximum", "Default");
	for (pos = 0; cnt--; pos++) {
		printf("%10ld %8ld %8ld %8ld %8s\n", pools[pos].pagesize,
			pools[pos].minimum, pools[pos].size,
			pools[pos].maximum, (pools[pos].is_default) ? "*" : "");
	}
}

enum {
	POOL_MIN,
	POOL_MAX,
};

static long value_adjust(char *adjust_str, long base)
{
	long adjust;
	char *iter;

	/* Convert and validate the adjust. */
	adjust = strtol(adjust_str, &iter, 0);
	if (*iter) {
		ERROR("%s: invalid adjustment\n", adjust_str);
		exit(EXIT_FAILURE);
	}

	if (adjust_str[0] != '+' && adjust_str[0] != '-')
		base = 0;

	/* Ensure we neither go negative nor exceed LONG_MAX. */
	if (adjust < 0 && -adjust > base) {
		adjust = -base;
	}
	if (adjust > 0 && (base + adjust) < base) {
		adjust = LONG_MAX - base;
	}
	base += adjust;

	return base;
}


void pool_adjust(char *cmd, unsigned int counter)
{
	struct hpage_pool pools[MAX_POOLS];
	int pos;
	int cnt;

	char *iter = NULL;
	char *page_size_str = NULL;
	char *adjust_str = NULL;
	long page_size;

	unsigned long min;
	unsigned long max;

	/* Extract the pagesize and adjustment. */
	page_size_str = strtok_r(cmd, ":", &iter);
	if (page_size_str)
		adjust_str = strtok_r(NULL, ":", &iter);

	if (!page_size_str || !adjust_str) {
		ERROR("%s: invalid resize specificiation\n", cmd);
		exit(EXIT_FAILURE);
	}
	DEBUG("page_size<%s> adjust<%s> counter<%d>\n",
					page_size_str, adjust_str, counter);

	/* Convert and validate the page_size. */
	page_size = parse_page_size(page_size_str);

	cnt = hpool_sizes(pools, MAX_POOLS);
	if (cnt < 0) {
		ERROR("unable to obtain pools list");
		exit(EXIT_FAILURE);
	}
	for (pos = 0; cnt--; pos++) {
		if (pools[pos].pagesize == page_size)
			break;
	}
	if (cnt < 0) {
		ERROR("%s: unknown page size\n", page_size_str);
		exit(EXIT_FAILURE);
	}

	min = pools[pos].minimum;
	max = pools[pos].maximum;

	if (counter == POOL_MIN) {
		min = value_adjust(adjust_str, min);
		if (min > max)
			max = min;
	} else {
		max = value_adjust(adjust_str, max);
		if (max < min)
			min = max;
	}

	DEBUG("%ld, %ld -> %ld, %ld\n", pools[pos].minimum, pools[pos].maximum,
		min, max);

	if ((pools[pos].maximum - pools[pos].minimum) < (max - min)) {
		DEBUG("setting HUGEPAGES_OC to %ld\n", (max - min));
		set_huge_page_counter(page_size, HUGEPAGES_OC, (max - min));
	}
	if (pools[pos].minimum != min) {
		DEBUG("setting HUGEPAGES_TOTAL to %ld\n", min);
		set_huge_page_counter(page_size, HUGEPAGES_TOTAL, min);
	}
	/*
	 * HUGEPAGES_TOTAL is not guarenteed to check to exactly the figure
	 * requested should there be insufficient pages.  Check the new
	 * value and adjust HUGEPAGES_OC accordingly.
	 */
	get_pool_size(page_size, &pools[pos]);
	if (pools[pos].minimum != min) {
		ERROR("failed to set pool minimum to %ld became %ld\n",
			min, pools[pos].minimum);
		min = pools[pos].minimum;
	}
	if (pools[pos].maximum != max) {
		DEBUG("setting HUGEPAGES_OC to %ld\n", (max - min));
		set_huge_page_counter(page_size, HUGEPAGES_OC, (max - min));
	}
}

void page_sizes(int all)
{
	struct hpage_pool pools[MAX_POOLS];
	int pos;
	int cnt;

	cnt = hpool_sizes(pools, MAX_POOLS);
	if (cnt < 0) {
		ERROR("unable to obtain pools list");
		exit(EXIT_FAILURE);
	}
	qsort(pools, cnt, sizeof(pools[0]), cmpsizes);

	for (pos = 0; cnt--; pos++) {
		if (all || (pools[pos].maximum &&
		    hugetlbfs_find_path_for_size(pools[pos].pagesize)))
			printf("%ld\n", pools[pos].pagesize);
	}
}

int main(int argc, char** argv)
{
	int ops;

	char opts[] = "+h";
	int ret = 0, index = 0;
	struct option long_opts[] = {
		{"help",       no_argument, NULL, 'h'},

		{"pool-list", no_argument, NULL, LONG_POOL_LIST},
		{"pool-pages-min", required_argument, NULL, LONG_POOL_MIN_ADJ},
		{"pool-pages-max", required_argument, NULL, LONG_POOL_MAX_ADJ},

		{"page-sizes", no_argument, NULL, LONG_PAGE_SIZES},
		{"page-sizes-all", no_argument, NULL, LONG_PAGE_AVAIL},

		{0},
	};

	hugetlbfs_setup_debug();
	setup_mounts();

	ops = 0;
	while (ret != -1) {
		ret = getopt_long(argc, argv, opts, long_opts, &index);
		switch (ret) {
		case -1:
			break;

		case '?':
			print_usage();
			exit(EXIT_FAILURE);

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);

		case LONG_POOL_LIST:
			pool_list();
			break;

		case LONG_POOL_MIN_ADJ:
			pool_adjust(optarg, POOL_MIN);
			break;

		case LONG_POOL_MAX_ADJ:
			pool_adjust(optarg, POOL_MAX);
			break;

		case LONG_PAGE_SIZES:
			page_sizes(0);
			break;

		case LONG_PAGE_AVAIL:
			page_sizes(1);
			break;

		default:
			WARNING("unparsed option %08x\n", ret);
			ret = -1;
			break;
		}
		if (ret != -1)
			ops++;
	}
	index = optind;

	if ((argc - index) != 0 || ops == 0) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
