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

int  isdn_x25_setup(isdn_net_dev *p, int encap);
void isdn_x25_cleanup(isdn_net_dev *p);
void isdn_x25_open(struct net_device *dev);
void isdn_x25_close(struct net_device *dev);
void isdn_x25_bhup(isdn_net_local *lp);
void isdn_x25_hangup(isdn_net_local *lp);
int  isdn_x25_start_xmit(struct sk_buff *skb, struct net_device *dev);
void isdn_x25_realrm(isdn_net_dev *p);

#else

static inline void
isdn_x25_cleanup(isdn_net_dev *p)
{
}

static inline int 
isdn_x25_setup(isdn_net_dev *p, int encap)
{
	printk(KERN_WARNING "ISDN: X25 support not configured\n");
	return -EINVAL;
}

static inline void
isdn_x25_open(struct net_device *dev)
{
}

static inline void
isdn_x25_close(struct net_device *dev)
{
}

static inline void
isdn_x25_dhup(isdn_net_local *lp)
{
}

static inline void
isdn_x25_bhup(isdn_net_local *lp)
{
}

static inline void
isdn_x25_hangup(isdn_net_local *lp)
{
}

static inline int
isdn_x25_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	return 0;
}

static inline void
isdn_x25_realrm(isdn_net_dev *p)
{
}

#endif
