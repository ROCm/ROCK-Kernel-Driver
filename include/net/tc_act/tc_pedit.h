#ifndef __NET_TC_PED_H
#define __NET_TC_PED_H

#include <net/pkt_sched.h>

struct tcf_pedit
{
	tca_gen(pedit);
	unsigned char           nkeys;
	unsigned char           flags;
	struct tc_pedit_key     keys[0];
};

#endif
