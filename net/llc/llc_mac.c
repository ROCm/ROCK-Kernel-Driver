/*
 * llc_mac.c - Manages interface between LLC and MAC
 *
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
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_tr.h>
#include <linux/rtnetlink.h>
#include <net/llc_if.h>
#include <net/llc_mac.h>
#include <net/llc_pdu.h>
#include <net/llc_sap.h>
#include <net/llc_conn.h>
#include <net/sock.h>
#include <net/llc_main.h>
#include <net/llc_evnt.h>
#include <net/llc_c_ev.h>
#include <net/llc_s_ev.h>
#include <linux/trdevice.h>

#if 0
#define dprintk(args...) printk(KERN_DEBUG args)
#else
#define dprintk(args...)
#endif

u8 llc_mac_null_var[IFHWADDRLEN];

static void fix_up_incoming_skb(struct sk_buff *skb);
static void llc_station_rcv(struct sk_buff *skb);
static void llc_sap_rcv(struct llc_sap *sap, struct sk_buff *skb);

/**
 *	llc_rcv - 802.2 entry point from net lower layers
 *	@skb: received pdu
 *	@dev: device that receive pdu
 *	@pt: packet type
 *
 *	When the system receives a 802.2 frame this function is called. It
 *	checks SAP and connection of received pdu and passes frame to
 *	llc_{station,sap,conn}_rcv for sending to proper state machine. If
 *	the frame is related to a busy connection (a connection is sending
 *	data now), it queues this frame in the connection's backlog.
 */
int llc_rcv(struct sk_buff *skb, struct net_device *dev,
	    struct packet_type *pt)
{
	struct llc_sap *sap;
	struct llc_pdu_sn *pdu;
	u8 dest;

	/*
	 * When the interface is in promisc. mode, drop all the crap that it
	 * receives, do not try to analyse it.
	 */
	if (skb->pkt_type == PACKET_OTHERHOST) {
		dprintk("%s: PACKET_OTHERHOST\n", __FUNCTION__);
		goto drop;
	}
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto out;
	fix_up_incoming_skb(skb);
	pdu = llc_pdu_sn_hdr(skb);
	if (!pdu->dsap) { /* NULL DSAP, refer to station */
		dprintk("%s: calling llc_station_rcv!\n", __FUNCTION__);
		llc_station_rcv(skb);
		goto out;
	}
	sap = llc_sap_find(pdu->dsap);
	if (!sap) {/* unknown SAP */
		dprintk("%s: llc_sap_find(%02X) failed!\n", __FUNCTION__,
		        pdu->dsap);
		goto drop;
	}
	llc_decode_pdu_type(skb, &dest);
	if (dest == LLC_DEST_SAP) { /* type 1 services */
		if (sap->rcv_func)
			sap->rcv_func(skb, dev, pt);
		else {
			struct llc_addr laddr;
			struct sock *sk;

			llc_pdu_decode_da(skb, laddr.mac);
			llc_pdu_decode_dsap(skb, &laddr.lsap);

			sk = llc_lookup_dgram(sap, &laddr);
			if (!sk)
				goto drop;
			skb->sk = sk;
			llc_sap_rcv(sap, skb);
			sock_put(sk);
		}
	} else if (dest == LLC_DEST_CONN) {
		struct llc_addr saddr, daddr;
		struct sock *sk;
		int rc;

		llc_pdu_decode_sa(skb, saddr.mac);
		llc_pdu_decode_ssap(skb, &saddr.lsap);
		llc_pdu_decode_da(skb, daddr.mac);
		llc_pdu_decode_dsap(skb, &daddr.lsap);

		sk = llc_lookup_established(sap, &saddr, &daddr);
		if (!sk) {
			/*
			 * Didn't find an active connection; verify if there
			 * is a listening socket for this llc addr
			 */
			struct llc_opt *llc;
			struct sock *parent;
			
			parent = llc_lookup_listener(sap, &daddr);

			if (!parent) {
				dprintk("llc_lookup_listener failed!\n");
				goto drop;
			}

			sk = llc_sk_alloc(parent->family, GFP_ATOMIC);
			if (!sk) {
				sock_put(parent);
				goto drop;
			}
			llc = llc_sk(sk);
			memcpy(&llc->laddr, &daddr, sizeof(llc->laddr));
			memcpy(&llc->daddr, &saddr, sizeof(llc->daddr));
			llc_sap_assign_sock(sap, sk);
			sock_hold(sk);
			sock_put(parent);
			skb->sk = parent;
		} else
			skb->sk = sk;
		bh_lock_sock(sk);
		if (!sock_owned_by_user(sk)) {
			/* rc = */ llc_conn_rcv(sk, skb);
			rc = 0;
		} else {
			dprintk("%s: adding to backlog...\n", __FUNCTION__);
			llc_set_backlog_type(skb, LLC_PACKET);
			sk_add_backlog(sk, skb);
			rc = 0;
		}
		bh_unlock_sock(sk);
		sock_put(sk);
		if (rc)
			goto drop;
	} else /* unknown or not supported pdu */
 		goto drop;
out:
	return 0;
drop:
	kfree_skb(skb);
	goto out;
}

