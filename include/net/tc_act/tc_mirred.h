#ifndef __NET_TC_MIR_H
#define __NET_TC_MIR_H

#include <net/pkt_sched.h>

struct tcf_mirred
{
	tca_gen(mirred);
	int eaction;
	int ifindex;
	int ok_push;
	struct net_device *dev;
};

#endif
