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

void isdn_x25_encap_changed(isdn_net_dev *p, isdn_net_ioctl_cfg *cfg);
int  isdn_x25_setup_dev(isdn_net_dev *p);

#else

static inline void
isdn_x25_encap_changed(isdn_net_dev *p, isdn_net_ioctl_cfg *cfg)
{
}

static inline int 
isdn_x25_setup_dev(isdn_net_dev *p)
{
	printk(KERN_WARNING "ISDN: SyncPPP support not configured\n");
	return -EINVAL;
}


#endif
