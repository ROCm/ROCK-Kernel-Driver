/*
 * ASIX AX8817x USB 2.0 10/100/HomePNA Ethernet controller driver
 *
 * $Id: ax8817x.c,v 1.11 2003/06/15 19:00:02 dhollis Exp $
 *
 * Copyright (c) 2002-2003 TiVo Inc.
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * History 
 *
 *	2003-06-15 - Dave Hollis <dhollis@davehollis.com>  2.0.0
 *		* Remove crc32 inline function, use core kernel instead
 *		* Set sane defaults for rx_buffers
 *		* Fix ethtool GETDRVINFO bits - use strlcpy and
 *		  usb_make_path
 *
 *	2003-06-05 - Dave Hollis <dhollis@davehollis.com>  0.10.0
 *		* Port to 2.5 series kernels
 *		* Remove #if 0 blocks that are confirmed
 *		  unnecessary
 *		* Re-did tx routines based off pegasus driver.
 *		  This resolved hard crashes and greatly simplified
 *		  things.
 *		* Redo mii/ethtool routines
 *
 *	2003-05-31 - Dave Hollis <dhollis@davehollis.com>  0.9.8
 *		* Don't stop/start the queue in start_xmit
 *		* Swallow URB status upon hard removal
 *		* Cleanup remaining comments (kill // style)
 *
 *	2003-05-29 - Dave Hollis <dhollis@davehollis.com>  0.9.7
 *		* Set module owner
 *		* Follow-up on suggestions from David Brownell &
 *		  Oliver Neukum which should help with robustness
 *		* Use ether_crc from stock kernel if available
 *
 *	2003-05-28 - Dave Hollis <dhollis@davehollis.com>  0.9.6
 *		* Added basic ethtool & mii support
 *
 *	2003-05-28 - Dave Hollis <dhollis@davehollis.com>  0.9.5
 *		* Workout devrequest change to usb_ctrlrequest structure
 *		* Replace FILL_BULK_URB macros to non-deprecated 
 *		  usb_fill_bulk_urb macros
 *		* Replace printks with equivalent macros
 *		* Use defines for module description, version, author to
 *		  simplify future changes
 *
 * Known Issues
 *
 * Todo
 *	Fix mii/ethtool output
*/

#include <linux/slab.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <asm/uaccess.h>
#include <linux/version.h>

/* Version Information */
#define DRIVER_VERSION "v2.0.0"
#define DRIVER_AUTHOR "TiVo, Inc."
#define DRIVER_DESC "ASIX AX8817x USB Ethernet driver"
#define DRIVER_NAME "ax8817x"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");

#define AX_REQ_READ         ( USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE )
#define AX_REQ_WRITE        ( USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE )

#define AX_CMD_SET_SW_MII           0x06
#define AX_CMD_READ_MII_REG         0x07
#define AX_CMD_WRITE_MII_REG        0x08
#define AX_CMD_SET_HW_MII           0x0a
#define AX_CMD_WRITE_RX_CTL         0x10
#define AX_CMD_WRITE_MULTI_FILTER   0x16
#define AX_CMD_READ_NODE_ID         0x17
#define AX_CMD_READ_PHY_ID          0x19
#define AX_CMD_WRITE_MEDIUM_MODE    0x1b
#define AX_CMD_WRITE_GPIOS          0x1f

#define AX_RX_MAX                   ETH_FRAME_LEN
#define AX_TIMEOUT_CMD              ( HZ / 10 )
#define AX_TIMEOUT_TX               ( HZ * 2 )
#define AX_MAX_MCAST                64

#define AX_DRV_STATE_INITIALIZING   0x00
#define AX_DRV_STATE_RUNNING        0x01
#define AX_DRV_STATE_EXITING        0x02

#define AX_PHY_STATE_INITIALIZING   0x00
#define AX_PHY_STATE_NO_LINK        0x01
#define AX_PHY_STATE_POLLING_1      0x02
#define AX_PHY_STATE_POLLING_2      0x03
#define AX_PHY_STATE_POLLING_3      0x04
#define AX_PHY_STATE_POLLING_4      0x05
#define AX_PHY_STATE_SETTING_MAC    0x06
#define AX_PHY_STATE_LINK           0x07
#define AX_PHY_STATE_ABORT_POLL     0x08
#define AX_PHY_STATE_ABORTING       0x09

#define AX_MAX_PHY_RETRY            50

#define AX_RX_URBS_DEFAULT	    2

static int n_rx_urbs = AX_RX_URBS_DEFAULT;

MODULE_PARM(n_rx_urbs, "i");
MODULE_PARM_DESC(n_rx_urbs,
		 "Number of rx buffers to queue at once (def 2)");

struct ax8817x_info;
struct ax_cmd_req;
typedef int (*ax_cmd_callback_t) (struct ax8817x_info *,
				  struct ax_cmd_req *);

struct ax_cmd_req {
	struct list_head list;
	ax_cmd_callback_t cmd_callback;
	void *priv;
	int status;
	void *data;
	int data_size;
	int timeout;
	struct usb_ctrlrequest devreq;
};

struct ax8817x_info {
	struct usb_device *usb;
	struct net_device *net;
	struct net_device_stats stats;
	struct mii_if_info mii;
	struct urb **rx_urbs;
	struct urb *int_urb;
	struct urb *tx_urb;
	u8 *int_buf;
	struct urb *ctl_urb;
	struct list_head ctl_queue;
	spinlock_t ctl_lock;
	atomic_t rx_refill_cnt;
	struct ax_cmd_req phy_req;
	u8 phy_id;
	u8 phy_state;
	u8 drv_state;
};


