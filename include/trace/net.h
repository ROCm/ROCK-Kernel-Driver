#ifndef _TRACE_NET_H
#define _TRACE_NET_H

#include <linux/tracepoint.h>

struct sk_buff;
DECLARE_TRACE(net_dev_xmit,
	TP_PROTO(struct sk_buff *skb),
	TP_ARGS(skb));
DECLARE_TRACE(net_dev_receive,
	TP_PROTO(struct sk_buff *skb),
	TP_ARGS(skb));

/*
 * Note these first 2 traces are actually in __napi_schedule and net_rx_action
 * respectively.  The former is in __napi_schedule because it uses at-most-once
 * logic and placing it in the calling routine (napi_schedule) would produce
 * countless trace events that were effectively  no-ops.  napi_poll is
 * implemented in net_rx_action, because thats where we do our polling on
 * devices.  The last trace point is in napi_complete, right where you would
 * think it would be.
 */
struct napi_struct;
DECLARE_TRACE(net_napi_schedule,
	TP_PROTO(struct napi_struct *n),
	TP_ARGS(n));
DECLARE_TRACE(net_napi_poll,
	TP_PROTO(struct napi_struct *n),
	TP_ARGS(n));
DECLARE_TRACE(net_napi_complete,
	TP_PROTO(struct napi_struct *n),
	TP_ARGS(n));

#endif
