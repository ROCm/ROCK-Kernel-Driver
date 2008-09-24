#ifndef _TRACE_IPV6_H
#define _TRACE_IPV6_H

#include <net/if_inet6.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(ipv6_addr_add,
	TPPROTO(struct inet6_ifaddr *ifa),
	TPARGS(ifa));
DEFINE_TRACE(ipv6_addr_del,
	TPPROTO(struct inet6_ifaddr *ifa),
	TPARGS(ifa));

#endif