const struct usb_device_id ax8817x_id_table[] __devinitdata = {
	/* Linksys USB200M */
      {USB_DEVICE(0x077b, 0x2226), driver_info:0x00130103},
	/* Hawking UF200, TRENDnet TU2-ET100 */
      {USB_DEVICE(0x07b8, 0x420a), driver_info:0x001f1d1f},
	/* NETGEAR FA120 */
      {USB_DEVICE(0x0846, 0x1040), driver_info:0x00130103},
	/* D-Link DUB-E100 */
      {USB_DEVICE(0x2001, 0x1a00), driver_info:0x009f9d9f},

	{}
};

MODULE_DEVICE_TABLE(usb, ax8817x_id_table);


static void ax_run_ctl_queue(struct ax8817x_info *, struct ax_cmd_req *,
			     int);
static void ax_rx_callback(struct urb *, struct pt_regs *);

static void ax_ctl_callback(struct urb *urb, struct pt_regs *regs)
{
	struct ax8817x_info *ax_info =
	    (struct ax8817x_info *) urb->context;

	ax_run_ctl_queue(ax_info, NULL,
			 urb->status ? urb->status : urb->actual_length);
}

/* 
 * Queue a new ctl request, or dequeue the first in the list
*/
static void ax_run_ctl_queue(struct ax8817x_info *ax_info,
			     struct ax_cmd_req *req, int status)
{
	struct ax_cmd_req *next_req = NULL;
	struct ax_cmd_req *last_req = NULL;
	unsigned long flags;

	/* Need to lock around queue list manipulation */
	spin_lock_irqsave(&ax_info->ctl_lock, flags);

	if (req == NULL) {
		last_req =
		    list_entry(ax_info->ctl_queue.next, struct ax_cmd_req,
			       list);
	} else {
		if (list_empty(&ax_info->ctl_queue)) {
			next_req = req;
		}

		req->status = -EINPROGRESS;
		list_add_tail(&req->list, &ax_info->ctl_queue);
	}

	while (1) {
		if (last_req != NULL) {
			/* dequeue completed entry */
			list_del(&last_req->list);

			last_req->status = status;
			if (last_req->cmd_callback(ax_info, last_req)) {
				/* requeue if told to do so */
				last_req->status = -EINPROGRESS;
				list_add_tail(&last_req->list,
					      &ax_info->ctl_queue);
			}

			if (list_empty(&ax_info->ctl_queue)) {
				next_req = NULL;
			} else {
				next_req =
				    list_entry(ax_info->ctl_queue.next,
					       struct ax_cmd_req, list);
			}
		}

		spin_unlock_irqrestore(&ax_info->ctl_lock, flags);

		if (next_req == NULL) {
			break;
		}

		/* XXX: do something with timeout */
		usb_fill_control_urb(ax_info->ctl_urb, ax_info->usb,
				     next_req->devreq.
				     bRequestType & USB_DIR_IN ?
				     usb_rcvctrlpipe(ax_info->usb,
						     0) :
				     usb_sndctrlpipe(ax_info->usb, 0),
				     (void *) &next_req->devreq,
				     next_req->data, next_req->data_size,
				     ax_ctl_callback, ax_info);

		status = usb_submit_urb(ax_info->ctl_urb, GFP_ATOMIC);
		if (status >= 0) {
			break;
		}

		last_req = next_req;

		spin_lock_irqsave(&ax_info->ctl_lock, flags);
	}
}

static int ax_sync_cmd_callback(struct ax8817x_info *unused,
				struct ax_cmd_req *req)
{
	wait_queue_head_t *wq = (wait_queue_head_t *) req->priv;

	wake_up(wq);

	return 0;
}

static int ax_async_cmd_callback(struct ax8817x_info *unused,
				 struct ax_cmd_req *req)
{
	if (req->status < 0) {
		err("%s: Async command %d failed: %d\n", __FUNCTION__,
		    req->devreq.bRequest, req->status);
	}

	/* Nothing else to do here, just need to free the request (and its
	   allocated data) */
	if (req->data != NULL) {
		kfree(req->data);
	}
	kfree(req);

	return 0;
}

/*
 * This is mostly the same as usb_control_msg(), except that it is able
 * to queue control messages
*/
static int ax_control_msg(struct ax8817x_info *ax_info, u8 requesttype,
			  u8 request, u16 value, u16 index, void *data,
			  u16 size, int timeout)
{
	struct ax_cmd_req *req;
	DECLARE_WAIT_QUEUE_HEAD(wq);
	DECLARE_WAITQUEUE(wait, current);
	int ret;

	req = kmalloc(sizeof(struct ax_cmd_req), GFP_KERNEL);
	if (req == NULL) {
		return -ENOMEM;
	}

	req->devreq.bRequestType = requesttype;
	req->devreq.bRequest = request;
	req->devreq.wValue = cpu_to_le16(value);
	req->devreq.wIndex = cpu_to_le16(index);
	req->devreq.wLength = cpu_to_le16(size);
	req->data = data;
	req->data_size = size;
	req->timeout = timeout;

	req->priv = &wq;
	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&wq, &wait);

	req->cmd_callback = ax_sync_cmd_callback;

	ax_run_ctl_queue(ax_info, req, 0);
	schedule();

	ret = req->status;

	kfree(req);

	return ret;
}

