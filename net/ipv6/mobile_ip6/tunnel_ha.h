#ifndef _TUNNEL_HA_H
#define _TUNNEL_HA_H

#include "tunnel.h"

extern int mipv6_max_tnls;
extern int mipv6_min_tnls;

extern void mipv6_initialize_tunnel(void);
extern void mipv6_shutdown_tunnel(void);

extern int mipv6_add_tnl_to_mn(struct in6_addr *coa, 
			       struct in6_addr *ha_addr,
			       struct in6_addr *home_addr);

extern int mipv6_del_tnl_to_mn(struct in6_addr *coa, 
			       struct in6_addr *ha_addr,
			       struct in6_addr *home_addr);

#endif
