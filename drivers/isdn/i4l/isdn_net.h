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

extern void isdn_net_init(void);
extern void isdn_net_exit(void);
extern void isdn_net_lib_init(void);
extern void isdn_net_lib_exit(void);
extern void isdn_net_hangup_all(void);
extern int isdn_net_ioctl(struct inode *, struct file *, uint, ulong);

extern int register_isdn_netif(int encap, struct isdn_netif_ops *ops);
extern int isdn_net_autodial(struct sk_buff *skb, struct net_device *ndev);
extern int isdn_net_start_xmit(struct sk_buff *skb, struct net_device *ndev);

extern int  isdn_net_dial(isdn_net_dev *idev);

extern int  isdn_net_bsent(isdn_net_dev *idev, isdn_ctrl *c);

extern int isdn_net_stat_callback(int, isdn_ctrl *);
extern int isdn_net_find_icall(int, int, int, setup_parm *);
extern int isdn_net_hangup(isdn_net_dev *);
extern int isdn_net_rcv_skb(int, struct sk_buff *);
extern int isdn_net_dial_req(isdn_net_dev *);
extern void isdn_net_writebuf_skb(isdn_net_dev *, struct sk_buff *skb);
extern void isdn_net_write_super(isdn_net_dev *, struct sk_buff *skb);
extern int isdn_net_online(isdn_net_dev *);

enum {
	ST_CHARGE_NULL,
	ST_CHARGE_GOT_CINF,  /* got a first charge info */
	ST_CHARGE_HAVE_CINT, /* got a second chare info and thus the timing */
};

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


