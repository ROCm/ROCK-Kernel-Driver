/*
 *  linux/include/linux/sunrpc/timer.h
 *
 *  Declarations for the RPC transport timer.
 *
 *  Copyright (C) 2002 Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#ifndef _LINUX_SUNRPC_TIMER_H
#define _LINUX_SUNRPC_TIMER_H

struct rpc_rtt {
	long timeo;		/* default timeout value */
	long srtt[5];		/* smoothed round trip time << 3 */
	long sdrtt[5];		/* soothed medium deviation of RTT */
};


extern void rpc_init_rtt(struct rpc_rtt *rt, long timeo);
extern void rpc_update_rtt(struct rpc_rtt *rt, int timer, long m);
extern long rpc_calc_rto(struct rpc_rtt *rt, int timer);

#endif /* _LINUX_SUNRPC_TIMER_H */