/*
 * Same, but can be used asynchronously, may fail, and returns no exit
 * status
*/
static void ax_control_msg_async(struct ax8817x_info *ax_info,
				 u8 requesttype, u8 request, u16 value,
				 u16 index, void *data, u16 size,
				 int timeout)
{
	struct ax_cmd_req *req;

	req = kmalloc(sizeof(struct ax_cmd_req), GFP_ATOMIC);
	if (req == NULL) {
		/* There's not much else we can do here... */
		err("%s: Failed alloc\n", __FUNCTION__);
		return;
	}

	req->devreq.bRequestType = requesttype;
	req->devreq.bRequest = request;
	req->devreq.wValue = cpu_to_le16(value);
	req->devreq.wIndex = cpu_to_le16(index);
	req->devreq.wLength = cpu_to_le16(size);
	req->data = data;
	req->data_size = size;
	req->timeout = timeout;

	req->cmd_callback = ax_async_cmd_callback;

	ax_run_ctl_queue(ax_info, req, 0);
}

static inline int ax_read_cmd(struct ax8817x_info *ax_info, u8 cmd,
			      u16 value, u16 index, u16 size, void *data)
{
	return ax_control_msg(ax_info, AX_REQ_READ, cmd, value, index,
			      data, size, AX_TIMEOUT_CMD);
}

static inline int ax_write_cmd(struct ax8817x_info *ax_info, u8 cmd,
			       u16 value, u16 index, u16 size, void *data)
{
	return ax_control_msg(ax_info, AX_REQ_WRITE, cmd, value, index,
			      data, size, AX_TIMEOUT_CMD);
}

static inline void ax_write_cmd_async(struct ax8817x_info *ax_info, u8 cmd,
				      u16 value, u16 index, u16 size,
				      void *data)
{
	ax_control_msg_async(ax_info, AX_REQ_WRITE, cmd, value, index,
			     data, size, AX_TIMEOUT_CMD);
}

static int ax_refill_rx_urb(struct ax8817x_info *ax_info, struct urb *urb)
{
	struct sk_buff *skb;
	int ret;

	skb = dev_alloc_skb(AX_RX_MAX + 2);
	if (skb != NULL) {
		skb_reserve(skb, 2);	/* for IP header alignment */
		skb->dev = ax_info->net;

		usb_fill_bulk_urb(urb, ax_info->usb,
				  usb_rcvbulkpipe(ax_info->usb, 3),
				  skb->data, AX_RX_MAX, ax_rx_callback,
				  skb);

		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret < 0) {
			err("Failed submit rx URB (%d)\n", ret);
			dev_kfree_skb_irq(skb);
			urb->context = NULL;
		} else {
			ret = 0;
		}
	} else {
		/* this just means we're low on memory at the moment. Try to
		   handle it gracefully. */
		urb->context = NULL;
		ret = 1;
	}

	return ret;
}

static int ax_phy_cmd_callback(struct ax8817x_info *ax_info,
			       struct ax_cmd_req *req)
{
	int full_duplex;
	int flow_control;
	u16 mii_data_le;

	if (req->status < 0) {
		err("%s: Failed at state %d: %d\n", __FUNCTION__,
		    ax_info->phy_state, req->status);
		/* Not sure what else we can do, so just bail */
		ax_info->phy_state = AX_PHY_STATE_ABORTING;
	}

	switch (ax_info->phy_state) {
		/* Now that we're in software MII mode, read the BMSR */
	case AX_PHY_STATE_POLLING_1:
		ax_info->phy_state = AX_PHY_STATE_POLLING_2;
		req->devreq.bRequestType = AX_REQ_READ;
		req->devreq.bRequest = AX_CMD_READ_MII_REG;
		req->devreq.wValue = cpu_to_le16(ax_info->phy_id);
		req->devreq.wIndex = cpu_to_le16(MII_BMSR);
		req->devreq.wLength = cpu_to_le16(2);
		req->data_size = 2;
		req->priv = 0;	/* This is the retry count */
		return 1;

		/* Done reading BMSR */
	case AX_PHY_STATE_POLLING_2:
		mii_data_le = *(u16 *) req->data;
		if ((mii_data_le &
		     cpu_to_le16(BMSR_LSTATUS | BMSR_ANEGCAPABLE))
		    == cpu_to_le16(BMSR_LSTATUS | BMSR_ANEGCAPABLE)) {
			if (mii_data_le & cpu_to_le16(BMSR_ANEGCOMPLETE)) {
				/* Autonegotiation done, go on to read LPA */
				ax_info->phy_state =
				    AX_PHY_STATE_POLLING_3;
				req->devreq.wIndex = cpu_to_le16(MII_LPA);
				return 1;
			} else if ((long) req->priv++ < AX_MAX_PHY_RETRY) {
				/* Reread BMSR if it's still autonegotiating. This is
				   probably unnecessary logic, I've never seen it take
				   more than 1 try... */
				return 1;
			}
			/* else fall through to abort */
		}
		/* XXX: should probably handle auto-neg failure better,
		   by reverting to manual setting of something safe. (?) */

		ax_info->phy_state = AX_PHY_STATE_ABORT_POLL;
		/* and then fall through to set hw MII */

		/* Got what we needed from PHY, set back to hardware MII mode
		   (Do same for abort in mid-poll) */
	case AX_PHY_STATE_POLLING_3:
	case AX_PHY_STATE_ABORT_POLL:
		ax_info->phy_state += 1;
		req->devreq.bRequestType = AX_REQ_WRITE;
		req->devreq.bRequest = AX_CMD_SET_HW_MII;
		req->devreq.wValue = cpu_to_le16(0);
		req->devreq.wIndex = cpu_to_le16(0);
		req->devreq.wLength = cpu_to_le16(0);
		req->data_size = 0;
		return 1;

		/* The end result, set the right duplex and flow control mode in the
		   MAC (based on the PHY's LPA reg, which should still be in the data
		   buffer) */
	case AX_PHY_STATE_POLLING_4:
		mii_data_le = *(u16 *) req->data;
		ax_info->phy_state = AX_PHY_STATE_SETTING_MAC;
		req->devreq.bRequest = AX_CMD_WRITE_MEDIUM_MODE;
		full_duplex = mii_data_le & cpu_to_le16(LPA_DUPLEX);
		flow_control = full_duplex &&
		    (mii_data_le & cpu_to_le16(0x0400));
		req->devreq.wValue = cpu_to_le16(0x04) |
		    (full_duplex ? cpu_to_le16(0x02) : 0) |
		    (flow_control ? cpu_to_le16(0x10) : 0);
		info("%s: Link established, %s duplex, flow control %sabled\n", ax_info->net->name, full_duplex ? "full" : "half", flow_control ? "en" : "dis");
		return 1;

		/* All done */
	case AX_PHY_STATE_SETTING_MAC:
		ax_info->phy_state = AX_PHY_STATE_LINK;
		netif_carrier_on(ax_info->net);
		return 0;

	default:
		err("%s: Unknown state %d\n", __FUNCTION__,
		    ax_info->phy_state);
		/* fall through */
	case AX_PHY_STATE_ABORTING:
		ax_info->phy_state = AX_PHY_STATE_NO_LINK;
		return 0;
	}
}

