/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <xen/evtchn.h>

#include "accel.h"
#include "accel_msg_iface.h"
#include "accel_util.h"
#include "accel_solarflare.h"

/* Send a HELLO to front end to start things off */
void netback_accel_msg_tx_hello(struct netback_accel *bend, unsigned version)
{
	unsigned long lock_state;
	struct net_accel_msg *msg = 
		net_accel_msg_start_send(bend->shared_page,
					 &bend->to_domU, &lock_state);
	/* The queue _cannot_ be full, we're the first users. */
	EPRINTK_ON(msg == NULL);

	if (msg != NULL) {
		net_accel_msg_init(msg, NET_ACCEL_MSG_HELLO);
		msg->u.hello.version = version;
		msg->u.hello.max_pages = bend->quotas.max_buf_pages; 
		VPRINTK("Sending hello to channel %d\n", bend->msg_channel);
		net_accel_msg_complete_send_notify(bend->shared_page, 
						   &bend->to_domU,
						   &lock_state, 
						   bend->msg_channel_irq);
	}
}

/* Send a local mac message to vnic */
static void netback_accel_msg_tx_localmac(struct netback_accel *bend, 
					  int type, const void *mac)
{
	unsigned long lock_state;
	struct net_accel_msg *msg;
	DECLARE_MAC_BUF(buf);

	BUG_ON(bend == NULL || mac == NULL);

	VPRINTK("Sending local mac message: %s\n", print_mac(buf, mac));
	
	msg = net_accel_msg_start_send(bend->shared_page, &bend->to_domU,
				       &lock_state);
	
	if (msg != NULL) {
		net_accel_msg_init(msg, NET_ACCEL_MSG_LOCALMAC);
		msg->u.localmac.flags = type;
		memcpy(msg->u.localmac.mac, mac, ETH_ALEN);
		net_accel_msg_complete_send_notify(bend->shared_page, 
						   &bend->to_domU,
						   &lock_state, 
						   bend->msg_channel_irq);
	} else {
		/*
		 * TODO if this happens we may leave a domU
		 * fastpathing packets when they should be delivered
		 * locally.  Solution is get domU to timeout entries
		 * in its fastpath lookup table when it receives no RX
		 * traffic
		 */
		EPRINTK("%s: saw full queue, may need ARP timer to recover\n",
			__FUNCTION__);
	}
}

/* Send an add local mac message to vnic */
void netback_accel_msg_tx_new_localmac(struct netback_accel *bend,
				       const void *mac)
{
	netback_accel_msg_tx_localmac(bend, NET_ACCEL_MSG_ADD, mac);
}


static int netback_accel_msg_rx_buffer_map(struct netback_accel *bend, 
					   struct net_accel_msg *msg)
{
	int log2_pages, rc;

	/* Can only allocate in power of two */
	log2_pages = log2_ge(msg->u.mapbufs.pages, 0);
	if (msg->u.mapbufs.pages != pow2(log2_pages)) {
		EPRINTK("%s: Can only alloc bufs in power of 2 sizes (%d)\n",
			__FUNCTION__, msg->u.mapbufs.pages);
		rc = -EINVAL;
		goto err_out;
	}
  
	/*
	 * Sanity.  Assumes NET_ACCEL_MSG_MAX_PAGE_REQ is same for
	 * both directions/domains
	 */
	if (msg->u.mapbufs.pages > NET_ACCEL_MSG_MAX_PAGE_REQ) {
		EPRINTK("%s: too many pages in a single message: %d %d\n", 
			__FUNCTION__, msg->u.mapbufs.pages,
			NET_ACCEL_MSG_MAX_PAGE_REQ);
		rc = -EINVAL;
		goto err_out;
	}
  
	if ((rc = netback_accel_add_buffers(bend, msg->u.mapbufs.pages, 
					    log2_pages, msg->u.mapbufs.grants, 
					    &msg->u.mapbufs.buf)) < 0) {
		goto err_out;
	}

	msg->id |= NET_ACCEL_MSG_REPLY;
  
	return 0;

 err_out:
	EPRINTK("%s: err_out\n", __FUNCTION__);
	msg->id |= NET_ACCEL_MSG_ERROR | NET_ACCEL_MSG_REPLY;
	return rc;
}


