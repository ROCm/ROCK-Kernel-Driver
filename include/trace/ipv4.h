#ifndef _TRACE_IPV4_H
#define _TRACE_IPV4_H

#include <linux/inetdevice.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(ipv4_addr_add,
	TP_PROTO(struct in_ifaddr *ifa),
	TP_ARGS(ifa));
DECLARE_TRACE(ipv4_addr_del,
	TP_PROTO(struct in_ifaddr *ifa),
	TP_ARGS(ifa));

#endif