static void ax_int_callback(struct urb *urb, struct pt_regs *regs)
{
	struct ax8817x_info *ax_info =
	    (struct ax8817x_info *) urb->context;
	u8 phy_link;

	if (ax_info->drv_state == AX_DRV_STATE_EXITING ||
	    urb->actual_length < 3) {
		return;
	}

	/* Ignore the first PHY link report, it will sometimes be reported as
	   link active, even though we just told the PHY to reset. If it
	   really has link, we'll pick it up next int callback.
	 */
	if (ax_info->phy_state == AX_PHY_STATE_INITIALIZING) {
		netif_carrier_off(ax_info->net);
		ax_info->phy_state = AX_PHY_STATE_NO_LINK;
		return;
	}

	/* Assume we're only interested in the primary PHY for now. */
	phy_link = ax_info->int_buf[2] & 1;

	if (phy_link ==
	    (ax_info->phy_state == AX_PHY_STATE_NO_LINK) ? 0 : 1) {
		/* Common case, no change */
		return;
	}

	if (phy_link == 0) {
		netif_carrier_off(ax_info->net);
		/* Abort an in-progress poll of the PHY if necessary */
		switch (ax_info->phy_state) {
		case AX_PHY_STATE_POLLING_1:
		case AX_PHY_STATE_POLLING_2:
		case AX_PHY_STATE_POLLING_3:
			ax_info->phy_state = AX_PHY_STATE_ABORT_POLL;
			break;

		case AX_PHY_STATE_POLLING_4:
		case AX_PHY_STATE_SETTING_MAC:
			ax_info->phy_state = AX_PHY_STATE_ABORTING;
			break;

		case AX_PHY_STATE_LINK:
			ax_info->phy_state = AX_PHY_STATE_NO_LINK;
			break;

		default:
			/* If we're already aborting, continue aborting */
			break;
		}
	} else {
		/* Note that we only fall into this case if previous phy_state was
		   AX_PHY_STATE_NO_LINK. When the link is reported active while
		   we're still polling, or when we're aborting, the logic above
		   will just return, and we'll check again next int callback. */

		ax_info->phy_state = AX_PHY_STATE_POLLING_1;
		ax_info->phy_req.devreq.bRequestType = AX_REQ_WRITE;
		ax_info->phy_req.devreq.bRequest = AX_CMD_SET_SW_MII;
		ax_info->phy_req.devreq.wValue = cpu_to_le16(0);
		ax_info->phy_req.devreq.wIndex = cpu_to_le16(0);
		ax_info->phy_req.devreq.wLength = cpu_to_le16(0);
		ax_info->phy_req.data_size = 0;
		ax_info->phy_req.timeout = AX_TIMEOUT_CMD;
		ax_info->phy_req.cmd_callback = ax_phy_cmd_callback;

		ax_run_ctl_queue(ax_info, &ax_info->phy_req, 0);
	}
}

