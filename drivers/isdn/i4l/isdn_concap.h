/* $Id: isdn_concap.h,v 1.3.6.1 2001/09/23 22:24:31 kai Exp $
 *
 * Linux ISDN subsystem, protocol encapsulation
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

extern struct concap_device_ops isdn_concap_reliable_dl_dops;
extern struct concap_device_ops isdn_concap_demand_dial_dops;

struct concap_proto *isdn_concap_new(int);

#ifdef CONFIG_ISDN_X25

extern struct isdn_netif_ops isdn_x25_ops;

int  isdn_x25_start_xmit(struct sk_buff *skb, struct net_device *dev);

#else

static inline int
isdn_x25_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	return 0;
}

#endif
