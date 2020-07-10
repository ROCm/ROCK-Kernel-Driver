/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_EVENTPOLL_H
#define AMDKCL_EVENTPOLL_H

#include <uapi/linux/eventpoll.h>
#ifndef EPOLLIN
#define EPOLLIN        0x00000001
#define EPOLLPRI       0x00000002
#define EPOLLOUT       0x00000004
#define EPOLLERR       0x00000008
#define EPOLLHUP       0x00000010
#define EPOLLRDNORM    0x00000040
#define EPOLLRDBAND    0x00000080
#define EPOLLWRNORM    0x00000100
#define EPOLLWRBAND    0x00000200
#define EPOLLMSG       0x00000400
#define EPOLLRDHUP     0x00002000
#endif
#endif
