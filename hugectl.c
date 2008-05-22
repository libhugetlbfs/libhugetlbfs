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

extern int errno;
extern int optind;
extern char *optarg;

void print_usage()
{
	fprintf(stderr, "hugectl [options] target\n");
	fprintf(stderr, "options:\n");
	fprintf(stderr, " --help,                   -h          Prints this message.\n");
}

int main(int argc, char** argv)
{
	char opts [] = "+h";
	int ret = 0, index = 0;
	struct option long_opts [] = {
		{"help",       0, 0, 'h'},
		{0,            0, 0, 0},
	};

	if (argc < 2) {
		print_usage();
		return 0;
	}

	while (ret != -1) {
		ret = getopt_long(argc, argv, opts, long_opts, &index);
		switch (ret) {
			case '?':
			case 'h':
				print_usage();
				return 0;

			case -1:
				break;

			default:
				ret = -1;
				break;
		}
	}
	index = optind;

	if (execvp(argv[index], &argv[index]) == -1) {
		ret = errno;
		fprintf(stderr, "Error calling execvp: '%s'\n", strerror(ret));
		return ret;
	}

	return 0;
}
