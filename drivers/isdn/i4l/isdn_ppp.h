/* $Id: isdn_ppp.h,v 1.17.6.1 2001/09/23 22:24:32 kai Exp $
 *
 * header for Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/ppp_defs.h>     /* for PPP_PROTOCOL */
#include <linux/isdn_ppp.h>	/* for isdn_ppp info */

extern struct file_operations isdn_ppp_fops;

extern int isdn_ppp_init(void);
extern void isdn_ppp_cleanup(void);
extern int isdn_ppp_dial_slave(char *);
extern int isdn_ppp_hangup_slave(char *);

#ifdef CONFIG_ISDN_PPP

int  isdn_ppp_setup(isdn_net_dev *p);
int  isdn_ppp_bind(isdn_net_local *);
int  isdn_ppp_xmit(struct sk_buff *, struct net_device *);

#else

static inline int
isdn_ppp_setup(isdn_net_dev *p)
{
	printk(KERN_WARNING "ISDN: SyncPPP support not configured\n");
	return -EINVAL;
}

static inline int
isdn_ppp_bind(isdn_net_local *)
{
	return 0;
}

static inline int
isdn_ppp_xmit(struct sk_buff *, struct net_device *);
{
	return 0;
}

#endif

#define IPPP_OPEN	0x01
#define IPPP_CONNECT	0x02
#define IPPP_CLOSEWAIT	0x04
#define IPPP_NOBLOCK	0x08
#define IPPP_ASSIGNED	0x10

#define IPPP_MAX_HEADER 10


