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
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include "libhugetlbfs_internal.h"
#include "hugetlbfs.h"

int shmget(key_t key, size_t size, int shmflg)
{
	static int (*real_shmget)(key_t key, size_t size, int shmflg) = NULL;
	char *error;
	int retval;
	char *hugetlbshm_env;
	size_t aligned_size = size;
	int hugetlbshm_enabled = 0;

	DEBUG("hugetlb_shmem: entering overridden shmget() call\n");

	/* Get a handle to the "real" shmget system call */
	if (!real_shmget) {
		real_shmget = dlsym(RTLD_NEXT, "shmget");
		if ((error = dlerror()) != NULL) {
			ERROR("%s", error);
			return -1;
		}
	}

	/* Determine if shmget() calls should be overridden */
	hugetlbshm_env = getenv("HUGETLB_SHM");
	if (hugetlbshm_env && !strcmp(hugetlbshm_env, "yes"))
		hugetlbshm_enabled = 1;

	/* Align the size and set SHM_HUGETLB on request */
	if (hugetlbshm_enabled) {
		/*
		 * Use /proc/meminfo because shm always uses the system
		 * default huge page size.
		 */
		long hpage_size = read_meminfo("Hugepagesize:") * 1024;
		aligned_size = ALIGN(size, hpage_size);
		if (size != aligned_size) {
			DEBUG("hugetlb_shmem: size growth align %zd -> %zd\n",
				size, aligned_size);
		}

		DEBUG("hugetlb_shmem: Adding SHM_HUGETLB flag\n");
		shmflg |= SHM_HUGETLB;
	} else {
		DEBUG("hugetlb_shmem: shmget override not requested\n");
	}

	/* Call the "real" shmget. If hugepages fail, use small pages */
	retval = real_shmget(key, aligned_size, shmflg);
	if (retval == -1 && hugetlbshm_enabled) {
		WARNING("While overriding shmget(%zd) to add SHM_HUGETLB: %s\n",
			aligned_size, strerror(errno));
		shmflg &= ~SHM_HUGETLB;
		retval = real_shmget(key, size, shmflg);
		DEBUG("Using small pages for shmget despite HUGETLB_SHM\n");
	}

	return retval;
}
