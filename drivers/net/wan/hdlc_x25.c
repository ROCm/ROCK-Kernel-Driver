/*
 * Generic HDLC support routines for Linux
 * X.25 support
 *
 * Copyright (C) 1999 - 2003 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/pkt_sched.h>
#include <linux/inetdevice.h>
#include <linux/lapb.h>
#include <linux/rtnetlink.h>
#include <linux/hdlc.h>

/* These functions are callbacks called by LAPB layer */

static void x25_connect_disconnect(void *token, int reason, int code)
{
	hdlc_device *hdlc = token;
	struct sk_buff *skb;
	unsigned char *ptr;

	if ((skb = dev_alloc_skb(1)) == NULL) {
		printk(KERN_ERR "%s: out of memory\n", hdlc_to_name(hdlc));
		return;
	}

	ptr = skb_put(skb, 1);
	*ptr = code;

	skb->dev = hdlc_to_dev(hdlc);
	skb->protocol = htons(ETH_P_X25);
	skb->mac.raw = skb->data;
	skb->pkt_type = PACKET_HOST;

	netif_rx(skb);
}



static void x25_connected(void *token, int reason)
{
	x25_connect_disconnect(token, reason, 1);
}



static void x25_disconnected(void *token, int reason)
{
	x25_connect_disconnect(token, reason, 2);
}



static int x25_data_indication(void *token, struct sk_buff *skb)
{
	hdlc_device *hdlc = token;
	unsigned char *ptr;

	skb_push(skb, 1);

	if (skb_cow(skb, 1))
		return NET_RX_DROP;

	ptr  = skb->data;
	*ptr = 0;

	skb->dev = hdlc_to_dev(hdlc);
	skb->protocol = htons(ETH_P_X25);
	skb->mac.raw = skb->data;
	skb->pkt_type = PACKET_HOST;

	return netif_rx(skb);
}



static void x25_data_transmit(void *token, struct sk_buff *skb)
{
	hdlc_device *hdlc = token;
	hdlc->xmit(skb, hdlc_to_dev(hdlc)); /* Ignore return value :-( */
}



static int x25_xmit(struct sk_buff *skb, struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	int result;


	/* X.25 to LAPB */
	switch (skb->data[0]) {
	case 0:		/* Data to be transmitted */
		skb_pull(skb, 1);
		if ((result = lapb_data_request(hdlc, skb)) != LAPB_OK)
			dev_kfree_skb(skb);
		return 0;

	case 1:
		if ((result = lapb_connect_request(hdlc))!= LAPB_OK) {
			if (result == LAPB_CONNECTED)
				/* Send connect confirm. msg to level 3 */
				x25_connected(hdlc, 0);
			else
				printk(KERN_ERR "%s: LAPB connect request "
				       "failed, error code = %i\n",
				       hdlc_to_name(hdlc), result);
		}
		break;

	case 2:
		if ((result = lapb_disconnect_request(hdlc)) != LAPB_OK) {
			if (result == LAPB_NOTCONNECTED)
				/* Send disconnect confirm. msg to level 3 */
				x25_disconnected(hdlc, 0);
			else
				printk(KERN_ERR "%s: LAPB disconnect request "
				       "failed, error code = %i\n",
				       hdlc_to_name(hdlc), result);
		}
		break;

	default:		/* to be defined */
		break;
	}

	dev_kfree_skb(skb);
	return 0;
}



static int x25_open(hdlc_device *hdlc)
{
	struct lapb_register_struct cb;
	int result;

	cb.connect_confirmation = x25_connected;
	cb.connect_indication = x25_connected;
	cb.disconnect_confirmation = x25_disconnected;
	cb.disconnect_indication = x25_disconnected;
	cb.data_indication = x25_data_indication;
	cb.data_transmit = x25_data_transmit;

	result = lapb_register(hdlc, &cb);
	if (result != LAPB_OK)
		return result;
	return 0;
}



static void x25_close(hdlc_device *hdlc)
{
	lapb_unregister(hdlc);
}



static int x25_rx(struct sk_buff *skb)
{
	hdlc_device *hdlc = dev_to_hdlc(skb->dev);

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL) {
		hdlc->stats.rx_dropped++;
		return NET_RX_DROP;
	}

	if (lapb_data_received(hdlc, skb) == LAPB_OK)
		return NET_RX_SUCCESS;

	hdlc->stats.rx_errors++;
	dev_kfree_skb_any(skb);
	return NET_RX_DROP;
}



int hdlc_x25_ioctl(hdlc_device *hdlc, struct ifreq *ifr)
{
	struct net_device *dev = hdlc_to_dev(hdlc);
	int result;

	switch (ifr->ifr_settings.type) {
	case IF_GET_PROTO:
		ifr->ifr_settings.type = IF_PROTO_X25;
		return 0; /* return protocol only, no settable parameters */

	case IF_PROTO_X25:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if(dev->flags & IFF_UP)
			return -EBUSY;

		result=hdlc->attach(hdlc, ENCODING_NRZ,PARITY_CRC16_PR1_CCITT);
		if (result)
			return result;

		hdlc_proto_detach(hdlc);
		memset(&hdlc->proto, 0, sizeof(hdlc->proto));

		hdlc->proto.open = x25_open;
		hdlc->proto.close = x25_close;
		hdlc->proto.netif_rx = x25_rx;
		hdlc->proto.type_trans = NULL;
		hdlc->proto.id = IF_PROTO_X25;
		dev->hard_start_xmit = x25_xmit;
		dev->hard_header = NULL;
		dev->type = ARPHRD_X25;
		dev->addr_len = 0;
		return 0;
	}

	return -EINVAL;
}
