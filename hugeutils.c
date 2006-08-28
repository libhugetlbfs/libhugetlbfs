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

#define _LARGEFILE64_SOURCE /* Need this for statfs64 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <features.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

#include "libhugetlbfs_internal.h"
#include "hugetlbfs.h"
#include "hugetlbd.h"

static int hpage_size; /* = 0 */
static char htlb_mount[PATH_MAX+1]; /* = 0 */
int sharing; /* =0 */

/********************************************************************/
/* Internal functions                                               */
/********************************************************************/

#define BUF_SZ 256
#define MEMINFO_SIZE	2048

static long read_meminfo(const char *tag)
{
	int fd;
	char buf[MEMINFO_SIZE];
	int len, readerr;
	char *p, *q;
	long val;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0) {
		ERROR("Couldn't open /proc/meminfo (%s)\n", strerror(errno));
		return -1;
	}

	len = read(fd, buf, sizeof(buf));
	readerr = errno;
	close(fd);
	if (len < 0) {
		ERROR("Error reading /proc/meminfo (%s)\n", strerror(readerr));
		return -1;
	}
	if (len == sizeof(buf)) {
		ERROR("/proc/meminfo is too large\n");
		return -1;
	}
	buf[len] = '\0';

	p = strstr(buf, tag);
	if (!p)
		return -1; /* looks like the line we want isn't there */

	p += strlen(tag);
	val = strtol(p, &q, 0);
	if (! isspace(*q)) {
		ERROR("Couldn't parse /proc/meminfo value\n");
		return -1;
	}

	return val;
}

/********************************************************************/
/* Library user visible functions                                   */
/********************************************************************/

long gethugepagesize(void)
{
	int hpage_kb;

	if (hpage_size)
		return hpage_size;

	hpage_kb = read_meminfo("Hugepagesize:");
	if (hpage_kb < 0)
		hpage_size = -1;
	else
		/* convert from kb to bytes */
		hpage_size = 1024 * hpage_kb;

	return hpage_size;
}

long hugetlbfs_vaddr_granularity(void)
{
#if defined(__powerpc64__)
	return (1L << 40);
#elif defined(__powerpc__)
	return (1L << 28);
#else
	return gethugepagesize();
#endif
}

int hugetlbfs_test_path(const char *mount)
{
	struct statfs64 sb;
	int err;

	/* Bugs in the 32<->64 translation code in pre-2.6.15 kernels
	 * mean that plain statfs() returns bogus errors on hugetlbfs
	 * filesystems.  Use statfs64() to work around. */
	err = statfs64(mount, &sb);
	if (err)
		return -1;

	return (sb.f_type == HUGETLBFS_MAGIC);
}

#define MOUNTS_SZ	4096

const char *hugetlbfs_find_path(void)
{
	int err, readerr;
	char *tmp;
	int fd, len;
	char buf[MOUNTS_SZ];

	/* Have we already located a mount? */
	if (*htlb_mount)
		return htlb_mount;

	/* No?  Let's see if we've been told where to look */
	tmp = getenv("HUGETLB_PATH");
	if (tmp) {
		err = hugetlbfs_test_path(tmp);
		if (err < 0) {
			ERROR("Can't statfs() \"%s\" (%s)\n",
			      tmp, strerror(errno));
			return NULL;
		} else if (err == 0) {
			ERROR("\"%s\" is not a hugetlbfs mount\n", tmp);
			return NULL;
		}
		strncpy(htlb_mount, tmp, sizeof(htlb_mount)-1);
		return htlb_mount;
	}

	/* Oh well, let's go searching for a mountpoint */
	fd = open("/proc/mounts", O_RDONLY);
	if (fd < 0) {
		fd = open("/etc/mtab", O_RDONLY);
		if (fd < 0) {
			ERROR("Couldn't open /proc/mounts or /etc/mtab (%s)\n",
			      strerror(errno));
			return NULL;
		}
	}

	len = read(fd, buf, sizeof(buf));
	readerr = errno;
	close(fd);
	if (len < 0) {
		ERROR("Error reading mounts (%s)\n", strerror(errno));
		return NULL;
	}
	if (len >= sizeof(buf)) {
		ERROR("/proc/mounts is too long\n");
		return NULL;
	}
	buf[sizeof(buf)-1] = '\0';

	tmp = buf;
	while (tmp) {
		err = sscanf(tmp,
			     "%*s %" stringify(PATH_MAX)
			     "s hugetlbfs ",
			     htlb_mount);
		if ((err == 1) && (hugetlbfs_test_path(htlb_mount) == 1))
			return htlb_mount;

		memset(htlb_mount, 0, sizeof(htlb_mount));

		tmp = strchr(tmp, '\n');
		if (tmp)
			tmp++;
	}

	ERROR("Could not find hugetlbfs mount point in /proc/mounts. "
							"Is it mounted?\n");
	return NULL;
}

