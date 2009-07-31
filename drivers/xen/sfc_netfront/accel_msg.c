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

#include <linux/stddef.h>
#include <linux/errno.h>

#include <xen/xenbus.h>

#include "accel.h"
#include "accel_msg_iface.h"
#include "accel_util.h"
#include "accel_bufs.h"

#include "netfront.h" /* drivers/xen/netfront/netfront.h */

static void vnic_start_interrupts(netfront_accel_vnic *vnic)
{
	unsigned long flags;
	
	/* Prime our interrupt */
	spin_lock_irqsave(&vnic->irq_enabled_lock, flags);
	if (!netfront_accel_vi_enable_interrupts(vnic)) {
		struct netfront_info *np = netdev_priv(vnic->net_dev);

		/* Cripes, that was quick, better pass it up */
		netfront_accel_disable_net_interrupts(vnic);
		vnic->irq_enabled = 0;
		NETFRONT_ACCEL_STATS_OP(vnic->stats.poll_schedule_count++);
		napi_schedule(&np->napi);
	} else {
		/*
		 * Nothing yet, make sure we get interrupts through
		 * back end 
		 */
		vnic->irq_enabled = 1;
		netfront_accel_enable_net_interrupts(vnic);
	}
	spin_unlock_irqrestore(&vnic->irq_enabled_lock, flags);
}


static void vnic_stop_interrupts(netfront_accel_vnic *vnic)
{
	unsigned long flags;

	spin_lock_irqsave(&vnic->irq_enabled_lock, flags);
	netfront_accel_disable_net_interrupts(vnic);
	vnic->irq_enabled = 0;
	spin_unlock_irqrestore(&vnic->irq_enabled_lock, flags);
}


static void vnic_start_fastpath(netfront_accel_vnic *vnic)
{
	struct net_device *net_dev = vnic->net_dev;
	struct netfront_info *np = netdev_priv(net_dev);
	unsigned long flags;

	DPRINTK("%s\n", __FUNCTION__);

	spin_lock_irqsave(&vnic->tx_lock, flags);
	vnic->tx_enabled = 1;
	spin_unlock_irqrestore(&vnic->tx_lock, flags);
	
	napi_disable(&np->napi);
	vnic->poll_enabled = 1;
	napi_enable(&np->napi);
	
	vnic_start_interrupts(vnic);
}


void vnic_stop_fastpath(netfront_accel_vnic *vnic)
{
	struct net_device *net_dev = vnic->net_dev;
	struct netfront_info *np = (struct netfront_info *)netdev_priv(net_dev);
	unsigned long flags1, flags2;

	DPRINTK("%s\n", __FUNCTION__);

	vnic_stop_interrupts(vnic);
	
	spin_lock_irqsave(&vnic->tx_lock, flags1);
	vnic->tx_enabled = 0;
	spin_lock_irqsave(&np->tx_lock, flags2);
	if (vnic->tx_skb != NULL) {
		dev_kfree_skb_any(vnic->tx_skb);
		vnic->tx_skb = NULL;
		if (netfront_check_queue_ready(net_dev)) {
			netif_wake_queue(net_dev);
			NETFRONT_ACCEL_STATS_OP
				(vnic->stats.queue_wakes++);
		}
	}
	spin_unlock_irqrestore(&np->tx_lock, flags2);
	spin_unlock_irqrestore(&vnic->tx_lock, flags1);
	
	/* Must prevent polls and hold lock to modify poll_enabled */
	napi_disable(&np->napi);
	spin_lock_irqsave(&vnic->irq_enabled_lock, flags1);
	vnic->poll_enabled = 0;
	spin_unlock_irqrestore(&vnic->irq_enabled_lock, flags1);
	napi_enable(&np->napi);
}


static void netfront_accel_interface_up(netfront_accel_vnic *vnic)
{
	if (!vnic->backend_netdev_up) {
		vnic->backend_netdev_up = 1;
		
		if (vnic->frontend_ready)
			vnic_start_fastpath(vnic);
	}
}


