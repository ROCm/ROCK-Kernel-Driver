/*
 *  include/linux/eventpoll.h ( Efficent event polling implementation )
 *  Copyright (C) 2001,...,2003	 Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H


/* Valid opcodes to issue to sys_epoll_ctl() */
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

/* Set the Edge Triggered behaviour for the target file descriptor */
#define EPOLLET (1 << 31)

struct epoll_event {
	__u32 events;
	__u64 data;
};

#ifdef __KERNEL__

/* Forward declarations to avoid compiler errors */
struct file;


/* Kernel space functions implementing the user space "epoll" API */
asmlinkage long sys_epoll_create(int size);
asmlinkage long sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
asmlinkage long sys_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
			       int timeout);

/* Used to initialize the epoll bits inside the "struct file" */
void eventpoll_init_file(struct file *file);

/* Used in fs/file_table.c:__fput() to unlink files from the eventpoll interface */
void eventpoll_release(struct file *file);

#endif /* #ifdef __KERNEL__ */

#endif /* #ifndef _LINUX_EVENTPOLL_H */

