/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2006 Nishanth Aravamudan, IBM Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include "libhugetlbfs_internal.h"
#include "hugetlbfs.h"
#include "hugetlbd.h"

#define QUEUE_LENGTH	SOMAXCONN
#define HUGETLBD_PORT	60046	/* some random number */
/* how often to reap due to idleness */
#define POLL_TIMEOUT	600000	/* milliseconds */
/* how long ago does a share need to have occurred to reap? */
#define SHARE_TIMEOUT	600	/* seconds */

struct shared_mapping {
	/* unique identifier */
	exe_ident_t id;
	/* filename */
	char path_to_file[PATH_MAX+1];
	/* virtual address of segments */
	vaddr_t vaddr;
	/* timestamp for file cleanup */
	time_t timestamp;
	/* linked-list pointer */
	struct shared_mapping *next;
};

static struct shared_mapping *head;

/* visible for multiple manipulation points */
static int sock;
/* visible for SIGALRM handling */
static int timeout;
/* visible for error handling */
static int shared_fd;
static FILE *logfd;

/**
 * reap_files - unlink all shared files
 * @quitting: 1 if exiting daemon
 *
 * no return value
 */
static void reap_files(int quitting)
{
	struct shared_mapping *smptr, *nextsmptr;
	time_t now = time(NULL);

	DEBUG("Reaping files\n");
	smptr = head;
	/* list is sorted by timestamp, youngest is at head */
	while (smptr != NULL) {
		/*
		 * Iterate through list until we get to an old enough
		 * sharing
		 */
		if (!quitting && (now - smptr->timestamp < SHARE_TIMEOUT)) {
			smptr = smptr->next;
			continue;
		}
		/* keep pointer to the next one */
		nextsmptr = smptr->next;
		unlink(smptr->path_to_file);
		/* if we're deleting head, head has to move */
		if (smptr == head) {
			head = nextsmptr;
		}
		free(smptr);
		smptr = nextsmptr;
	}
}

/**
 * kill_daemon - exit the daemon process
 * @ret: if 0, expected quit
 * 	   -1, unexpected quit
 * 	   also functions as return value to exit with
 *
 * no return value
 */
static void kill_daemon(int ret)
{
	DEBUG("Killing daemon\n");
	reap_files(1);
	/*
	 * Close the socket here, because we might dying due to an
	 * asynchronous signal.
	 */
	close(sock);
	exit(ret);
}

/**
 * signal_handler - respond to catchable signals
 * @signal: the signal to handle
 *
 * no return value
 */
static void signal_handler(int signal)
{
	switch (signal) {
		case SIGHUP:
			/* reap files */
			DEBUG("Caught SIGHUP\n");
			reap_files(1);
			break;
		case SIGINT:
			/* expectedly quit */
			kill_daemon(0);
			break;
		case SIGALRM:
			timeout = 1;
			break;
	}
}

/**
 * idcpy - copy id from src to dst
 * @src: source id
 * @dst: destination id
 *
 * Would need to be changed if exe_ident_t is changed to not be a basic
 * type.
 *
 * no return value
 */
static void idcpy(exe_ident_t *dst, exe_ident_t *src)
{
	*dst = *src;
}

/**
 * idcmp - compare id1 to id2
 * @id1: first id
 * @dst: second id
 *
 * Would need to be changed if exe_ident_t is changed to not be a basic
 * type.
 *
 * returns:	0, if ids match
 * 		1, if ids don't match
 */
static int idcmp(exe_ident_t *i1, exe_ident_t *i2)
{
	if (*i1 == *i2)
		return 0;
	else
		return 1;
}

/**
 * set_path_to_file - create new unique file in hugetlbfs
 * @tgt: target character array to store filename in
 * @size: size of @tgt
 *
 * Creates a new uniquely named file in the hugetlbfs mount point. File
 * is opened in mkstemp, and the file descriptor is stored in a globally
 * visible variable, because the caller's return value is already
 * overloaded.
 *
 * Effectively identical to get_unlinked_fd().
 *
 * no return value
 */
static void set_path_to_file(char *tgt, int size)
{
	const char *path;
	shared_fd = -1;

	tgt[size - 1] = '\0';

	path = hugetlbfs_find_path();
	if (!path) {
		return;
	}

	strcpy(tgt, path);
	strncat(tgt, "/libhugetlbfs.tmp.XXXXXX", size - 1);

	shared_fd = mkstemp(tgt);
	if (shared_fd > 0) {
		/* prevent access to shared file */
		int ret = chmod(tgt, 0);
		if (ret < 0) {
			DEBUG("chmod failed\n");
			shared_fd = -1;
		}
	}
}