int hugetlbfs_unlinked_fd(void)
{
	const char *path;
	char name[PATH_MAX+1];
	int fd;

	path = hugetlbfs_find_path();
	if (!path)
		return -1;

	name[sizeof(name)-1] = '\0';

	strcpy(name, path);
	strncat(name, "/libhugetlbfs.tmp.XXXXXX", sizeof(name)-1);
	/* FIXME: deal with overflows */

	fd = mkstemp(name);

	if (fd < 0) {
		ERROR("mkstemp() failed: %s\n", strerror(errno));
		return -1;
	}

	unlink(name);

	return fd;
}

/**
 * create_id - create unique id for current executable
 *
 * Uses IPC ftok() to generate a roughly unique identifier for our own
 * executable. This is used to match shareable mappings.
 *
 * returns:	-1, on error
 * 		a unique identifier, otherwise
 */
static exe_ident_t create_id(void)
{
	exe_ident_t id;
	int ret;
	char my_exe[PATH_MAX];

	ret = readlink("/proc/self/exe", my_exe, PATH_MAX);
	if (ret < 0) {
		ERROR("readlink failed: %s\n", strerror(errno));
		return -1;
	}

	id = (exe_ident_t)ftok(my_exe, ID_KEY);
	if (id < 0) {
		ERROR("ftok failed: %s\n", strerror(errno));
		return -1;
	}

	return id;
}

static int sock;
static int timeout;
/* global to allow multiple callsites to set members */

static void signal_handler(int signal)
{
	switch (signal) {
		case SIGALRM:
			timeout = 1;
	}
}

static int client_complete(struct seg_info *htlb_seg_info, int success);

/**
 * hugetlbfs_shared_file - communicate with daemon to get one shareable file
 * @htlb_seg_info: pointer to program's segment data
 *
 * returns:
 *   -1, on failure
 *   0, on success, and caller does not need to prepare the segments
 *   1, on success, and caller does need to prepare the segments
 */
static int hugetlbfs_shared_file(struct seg_info *htlb_seg_info)
{
	int ret, fd;
	struct client_request creq;
	struct daemon_response dresp;
	struct sockaddr_un sun;
	struct sigaction sigact = {
		.sa_handler	= signal_handler,
		.sa_flags	= 0,
	};
	struct sigaction prev;
	int open_flags;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		ERROR("socket() failed: %s\n", strerror(errno));
		return -1;
	}

	sun.sun_family = AF_UNIX;
	strcpy(sun.sun_path, "/tmp/libhugetlbfs-sock");
	ret = connect(sock, &sun, sizeof(sun));
	if (ret < 0) {
		ERROR("connect() failed: %s\n", strerror(errno));
		return -1;
	}

	sigemptyset(&(sigact.sa_mask));

	sigaction(SIGALRM, &sigact, &prev);

	/* add data to request */
	creq.id = create_id();
	/*
	 * This results in (non-harmful) compilation warning for
	 * 32-bit applications
	 */
	creq.vaddr = (vaddr_t)htlb_seg_info->vaddr;

	alarm(60);
	ret = write(sock, &creq, sizeof(creq));
	/* cancel any alarms */
	alarm(0);
	if (ret < 0) {
		if (timeout) {
			DEBUG("write timed out\n");
			goto fail;
		} else {
			ERROR("client request write failed: %s\n",
				strerror(errno));
			goto fail;
		}
	} else if (ret != sizeof(creq)) {
		DEBUG("short write (got %d, should have been %zu)\n",
			ret, sizeof(creq));
		goto fail;
	} /* else successfully wrote */

	/* wait slightly longer for the response */
	alarm(120);
	ret = read(sock, &dresp, sizeof(dresp));
	alarm(0);
	if (ret < 0) {
		if (timeout) {
			DEBUG("read timed out\n");
			goto fail;
		} else {
			ERROR("read failed: %s\n", strerror(errno));
			goto fail;
		}
	} else if (ret != sizeof(dresp)) {
		DEBUG("short read (got %d, should have been %zu)\n",
			ret, sizeof(dresp));
		goto fail;
	} /* else successfully read */

	sigaction(SIGALRM, &prev, NULL);

	/* Convert daemon response to open mode bits */
	if (dresp.need_to_prepare) {
		open_flags = O_RDWR;
	} else {
		open_flags = O_RDONLY;
	}

	/* get local handle on file */
	fd = open(dresp.path_to_file, open_flags);
	if (fd < 0) {
		ERROR("open(%s) failed: %s\n", dresp.path_to_file,
			strerror(errno));
		goto fail;
	}

	if (dresp.need_to_prepare) {
		DEBUG("daemon said to prepare\n");
	} else {
		/* ACK right away (closes socket) */
		DEBUG("daemon said not to prepare\n");
		ret = client_complete(htlb_seg_info, 0);
		if (ret < 0)
			goto fail;
	}

	htlb_seg_info->fd = fd;

	/* leave socket open until finished_prepare is called */
	return dresp.need_to_prepare;

