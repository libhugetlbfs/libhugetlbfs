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

extern int optind;
extern char *optarg;

#define OPTION(opts, text)	fprintf(stderr, " %-25s  %s\n", opts, text)
#define CONT(text) 		fprintf(stderr, " %-25s  %s\n", "", text)

void print_usage()
{
	fprintf(stderr, "hugectl [options] target\n");
	fprintf(stderr, "options:\n");

	OPTION("--help, -h", "Prints this message");
}

int opt_dry_run = 0;

int main(int argc, char** argv)
{
	char opts[] = "+h";
	int ret = 0, index = 0;
	struct option long_opts[] = {
		{"help",       no_argument, NULL, 'h'},

		{0},
	};

	__lh_hugetlbfs_setup_debug();

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

		default:
			WARNING("unparsed option %08x\n", ret);
			ret = -1;
			break;
		}
	}
	index = optind;

	if ((argc - index) != 0) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