static void ax_rx_callback(struct urb *urb, struct pt_regs *regs)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct net_device *net = skb->dev;
	struct ax8817x_info *ax_info = (struct ax8817x_info *) net->priv;
	int ret, len, refill;

	switch (urb->status) {
	case 0:
		break;

	default:
		err("%s: URB status %d\n", __FUNCTION__, urb->status);
		/* It's not clear that we can do much in this case, the rx pipe
		   doesn't ever seem to stall, so if we got -ETIMEDOUT, that
		   usually means the device was unplugged, and we just haven't
		   noticed yet.
		   Just fall through and free skb without resubmitting urb. */
	case -ENOENT:		/* */
	case -ECONNRESET:	/* Async unlink */
	case -ESHUTDOWN:	/* Hardware gone */
	case -EILSEQ:		/* Get this when you yank it out on UHCI */
	case -ETIMEDOUT:	/* OHCI */
	case -EPROTO:		/* EHCI */
	case -EPIPE:
		dev_kfree_skb_any(skb);
		urb->context = NULL;
		return;
	}

	if (ax_info->drv_state == AX_DRV_STATE_INITIALIZING) {
		/* Not really expecting this to ever happen, since we haven't yet
		   enabled receive in the rx_ctl register, but ya never know... */
		goto refill_same;
	} else if (ax_info->drv_state == AX_DRV_STATE_EXITING) {
		dev_kfree_skb_any(skb);
		urb->context = NULL;
		return;
	}

	len = urb->actual_length;
	if (len == 0) {
		/* this shouldn't happen... */
		goto refill_same;
	}

	refill = ax_refill_rx_urb(ax_info, urb);

	if (refill == 0
	    || atomic_read(&ax_info->rx_refill_cnt) < n_rx_urbs) {
		/* Send the receive buffer up the network stack */
		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, net);
		net->last_rx = jiffies;
		ax_info->stats.rx_packets++;
		ax_info->stats.rx_bytes += len;

		netif_rx(skb);

		if (refill == 0) {
			int i;

			/* This is the common case. This URB got refilled OK, and
			   no other URBs need to be refilled. */
			if (atomic_read(&ax_info->rx_refill_cnt) == 0) {
				return;
			}

			for (i = 0; i < n_rx_urbs; i++) {
				struct urb *urb = ax_info->rx_urbs[i];

				if (urb->context == NULL) {
					if (ax_refill_rx_urb(ax_info, urb)
					    == 0) {
						atomic_dec(&ax_info->
							   rx_refill_cnt);
					} else {
						break;
					}
				}
			}
		} else {
			/* remember to refill this one later */
			atomic_inc(&ax_info->rx_refill_cnt);
		}

		return;
	} else {
		ax_info->stats.rx_dropped++;
		if (refill < 0) {
			/* the error code was already printk'ed in ax_refill_rx_urb()
			   so just note the consequences here: */
			warn("Halting rx due to error\n");
			return;
		}

		/* fall through to resubmit this URB with the existing skb
		   will try to reallocate skb's on next rx callback */
	}

refill_same:
	usb_fill_bulk_urb(urb, ax_info->usb,
			  usb_rcvbulkpipe(ax_info->usb, 3), skb->data,
			  AX_RX_MAX, ax_rx_callback, skb);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret < 0) {
		err("Failed submit rx URB (%d)\n", ret);
	}
}

static int ax8817x_open(struct net_device *net)
{
	struct ax8817x_info *ax_info = (struct ax8817x_info *) net->priv;
	u8 buf[4];
	int i, ret;

	ret = ax_write_cmd(ax_info, AX_CMD_WRITE_RX_CTL, 0x80, 0, 0, buf);
	if (ret < 0) {
		return ret;
	}

	ret = 0;

	ax_info->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ax_info->tx_urb == NULL) {
		err("Error allocating tx_urb!");
		ret = -ENOMEM;
	}

	atomic_set(&ax_info->rx_refill_cnt, 0);

	for (i = 0; i < n_rx_urbs && ret == 0; i++) {
		struct urb *urb = ax_info->rx_urbs[i];

		if (urb == NULL) {
			urb = ax_info->rx_urbs[i] =
			    usb_alloc_urb(0, GFP_KERNEL);
			if (urb == NULL) {
				ret = -ENOMEM;
				break;
			}
			if (n_rx_urbs > 1) {
				urb->transfer_flags |= URB_NO_INTERRUPT;	/* FIXME: Was USB_QUEUE_BULK */
			}
		}
		ret = ax_refill_rx_urb(ax_info, urb);
		if (ret == 1) {
			atomic_inc(&ax_info->rx_refill_cnt);
			ret = 0;
		}
	}

	/* XXX: should handle the case where we couldn't allocate any skb's
	   better. They get allocated with GFP_ATOMIC, so they may all fail... */
	if (ret == 0 && atomic_read(&ax_info->rx_refill_cnt) < n_rx_urbs) {
		netif_start_queue(net);
	} else {
		/* Error: clean up anything we allocated and bail. */
		usb_free_urb(ax_info->tx_urb);

		for (i = 0; i < n_rx_urbs; i++) {
			struct urb *urb = ax_info->rx_urbs[i];

			if (urb != NULL) {
				/* skb gets freed in the URB callback */
				usb_unlink_urb(urb);
				usb_free_urb(urb);
			}
		}

		err("%s: Failed start rx queue (%d)\n", __FUNCTION__, ret);
	}
	return ret;
}

static int ax8817x_stop(struct net_device *net)
{
	struct ax8817x_info *ax_info = (struct ax8817x_info *) net->priv;
	u8 buf[4];
	int i, ret;

	netif_stop_queue(net);

	ret = ax_write_cmd(ax_info, AX_CMD_WRITE_RX_CTL, 0x80, 0, 0, buf);
	if (ret < 0 && ax_info->drv_state != AX_DRV_STATE_EXITING) {
		err("%s: Failed cmd (%d)\n", __FUNCTION__, ret);
	}
	if (ax_info->tx_urb != NULL) {
		usb_unlink_urb(ax_info->tx_urb);
		usb_free_urb(ax_info->tx_urb);
		ax_info->tx_urb = NULL;
	}

	for (i = 0; i < n_rx_urbs; i++) {
		struct urb *urb = ax_info->rx_urbs[i];
		if (urb != NULL) {
			/* skb gets freed in the URB callback */
			usb_unlink_urb(urb);
			usb_free_urb(urb);
			ax_info->rx_urbs[i] = NULL;
		}
	}

	return 0;
}

static void write_bulk_callback(struct urb *urb, struct pt_regs *regs)
{
	struct ax8817x_info *ax_info = urb->context;

	if (!ax_info || (ax_info->drv_state == AX_DRV_STATE_EXITING))
		return;

	if (!netif_device_present(ax_info->net))
		return;

	if (urb->status)
		info("%s: TX status %d", ax_info->net->name, urb->status);

	ax_info->net->trans_start = jiffies;
	netif_wake_queue(ax_info->net);
}

