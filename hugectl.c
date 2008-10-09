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
#include <limits.h>

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
	OPTION("--verbose <level>, -v", "Increases/sets tracing levels");

	OPTION("--text", "Requests remapping of the program text");
	OPTION("--data", "Requests remapping of the program data");
	OPTION("--bss", "Requests remapping of the program bss");
	OPTION("--heap", "Requests remapping of the program heap");
	CONT("(malloc space)");

	OPTION("--no-preload", "Disable preloading the libhugetlbfs library");

	OPTION("--dry-run", "describe what would be done without doing it");

	OPTION("--library-use-path", "Use the system library path");
	OPTION("--library-path <path>", "Select a library prefix");
	CONT("(Default: "
#ifdef LIBDIR32
		LIBDIR32 ":"
#endif
#ifdef LIBDIR32
		LIBDIR32 ":"
#endif
		")");
}

int opt_dry_run = 0;
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

void verbose(char *which)
{
	int new_level;

	if (which) {
		new_level = atoi(which);
		if (new_level < 0 || new_level > 99) {
			ERROR("%d: verbosity out of range 0-99\n",
				new_level);
			exit(EXIT_FAILURE);
		}
	} else {
		new_level = verbose_level + 1;
		if (new_level == 100) {
			WARNING("verbosity limited to 99\n");
			new_level--;
		}
	}
	verbose_level = new_level;
}

void setup_environment(char *var, char *val)
{
	setenv(var, val, 1);
	DEBUG("%s='%s'\n", var, val);

	if (opt_dry_run)
		printf("%s='%s'\n", var, val);
}

void verbose_expose(void)
{
	char level[3];

	if (verbose_level == 99) {
		setup_environment("HUGETLB_DEBUG", "yes");
	}
	snprintf(level, sizeof(level), "%d", verbose_level);
	setup_environment("HUGETLB_VERBOSE", level);
}

/*
 * getopts return values for options which are long only.
 */
#define MAP_BASE	0x1000
#define LONG_BASE	0x2000

#define LONG_NO_PRELOAD	(LONG_BASE | 'p')

#define LONG_DRY_RUN	(LONG_BASE | 'd')

#define LONG_NO_LIBRARY	(LONG_BASE | 'L')
#define LONG_LIBRARY	(LONG_BASE | 'l')

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

#define LIBRARY_DISABLE ((void *)-1)

void library_path(char *path)
{
	char val[NAME_MAX] = "";
	char *env;

	env = getenv("LD_LIBRARY_PATH");

	/*
	 * Select which libraries we wish to use.  If the path is NULL
	 * use the libraries included with hugectl.  If the path is valid
	 * and points to a directory including a libhugetlbfs.so use it
	 * directly.  Else path is assumed to be a prefix to the 32/64 bit
	 * directories both of which are added, where available.
	 */
	if (path) {
		snprintf(val, sizeof(val), "%s/libhugetlbfs.so", path);
		if (access(val, F_OK) == 0) {
			/* $PATH */
			snprintf(val, sizeof(val), "%s:%s",
				path, env ? env : "");

		} else {
			/* [$PATH/LIB32:][$PATH/LIB64:]$LD_LIBRARY_PATH */
			snprintf(val, sizeof(val), ""
#ifdef LIBDIR32
				"%s/" LIB32 ":"
#endif
#ifdef LIBDIR64
				"%s/" LIB64 ":"
#endif
				"%s",
#ifdef LIBDIR32
				path,
#endif
#ifdef LIBDIR64
				path,
#endif
				env ? env : "");
		}

	} else {
		/* [LIBDIR32:][LIBDIR64:]$LD_LIBRARY_PATH */
		snprintf(val, sizeof(val), ""
#ifdef LIBDIR32
			LIBDIR32 ":"
#endif
#ifdef LIBDIR64
			LIBDIR64 ":"
#endif
			"%s", env ? env : "");
	}
	setup_environment("LD_LIBRARY_PATH", val);
}

void ldpreload(int which)
{
	if (which == MAP_HEAP) {
		setup_environment("LD_PRELOAD", "libhugetlbfs.so");
		WARNING("LD_PRELOAD in use for lone --heap\n");
	} else {
		DEBUG("LD_PRELOAD not appropriate for this map combination\n");
	}
}

int main(int argc, char** argv)
{
	int opt_mappings = 0;
	int opt_preload = 1;
	char *opt_library = NULL;

	char opts[] = "+hv";
	int ret = 0, index = 0;
	struct option long_opts[] = {
		{"help",       no_argument, NULL, 'h'},
		{"verbose",    required_argument, NULL, 'v' },
		{"no-preload", no_argument, NULL, LONG_NO_PRELOAD},
		{"dry-run",    no_argument, NULL, LONG_DRY_RUN},
		{"library-path",
			       required_argument, NULL, LONG_LIBRARY},
		{"library-use-path",
			       no_argument, NULL, LONG_NO_LIBRARY},

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

		case 'v':
			verbose(optarg);
			break;

		case LONG_NO_PRELOAD:
			opt_preload = 0;
			DEBUG("LD_PRELOAD disabled\n");
			break;

		case LONG_DRY_RUN:
			opt_dry_run = 1;
			break;

		case LONG_NO_LIBRARY:
			opt_library = LIBRARY_DISABLE;
			DEBUG("using LD_LIBRARY_PATH to find library\n");
			break;

		case LONG_LIBRARY:
			opt_library = optarg;
			break;

		default:
			WARNING("unparsed option %08x\n", ret);
			ret = -1;
			break;
		}
	}
	index = optind;
	opt_mappings &= ~MAP_BASE;

	if (!opt_dry_run && (argc - index) < 1) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	verbose_expose();

	if (opt_library != LIBRARY_DISABLE)
		library_path(opt_library);

	if (opt_mappings)
		setup_mappings(opt_mappings);

	if (opt_preload)
		ldpreload(opt_mappings);

	if (opt_dry_run)
		exit(EXIT_SUCCESS);

	execvp(argv[index], &argv[index]);
	ERROR("exec failed: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
}
