#ifndef LLC_H
#define LLC_H
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

struct llc_sap;
struct net_device;
struct packet_type;
struct sk_buff;

extern int llc_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt);

extern void llc_add_pack(int type, void (*handler)(struct llc_sap *sap,
						   struct sk_buff *skb));
extern void llc_remove_pack(int type);

extern void llc_set_station_handler(void (*handler)(struct sk_buff *skb));
#endif /* LLC_H */
