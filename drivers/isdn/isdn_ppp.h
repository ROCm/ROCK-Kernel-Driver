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

extern int isdn_ppp_open(struct inode *, struct file *);
extern int isdn_ppp_release(struct inode *, struct file *);
extern int isdn_ppp_read(struct file *, char *, int, loff_t *off);
extern int isdn_ppp_write(struct file *, const char *, int, loff_t *off);
extern int isdn_ppp_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
extern unsigned int isdn_ppp_poll(struct file *, struct poll_table_struct *);

extern int isdn_ppp_init(void);
extern void isdn_ppp_cleanup(void);
extern int isdn_ppp_free(isdn_net_local *);
extern int isdn_ppp_bind(isdn_net_local *);
extern int isdn_ppp_xmit(struct sk_buff *, struct net_device *);
extern void isdn_ppp_receive(isdn_net_dev *, isdn_net_local *, struct sk_buff *);
extern int isdn_ppp_dev_ioctl(struct net_device *, struct ifreq *, int);
extern int isdn_ppp_dial_slave(char *);
extern void isdn_ppp_wakeup_daemon(isdn_net_local *);

extern int isdn_ppp_register_compressor(struct isdn_ppp_compressor *ipc);
extern int isdn_ppp_unregister_compressor(struct isdn_ppp_compressor *ipc);

#define IPPP_OPEN	0x01
#define IPPP_CONNECT	0x02
#define IPPP_CLOSEWAIT	0x04
#define IPPP_NOBLOCK	0x08
#define IPPP_ASSIGNED	0x10

#define IPPP_MAX_HEADER 10