static void netfront_accel_interface_down(netfront_accel_vnic *vnic)
{
	if (vnic->backend_netdev_up) {
		vnic->backend_netdev_up = 0;
		
		if (vnic->frontend_ready)
			vnic_stop_fastpath(vnic);
	}
}


static int vnic_add_bufs(netfront_accel_vnic *vnic, 
			 struct net_accel_msg *msg)
{
	int rc, offset;
	struct netfront_accel_bufinfo *bufinfo;
  
	BUG_ON(msg->u.mapbufs.pages > NET_ACCEL_MSG_MAX_PAGE_REQ);

	offset = msg->u.mapbufs.reqid;

	if (offset < vnic->bufpages.max_pages - 
	    (vnic->bufpages.max_pages / sfc_netfront_buffer_split)) {
		bufinfo = vnic->rx_bufs;
	} else
		bufinfo = vnic->tx_bufs;

	/* Queue up some Rx buffers to start things off. */
	if ((rc = netfront_accel_add_bufs(&vnic->bufpages, bufinfo, msg)) == 0) {
		netfront_accel_vi_add_bufs(vnic, bufinfo == vnic->rx_bufs);

		if (offset + msg->u.mapbufs.pages == vnic->bufpages.max_pages) {
			VPRINTK("%s: got all buffers back\n", __FUNCTION__);
			vnic->frontend_ready = 1;
			if (vnic->backend_netdev_up)
				vnic_start_fastpath(vnic);
		} else {
			VPRINTK("%s: got buffers back %d %d\n", __FUNCTION__, 
				offset, msg->u.mapbufs.pages);
		}
	}

	return rc;
}


/* The largest [o] such that (1u << o) <= n.  Requires n > 0. */

inline unsigned log2_le(unsigned long n) {
	unsigned order = 1;
	while ((1ul << order) <= n) ++order;
	return (order - 1);
}

static int vnic_send_buffer_requests(netfront_accel_vnic *vnic,
				     struct netfront_accel_bufpages *bufpages)
{
	int pages, offset, rc = 0, sent = 0;
	struct net_accel_msg msg;

	while (bufpages->page_reqs < bufpages->max_pages) {
		offset = bufpages->page_reqs;

		pages = pow2(log2_le(bufpages->max_pages - 
				     bufpages->page_reqs));
		pages = pages < NET_ACCEL_MSG_MAX_PAGE_REQ ? 
			pages : NET_ACCEL_MSG_MAX_PAGE_REQ;

		BUG_ON(offset < 0);
		BUG_ON(pages <= 0);

		rc = netfront_accel_buf_map_request(vnic->dev, bufpages,
						    &msg, pages, offset);
		if (rc == 0) {
			rc = net_accel_msg_send(vnic->shared_page, 
						&vnic->to_dom0, &msg);
			if (rc < 0) {
				VPRINTK("%s: queue full, stopping for now\n",
					__FUNCTION__);
				break;
			}
			sent++;
		} else {
			EPRINTK("%s: problem with grant, stopping for now\n",
				__FUNCTION__);
			break;
		}

		bufpages->page_reqs += pages;
	}

	if (sent)
		net_accel_msg_notify(vnic->msg_channel_irq);

	return rc;
}


/*
 * In response to dom0 saying "my queue is full", we reply with this
 * when it is no longer full
 */
inline void vnic_set_queue_not_full(netfront_accel_vnic *vnic)
{

	if (test_and_set_bit(NET_ACCEL_MSG_AFLAGS_QUEUE0NOTFULL_B,
			    (unsigned long *)&vnic->shared_page->aflags))
		notify_remote_via_irq(vnic->msg_channel_irq);
	else
		VPRINTK("queue not full bit already set, not signalling\n");
}