static int ax8817x_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct ax8817x_info *ax_info = net->priv;
	int res;

	netif_stop_queue(net);

	ax_info->tx_urb->transfer_flags |= URB_ZERO_PACKET;
	usb_fill_bulk_urb(ax_info->tx_urb, ax_info->usb,
			  usb_sndbulkpipe(ax_info->usb, 2),
			  skb->data, skb->len, write_bulk_callback,
			  ax_info);
	if ((res = usb_submit_urb(ax_info->tx_urb, GFP_ATOMIC))) {
		warn("Failed tx_urb %d", res);
		ax_info->stats.tx_errors++;
		netif_start_queue(net);
	} else {
		ax_info->stats.tx_packets++;
		ax_info->stats.tx_bytes += skb->len;
		net->trans_start = jiffies;
	}
	dev_kfree_skb(skb);

	return 0;
}

static void ax8817x_tx_timeout(struct net_device *net)
{
	struct ax8817x_info *ax_info = net->priv;

	if (!ax_info)
		return;

	warn("%s: Tx timed out.", net->name);
	ax_info->tx_urb->transfer_flags |= URB_ASYNC_UNLINK;
	usb_unlink_urb(ax_info->tx_urb);
	ax_info->stats.tx_errors++;
}

static struct net_device_stats *ax8817x_stats(struct net_device *net)
{
	struct ax8817x_info *ax_info = (struct ax8817x_info *) net->priv;

	return &ax_info->stats;
}

static void ax8817x_set_multicast(struct net_device *net)
{
	struct ax8817x_info *ax_info = (struct ax8817x_info *) net->priv;
	u8 rx_ctl = 0x8c;

	if (net->flags & IFF_PROMISC) {
		rx_ctl |= 0x01;
	} else if (net->flags & IFF_ALLMULTI
		   || net->mc_count > AX_MAX_MCAST) {
		rx_ctl |= 0x02;
	} else if (net->mc_count == 0) {
		/* just broadcast and directed */
	} else {
		struct dev_mc_list *mc_list = net->mc_list;
		u8 *multi_filter;
		u32 crc_bits;
		int i;

		multi_filter = kmalloc(8, GFP_ATOMIC);
		if (multi_filter == NULL) {
			/* Oops, couldn't allocate a DMA buffer for setting the multicast
			   filter. Try all multi mode, although the ax_write_cmd_async
			   will almost certainly fail, too... (but it will printk). */
			rx_ctl |= 0x02;
		} else {
			memset(multi_filter, 0, 8);

			/* Build the multicast hash filter. */
			for (i = 0; i < net->mc_count; i++) {
				crc_bits =
				    ether_crc(ETH_ALEN,
					      mc_list->dmi_addr) >> 26;
				multi_filter[crc_bits >> 3] |=
				    1 << (crc_bits & 7);
				mc_list = mc_list->next;
			}

			ax_write_cmd_async(ax_info,
					   AX_CMD_WRITE_MULTI_FILTER, 0, 0,
					   8, multi_filter);

			rx_ctl |= 0x10;
		}
	}

	ax_write_cmd_async(ax_info, AX_CMD_WRITE_RX_CTL, rx_ctl, 0, 0,
			   NULL);
}

static int read_mii_word(struct ax8817x_info *ax_info, __u8 phy, __u8 indx,
			 __u16 * regd)
{
	int ret;

	ax_write_cmd(ax_info, AX_CMD_SET_SW_MII, 0, 0, 0, NULL);
	ret =
	    ax_read_cmd(ax_info, AX_CMD_READ_MII_REG, phy, indx, 2, regd);
	ax_write_cmd(ax_info, AX_CMD_SET_HW_MII, 0, 0, 0, NULL);

	return 0;
}

static int write_mii_word(struct ax8817x_info *ax_info, __u8 phy,
			  __u8 indx, __u16 regd)
{
	warn("write_mii_word - not implemented!");
	return 0;
}

static int mdio_read(struct net_device *dev, int phy_id, int loc)
{
	struct ax8817x_info *ax_info = dev->priv;
	int res;

	read_mii_word(ax_info, phy_id, loc, (u16 *) & res);
	return res & 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int loc,
		       int val)
{
	struct ax8817x_info *ax_info = dev->priv;

	write_mii_word(ax_info, phy_id, loc, val);
}

static int ax8817x_ethtool_ioctl(struct net_device *net, void __user *uaddr)
{
	struct ax8817x_info *ax_info;
	int cmd;

	ax_info = net->priv;
	if (get_user(cmd, (int *) uaddr))
		return -EFAULT;

	switch (cmd) {
	case ETHTOOL_GDRVINFO:{
			struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };

			strlcpy(info.driver, DRIVER_NAME,
				ETHTOOL_BUSINFO_LEN);
			strlcpy(info.version, DRIVER_VERSION,
				ETHTOOL_BUSINFO_LEN);
			usb_make_path(ax_info->usb, info.bus_info,sizeof info.bus_info);
			if (copy_to_user(uaddr, &info, sizeof(info)))
				return -EFAULT;
			return 0;
		}
	case ETHTOOL_GSET:{
			struct ethtool_cmd ecmd;

			mii_ethtool_gset(&ax_info->mii, &ecmd);
			if (copy_to_user(uaddr, &ecmd, sizeof(ecmd)))
				return -EFAULT;
			return 0;
		}
	case ETHTOOL_SSET:{
			int r;
			struct ethtool_cmd ecmd;

			if (copy_from_user(&ecmd, uaddr, sizeof(ecmd)))
				return -EFAULT;
			r = mii_ethtool_sset(&ax_info->mii, &ecmd);
			return r;
		}
	case ETHTOOL_NWAY_RST:{
			return mii_nway_restart(&ax_info->mii);
		}
	case ETHTOOL_GLINK:{
			struct ethtool_value edata = { ETHTOOL_GLINK };

			edata.data =
			    ax_info->phy_state == AX_PHY_STATE_LINK;
			if (copy_to_user(uaddr, &edata, sizeof(edata)))
				return -EFAULT;
			return 0;
		}
	case ETHTOOL_GMSGLVL:{
			struct ethtool_value edata = { ETHTOOL_GMSGLVL };
			/* edata.data = ax_info->msg_enable; FIXME */
			if (copy_to_user(uaddr, &edata, sizeof(edata)))
				return -EFAULT;
			return 0;
		}
	case ETHTOOL_SMSGLVL:{
			struct ethtool_value edata;

			if (copy_from_user(&edata, uaddr, sizeof(edata)))
				return -EFAULT;
			/* sp->msg_enable = edata.data;  FIXME */
			return 0;
		}
	}
	return -EOPNOTSUPP;
}

