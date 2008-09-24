#ifndef _TRACE_SOCKET_H
#define _TRACE_SOCKET_H

#include <net/sock.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(socket_sendmsg,
	TPPROTO(struct socket *sock, struct msghdr *msg, size_t size, int ret),
	TPARGS(sock, msg, size, ret));
DEFINE_TRACE(socket_recvmsg,
	TPPROTO(struct socket *sock, struct msghdr *msg, size_t size, int flags,
		int ret),
	TPARGS(sock, msg, size, flags, ret));
DEFINE_TRACE(socket_create,
	TPPROTO(struct socket *sock, int fd),
	TPARGS(sock, fd));
/*
 * socket_call
 *
 * TODO : This tracepoint should be expanded to cover each element of the
 * switch in sys_socketcall().
 */
DEFINE_TRACE(socket_call,
	TPPROTO(int call, unsigned long a0),
	TPARGS(call, a0));
#endif