/* 
 * Notify dom0 that the queue we want to use is full, it should
 * respond by setting MSG_AFLAGS_QUEUEUNOTFULL in due course
 */
inline void vnic_set_queue_full(netfront_accel_vnic *vnic)
{

	if (!test_and_set_bit(NET_ACCEL_MSG_AFLAGS_QUEUEUFULL_B,
			     (unsigned long *)&vnic->shared_page->aflags))
		notify_remote_via_irq(vnic->msg_channel_irq);
	else
		VPRINTK("queue full bit already set, not signalling\n");
}


static int vnic_check_hello_version(unsigned version) 
{
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

	BUG_ON(version != NET_ACCEL_MSG_VERSION);
	return 0;
}


static int vnic_process_hello_msg(netfront_accel_vnic *vnic,
				  struct net_accel_msg *msg)
{
	int err = 0;
	unsigned pages = sfc_netfront_max_pages;

	if (vnic_check_hello_version(msg->u.hello.version) < 0) {
		msg->id = NET_ACCEL_MSG_HELLO | NET_ACCEL_MSG_REPLY 
			| NET_ACCEL_MSG_ERROR;
		msg->u.hello.version = NET_ACCEL_MSG_VERSION;
	} else {
		vnic->backend_netdev_up
			= vnic->shared_page->net_dev_up;
		
		msg->id = NET_ACCEL_MSG_HELLO | NET_ACCEL_MSG_REPLY;
		msg->u.hello.version = NET_ACCEL_MSG_VERSION;
		if (msg->u.hello.max_pages &&
		    msg->u.hello.max_pages < pages)
			pages = msg->u.hello.max_pages;
		msg->u.hello.max_pages = pages;
		
		/* Half of pages for rx, half for tx */ 
		err = netfront_accel_alloc_buffer_mem(&vnic->bufpages,
						      vnic->rx_bufs, 
						      vnic->tx_bufs,
						      pages);
		if (err)
			msg->id |= NET_ACCEL_MSG_ERROR;		
	}
	
	/* Send reply */
	net_accel_msg_reply_notify(vnic->shared_page, vnic->msg_channel_irq,
				   &vnic->to_dom0, msg);
	return err;
}


static int vnic_process_localmac_msg(netfront_accel_vnic *vnic,
				     struct net_accel_msg *msg)
{
	unsigned long flags;
	cuckoo_hash_mac_key key;

	if (msg->u.localmac.flags & NET_ACCEL_MSG_ADD) {
		DECLARE_MAC_BUF(buf);

		DPRINTK("MAC has moved, could be local: %s\n",
			print_mac(buf, msg->u.localmac.mac));
		key = cuckoo_mac_to_key(msg->u.localmac.mac);
		spin_lock_irqsave(&vnic->table_lock, flags);
		/* Try to remove it, not a big deal if not there */
		cuckoo_hash_remove(&vnic->fastpath_table, 
				   (cuckoo_hash_key *)&key);
		spin_unlock_irqrestore(&vnic->table_lock, flags);
	}
	
	return 0;
}


static 
int vnic_process_rx_msg(netfront_accel_vnic *vnic,
			struct net_accel_msg *msg)
{
	int err;

