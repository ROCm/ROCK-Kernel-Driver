/*
 *  include/linux/eventpoll.h ( Efficent event polling implementation )
 *  Copyright (C) 2001,...,2002	 Davide Libenzi
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

/* Forward declarations to avoid compiler errors */
struct file;
struct pollfd;


/* Valid opcodes to issue to sys_epoll_ctl() */
#define EP_CTL_ADD 1
#define EP_CTL_DEL 2
#define EP_CTL_MOD 3


/* Kernel space functions implementing the user space "epoll" API */
asmlinkage int sys_epoll_create(int size);
asmlinkage int sys_epoll_ctl(int epfd, int op, int fd, unsigned int events);
asmlinkage int sys_epoll_wait(int epfd, struct pollfd *events, int maxevents,
			      int timeout);

/* Used in fs/file_table.c:__fput() to unlink files from the eventpoll interface */
void ep_notify_file_close(struct file *file);


#endif

