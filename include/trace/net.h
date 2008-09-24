#ifndef _TRACE_NET_H
#define _TRACE_NET_H

#include <net/sock.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(net_dev_xmit,
	TPPROTO(struct sk_buff *skb),
	TPARGS(skb));
DEFINE_TRACE(net_dev_receive,
	TPPROTO(struct sk_buff *skb),
	TPARGS(skb));

#endif