fail:
	close(sock);
	return -1;
}

/**
 * client_complete - send ACK to daemon that we no longer need file
 * @htlb_seg_info: pointer to program's segment data
 * @success: if 0, successful prepare, else failed
 *
 * returns: 	0, if successful transmission (none occurs if 
 * 			not sharing)
 * 		-1, if failed transmission
 */
static int client_complete(struct seg_info *htlb_seg_info, int success)
{
	struct sigaction sigact = {
		.sa_handler = signal_handler,
		.sa_flags = 0,
	};
	struct sigaction prev;
	struct client_response cresp;
	int ret;

	sigemptyset(&(sigact.sa_mask));

	sigaction(SIGALRM, &sigact, &prev);

	cresp.succeeded = success;

	alarm(60);
	ret = write(sock, &cresp, sizeof(cresp));
	alarm(0);
	if (ret < 0) {
		if (timeout) {
			DEBUG("write timed out\n");
			goto fail;
		} else {
			ERROR("client response write failed: %s\n",
				strerror(errno));
			goto fail;
		}
	} else if (ret != sizeof(cresp)) {
		DEBUG("short write (got %d, should have been %zu)\n",
			ret, sizeof(cresp));
		goto fail;
	} /* else successfully wrote */

	sigaction(SIGALRM, &prev, NULL);

	if (success < 0)
		/* back out open'd fd since we failed */
		close(htlb_seg_info->fd);

	close(sock);
	return 0;

fail:
	close(htlb_seg_info->fd);
	close(sock);
	return -1;
}

/**
 * finished_prepare - callback for ACK'ing prepare is done
 * @htlb_seg_info: pointer to program's segment data
 * @success: if 0, successful prepare, else failed
 *
 * returns: 	0, if successful transmission (none occurs if 
 * 			not sharing)
 * 		-1, if failed transmission
 */
int finished_prepare(struct seg_info *htlb_seg_info, int success)
{
	if (sharing)
		return client_complete(htlb_seg_info, success);
	return 0;
}

/**
 * hugetlbfs_set_fds - multiplex callers depending on if sharing or not
 * @htlb_seg_info: pointer to program's segment data
 *
 * returns:	
 *  -1, on error
 *  0, on success, caller expects and gets shared, does not need to prepare
 *  1, on success, caller expects and gets shared, does need to prepare
 *  2, on success, caller expects shared and gets unlinked, does need to prepare
 *  3, on success, caller expects and gets unlinked, does need to prepare
 */
int hugetlbfs_set_fd(struct seg_info *htlb_seg_info)
{
	char *env;
	int fd;
	/* assume unlinked, all unlinked files need to be prepared */
	int ret = 3;

	env = getenv("HUGETLB_SHARE");
	if (env)
		sharing = atoi(env);
	DEBUG("HUGETLB_SHARE=%d, sharing ", sharing);
	if (sharing == 2) {
		DEBUG_CONT("enabled for all segments\n");
	} else {
		if (sharing == 1) {
			DEBUG_CONT("enabled for only read-only segments\n");
		} else {
			DEBUG_CONT("disabled\n");
		}
	}

	/* Either share all segments or share only read-only segments */
	if ((sharing == 2) || ((sharing == 1) &&
				!(htlb_seg_info->prot & PROT_WRITE))) {
		/* first, try to share */
		ret = hugetlbfs_shared_file(htlb_seg_info);
		if (ret >= 0)
			return ret;
		/* but, fall through to unlinked files, if sharing fails */
		ret = 2;
		DEBUG("Falling back to unlinked files\n");
	}
	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		return -1;
	htlb_seg_info->fd = fd;
	return ret;
}


/********************************************************************/
/* Library user visible DIAGNOSES/DEBUGGING ONLY functions          */
/********************************************************************/

long hugetlbfs_num_free_pages(void)
{
	return read_meminfo("HugePages_Free:");
}

long hugetlbfs_num_pages(void)
{
	return read_meminfo("HugePages_Total:");
}
