/*
 *  include/linux/eventpoll.h ( Efficent event polling implementation )
 *  Copyright (C) 2001,...,2002  Davide Libenzi
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


#define EVENTPOLL_MINOR	124
#define POLLFD_X_PAGE (PAGE_SIZE / sizeof(struct pollfd))
#define MAX_FDS_IN_EVENTPOLL (1024 * 128)
#define MAX_EVENTPOLL_PAGES (MAX_FDS_IN_EVENTPOLL / POLLFD_X_PAGE)
#define EVENT_PAGE_INDEX(n) ((n) / POLLFD_X_PAGE)
#define EVENT_PAGE_REM(n) ((n) % POLLFD_X_PAGE)
#define EVENT_PAGE_OFFSET(n) (((n) % POLLFD_X_PAGE) * sizeof(struct pollfd))
#define EP_FDS_PAGES(n) (((n) + POLLFD_X_PAGE - 1) / POLLFD_X_PAGE)
#define EP_MAP_SIZE(n) (EP_FDS_PAGES(n) * PAGE_SIZE * 2)


struct evpoll {
	int ep_timeout;
	unsigned long ep_resoff;
};

#define EP_ALLOC _IOR('P', 1, int)
#define EP_POLL _IOWR('P', 2, struct evpoll)
#define EP_FREE _IO('P', 3)
#define EP_ISPOLLED _IOWR('P', 4, struct pollfd)

#define EP_CTL_ADD 1
#define EP_CTL_DEL 2
#define EP_CTL_MOD 3


asmlinkage int sys_epoll_create(int maxfds);
asmlinkage int sys_epoll_ctl(int epfd, int op, int fd, unsigned int events);
asmlinkage int sys_epoll_wait(int epfd, struct pollfd const **events, int timeout);



#endif

