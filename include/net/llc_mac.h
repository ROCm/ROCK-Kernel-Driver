#ifndef LLC_MAC_H
#define LLC_MAC_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
/* Defines MAC-layer interface to LLC layer */
extern int mac_send_pdu(struct sk_buff *skb);
extern int llc_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt);
extern struct net_device *mac_dev_peer(struct net_device *current_dev,
				       int type, u8 *mac);
extern int llc_pdu_router(struct llc_sap *sap, struct sock *sk,
			  struct sk_buff *skb, u8 type);
extern u16 lan_hdrs_init(struct sk_buff *skb, u8 *sa, u8 *da);

static __inline__ void llc_set_backlog_type(struct sk_buff *skb, char type)
{
	skb->cb[sizeof(skb->cb) - 1] = type;
}

static __inline__ char llc_backlog_type(struct sk_buff *skb)
{
	return skb->cb[sizeof(skb->cb) - 1];
}

#endif /* LLC_MAC_H */
