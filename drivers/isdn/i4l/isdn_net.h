/* Linux ISDN subsystem, network related functions
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/isdn.h>

void isdn_net_init(void);
void isdn_net_exit(void);
void isdn_net_lib_init(void);
void isdn_net_lib_exit(void);
void isdn_net_hangup_all(void);
int  isdn_net_ioctl(struct inode *, struct file *, uint, ulong);

int  register_isdn_netif(int encap, struct isdn_netif_ops *ops);
int  isdn_net_start_xmit(struct sk_buff *skb, struct net_device *ndev);
void isdn_net_online(isdn_net_dev *idev);
void isdn_net_offline(isdn_net_dev *idev);

int  isdn_net_stat_callback(int, isdn_ctrl *);
int  isdn_net_find_icall(int, int, int, setup_parm *);
int  isdn_net_rcv_skb(int, struct sk_buff *);

int  isdn_net_hangup(isdn_net_dev *);
int  isdn_net_dial_req(isdn_net_dev *);
void isdn_net_writebuf_skb(isdn_net_dev *, struct sk_buff *skb);
void isdn_net_write_super(isdn_net_dev *, struct sk_buff *skb);
int  isdn_net_autodial(struct sk_buff *skb, struct net_device *ndev);
isdn_net_dev *isdn_net_get_xmit_dev(isdn_net_local *mlp);
void isdn_netif_rx(isdn_net_dev *idev, struct sk_buff *skb, u16 protocol);

/* ====================================================================== */

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


