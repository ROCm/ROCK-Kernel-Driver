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
extern struct isdn_netif_ops isdn_ppp_ops;

int isdn_ppp_init(void);
void isdn_ppp_cleanup(void);
int isdn_ppp_dial_slave(char *);
int isdn_ppp_hangup_slave(char *);

void
isdn_ppp_frame_log(char *info, char *data, int len, int maxlen, 
		   int unit, int slot);
int
isdn_ppp_strip_proto(struct sk_buff *skb);

#define IPPP_OPEN	0x01
#define IPPP_CONNECT	0x02
#define IPPP_CLOSEWAIT	0x04
#define IPPP_NOBLOCK	0x08
#define IPPP_ASSIGNED	0x10

#define IPPP_MAX_HEADER 10