/**
 *	fix_up_incoming_skb - initializes skb pointers
 *	@skb: This argument points to incoming skb
 *
 *	Initializes internal skb pointer to start of network layer by deriving
 *	length of LLC header; finds length of LLC control field in LLC header
 *	by looking at the two lowest-order bits of the first control field
 *	byte; field is either 3 or 4 bytes long.
 */
static void fix_up_incoming_skb(struct sk_buff *skb)
{
	u8 llc_len = 2;
	struct llc_pdu_sn *pdu = (struct llc_pdu_sn *)skb->data;

	if ((pdu->ctrl_1 & LLC_PDU_TYPE_MASK) == LLC_PDU_TYPE_U)
		llc_len = 1;
	llc_len += 2;
	skb->h.raw += llc_len;
	skb_pull(skb, llc_len);
	if (skb->protocol == htons(ETH_P_802_2)) {
		u16 pdulen = ((struct ethhdr *)skb->mac.raw)->h_proto,
		    data_size = ntohs(pdulen) - llc_len;

		skb_trim(skb, data_size);
	}
}

/*
 *	llc_station_rcv - send received pdu to the station state machine
 *	@skb: received frame.
 *
 *	Sends data unit to station state machine.
 */
static void llc_station_rcv(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	ev->type   = LLC_STATION_EV_TYPE_PDU;
	ev->reason = 0;
	llc_station_state_process(&llc_main_station, skb);
}


/**
 *	llc_conn_rcv - sends received pdus to the connection state machine
 *	@sk: current connection structure.
 *	@skb: received frame.
 *
 *	Sends received pdus to the connection state machine.
 */
int llc_conn_rcv(struct sock* sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);
	struct llc_opt *llc = llc_sk(sk);

	if (!llc->dev)
		llc->dev = skb->dev;
	ev->type   = LLC_CONN_EV_TYPE_PDU;
	ev->reason = 0;
	return llc_conn_state_process(sk, skb);
}

/**
 *	llc_sap_rcv - sends received pdus to the sap state machine
 *	@sap: current sap component structure.
 *	@skb: received frame.
 *
 *	Sends received pdus to the sap state machine.
 */
static void llc_sap_rcv(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	ev->type   = LLC_SAP_EV_TYPE_PDU;
	ev->reason = 0;
	llc_sap_state_process(sap, skb);
}

/**
 *	lan_hdrs_init - fills MAC header fields
 *	@skb: Address of the frame to initialize its MAC header
 *	@sa: The MAC source address
 *	@da: The MAC destination address
 *
 *	Fills MAC header fields, depending on MAC type. Returns 0, If MAC type
 *	is a valid type and initialization completes correctly 1, otherwise.
 */
u16 lan_hdrs_init(struct sk_buff *skb, u8 *sa, u8 *da)
{
	u16 rc = 0;

	switch (skb->dev->type) {
#ifdef CONFIG_TR
	case ARPHRD_IEEE802_TR: {
		struct trh_hdr *trh;
		struct net_device *dev = skb->dev;

		trh = (struct trh_hdr *)skb_push(skb, sizeof(*trh));
		trh->ac = AC;
		trh->fc = LLC_FRAME;
		if (sa)
			memcpy(trh->saddr, sa, dev->addr_len);
		else
			memset(trh->saddr, 0, dev->addr_len);
		if (da) {
			memcpy(trh->daddr, da, dev->addr_len);
			tr_source_route(skb, trh, dev);
		}
		skb->mac.raw = skb->data;
		break;
	}
#endif
	case ARPHRD_ETHER:
	case ARPHRD_LOOPBACK: {
		unsigned short len = skb->len;
		struct ethhdr *eth;

		skb->mac.raw = skb_push(skb, sizeof(*eth));
		eth = (struct ethhdr *)skb->mac.raw;
		eth->h_proto = htons(len);
		memcpy(eth->h_dest, da, ETH_ALEN);
		memcpy(eth->h_source, sa, ETH_ALEN);
		break;
	}
	default:
		printk(KERN_WARNING "Unknown DEVICE type : %d\n",
		       skb->dev->type);
		rc = 1;
	}
	return rc;
}
