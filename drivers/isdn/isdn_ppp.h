/* $Id: isdn_ppp.h,v 1.17 2000/08/10 22:52:46 kai Exp $

 * header for Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/ppp_defs.h>     /* for PPP_PROTOCOL */
#include <linux/isdn_ppp.h>	/* for isdn_ppp info */
extern int isdn_ppp_read(int, struct file *, char *, int);
extern int isdn_ppp_write(int, struct file *, const char *, int);
extern int isdn_ppp_open(int, struct file *);
extern int isdn_ppp_init(void);
extern void isdn_ppp_cleanup(void);
extern int isdn_ppp_free(isdn_net_local *);
extern int isdn_ppp_bind(isdn_net_local *);
extern int isdn_ppp_xmit(struct sk_buff *, struct net_device *);
extern void isdn_ppp_receive(isdn_net_dev *, isdn_net_local *, struct sk_buff *);
extern int isdn_ppp_dev_ioctl(struct net_device *, struct ifreq *, int);
extern unsigned int isdn_ppp_poll(struct file *, struct poll_table_struct *);
extern int isdn_ppp_ioctl(int, struct file *, unsigned int, unsigned long);
extern void isdn_ppp_release(int, struct file *);
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


