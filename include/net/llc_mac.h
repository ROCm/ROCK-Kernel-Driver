#ifndef LLC_MAC_H
#define LLC_MAC_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001, 2002 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
extern int llc_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt);
extern u16 lan_hdrs_init(struct sk_buff *skb, u8 *sa, u8 *da);
extern int llc_conn_rcv(struct sock *sk, struct sk_buff *skb);

static __inline__ void llc_set_backlog_type(struct sk_buff *skb, char type)
{
	skb->cb[sizeof(skb->cb) - 1] = type;
}

static __inline__ char llc_backlog_type(struct sk_buff *skb)
{
	return skb->cb[sizeof(skb->cb) - 1];
}

extern u8 llc_mac_null_var[IFHWADDRLEN];

/**
 *      llc_mac_null - determines if a address is a null mac address
 *      @mac: Mac address to test if null.
 *
 *      Determines if a given address is a null mac address.  Returns 0 if the
 *      address is not a null mac, 1 if the address is a null mac.
 */
static __inline__ int llc_mac_null(u8 *mac)
{
	return !memcmp(mac, llc_mac_null_var, IFHWADDRLEN);
}

static __inline__ int llc_addrany(struct llc_addr *addr)
{
	return llc_mac_null(addr->mac) && !addr->lsap;
}

/**
 *	llc_mac_match - determines if two mac addresses are the same
 *	@mac1: First mac address to compare.
 *	@mac2: Second mac address to compare.
 *
 *	Determines if two given mac address are the same.  Returns 0 if there
 *	is not a complete match up to len, 1 if a complete match up to len is
 *	found.
 */
static __inline__ int llc_mac_match(u8 *mac1, u8 *mac2)
{
	return !memcmp(mac1, mac2, IFHWADDRLEN);
}
#endif /* LLC_MAC_H */
