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
 * hugectl is inspired by numactl as a single front end to a large number of
 * options for controlling a very specific environment.  Eventually it will
 * have support for controlling the all of the environment variables for
 * libhugetlbfs, but options will only be added after they have been in the
 * library for some time and are throughly tested and stable.
 *
 * This program should be treated as an ABI for using libhugetlbfs.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define _GNU_SOURCE /* for getopt_long */
#include <unistd.h>
#include <getopt.h>

#define REPORT(level, prefix, format, ...)				      \
	do {								      \
		if (verbose_level >= level)				      \
			fprintf(stderr, "hugectl: " prefix ": " format,	      \
				##__VA_ARGS__);				      \
	} while (0);

#include "libhugetlbfs_debug.h"

extern int errno;
extern int optind;
extern char *optarg;

#define OPTION(opts, text)	fprintf(stderr, " %-25s  %s\n", opts, text)
#define CONT(text) 		fprintf(stderr, " %-25s  %s\n", "", text)

void print_usage()
{
	fprintf(stderr, "hugectl [options] target\n");
	fprintf(stderr, "options:\n");

	OPTION("--help, -h", "Prints this message");

	OPTION("--text", "Requests remapping of the program text");
	OPTION("--data", "Requests remapping of the program data");
	OPTION("--bss", "Requests remapping of the program bss");
	OPTION("--heap", "Requests remapping of the program heap");
	CONT("(malloc space)");
}

int verbose_level = VERBOSITY_DEFAULT;

void verbose_init(void)
{
	char *env;

	env = getenv("HUGETLB_VERBOSE");
	if (env)
		verbose_level = atoi(env);
	env = getenv("HUGETLB_DEBUG");
	if (env)
		verbose_level = VERBOSITY_MAX;
}

void setup_environment(char *var, char *val)
{
	setenv(var, val, 1);
	DEBUG("%s='%s'\n", var, val);
}


/*
 * getopts return values for options which are long only.
 */
#define MAP_BASE	0x1000

/*
 * Mapping selectors, one bit per remappable/backable area as requested
 * by the user.  These are also used as returns from getopts where they
 * are offset from MAP_BASE, which must be removed before they are compared.
 */
#define MAP_DISABLE	0x0001
#define MAP_TEXT	0x0002
#define MAP_DATA	0x0004
#define MAP_BSS		0x0008
#define MAP_HEAP	0x0010

void setup_mappings(int which)
{
	char remap[3] = { 0, 0, 0 };
	int n = 0;

	/*
	 * HUGETLB_ELFMAP should be set to either a combination of 'R' and 'W'
	 * which indicate which segments should be remapped.  It may also be
	 * set to 'no' to prevent remapping.
	 */
	if (which & MAP_TEXT)
		remap[n++] = 'R';
	if (which & (MAP_DATA|MAP_BSS)) {
		if ((which & (MAP_DATA|MAP_BSS)) != (MAP_DATA|MAP_BSS))
			WARNING("data and bss remapped together\n");
		remap[n++] = 'W';
	}
	if (which & MAP_DISABLE) {
		if (which != MAP_DISABLE)
			WARNING("--disable masks requested remap\n");
		n = 0;
		remap[n++] = 'n';
		remap[n++] = 'o';
	}

	if (n)
		setup_environment("HUGETLB_ELFMAP", remap);

	if (which & MAP_HEAP)
		setup_environment("HUGETLB_MORECORE", "yes");
}

int main(int argc, char** argv)
{
	int opt_mappings = 0;

	char opts[] = "+h";
	int ret = 0, index = 0;
	struct option long_opts[] = {
		{"help",       no_argument, NULL, 'h'},

		{"disable",    no_argument, NULL, MAP_BASE|MAP_DISABLE},
		{"text",       no_argument, NULL, MAP_BASE|MAP_TEXT},
		{"data",       no_argument, NULL, MAP_BASE|MAP_DATA},
		{"bss",        no_argument, NULL, MAP_BASE|MAP_BSS},
		{"heap",       no_argument, NULL, MAP_BASE|MAP_HEAP},

		{0},
	};

	verbose_init();

	while (ret != -1) {
		ret = getopt_long(argc, argv, opts, long_opts, &index);
		if (ret > 0 && (ret & MAP_BASE)) {
			opt_mappings |= ret;
			continue;
		}
		switch (ret) {
		case '?':
			print_usage();
			exit(EXIT_FAILURE);

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);

		default:
			WARNING("unparsed option %08x\n", ret);
			ret = -1;
			break;
		}
	}
	index = optind;
	opt_mappings &= ~MAP_BASE;

	if ((argc - index) < 1) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	if (opt_mappings)
		setup_mappings(opt_mappings);

	execvp(argv[index], &argv[index]);
	ERROR("exec failed: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
}