/* Hint from frontend that one of our filters is out of date */
static int netback_accel_process_fastpath(struct netback_accel *bend, 
					  struct net_accel_msg *msg)
{
	struct netback_accel_filter_spec spec;

	if (msg->u.fastpath.flags & NET_ACCEL_MSG_REMOVE) {
		/* 
		 * Would be nice to BUG() this but would leave us
		 * vulnerable to naughty frontend
		 */
		EPRINTK_ON(msg->u.fastpath.flags & NET_ACCEL_MSG_ADD);
		
		memcpy(spec.mac, msg->u.fastpath.mac, ETH_ALEN);
		spec.destport_be = msg->u.fastpath.port;
		spec.destip_be = msg->u.fastpath.ip;
		spec.proto = msg->u.fastpath.proto;

		netback_accel_filter_remove_spec(bend, &spec);
	}

	return 0;
}


/* Flow control for message queues */
inline void set_queue_not_full(struct netback_accel *bend)
{
	if (!test_and_set_bit(NET_ACCEL_MSG_AFLAGS_QUEUEUNOTFULL_B, 
			      (unsigned long *)&bend->shared_page->aflags))
		notify_remote_via_irq(bend->msg_channel_irq);
	else
		VPRINTK("queue not full bit already set, not signalling\n");
}


/* Flow control for message queues */
inline void set_queue_full(struct netback_accel *bend)
{
	if (!test_and_set_bit(NET_ACCEL_MSG_AFLAGS_QUEUE0FULL_B,
			      (unsigned long *)&bend->shared_page->aflags))
		notify_remote_via_irq(bend->msg_channel_irq);
	else
		VPRINTK("queue full bit already set, not signalling\n");
}


void netback_accel_set_interface_state(struct netback_accel *bend, int up)
{
	bend->shared_page->net_dev_up = up;
	if (!test_and_set_bit(NET_ACCEL_MSG_AFLAGS_NETUPDOWN_B, 
			     (unsigned long *)&bend->shared_page->aflags))
		notify_remote_via_irq(bend->msg_channel_irq);
	else
		VPRINTK("interface up/down bit already set, not signalling\n");
}


static int check_rx_hello_version(unsigned version) 
{
	/* Should only happen if there's been a version mismatch */
	BUG_ON(version == NET_ACCEL_MSG_VERSION);

	if (version > NET_ACCEL_MSG_VERSION) {
		/* Newer protocol, we must refuse */
		return -EPROTO;
	}

	if (version < NET_ACCEL_MSG_VERSION) {
		/*
		 * We are newer, so have discretion to accept if we
		 * wish.  For now however, just reject
		 */
		return -EPROTO;
	}

	return -EINVAL;
}


static int process_rx_msg(struct netback_accel *bend,
			  struct net_accel_msg *msg)
{
	int err = 0;
		      
