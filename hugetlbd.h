/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2006 Nishanth Aravamudan, IBM Corporation.
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

#ifndef _HUGETLBD_H
#define _HUGETLBD_H

#ifndef __LIBHUGETLBFS__
#error This header should not be included by library users.
#endif /* __LIBHUGETLBFS__ */

#define ID_KEY	0x56
#define LOGFILE "/tmp/hugetlbd.log"

/*
 * Ideally, would like to deal with this better, so that a 32-bit daemon
 * only deals in 32-bit addresses. But, for now, this makes 32-bit and
 * 64-bit apps deal ok with the socket messages.
 */
typedef uint64_t vaddr_t;
typedef uint64_t exe_ident_t;

struct client_request {
	/*
	 * unique identifer
	 * if id == 0, then the request is actually a response that the
	 * file is prepared
	 */
	exe_ident_t id;
	/*
	 * to identify the segment
	 */
	vaddr_t vaddr;
};

struct client_response {
	int succeeded;
};

struct daemon_response {
	int need_to_prepare;
	char path_to_file[PATH_MAX+1];
};

#endif /* _HUGETLBD_H */
