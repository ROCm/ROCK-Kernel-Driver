#ifndef _TRACE_IPV4_H
#define _TRACE_IPV4_H

#include <linux/inetdevice.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(ipv4_addr_add,
	TPPROTO(struct in_ifaddr *ifa),
	TPARGS(ifa));
DEFINE_TRACE(ipv4_addr_del,
	TPPROTO(struct in_ifaddr *ifa),
	TPARGS(ifa));

#endif
