#ifndef _TUNNEL_MN_H
#define _TUNNEL_MN_H

#include "tunnel.h"

extern int mipv6_add_tnl_to_ha(void);

extern int mipv6_mv_tnl_to_ha(struct in6_addr *ha_addr, 
			      struct in6_addr *coa,
			      struct in6_addr *home_addr, int add);

extern int mipv6_del_tnl_to_ha(void);

#endif
