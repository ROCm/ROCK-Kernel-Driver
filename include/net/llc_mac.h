#ifndef LLC_MAC_H
#define LLC_MAC_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#define LLC_DEST_INVALID         0      /* Invalid LLC PDU type */
#define LLC_DEST_SAP             1      /* Type 1 goes here */
#define LLC_DEST_CONN            2      /* Type 2 goes here */

extern int llc_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt);
extern u16 lan_hdrs_init(struct sk_buff *skb, u8 *sa, u8 *da);
#endif /* LLC_MAC_H */