	switch (msg->id) {
	case NET_ACCEL_MSG_REPLY | NET_ACCEL_MSG_HELLO:
		/* Reply to a HELLO; mark ourselves as connected */
		DPRINTK("got Hello reply, version %.8x\n",
			msg->u.hello.version);
		
		/*
		 * Check that we've not successfully done this
		 * already.  NB no check at the moment that this reply
		 * comes after we've actually sent a HELLO as that's
		 * not possible with the current code structure
		 */
		if (bend->hw_state != NETBACK_ACCEL_RES_NONE)
			return -EPROTO;

		/* Store max_pages for accel_setup */
		if (msg->u.hello.max_pages > bend->quotas.max_buf_pages) {
			EPRINTK("More pages than quota allows (%d > %d)\n",
				msg->u.hello.max_pages, 
				bend->quotas.max_buf_pages);
			/* Force it down to the quota */
			msg->u.hello.max_pages = bend->quotas.max_buf_pages;
		}
		bend->max_pages = msg->u.hello.max_pages;
		
		/* Set up the hardware visible to the other end */
		err = bend->accel_setup(bend);
		if (err) {
			/* This is fatal */
			DPRINTK("Hello gave accel_setup error %d\n", err);
			netback_accel_set_closing(bend);
		} else {
			/*
			 * Now add the context so that packet
			 * forwarding will commence
			 */
			netback_accel_fwd_set_context(bend->mac, bend, 
						      bend->fwd_priv);
		}
		break;
	case NET_ACCEL_MSG_REPLY | NET_ACCEL_MSG_HELLO | NET_ACCEL_MSG_ERROR:
		EPRINTK("got Hello error, versions us:%.8x them:%.8x\n",
			NET_ACCEL_MSG_VERSION, msg->u.hello.version);

		if (bend->hw_state != NETBACK_ACCEL_RES_NONE)
			return -EPROTO;

		if (msg->u.hello.version != NET_ACCEL_MSG_VERSION) {
			/* Error is due to version mismatch */
			err = check_rx_hello_version(msg->u.hello.version);
			if (err == 0) {
				/*
				 * It's OK to be compatible, send
				 * another hello with compatible version
				 */
				netback_accel_msg_tx_hello
					(bend, msg->u.hello.version);
			} else {
				/*
				 * Tell frontend that we're not going to
				 * send another HELLO by going to Closing.
				 */
				netback_accel_set_closing(bend);
			}
		} 
		break;
	case NET_ACCEL_MSG_MAPBUF:
		VPRINTK("Got mapped buffers request %d\n",
			msg->u.mapbufs.reqid);

		if (bend->hw_state == NETBACK_ACCEL_RES_NONE)
			return -EPROTO;

		/*
		 * Frontend wants a buffer table entry for the
		 * supplied pages
		 */
		err = netback_accel_msg_rx_buffer_map(bend, msg);
		if (net_accel_msg_reply_notify(bend->shared_page,
					       bend->msg_channel_irq, 
					       &bend->to_domU, msg)) {
			/*
			 * This is fatal as we can't tell the frontend
			 * about the problem through the message
			 * queue, and so would otherwise stalemate
			 */
			netback_accel_set_closing(bend);
		}
		break;
	case NET_ACCEL_MSG_FASTPATH:
		DPRINTK("Got fastpath request\n");

		if (bend->hw_state == NETBACK_ACCEL_RES_NONE)
			return -EPROTO;

		err = netback_accel_process_fastpath(bend, msg);
		break;
	default:
		EPRINTK("Huh? Message code is %x\n", msg->id);
		err = -EPROTO;
		break;
	}
	return err;
}


/*  Demultiplex an IRQ from the frontend driver.  */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
void netback_accel_msg_rx_handler(struct work_struct *arg)
#else
void netback_accel_msg_rx_handler(void *bend_void)
#endif
{
	struct net_accel_msg msg;
	int err, queue_was_full = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	struct netback_accel *bend = 
		container_of(arg, struct netback_accel, handle_msg);
#else
	struct netback_accel *bend = (struct netback_accel *)bend_void;
#endif

	mutex_lock(&bend->bend_mutex);

	/*
	 * This happens when the shared pages have been unmapped, but
	 * the workqueue not flushed yet
	 */
	if (bend->shared_page == NULL)
		goto done;

	if ((bend->shared_page->aflags &
	     NET_ACCEL_MSG_AFLAGS_TO_DOM0_MASK) != 0) {
		if (bend->shared_page->aflags &
		    NET_ACCEL_MSG_AFLAGS_QUEUE0NOTFULL) {
			/* We've been told there may now be space. */
			clear_bit(NET_ACCEL_MSG_AFLAGS_QUEUE0NOTFULL_B, 
				  (unsigned long *)&bend->shared_page->aflags);
		}

		if (bend->shared_page->aflags &
		    NET_ACCEL_MSG_AFLAGS_QUEUEUFULL) {
			clear_bit(NET_ACCEL_MSG_AFLAGS_QUEUEUFULL_B, 
				  (unsigned long *)&bend->shared_page->aflags);
			queue_was_full = 1;
		}
	}

	while ((err = net_accel_msg_recv(bend->shared_page, &bend->from_domU,
					 &msg)) == 0) {
		err = process_rx_msg(bend, &msg);
		
		if (err != 0) {
			EPRINTK("%s: Error %d\n", __FUNCTION__, err);
			goto err;
		}
	}

 err:
	/* There will be space now if we can make any. */
	if (queue_was_full) 
		set_queue_not_full(bend);
 done:
	mutex_unlock(&bend->bend_mutex);

	return;
}
