#ifndef _NET_P8022_H
#define _NET_P8022_H
#include <net/llc_if.h>

extern struct datalink_proto *register_8022_client(unsigned char type,
			   int (*indicate)(struct llc_prim_if_block *prim));
extern void unregister_8022_client(struct datalink_proto *proto);

#endif