static int ax8817x_mii_ioctl(struct net_device *net, struct ifreq *ifr,
			     int cmd)
{
	struct ax8817x_info *ax_info;
	struct mii_ioctl_data *data_ptr =
	    (struct mii_ioctl_data *) &(ifr->ifr_data);

	ax_info = net->priv;

	switch (cmd) {
	case SIOCGMIIPHY:
		data_ptr->phy_id = ax_info->phy_id;
		break;
	case SIOCGMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		ax_read_cmd(ax_info, AX_CMD_READ_MII_REG, 0,
			    data_ptr->reg_num & 0x1f, 2,
			    &(data_ptr->val_out));
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ax8817x_ioctl(struct net_device *net, struct ifreq *ifr,
			 int cmd)
{
	struct ax8817x_info *ax_info;
	int res;

	ax_info = net->priv;
	res = 0;

	switch (cmd) {
	case SIOCETHTOOL:
		res = ax8817x_ethtool_ioctl(net, (void __user *)ifr->ifr_data);
		break;
	case SIOCGMIIPHY:	/* Get address of PHY in use */
	case SIOCGMIIREG:	/* Read from MII PHY register */
	case SIOCSMIIREG:	/* Write to MII PHY register */
		return ax8817x_mii_ioctl(net, ifr, cmd);
	default:
		res = -EOPNOTSUPP;
	}

	return res;
}

static int ax8817x_net_init(struct net_device *net)
{
	struct ax8817x_info *ax_info = (struct ax8817x_info *) net->priv;
	u8 buf[6];
	u16 *buf16 = (u16 *) buf;
	int ret;

	ret = ax_write_cmd(ax_info, AX_CMD_WRITE_RX_CTL, 0x80, 0, 0, buf);
	if (ret < 0) {
		return ret;
	}

	memset(buf, 0, 6);

	/* Get the MAC address */
	ret = ax_read_cmd(ax_info, AX_CMD_READ_NODE_ID, 0, 0, 6, buf);
	if (ret < 0) {
		return ret;
	}

	memcpy(net->dev_addr, buf, 6);

	/* Get the PHY id */
	ret = ax_read_cmd(ax_info, AX_CMD_READ_PHY_ID, 0, 0, 2, buf);
	if (ret < 0) {
		return ret;
	} else if (ret < 2) {
		/* this should always return 2 bytes */
		return -EIO;
	}

	/* Reset the PHY, and drop it into auto-negotiation mode */
	ax_info->phy_id = buf[1];
	ax_info->phy_state = AX_PHY_STATE_INITIALIZING;

	ret = ax_write_cmd(ax_info, AX_CMD_SET_SW_MII, 0, 0, 0, &buf);
	if (ret < 0) {
		return ret;
	}

	*buf16 = cpu_to_le16(BMCR_RESET);
	ret = ax_write_cmd(ax_info, AX_CMD_WRITE_MII_REG,
			   ax_info->phy_id, MII_BMCR, 2, buf16);
	if (ret < 0) {
		return ret;
	}

	/* Advertise that we can do full-duplex pause */
	*buf16 = cpu_to_le16(ADVERTISE_ALL | ADVERTISE_CSMA | 0x0400);
	ret = ax_write_cmd(ax_info, AX_CMD_WRITE_MII_REG,
			   ax_info->phy_id, MII_ADVERTISE, 2, buf16);
	if (ret < 0) {
		return ret;
	}

	*buf16 = cpu_to_le16(BMCR_ANENABLE | BMCR_ANRESTART);
	ret = ax_write_cmd(ax_info, AX_CMD_WRITE_MII_REG,
			   ax_info->phy_id, MII_BMCR, 2, buf16);
	if (ret < 0) {
		return ret;
	}

	ret = ax_write_cmd(ax_info, AX_CMD_SET_HW_MII, 0, 0, 0, &buf);
	if (ret < 0) {
		return ret;
	}

	net->open = ax8817x_open;
	net->stop = ax8817x_stop;
	net->hard_start_xmit = ax8817x_start_xmit;
	net->tx_timeout = ax8817x_tx_timeout;
	net->watchdog_timeo = AX_TIMEOUT_TX;
	net->get_stats = ax8817x_stats;
	net->do_ioctl = ax8817x_ioctl;
	net->set_multicast_list = ax8817x_set_multicast;

	return 0;
}

static int ax8817x_bind(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *usb = interface_to_usbdev(intf);
	struct ax8817x_info *ax_info;
	struct net_device *net;
	int i, ret;
	unsigned long gpio_bits = id->driver_info;
	u8 buf[2];

	/* Allocate the URB lists along with the device info struct */
	ax_info = kmalloc(sizeof(struct ax8817x_info) +
			  n_rx_urbs * sizeof(struct urb *), GFP_KERNEL);
	if (ax_info == NULL) {
		err("%s: Failed ax alloc\n", __FUNCTION__);
		goto exit_err;
	}

	memset(ax_info, 0, sizeof(struct ax8817x_info) +
	       n_rx_urbs * sizeof(struct urb *));

	ax_info->drv_state = AX_DRV_STATE_INITIALIZING;
	ax_info->rx_urbs = (struct urb **) (ax_info + 1);
	ax_info->usb = usb;

	/* Set up the control URB queue */

	INIT_LIST_HEAD(&ax_info->ctl_queue);
	spin_lock_init(&ax_info->ctl_lock);
	ax_info->ctl_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ax_info->ctl_urb == NULL) {
		goto exit_err_free_ax;
	}

	/* Toggle the GPIOs in a manufacturer/model specific way */

	for (i = 2; i >= 0; i--) {
		ret = ax_write_cmd(ax_info, AX_CMD_WRITE_GPIOS,
				   (gpio_bits >> (i * 8)) & 0xff, 0, 0,
				   buf);
		if (ret < 0) {
			goto exit_err_free_ax;
		}
		wait_ms(5);
	}

	/* Set up the net device */

	net = alloc_etherdev(0);
	if (net == NULL) {
		err("%s: Failed net alloc\n", __FUNCTION__);
		goto exit_err_free_ax;
	}

	ax_info->net = net;

	SET_MODULE_OWNER(net);
	net->init = ax8817x_net_init;
	net->priv = ax_info;

	ret = register_netdev(net);
	if (ret < 0) {
		err("%s: Failed net init (%d)\n", __FUNCTION__, ret);
		goto exit_err_free_net;
	}

	/* Setup mii structure */
	ax_info->mii.dev = net;
	ax_info->mii.mdio_read = mdio_read;
	ax_info->mii.mdio_write = mdio_write;
	ax_info->mii.phy_id_mask = 0x1f;
	ax_info->mii.reg_num_mask = 0x1f;

	/* Set up the interrupt URB, and start PHY state monitoring */

	ax_info->int_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ax_info->int_urb == NULL) {
		goto exit_err_unregister_net;
	}
	ax_info->int_buf = kmalloc(8, GFP_KERNEL);
	if (ax_info->int_buf == NULL) {
		goto exit_err_free_int_urb;
	}
	ax_info->phy_req.data = kmalloc(2, GFP_KERNEL);
	if (ax_info->phy_req.data == NULL) {
		goto exit_err_free_int_buf;
	}

	usb_fill_int_urb(ax_info->int_urb, usb, usb_rcvintpipe(usb, 1),
			 ax_info->int_buf, 8, ax_int_callback, ax_info,
			 100);

	ret = usb_submit_urb(ax_info->int_urb, GFP_ATOMIC);
	if (ret < 0) {
		err("%s: Failed int URB submit (%d)\n", __FUNCTION__, ret);
		goto exit_err_free_phy_buf;
	}

	ax_info->drv_state = AX_DRV_STATE_RUNNING;
	usb_set_intfdata(intf, ax_info);

	return 0;

      exit_err_free_phy_buf:
	kfree(ax_info->phy_req.data);

      exit_err_free_int_buf:
	kfree(ax_info->int_buf);

      exit_err_free_int_urb:
	usb_free_urb(ax_info->int_urb);

      exit_err_unregister_net:
	ax_info->drv_state = AX_DRV_STATE_EXITING;
	unregister_netdev(net);

      exit_err_free_net:
	kfree(net);

      exit_err_free_ax:
	if (ax_info->ctl_urb != NULL) {
		/* no need to unlink, since there should not be any ctl URBs
		   pending at this point */
		usb_free_urb(ax_info->ctl_urb);
	}

	kfree(ax_info);

exit_err:
	err("%s: Failed to initialize\n", __FUNCTION__);
	return -EIO;
}

