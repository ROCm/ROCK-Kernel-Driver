/* $Id: isdn_net.h,v 1.19.6.4 2001/09/28 08:05:29 kai Exp $
 *
 * header for Linux ISDN subsystem, network related functions (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/isdn.h>
  			      /* Definitions for hupflags:                */
#define ISDN_CHARGEHUP   4      /* We want to use the charge mechanism      */
#define ISDN_INHUP       8      /* Even if incoming, close after huptimeout */
#define ISDN_MANCHARGE  16      /* Charge Interval manually set             */

/*
 * Definitions for Cisco-HDLC header.
 */

#define CISCO_ADDR_UNICAST    0x0f
#define CISCO_ADDR_BROADCAST  0x8f
#define CISCO_CTRL            0x00
#define CISCO_TYPE_CDP        0x2000
#define CISCO_TYPE_SLARP      0x8035
#define CISCO_SLARP_REQUEST   0
#define CISCO_SLARP_REPLY     1
#define CISCO_SLARP_KEEPALIVE 2

extern void isdn_net_init_module(void);

extern int isdn_net_new(char *, isdn_net_local *);
extern int isdn_net_newslave(char *);
extern int isdn_net_rm(char *);
extern int isdn_net_rmall(void);
extern int isdn_net_stat_callback(int, isdn_ctrl *);
extern int isdn_net_setcfg(isdn_net_ioctl_cfg *);
extern int isdn_net_getcfg(isdn_net_ioctl_cfg *);
extern int isdn_net_addphone(isdn_net_ioctl_phone *);
extern int isdn_net_getphones(isdn_net_ioctl_phone *, char *);
extern int isdn_net_getpeer(isdn_net_ioctl_phone *, isdn_net_ioctl_phone *);
extern int isdn_net_delphone(isdn_net_ioctl_phone *);
extern int isdn_net_find_icall(int, int, int, setup_parm *);
extern void isdn_net_hangup(isdn_net_dev *);
extern void isdn_net_hangup_all(void);
extern int isdn_net_force_hangup(char *);
extern int isdn_net_force_dial(char *);
extern isdn_net_dev *isdn_net_findif(char *);
extern int isdn_net_rcv_skb(int, struct sk_buff *);
extern int isdn_net_dial_req(isdn_net_dev *);
extern void isdn_net_writebuf_skb(isdn_net_dev *, struct sk_buff *skb);
extern void isdn_net_write_super(isdn_net_dev *, struct sk_buff *skb);
extern int isdn_net_online(isdn_net_dev *);

#define ISDN_NET_MAX_QUEUE_LENGTH 2

/*
 * is this particular channel busy?
 */
static inline int
isdn_net_dev_busy(isdn_net_dev *idev)
{
	if (atomic_read(&idev->frame_cnt) < ISDN_NET_MAX_QUEUE_LENGTH)
		return 0;
	else 
		return 1;
}

/*
 * For the given net device, this will get a non-busy channel out of the
 * corresponding bundle. The returned channel is locked.
 */
static inline isdn_net_dev *
isdn_net_get_locked_dev(isdn_net_local *mlp)
{
	unsigned long flags;
	isdn_net_dev *idev;

	spin_lock_irqsave(&mlp->online_lock, flags);

	list_for_each_entry(idev, &mlp->online, online) {
		spin_lock_bh(&idev->xmit_lock);
		if (!isdn_net_dev_busy(idev)) {
			/* point the head to next online channel */
			list_del(&mlp->online);
			list_add(&mlp->online, &idev->online);
			goto found;
		}
		spin_unlock_bh(&idev->xmit_lock);
	}
	idev = NULL;

 found:
	spin_unlock_irqrestore(&mlp->online_lock, flags);
	return idev;
}

/*
 * add a channel to a bundle
 */
static inline void
isdn_net_add_to_bundle(isdn_net_local *mlp, isdn_net_dev *idev)
{
	unsigned long flags;

	spin_lock_irqsave(&mlp->online_lock, flags);
	list_add(&idev->online, &mlp->online);
	spin_unlock_irqrestore(&mlp->online_lock, flags);
}
/*
 * remove a channel from the bundle it belongs to
 */
static inline void
isdn_net_rm_from_bundle(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	unsigned long flags;

	spin_lock_irqsave(&mlp->online_lock, flags);
	list_del(&idev->online);
	spin_unlock_irqrestore(&mlp->online_lock, flags);
}

/*
 * wake up the network -> net_device queue.
 * For slaves, wake the corresponding master interface.
 */
static inline void
isdn_net_dev_wake_queue(isdn_net_dev *idev)
{
	netif_wake_queue(&idev->mlp->dev);
}

static inline int
isdn_net_bound(isdn_net_dev *idev)
{
	return idev->isdn_slot >= 0;
}

static inline int
put_u8(unsigned char *p, u8 x)
{
	*p = x;
	return 1;
}

static inline int
put_u16(unsigned char *p, u16 x)
{
	*((u16 *)p) = htons(x);
	return 2;
}

static inline int
put_u32(unsigned char *p, u32 x)
{
	*((u32 *)p) = htonl(x);
	return 4;
}

static inline int
get_u8(unsigned char *p, u8 *x)
{
	*x = *p;
	return 1;
}

static inline int
get_u16(unsigned char *p, u16 *x)
{
	*x = ntohs(*((u16 *)p));
	return 2;
}

static inline int
get_u32(unsigned char *p, u32 *x)
{
	*x = ntohl(*((u32 *)p));
	return 4;
}