/**
 * do_shared_file - find shareable mapping, or create new one
 * @req: contains executable id and virtual addresses of segments
 * @resp: contains filename and whether prepare() needs to be run
 *
 * The core routine for sharing mappings. The request and response
 * structures allow for accepting many parameters and returning many
 * response values independent of the actual return value, which acts
 * kind of like metadata about @resp.
 *
 * returns:	-1, if failed to create mapping
 * 		NULL, if sharing mapping already at head of list
 * 		pointer to previous entry in linked list, else
 */
static struct shared_mapping *do_shared_file(struct client_request *creq,
						struct daemon_response *dresp)
{
	struct shared_mapping *smptr, *prevsmptr;

	if (head == NULL) {
		/* allocating first member of linked list */
		goto create_mapping;
	}

	prevsmptr = NULL;
	smptr = head;

	while (smptr != NULL) {
		if (!idcmp(&(smptr->id), &(creq->id))
					&& (smptr->vaddr == creq->vaddr))
				/* found match */
			goto share_mapping;

		/*
		 * Keep track of the previous entry in the list, because
		 * we're going to need to move the one we're currently
		 * at. This prevents an extra loop later.
		 */
		prevsmptr = smptr;
		smptr = smptr->next;
	}

create_mapping:
	/* first attempt to map this segment, create a new mapping */
	smptr = (struct shared_mapping *)malloc(sizeof(struct shared_mapping));
	if (smptr == NULL) {
		ERROR("Failed to allocate new shared mapping: %s\n",
			strerror(errno));
		return (struct shared_mapping *)-1;
	}

	set_path_to_file(smptr->path_to_file, sizeof(smptr->path_to_file));
	if (shared_fd < 0) {
		ERROR("Failed to create new shared file\n");
		free(smptr);
		return (struct shared_mapping *)-1;
	}

	DEBUG("created shared mapping to %s\n", smptr->path_to_file);

	/* grab data out of request */
	idcpy(&(smptr->id), &(creq->id));
	smptr->vaddr = creq->vaddr;
	/* timestamp will be set upon insert */

	/* client will need to prepare file */
	dresp->need_to_prepare = 1;
	strcpy(dresp->path_to_file, smptr->path_to_file);

	/*
	 * Cannot be NULL. Caller is responsible for freeing if an error
	 * occurs
	 */
	return smptr;

share_mapping:
	/* store data in response */
	dresp->need_to_prepare = 0;
	strcpy(dresp->path_to_file, smptr->path_to_file);

	DEBUG("sharing mapping to %s\n", smptr->path_to_file);

	/*
	 * Can be NULL, if sharing at the head of the list. Caller does
	 * not need to deal with freeing, but must deal with both NULL
	 * and non-NULL cases.
	 * We are moving smptr to the front of the list if the sharing
	 * succeeds, but send prevsmptr back so that we don't need to
	 * iterate to do the move.
	 */
	return prevsmptr;
}