	switch (msg->id) {
	case NET_ACCEL_MSG_HELLO:
		/* Hello, reply with Reply */
		DPRINTK("got Hello, with version %.8x\n",
			msg->u.hello.version);
		BUG_ON(vnic->msg_state != NETFRONT_ACCEL_MSG_NONE);
		err = vnic_process_hello_msg(vnic, msg);
		if (err == 0)
			vnic->msg_state = NETFRONT_ACCEL_MSG_HELLO;
		break;
	case NET_ACCEL_MSG_SETHW:
		/* Hardware info message */
		DPRINTK("got H/W info\n");
		BUG_ON(vnic->msg_state != NETFRONT_ACCEL_MSG_HELLO);
		err = netfront_accel_vi_init(vnic, &msg->u.hw);
		if (err == 0)
			vnic->msg_state = NETFRONT_ACCEL_MSG_HW;
		break;
	case NET_ACCEL_MSG_MAPBUF | NET_ACCEL_MSG_REPLY:
		VPRINTK("Got mapped buffers back\n");
		BUG_ON(vnic->msg_state != NETFRONT_ACCEL_MSG_HW);
		err = vnic_add_bufs(vnic, msg);
		break;
	case NET_ACCEL_MSG_MAPBUF | NET_ACCEL_MSG_REPLY | NET_ACCEL_MSG_ERROR:
		/* No buffers.  Can't use the fast path. */
		EPRINTK("Got mapped buffers error.  Cannot accelerate.\n");
		BUG_ON(vnic->msg_state != NETFRONT_ACCEL_MSG_HW);
		err = -EIO;
		break;
	case NET_ACCEL_MSG_LOCALMAC:
		/* Should be add, remove not currently used */
		EPRINTK_ON(!(msg->u.localmac.flags & NET_ACCEL_MSG_ADD));
		BUG_ON(vnic->msg_state != NETFRONT_ACCEL_MSG_HW);
		err = vnic_process_localmac_msg(vnic, msg);
		break;
	default:
		EPRINTK("Huh? Message code is 0x%x\n", msg->id);
		err = -EPROTO;
		break;
	}

	return err;
}


/* Process an IRQ received from back end driver */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
void netfront_accel_msg_from_bend(struct work_struct *context)
#else
void netfront_accel_msg_from_bend(void *context)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	netfront_accel_vnic *vnic = 
		container_of(context, netfront_accel_vnic, msg_from_bend);
#else
	netfront_accel_vnic *vnic = (netfront_accel_vnic *)context;
#endif
	struct net_accel_msg msg;
	int err, queue_was_full = 0;
	
	mutex_lock(&vnic->vnic_mutex);

	/*
	 * This happens when the shared pages have been unmapped but
	 * the workqueue has yet to be flushed 
	 */
	if (!vnic->dom0_state_is_setup) 
		goto unlock_out;

	while ((vnic->shared_page->aflags & NET_ACCEL_MSG_AFLAGS_TO_DOMU_MASK)
	       != 0) {
		if (vnic->shared_page->aflags &
		    NET_ACCEL_MSG_AFLAGS_QUEUEUNOTFULL) {
			/* We've been told there may now be space. */
			clear_bit(NET_ACCEL_MSG_AFLAGS_QUEUEUNOTFULL_B,
				  (unsigned long *)&vnic->shared_page->aflags);
		}

		if (vnic->shared_page->aflags &
		    NET_ACCEL_MSG_AFLAGS_QUEUE0FULL) {
			/*
			 * There will be space at the end of this
			 * function if we can make any.
			 */
			clear_bit(NET_ACCEL_MSG_AFLAGS_QUEUE0FULL_B,
				  (unsigned long *)&vnic->shared_page->aflags);
			queue_was_full = 1;
		}

		if (vnic->shared_page->aflags &
		    NET_ACCEL_MSG_AFLAGS_NETUPDOWN) {
			DPRINTK("%s: net interface change\n", __FUNCTION__);
			clear_bit(NET_ACCEL_MSG_AFLAGS_NETUPDOWN_B,
				  (unsigned long *)&vnic->shared_page->aflags);
			if (vnic->shared_page->net_dev_up)
				netfront_accel_interface_up(vnic);
			else
				netfront_accel_interface_down(vnic);
		}
	}

	/* Pull msg out of shared memory */
	while ((err = net_accel_msg_recv(vnic->shared_page, &vnic->from_dom0,
					 &msg)) == 0) {
		err = vnic_process_rx_msg(vnic, &msg);
		
		if (err != 0)
			goto done;
	}

	/*
	 * Send any pending buffer map request messages that we can,
	 * and mark domU->dom0 as full if necessary.  
	 */
	if (vnic->msg_state == NETFRONT_ACCEL_MSG_HW &&
	    vnic->bufpages.page_reqs < vnic->bufpages.max_pages) {
		if (vnic_send_buffer_requests(vnic, &vnic->bufpages) == -ENOSPC)
			vnic_set_queue_full(vnic);
	}

	/* 
	 * If there are no messages then this is not an error.  It
	 * just means that we've finished processing the queue.
	 */
	if (err == -ENOENT)
		err = 0;
 done:
	/* We will now have made space in the dom0->domU queue if we can */
	if (queue_was_full)
		vnic_set_queue_not_full(vnic);

	if (err != 0) {
		EPRINTK("%s returned %d\n", __FUNCTION__, err);
		netfront_accel_set_closing(vnic);
	}

 unlock_out:
	mutex_unlock(&vnic->vnic_mutex);

	return;
}


