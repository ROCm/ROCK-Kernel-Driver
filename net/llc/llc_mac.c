/*
 * llc_mac.c - Manages interface between LLC and MAC
 *
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

#if 1
#define dprintk(args...) printk(KERN_DEBUG args)
#else
#define dprintk(args...)
#endif

/* function prototypes */
static void fix_up_incoming_skb(struct sk_buff *skb);

/**
 *	mac_send_pdu - Sends PDU to specific device.
 *	@skb: pdu which must be sent
 *
 *	If module is not initialized then returns failure, else figures out
 *	where to direct this PDU. Sends PDU to specific device, at this point a
 *	device must has been assigned to the PDU; If not, can't transmit the
 *	PDU. PDU sent to MAC layer, is free to re-send at a later time. Returns
 *	0 on success, 1 for failure.
 */
int mac_send_pdu(struct sk_buff *skb)
{
	struct sk_buff *skb2;
	int pri = GFP_ATOMIC, rc = -1;

	if (!skb->dev) {
		dprintk("%s: skb->dev == NULL!", __FUNCTION__);
		goto out;
	}
	if (skb->sk)
		pri = (int)skb->sk->priority;
	skb2 = skb_clone(skb, pri);
	if (!skb2)
		goto out;
	rc = 0;
	dev_queue_xmit(skb2);
out:
	return rc;
}

/**
 *	llc_rcv - 802.2 entry point from net lower layers
 *	@skb: received pdu
 *	@dev: device that receive pdu
 *	@pt: packet type
 *
 *	When the system receives a 802.2 frame this function is called. It
 *	checks SAP and connection of received pdu and passes frame to
 *	llc_pdu_router for sending to proper state machine. If frame is
 *	related to a busy connection (a connection is sending data now),
 *	function queues this frame in connection's backlog.
 */
int llc_rcv(struct sk_buff *skb, struct net_device *dev,
		 struct packet_type *pt)
{
	struct llc_sap *sap;
	struct llc_pdu_sn *pdu;
	u8 dest;

	/* When the interface is in promisc. mode, drop all the crap that it
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
		if (llc_pdu_router(NULL, NULL, skb, 0))
			goto drop;
		goto out;
	}
	sap = llc_sap_find(pdu->dsap);
	if (!sap) /* unknown SAP */
		goto drop;
	llc_decode_pdu_type(skb, &dest);
	if (dest == LLC_DEST_SAP) { /* type 1 services */
		if (llc_pdu_router(sap, NULL, skb, LLC_TYPE_1))
			goto drop;
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
			 * FIXME: here we'll pass the sk->family of the
			 * listening socket, if found, when
			 * llc_lookup_listener is added in the next patches.
			 */
			sk = llc_sock_alloc(PF_LLC);
			if (!sk)
				goto drop;
			memcpy(&llc_sk(sk)->daddr, &saddr, sizeof(saddr));
			llc_sap_assign_sock(sap, sk);
			sock_hold(sk);
		}
		bh_lock_sock(sk);
		if (!sk->lock.users)
			rc = llc_pdu_router(llc_sk(sk)->sap, sk, skb,
					    LLC_TYPE_2);
		else {
			dprintk("%s: add to backlog\n", __FUNCTION__);
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

/**
 *	llc_pdu_router - routes received pdus to the upper layers
 *	@sap: current sap component structure.
 *	@sk: current connection structure.
 *	@frame: received frame.
 *	@type: type of received frame, that is LLC_TYPE_1 or LLC_TYPE_2
 *
 *	Queues received PDUs from LLC_MAC PDU receive queue until queue is
 *	empty; examines LLC header to determine the destination of PDU, if DSAP
 *	is NULL then data unit destined for station else frame destined for SAP
 *	or connection; finds a matching open SAP, if one, forwards the packet
 *	to it; if no matching SAP, drops the packet. Returns 0 or the return of
 *	llc_conn_state_process (that may well result in the connection being
 *	destroyed)
 */
int llc_pdu_router(struct llc_sap *sap, struct sock* sk,
		   struct sk_buff *skb, u8 type)
{
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	int rc = 0;

	if (!pdu->dsap) {
		struct llc_station *station = llc_station_get();
		struct llc_station_state_ev *ev = llc_station_ev(skb);

		ev->type	    = LLC_STATION_EV_TYPE_PDU;
		ev->data.pdu.reason = 0;
		llc_station_state_process(station, skb);
	} else if (type == LLC_TYPE_1) {
		struct llc_sap_state_ev *ev = llc_sap_ev(skb);

		ev->type	    = LLC_SAP_EV_TYPE_PDU;
		ev->data.pdu.reason = 0;
		llc_sap_state_process(sap, skb);
	} else if (type == LLC_TYPE_2) {
		struct llc_conn_state_ev *ev = llc_conn_ev(skb);
		struct llc_opt *llc = llc_sk(sk);

		if (!llc->dev)
			llc->dev = skb->dev;

		ev->type	    = LLC_CONN_EV_TYPE_PDU;
		ev->data.pdu.reason = 0;
		rc = llc_conn_state_process(sk, skb);
	} else
		rc = -EINVAL;
	return rc;
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

/**
 *	mac_dev_peer - search the appropriate dev to send packets to peer
 *	@current_dev - Current device suggested by upper layer
 *	@type - hardware type
 *	@mac - mac address
 *
 *	Check if the we should use loopback to send packets, i.e., if the
 *	dmac belongs to one of the local interfaces, returning the pointer
 *	to the loopback &net_device struct or the current_dev if it is not
 *	local.
 */
struct net_device *mac_dev_peer(struct net_device *current_dev, int type,
				u8 *mac)
{
	struct net_device *dev;

        rtnl_lock();
        dev = dev_getbyhwaddr(type, mac);
        if (dev)
                dev = __dev_get_by_name("lo");
        rtnl_unlock();
	return dev ? : current_dev;
}