void daemonize()
{
	pid_t pid, sid;

	pid = fork();
	if (pid < 0) {
		ERROR("daemonize: fork() failed\n");
		exit(1);
	}
	if (pid > 0)
		exit(0);

	umask(0);
	
	sid = setsid();
	if (sid < 0) {
		ERROR("daemonize: Failed to set session ID\n");
		exit(1);
	}

	if ((chdir("/")) < 0) {
		ERROR("daemonize: Failed to change directory\n");
		exit(1);
	}

	logfd = fopen(LOGFILE, "a+");
	if (!logfd) {
		ERROR("daemonize: Failed to open log file %s\n", LOGFILE);
		exit(1);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	stderr = logfd;
}

/**
 * main - central event loop for daemon
 * @argc: ignored
 * @argv: ignored
 *
 * returns:	0, on success
 * 		-1, on failure
 */
int main(int argc, char *argv[])
{
	int ret;
	uid_t my_uid;
	/* local socket structures */
	struct sockaddr_un sun, from;
	socklen_t len = (socklen_t)sizeof(from);
	int local_sock;
	/* used to allow for handling SIGALRM and SIGHUP */
	struct sigaction sigact = {
		.sa_handler	= signal_handler,
		.sa_flags	= 0,
	};
	struct sigaction prevact;
	/* needed for poll */
	struct pollfd ufds;
	/*
	 * Needed for socket messages -- could switch to dynamic
	 * allocation
	 */
	struct client_request creq;
	struct daemon_response dresp;
	struct client_response cresp;
	/* handle on some state that may need to be freed */
	struct shared_mapping *smptr;
	mode_t mode;

	/* Perform all the steps to become a real daemon */
	daemonize();
	
	my_uid = getuid();

	sigemptyset(&(sigact.sa_mask));

	/*
	 * Probably want to do this if we actually are going to run as a
	 * daemon -- send all output to a file somewhere, maybe?
	 * close(stdin);
	 * close(stdout);
	 * close(stderr);
	 */

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		ERROR("socket() failed: %s\n", strerror(errno));
		return -1;
	}

	sun.sun_family = AF_UNIX;
	/* clear out any previous socket */
	unlink("/tmp/libhugetlbfs-sock");
	strcpy(sun.sun_path, "/tmp/libhugetlbfs-sock");
	ret = bind(sock, (struct sockaddr *)(&sun), sizeof(sun));
	if (ret < 0) {
		ERROR("bind() failed: %s\n", strerror(errno));
		goto die;
	}

	chmod("/tmp/libhugetlbfs-sock", 0666);

	ret = listen(sock, QUEUE_LENGTH);
	if (ret < 0) {
		ERROR("listen() failed: %s\n", strerror(errno));
		goto die;
	}

	/* We will poll on sock for any incoming connections */
	ufds.fd	= sock;
	ufds.events = POLLIN;

	/* install our custom signal-handler */
	sigaction(SIGHUP, &sigact, NULL);

	for (;;) {
		/* gets set by SIGALRM handler */
		timeout = 0;
		/* not sure if this is necessary, but ok */
		ufds.revents = 0;
		/* helps with error handling */
		dresp.need_to_prepare = 0;
		/* reset mode of shared file */
		mode = 0;

		sigaction(SIGINT, &sigact, &prevact);

do_poll:
		ret = poll(&ufds, 1, POLL_TIMEOUT);
		if (ret < 0) {
			/* catch calls to SIGHUP as non-fail case */
			if (errno == EINTR)
				goto do_poll;
			ERROR("poll() failed: %s\n", strerror(errno));
			goto die;
		} else if (ret == 0) {
			/* poll timeout, reap any old files */
			DEBUG("poll() timed out\n");
			reap_files(0);
			continue;
		}

		switch (ufds.revents) {
			case POLLERR:
				ERROR("poll returned a POLLERR event\n");
				goto die;
				break;
			case POLLNVAL:
				ERROR("poll returned a POLLNVAL event\n");
				goto die;
				break;
			case POLLHUP:
				ERROR("poll returned a POLLHUP event\n");
				goto die;
				break;
			case POLLIN:
				break;
			default:
				ERROR("poll() returned an event we did not expect: %d\n", ufds.revents);
				goto die;
		}

		local_sock = accept(sock, (struct sockaddr *)(&from), &len);
		if (local_sock < 0) {
			if (errno == ECONNABORTED) {
				/*
				 * I think this is ok as a non-failure
				 * case, but not positive, since the
				 * client is what failed, not us.
				 */
				DEBUG("accept() returned connection aborted\n");
				continue;
			}
			ERROR("accept() failed: %s\n", strerror(errno));
			goto die;
		}

		sigaction(SIGINT, &prevact, NULL);
		sigaction(SIGALRM, &sigact, NULL);

		alarm(60);
		ret = read(local_sock, &creq, sizeof(creq));
		/* cancel any alarms */
		alarm(0);
		if (ret < 0) {
			/* timeout == 1 means that the SIGALRM handler ran */
			if (timeout) {
				DEBUG("read timed out\n");
				goto next;
			}
			ERROR("read failed: %s\n", strerror(errno));
			goto close_and_die;
		}
		if (ret != sizeof(creq)) {
			/*
			 * This should never happen, but is useful for
			 * debugging size issues with mixed environments
			 */
			DEBUG("short first read (got %d, should have been %zu\n", ret, sizeof(creq));
			goto next;
		}

		/* Use the request to figure out the mapping ... */
		smptr = do_shared_file(&creq, &dresp);
		if (smptr == (struct shared_mapping *)-1) {
			ERROR("do_shared_file failed\n");
			goto close_and_die;
		}

		mode = S_IRUSR | S_IRGRP | S_IROTH;
		/* only allow preparer to write to file */
		if (dresp.need_to_prepare)
			mode |= S_IWUSR | S_IWGRP | S_IWOTH;
		ret = chmod(dresp.path_to_file, mode);
		if (ret < 0) {
			DEBUG("chmod(%s) to read (maybe writable) failed: %s\n", dresp.path_to_file, strerror(errno));
			if (dresp.need_to_prepare)
				goto failed_next;
			goto next;
		}

		/*
		 * ... and acknowledge it with a filename and whether
		 * the caller needs to prepare
		 */
		alarm(60);
		ret = write(local_sock, &dresp, sizeof(dresp));
		alarm(0);
		/*
		 * If dresp.need_to_prepare is set, then we know we
		 * dynamically allocated an element (which is pointed to
		 * by smptr). We need to backout any changes it made to
		 * the system state (open file, memory consumption) on
		 * errors.
		 */
		if (ret < 0) {
			if (timeout) {
				DEBUG("write timed out\n");
				if (dresp.need_to_prepare)
					goto failed_next;
				goto next;
			}
			ERROR("write failed: %s\n", strerror(errno));
			if (dresp.need_to_prepare)
				goto failed_close_and_die;
			goto close_and_die;
		}
		if (ret != sizeof(dresp)) {
			DEBUG("short write (got %d, should have been %zu\n", ret, sizeof(dresp));
			if (dresp.need_to_prepare)
				goto failed_next;
			goto next;
		} /* else successfully wrote */

		DEBUG("waiting for client's response\n");
		/* this may be unnecessarily long */
		alarm(300);
		/*
		 * Wait for notification that file is prepared and
		 * segments have been remapped
		 */
		ret = read(local_sock, &cresp, sizeof(cresp));
		alarm(0);
		if (ret < 0) {
			if (timeout) {
				DEBUG("read timed out\n");
				if (dresp.need_to_prepare)
					goto failed_next;
				goto next;
			}
			ERROR("read failed: %s\n", strerror(errno));
			if (dresp.need_to_prepare)
				goto failed_close_and_die;
			goto close_and_die;
		}
		if (ret != sizeof(cresp)) {
			DEBUG("short second read (got %d, should have been %zu\n", ret, sizeof(cresp));
			if (dresp.need_to_prepare)
				goto failed_next;
			goto next;
		}
		/*
		 * Determine if the segments were prepared/remapped as
		 * expected.
		 */
		if (cresp.succeeded != 0) {
			DEBUG("file not prepared\n");
			if (dresp.need_to_prepare)
				goto failed_next;
			goto next;
		}

		DEBUG("File prepared / opened successfully\n");
		if (dresp.need_to_prepare) {
			mode = S_IRUSR | S_IRGRP | S_IROTH;
			ret = chmod(dresp.path_to_file, mode);
			if (ret < 0) {
				DEBUG("chmod(%s) to read-only failed: %s\n", dresp.path_to_file, strerror(errno));
				goto failed_next;
			}
			/*
			 * Insert new element at head of list
			 */
			if (head != NULL)
				smptr->next = head;
			else
				smptr->next = NULL;
			head = smptr;

			close(shared_fd);
		} else {
			/*
			 * Move element to head of list. We have pointer
			 * to the element just before the element to
			 * move. If it is NULL, then we are sharing at
			 * the head of the list, and no moving is
			 * necessary. Else, shift the pointers around.
			 * We obviously also know that head != NULL.
			 */
			if (smptr != NULL) {
				struct shared_mapping *temp;
				temp = smptr->next;
				smptr->next = temp->next;
				temp->next = head;
				head = temp;
			}
		}
		/*
		 * Shared mapping is at the head of the list now, update
		 * timestamp.
		 */
		head->timestamp = time(NULL);

		goto next;

failed_next:
		close(shared_fd);
		unlink(dresp.path_to_file);
		free(smptr);
next:
		close(local_sock);
	}

failed_close_and_die:
	close(shared_fd);
	unlink(dresp.path_to_file);
	free(smptr);
close_and_die:
	close(local_sock);
die:
	kill_daemon(-1);

	return -1;
}
