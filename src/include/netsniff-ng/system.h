/* XXX: Coding Style - use the tool indent with the following (Linux kernel
 *                     code indents)
 *
 * indent -nbad -bap -nbc -bbo -hnl -br -brs -c33 -cd33 -ncdb -ce -ci4  \
 *        -cli0 -d0 -di1 -nfc1 -i8 -ip0 -l80 -lp -npcs -nprs -npsl -sai \
 *        -saf -saw -ncs -nsc -sob -nfca -cp33 -ss -ts8 -il1
 *
 *
 * netsniff-ng
 *
 * High performance network sniffer for packet inspection
 *
 * Copyright (C) 2009, 2010  Daniel Borkmann <danborkmann@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2 of the License, or (at 
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License 
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, write to the Free Software Foundation, Inc., 
 * 51 Franklin St, Fifth Floor, Boston, MA 02110, USA
 *
 * Note: Your kernel has to be compiled with CONFIG_PACKET_MMAP=y option in 
 *       order to use this.
 */

/*
 * Contains: 
 *    System related stuff like tweaking of scheduling params or CPU affinity
 */

#ifndef _NET_SYSTEM_H_
#define _NET_SYSTEM_H_

#include <sched.h>

#include <sys/poll.h>
#include <sys/resource.h>

#define DEFAULT_SCHED_POLICY	SCHED_FIFO
#define DEFAULT_SCHED_PRIO	sched_get_priority_max(DEFAULT_SCHED_POLICY)
#define DEFAULT_PROCESS_PRIO	(-20)

/* Function signatures */

extern int set_cpu_affinity(const char *str);
extern char *get_cpu_affinity(char *cpu_string, size_t len);
extern int set_proc_prio(int prio);
extern int set_sched_status(int policy, int priority);
extern void check_for_root(void);
extern int undaemonize(const char *pidfile);
extern int daemonize(const char *pidfile, const char *logfile,
		     const char *sockfile, void *(*start_server) (void *sock));

/* Inline stuff */

/**
 * prepare_polling - Sets params for ringbuff polling
 * @sock:           socket
 * @pfd:            file descriptor for polling
 */
static inline void prepare_polling(int sock, struct pollfd *pfd)
{
	pfd->fd = sock;
	pfd->revents = 0;
	pfd->events = POLLIN | POLLERR;
}

#endif				/* _NET_SYSTEM_H_ */