static void ax8817x_disconnect(struct usb_interface *intf)
{
	struct ax8817x_info *ax_info = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (ax_info) {
		ax_info->drv_state = AX_DRV_STATE_EXITING;

		if (ax_info->int_urb != NULL) {
			usb_unlink_urb(ax_info->int_urb);
			usb_free_urb(ax_info->int_urb);
			kfree(ax_info->int_buf);
		}

		unregister_netdev(ax_info->net);

		/* XXX: hmmm... need to go through and clear out the ctl queue, too... */
		if (ax_info->ctl_urb != NULL) {
			usb_unlink_urb(ax_info->ctl_urb);
			usb_free_urb(ax_info->ctl_urb);
		}

		kfree(ax_info);
	}
}

static struct usb_driver ax8817x_driver = {
	.owner = THIS_MODULE,
	.name = DRIVER_NAME,
	.probe = ax8817x_bind,
	.disconnect = ax8817x_disconnect,
	.id_table = ax8817x_id_table,
};

static int __init ax8817x_init(void)
{
	int ret;

	if (n_rx_urbs < 1)
		n_rx_urbs = AX_RX_URBS_DEFAULT;

	ret = usb_register(&ax8817x_driver);
	if (ret < 0) {
		err("%s: Failed to register\n", __FUNCTION__);
	} else {
		info(DRIVER_DESC " " DRIVER_VERSION);
	}

	return ret;
}

static void __exit ax8817x_exit(void)
{
	usb_deregister(&ax8817x_driver);
}

module_init(ax8817x_init);
module_exit(ax8817x_exit);
