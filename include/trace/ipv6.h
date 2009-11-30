#ifndef _TRACE_IPV6_H
#define _TRACE_IPV6_H

#include <net/if_inet6.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(ipv6_addr_add,
	TP_PROTO(struct inet6_ifaddr *ifa),
	TP_ARGS(ifa));
DECLARE_TRACE(ipv6_addr_del,
	TP_PROTO(struct inet6_ifaddr *ifa),
	TP_ARGS(ifa));

#endif