irqreturn_t netfront_accel_msg_channel_irq_from_bend(int irq, void *context)
{
	netfront_accel_vnic *vnic = (netfront_accel_vnic *)context;
	VPRINTK("irq %d from device %s\n", irq, vnic->dev->nodename);

	queue_work(netfront_accel_workqueue, &vnic->msg_from_bend);

	return IRQ_HANDLED;
}

/* Process an interrupt received from the NIC via backend */
irqreturn_t netfront_accel_net_channel_irq_from_bend(int irq, void *context)
{
	netfront_accel_vnic *vnic = (netfront_accel_vnic *)context;
	struct net_device *net_dev = vnic->net_dev;
	unsigned long flags;

	VPRINTK("net irq %d from device %s\n", irq, vnic->dev->nodename);
	
	NETFRONT_ACCEL_STATS_OP(vnic->stats.irq_count++);

	BUG_ON(net_dev==NULL);

	spin_lock_irqsave(&vnic->irq_enabled_lock, flags);
	if (vnic->irq_enabled) {
		struct netfront_info *np = netdev_priv(net_dev);

		netfront_accel_disable_net_interrupts(vnic);
		vnic->irq_enabled = 0;
		spin_unlock_irqrestore(&vnic->irq_enabled_lock, flags);

#if NETFRONT_ACCEL_STATS
		vnic->stats.poll_schedule_count++;
		if (vnic->stats.event_count_since_irq >
		    vnic->stats.events_per_irq_max)
			vnic->stats.events_per_irq_max = 
				vnic->stats.event_count_since_irq;
		vnic->stats.event_count_since_irq = 0;
#endif
		napi_schedule(&np->napi);
	}
	else {
		spin_unlock_irqrestore(&vnic->irq_enabled_lock, flags);
		NETFRONT_ACCEL_STATS_OP(vnic->stats.useless_irq_count++);
		DPRINTK("%s: irq when disabled\n", __FUNCTION__);
	}
	
	return IRQ_HANDLED;
}


void netfront_accel_msg_tx_fastpath(netfront_accel_vnic *vnic, const void *mac,
				    u32 ip, u16 port, u8 protocol)
{
	unsigned long lock_state;
	struct net_accel_msg *msg;

	msg = net_accel_msg_start_send(vnic->shared_page, &vnic->to_dom0,
				       &lock_state);

	if (msg == NULL)
		return;

	net_accel_msg_init(msg, NET_ACCEL_MSG_FASTPATH);
	msg->u.fastpath.flags = NET_ACCEL_MSG_REMOVE;
	memcpy(msg->u.fastpath.mac, mac, ETH_ALEN);

	msg->u.fastpath.port = port;
	msg->u.fastpath.ip = ip;
	msg->u.fastpath.proto = protocol;

	net_accel_msg_complete_send_notify(vnic->shared_page, &vnic->to_dom0, 
					   &lock_state, vnic->msg_channel_irq);
}
