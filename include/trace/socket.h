#ifndef _TRACE_SOCKET_H
#define _TRACE_SOCKET_H

#include <net/sock.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(socket_sendmsg,
	TP_PROTO(struct socket *sock, struct msghdr *msg, size_t size, int ret),
	TP_ARGS(sock, msg, size, ret));
DECLARE_TRACE(socket_recvmsg,
	TP_PROTO(struct socket *sock, struct msghdr *msg, size_t size, int flags,
		int ret),
	TP_ARGS(sock, msg, size, flags, ret));
DECLARE_TRACE(socket_create,
	TP_PROTO(struct socket *sock, int fd),
	TP_ARGS(sock, fd));
/*
 * socket_call
 *
 * TODO : This tracepoint should be expanded to cover each element of the
 * switch in sys_socketcall().
 */
DECLARE_TRACE(socket_call,
	TP_PROTO(int call, unsigned long a0),
	TP_ARGS(call, a0));
#endif
