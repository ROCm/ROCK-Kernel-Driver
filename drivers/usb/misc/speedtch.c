/******************************************************************************
 *  speedtouch.c  -  Alcatel SpeedTouch USB xDSL modem driver
 *
 *  Copyright (C) 2001, Alcatel
 *  Copyright (C) 2003, Duncan Sands
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ******************************************************************************/

/*
 *  Written by Johan Verrept, maintained by Duncan Sands (duncan.sands@wanadoo.fr)
 *
 *  1.6:	- No longer opens a connection if the firmware is not loaded
 *  		- Added support for the speedtouch 330
 *  		- Removed the limit on the number of devices
 *  		- Module now autoloads on device plugin
 *  		- Merged relevant parts of sarlib
 *  		- Replaced the kernel thread with a tasklet
 *  		- New packet transmission code
 *  		- Changed proc file contents
 *  		- Fixed all known SMP races
 *  		- Many fixes and cleanups
 *  		- Various fixes by Oliver Neukum (oliver@neukum.name)
 *
 *  1.5A:	- Version for inclusion in 2.5 series kernel
 *		- Modifications by Richard Purdie (rpurdie@rpsys.net)
 *		- made compatible with kernel 2.5.6 onwards by changing
 *		udsl_usb_send_data_context->urb to a pointer and adding code
 *		to alloc and free it
 *		- remove_wait_queue() added to udsl_atm_processqueue_thread()
 *
 *  1.5:	- fixed memory leak when atmsar_decode_aal5 returned NULL.
 *		(reported by stephen.robinson@zen.co.uk)
 *
 *  1.4:	- changed the spin_lock() under interrupt to spin_lock_irqsave()
 *		- unlink all active send urbs of a vcc that is being closed.
 *
 *  1.3.1:	- added the version number
 *
 *  1.3:	- Added multiple send urb support
 *		- fixed memory leak and vcc->tx_inuse starvation bug
 *		  when not enough memory left in vcc.
 *
 *  1.2:	- Fixed race condition in udsl_usb_send_data()
 *  1.1:	- Turned off packet debugging
 *
 */

#include <asm/semaphore.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/crc32.h>
#include <linux/init.h>

/*
#define DEBUG
#define VERBOSE_DEBUG
*/

#include <linux/usb.h>


#ifdef VERBOSE_DEBUG
static int udsl_print_packet (const unsigned char *data, int len);
#define PACKETDEBUG(arg...)	udsl_print_packet (arg)
#define vdbg(arg...)		dbg (arg)
#else
#define PACKETDEBUG(arg...)
#define vdbg(arg...)
#endif

#define DRIVER_AUTHOR	"Johan Verrept, Duncan Sands <duncan.sands@wanadoo.fr>"
#define DRIVER_DESC	"Alcatel SpeedTouch USB driver"
#define DRIVER_VERSION	"1.6"

static const char udsl_driver_name [] = "speedtch";

#define SPEEDTOUCH_VENDORID		0x06b9
#define SPEEDTOUCH_PRODUCTID		0x4061

#define UDSL_NUM_RCV_URBS		1
#define UDSL_NUM_SND_URBS		1
#define UDSL_NUM_SND_BUFS		(2*UDSL_NUM_SND_URBS)
#define UDSL_RCV_BUF_SIZE		64 /* ATM cells */
#define UDSL_SND_BUF_SIZE		64 /* ATM cells */
/* max should be (1500 IP mtu + 2 ppp bytes + 32 * 5 cellheader overhead) for
 * PPPoA and (1500 + 14 + 32*5 cellheader overhead) for PPPoE */
#define UDSL_MAX_AAL5_MRU		2048

#define UDSL_IOCTL_LINE_UP		1
#define UDSL_IOCTL_LINE_DOWN		2

#define UDSL_ENDPOINT_DATA_OUT		0x07
#define UDSL_ENDPOINT_DATA_IN		0x87

#define ATM_CELL_HEADER			(ATM_CELL_SIZE - ATM_CELL_PAYLOAD)

#define hex2int(c) ( (c >= '0') && (c <= '9') ? (c - '0') : ((c & 0xf) + 9) )

static struct usb_device_id udsl_usb_ids [] = {
	{ USB_DEVICE (SPEEDTOUCH_VENDORID, SPEEDTOUCH_PRODUCTID) },
	{ }
};

MODULE_DEVICE_TABLE (usb, udsl_usb_ids);

/* receive */

struct udsl_receiver {
	struct list_head list;
	struct sk_buff *skb;
	struct urb *urb;
	struct udsl_instance_data *instance;
};

struct udsl_vcc_data {
	/* vpi/vci lookup */
	struct list_head list;
	short vpi;
	int vci;
	struct atm_vcc *vcc;

	/* raw cell reassembly */
	struct sk_buff *skb;
	unsigned short max_pdu;
};

/* send */

struct udsl_send_buffer {
	struct list_head list;
	unsigned char *base;
	unsigned char *free_start;
	unsigned int free_cells;
};

struct udsl_sender {
	struct list_head list;
	struct udsl_send_buffer *buffer;
	struct urb *urb;
	struct udsl_instance_data *instance;
};

struct udsl_control {
	struct atm_skb_data atm_data;
	unsigned int num_cells;
	unsigned int num_entire;
	unsigned char cell_header [ATM_CELL_HEADER];
	unsigned int pdu_padding;
	unsigned char aal5_trailer [ATM_AAL5_TRAILER];
};

#define UDSL_SKB(x)		((struct udsl_control *)(x)->cb)

/* main driver data */

struct udsl_instance_data {
	struct semaphore serialize;

	/* USB device part */
	struct usb_device *usb_dev;
	char description [64];
	int firmware_loaded;

	/* ATM device part */
	struct atm_dev *atm_dev;
	struct list_head vcc_list;

	/* receive */
	struct udsl_receiver receivers [UDSL_NUM_RCV_URBS];

	spinlock_t spare_receivers_lock;
	struct list_head spare_receivers;

	spinlock_t completed_receivers_lock;
	struct list_head completed_receivers;

	struct tasklet_struct receive_tasklet;

	/* send */
	struct udsl_sender senders [UDSL_NUM_SND_URBS];
	struct udsl_send_buffer send_buffers [UDSL_NUM_SND_BUFS];

	struct sk_buff_head sndqueue;

	spinlock_t send_lock;
	struct list_head spare_senders;
	struct list_head spare_send_buffers;

	struct tasklet_struct send_tasklet;
	struct sk_buff *current_skb;			/* being emptied */
	struct udsl_send_buffer *current_buffer;	/* being filled */
	struct list_head filled_send_buffers;
};

/* ATM */

static void udsl_atm_dev_close (struct atm_dev *dev);
static int udsl_atm_open (struct atm_vcc *vcc, short vpi, int vci);
static void udsl_atm_close (struct atm_vcc *vcc);
static int udsl_atm_ioctl (struct atm_dev *dev, unsigned int cmd, void *arg);
static int udsl_atm_send (struct atm_vcc *vcc, struct sk_buff *skb);
static int udsl_atm_proc_read (struct atm_dev *atm_dev, loff_t *pos, char *page);

static struct atmdev_ops udsl_atm_devops = {
	.dev_close =	udsl_atm_dev_close,
	.open =		udsl_atm_open,
	.close =	udsl_atm_close,
	.ioctl =	udsl_atm_ioctl,
	.send =		udsl_atm_send,
	.proc_read =	udsl_atm_proc_read,
};

/* USB */

static int udsl_usb_probe (struct usb_interface *intf, const struct usb_device_id *id);
static void udsl_usb_disconnect (struct usb_interface *intf);
static int udsl_usb_ioctl (struct usb_interface *intf, unsigned int code, void *user_data);

static struct usb_driver udsl_usb_driver = {
	.name =		udsl_driver_name,
	.probe =	udsl_usb_probe,
	.disconnect =	udsl_usb_disconnect,
	.ioctl =	udsl_usb_ioctl,
	.id_table =	udsl_usb_ids,
};


/*************
**  decode  **
*************/

static inline struct udsl_vcc_data *udsl_find_vcc (struct udsl_instance_data *instance, short vpi, int vci)
{
	struct udsl_vcc_data *vcc;

	list_for_each_entry (vcc, &instance->vcc_list, list)
		if ((vcc->vpi == vpi) && (vcc->vci == vci))
			return vcc;
	return NULL;
}

static struct sk_buff *udsl_decode_rawcell (struct udsl_instance_data *instance, struct sk_buff *skb, struct udsl_vcc_data **ctx)
{
	if (!instance || !skb || !ctx)
		return NULL;
	if (!skb->data || !skb->tail)
		return NULL;

	while (skb->len) {
		unsigned char *cell = skb->data;
		unsigned char *cell_payload;
		struct udsl_vcc_data *vcc;
		short vpi;
		int vci;

		vpi = ((cell [0] & 0x0f) << 4) | (cell [1] >> 4);
		vci = ((cell [1] & 0x0f) << 12) | (cell [2] << 4) | (cell [3] >> 4);

		vdbg ("udsl_decode_rawcell (0x%p, 0x%p, 0x%p) called", instance, skb, ctx);
		vdbg ("udsl_decode_rawcell skb->data %p, skb->tail %p", skb->data, skb->tail);

		/* here should the header CRC check be... */

		if (!(vcc = udsl_find_vcc (instance, vpi, vci))) {
			dbg ("udsl_decode_rawcell: no vcc found for packet on vpi %d, vci %d", vpi, vci);
			__skb_pull (skb, min (skb->len, (unsigned) 53));
		} else {
			vdbg ("udsl_decode_rawcell found vcc %p for packet on vpi %d, vci %d", vcc, vpi, vci);

			if (skb->len >= 53) {
				cell_payload = cell + 5;

				if (!vcc->skb)
					vcc->skb = dev_alloc_skb (vcc->max_pdu);

				/* if alloc fails, we just drop the cell. it is possible that we can still
				 * receive cells on other vcc's
				 */
				if (vcc->skb) {
					/* if (buffer overrun) discard received cells until now */
					if ((vcc->skb->len) > (vcc->max_pdu - 48))
						skb_trim (vcc->skb, 0);

					/* copy data */
					memcpy (vcc->skb->tail, cell_payload, 48);
					skb_put (vcc->skb, 48);

					/* check for end of buffer */
					if (cell [3] & 0x2) {
						struct sk_buff *tmp;

						/* the aal5 buffer ends here, cut the buffer. */
						/* buffer will always have at least one whole cell, so */
						/* don't need to check return from skb_pull */
						skb_pull (skb, 53);
						*ctx = vcc;
						tmp = vcc->skb;
						vcc->skb = NULL;

						vdbg ("udsl_decode_rawcell returns ATM_AAL5 pdu 0x%p with length %d", tmp, tmp->len);
						return tmp;
					}
				}
				/* flush the cell */
				/* buffer will always contain at least one whole cell, so don't */
				/* need to check return value from skb_pull */
				skb_pull (skb, 53);
			} else {
				/* If data is corrupt and skb doesn't hold a whole cell, flush the lot */
				__skb_pull (skb, skb->len);
				return NULL;
			}
		}
	}

	return NULL;
}

static struct sk_buff *udsl_decode_aal5 (struct udsl_vcc_data *ctx, struct sk_buff *skb)
{
	uint crc = 0xffffffff;
	uint length, pdu_crc, pdu_length;

	vdbg ("udsl_decode_aal5 (0x%p, 0x%p) called", ctx, skb);

	if (skb->len && (skb->len % 48))
		return NULL;

	length = (skb->tail [-6] << 8) + skb->tail [-5];
	pdu_crc =
	    (skb->tail [-4] << 24) + (skb->tail [-3] << 16) + (skb->tail [-2] << 8) + skb->tail [-1];
	pdu_length = ((length + 47 + 8) / 48) * 48;

	vdbg ("udsl_decode_aal5: skb->len = %d, length = %d, pdu_crc = 0x%x, pdu_length = %d", skb->len, length, pdu_crc, pdu_length);

	/* is skb long enough ? */
	if (skb->len < pdu_length) {
		if (ctx->vcc->stats)
			atomic_inc (&ctx->vcc->stats->rx_err);
		return NULL;
	}

	/* is skb too long ? */
	if (skb->len > pdu_length) {
		dbg ("udsl_decode_aal5: Warning: readjusting illegal size %d -> %d", skb->len, pdu_length);
		/* buffer is too long. we can try to recover
		 * if we discard the first part of the skb.
		 * the crc will decide whether this was ok
		 */
		skb_pull (skb, skb->len - pdu_length);
	}

	crc = ~crc32_be (crc, skb->data, pdu_length - 4);

	/* check crc */
	if (pdu_crc != crc) {
		dbg ("udsl_decode_aal5: crc check failed!");
		if (ctx->vcc->stats)
			atomic_inc (&ctx->vcc->stats->rx_err);
		return NULL;
	}

	/* pdu is ok */
	skb_trim (skb, length);

	/* update stats */
	if (ctx->vcc->stats)
		atomic_inc (&ctx->vcc->stats->rx);

	vdbg ("udsl_decode_aal5 returns pdu 0x%p with length %d", skb, skb->len);
	return skb;
}


/*************
**  encode  **
*************/

static const unsigned char zeros [ATM_CELL_PAYLOAD];

static void udsl_groom_skb (struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct udsl_control *ctrl = UDSL_SKB (skb);
	unsigned int zero_padding;
	u32 crc;

	ctrl->atm_data.vcc = vcc;
	ctrl->cell_header [0] = vcc->vpi >> 4;
	ctrl->cell_header [1] = (vcc->vpi << 4) | (vcc->vci >> 12);
	ctrl->cell_header [2] = vcc->vci >> 4;
	ctrl->cell_header [3] = vcc->vci << 4;
	ctrl->cell_header [4] = 0xec;

	ctrl->num_cells = (skb->len + ATM_AAL5_TRAILER + ATM_CELL_PAYLOAD - 1) / ATM_CELL_PAYLOAD;
	ctrl->num_entire = skb->len / ATM_CELL_PAYLOAD;

	zero_padding = ctrl->num_cells * ATM_CELL_PAYLOAD - skb->len - ATM_AAL5_TRAILER;

	if (ctrl->num_entire + 1 < ctrl->num_cells)
		ctrl->pdu_padding = zero_padding - (ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER);
	else
		ctrl->pdu_padding = zero_padding;

	ctrl->aal5_trailer [0] = 0; /* UU = 0 */
	ctrl->aal5_trailer [1] = 0; /* CPI = 0 */
	ctrl->aal5_trailer [2] = skb->len >> 8;
	ctrl->aal5_trailer [3] = skb->len;

	crc = crc32_be (~0, skb->data, skb->len);
	crc = crc32_be (crc, zeros, zero_padding);
	crc = crc32_be (crc, ctrl->aal5_trailer, 4);
	crc = ~crc;

	ctrl->aal5_trailer [4] = crc >> 24;
	ctrl->aal5_trailer [5] = crc >> 16;
	ctrl->aal5_trailer [6] = crc >> 8;
	ctrl->aal5_trailer [7] = crc;
}

static unsigned int udsl_write_cells (unsigned int howmany, struct sk_buff *skb, unsigned char **target_p)
{
	struct udsl_control *ctrl = UDSL_SKB (skb);
	unsigned char *target = *target_p;
	unsigned int nc, ne, i;

	vdbg ("udsl_write_cells: howmany=%u, skb->len=%d, num_cells=%u, num_entire=%u, pdu_padding=%u", howmany, skb->len, ctrl->num_cells, ctrl->num_entire, ctrl->pdu_padding);

	nc = ctrl->num_cells;
	ne = min (howmany, ctrl->num_entire);

	for (i = 0; i < ne; i++) {
		memcpy (target, ctrl->cell_header, ATM_CELL_HEADER);
		target += ATM_CELL_HEADER;
		memcpy (target, skb->data, ATM_CELL_PAYLOAD);
		target += ATM_CELL_PAYLOAD;
		__skb_pull (skb, ATM_CELL_PAYLOAD);
	}

	ctrl->num_entire -= ne;

	if (!(ctrl->num_cells -= ne) || !(howmany -= ne))
		goto out;

	memcpy (target, ctrl->cell_header, ATM_CELL_HEADER);
	target += ATM_CELL_HEADER;
	memcpy (target, skb->data, skb->len);
	target += skb->len;
	__skb_pull (skb, skb->len);
	memset (target, 0, ctrl->pdu_padding);
	target += ctrl->pdu_padding;

	if (--ctrl->num_cells) {
		if (!--howmany) {
			ctrl->pdu_padding = ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER;
			goto out;
		}

		memcpy (target, ctrl->cell_header, ATM_CELL_HEADER);
		target += ATM_CELL_HEADER;
		memset (target, 0, ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER);
		target += ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER;

		if (--ctrl->num_cells)
			BUG();
	}

	memcpy (target, ctrl->aal5_trailer, ATM_AAL5_TRAILER);
	target += ATM_AAL5_TRAILER;
	/* set pti bit in last cell */
	*(target + 3 - ATM_CELL_SIZE) |= 0x2;

out:
	*target_p = target;
	return nc - ctrl->num_cells;
}


/**************
**  receive  **
**************/

static void udsl_complete_receive (struct urb *urb, struct pt_regs *regs)
{
	struct udsl_instance_data *instance;
	struct udsl_receiver *rcv;
	unsigned long flags;

	if (!urb || !(rcv = urb->context) || !(instance = rcv->instance)) {
		dbg ("udsl_complete_receive: bad urb!");
		return;
	}

	vdbg ("udsl_complete_receive entered (urb 0x%p, status %d)", urb, urb->status);

	/* may not be in_interrupt() */
	spin_lock_irqsave (&instance->completed_receivers_lock, flags);
	list_add_tail (&rcv->list, &instance->completed_receivers);
	tasklet_schedule (&instance->receive_tasklet);
	spin_unlock_irqrestore (&instance->completed_receivers_lock, flags);
}

static void udsl_process_receive (unsigned long data)
{
	struct udsl_instance_data *instance = (struct udsl_instance_data *) data;
	struct udsl_receiver *rcv;
	unsigned char *data_start;
	struct sk_buff *skb;
	struct urb *urb;
	struct udsl_vcc_data *atmsar_vcc = NULL;
	struct sk_buff *new = NULL, *tmp = NULL;
	int err;

	vdbg ("udsl_process_receive entered");

	spin_lock_irq (&instance->completed_receivers_lock);
	while (!list_empty (&instance->completed_receivers)) {
		rcv = list_entry (instance->completed_receivers.next, struct udsl_receiver, list);
		list_del (&rcv->list);
		spin_unlock_irq (&instance->completed_receivers_lock);

		urb = rcv->urb;
		vdbg ("udsl_process_receive: got packet %p with length %d and status %d", urb, urb->actual_length, urb->status);

		switch (urb->status) {
		case 0:
			vdbg ("udsl_process_receive: processing urb with rcv %p, urb %p, skb %p", rcv, urb, rcv->skb);

			/* update the skb structure */
			skb = rcv->skb;
			skb_trim (skb, 0);
			skb_put (skb, urb->actual_length);
			data_start = skb->data;

			vdbg ("skb->len = %d", skb->len);
			PACKETDEBUG (skb->data, skb->len);

			while ((new = udsl_decode_rawcell (instance, skb, &atmsar_vcc))) {
				vdbg ("(after cell processing)skb->len = %d", new->len);

				tmp = new;
				new = udsl_decode_aal5 (atmsar_vcc, new);

				/* we can't send NULL skbs upstream, the ATM layer would try to close the vcc... */
				if (new) {
					vdbg ("(after aal5 decap) skb->len = %d", new->len);
					if (new->len && atm_charge (atmsar_vcc->vcc, new->truesize)) {
						PACKETDEBUG (new->data, new->len);
						atmsar_vcc->vcc->push (atmsar_vcc->vcc, new);
					} else {
						dbg
						    ("dropping incoming packet : vcc->sk->rcvbuf = %d, skb->true_size = %d",
						     atmsar_vcc->vcc->sk->rcvbuf, new->truesize);
						dev_kfree_skb (new);
					}
				} else {
					dbg ("udsl_decode_aal5 returned NULL!");
					dev_kfree_skb (tmp);
				}
			}

			/* restore skb */
			skb_push (skb, skb->data - data_start);

			usb_fill_bulk_urb (urb,
					   instance->usb_dev,
					   usb_rcvbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_IN),
					   (unsigned char *) rcv->skb->data,
					   UDSL_RCV_BUF_SIZE * ATM_CELL_SIZE,
					   udsl_complete_receive,
					   rcv);
			if (!(err = usb_submit_urb (urb, GFP_ATOMIC)))
				break;
			dbg ("udsl_process_receive: submission failed (%d)", err);
			/* fall through */
		default: /* error or urb unlinked */
			vdbg ("udsl_process_receive: adding to spare_receivers");
			spin_lock_irq (&instance->spare_receivers_lock);
			list_add (&rcv->list, &instance->spare_receivers);
			spin_unlock_irq (&instance->spare_receivers_lock);
			break;
		} /* switch */

		spin_lock_irq (&instance->completed_receivers_lock);
	} /* while */
	spin_unlock_irq (&instance->completed_receivers_lock);
	vdbg ("udsl_process_receive successful");
}

static void udsl_fire_receivers (struct udsl_instance_data *instance)
{
	struct list_head receivers, *pos, *n;

	INIT_LIST_HEAD (&receivers);

	down (&instance->serialize);

	spin_lock_irq (&instance->spare_receivers_lock);
	list_splice_init (&instance->spare_receivers, &receivers);
	spin_unlock_irq (&instance->spare_receivers_lock);

	list_for_each_safe (pos, n, &receivers) {
		struct udsl_receiver *rcv = list_entry (pos, struct udsl_receiver, list);

		dbg ("udsl_fire_receivers: firing urb %p", rcv->urb);

		usb_fill_bulk_urb (rcv->urb,
				   instance->usb_dev,
				   usb_rcvbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_IN),
				   (unsigned char *) rcv->skb->data,
				   UDSL_RCV_BUF_SIZE * ATM_CELL_SIZE,
				   udsl_complete_receive,
				   rcv);

		if (usb_submit_urb (rcv->urb, GFP_KERNEL) < 0) {
			dbg ("udsl_fire_receivers: submit failed!");
			spin_lock_irq (&instance->spare_receivers_lock);
			list_move (pos, &instance->spare_receivers);
			spin_unlock_irq (&instance->spare_receivers_lock);
		}
	}

	up (&instance->serialize);
}


/***********
**  send  **
***********/

static void udsl_complete_send (struct urb *urb, struct pt_regs *regs)
{
	struct udsl_instance_data *instance;
	struct udsl_sender *snd;
	unsigned long flags;

	if (!urb || !(snd = urb->context) || !(instance = snd->instance)) {
		dbg ("udsl_complete_send: bad urb!");
		return;
	}

	vdbg ("udsl_complete_send: urb 0x%p, status %d, snd 0x%p, buf 0x%p", urb, urb->status, snd, snd->buffer);

	/* may not be in_interrupt() */
	spin_lock_irqsave (&instance->send_lock, flags);
	list_add (&snd->list, &instance->spare_senders);
	list_add (&snd->buffer->list, &instance->spare_send_buffers);
	tasklet_schedule (&instance->send_tasklet);
	spin_unlock_irqrestore (&instance->send_lock, flags);
}

static void udsl_process_send (unsigned long data)
{
	struct udsl_send_buffer *buf;
	int err;
	struct udsl_instance_data *instance = (struct udsl_instance_data *) data;
	unsigned int num_written;
	struct sk_buff *skb;
	struct udsl_sender *snd;

made_progress:
	spin_lock_irq (&instance->send_lock);
	while (!list_empty (&instance->spare_senders)) {
		if (!list_empty (&instance->filled_send_buffers)) {
			buf = list_entry (instance->filled_send_buffers.next, struct udsl_send_buffer, list);
			list_del (&buf->list);
		} else if ((buf = instance->current_buffer)) {
			instance->current_buffer = NULL;
		} else /* all buffers empty */
			break;

		snd = list_entry (instance->spare_senders.next, struct udsl_sender, list);
		list_del (&snd->list);
		spin_unlock_irq (&instance->send_lock);

		snd->buffer = buf;
	        usb_fill_bulk_urb (snd->urb,
				   instance->usb_dev,
				   usb_sndbulkpipe (instance->usb_dev, UDSL_ENDPOINT_DATA_OUT),
				   buf->base,
				   (UDSL_SND_BUF_SIZE - buf->free_cells) * ATM_CELL_SIZE,
				   udsl_complete_send,
				   snd);

		vdbg ("udsl_process_send: submitting urb 0x%p (%d cells), snd 0x%p, buf 0x%p", snd->urb, UDSL_SND_BUF_SIZE - buf->free_cells, snd, buf);

		if ((err = usb_submit_urb(snd->urb, GFP_ATOMIC)) < 0) {
			dbg ("udsl_process_send: urb submission failed (%d)!", err);
			spin_lock_irq (&instance->send_lock);
			list_add (&snd->list, &instance->spare_senders);
			spin_unlock_irq (&instance->send_lock);
			list_add (&buf->list, &instance->filled_send_buffers);
			return;
		}

		spin_lock_irq (&instance->send_lock);
	} /* while */
	spin_unlock_irq (&instance->send_lock);

	if (!instance->current_skb && !(instance->current_skb = skb_dequeue (&instance->sndqueue))) {
		return; /* done - no more skbs */
	}

	skb = instance->current_skb;

	if (!(buf = instance->current_buffer)) {
		spin_lock_irq (&instance->send_lock);
		if (list_empty (&instance->spare_send_buffers)) {
			instance->current_buffer = NULL;
			spin_unlock_irq (&instance->send_lock);
			return; /* done - no more buffers */
		}
		buf = list_entry (instance->spare_send_buffers.next, struct udsl_send_buffer, list);
		list_del (&buf->list);
		spin_unlock_irq (&instance->send_lock);

		buf->free_start = buf->base;
		buf->free_cells = UDSL_SND_BUF_SIZE;

		instance->current_buffer = buf;
	}

	num_written = udsl_write_cells (buf->free_cells, skb, &buf->free_start);

	vdbg ("udsl_process_send: wrote %u cells from skb 0x%p to buffer 0x%p", num_written, skb, buf);

	if (!(buf->free_cells -= num_written)) {
		list_add_tail (&buf->list, &instance->filled_send_buffers);
		instance->current_buffer = NULL;
	}

	vdbg ("udsl_process_send: buffer contains %d cells, %d left", UDSL_SND_BUF_SIZE - buf->free_cells, buf->free_cells);

	if (!UDSL_SKB (skb)->num_cells) {
		struct atm_vcc *vcc = UDSL_SKB (skb)->atm_data.vcc;

		if (vcc->pop)
			vcc->pop (vcc, skb);
		else
			kfree_skb (skb);
		instance->current_skb = NULL;

		if (vcc->stats)
			atomic_inc (&vcc->stats->tx);
	}

	goto made_progress;
}

static void udsl_cancel_send (struct udsl_instance_data *instance, struct atm_vcc *vcc)
{
	struct sk_buff *skb, *n;

	dbg ("udsl_cancel_send entered");
	spin_lock_irq (&instance->sndqueue.lock);
	for (skb = instance->sndqueue.next, n = skb->next; skb != (struct sk_buff *)&instance->sndqueue; skb = n, n = skb->next)
		if (UDSL_SKB (skb)->atm_data.vcc == vcc) {
			dbg ("udsl_cancel_send: popping skb 0x%p", skb);
			__skb_unlink (skb, &instance->sndqueue);
			if (vcc->pop)
				vcc->pop (vcc, skb);
			else
				kfree_skb (skb);
		}
	spin_unlock_irq (&instance->sndqueue.lock);

	tasklet_disable (&instance->send_tasklet);
	if ((skb = instance->current_skb) && (UDSL_SKB (skb)->atm_data.vcc == vcc)) {
		dbg ("udsl_cancel_send: popping current skb (0x%p)", skb);
		instance->current_skb = NULL;
		if (vcc->pop)
			vcc->pop (vcc, skb);
		else
			kfree_skb (skb);
	}
	tasklet_enable (&instance->send_tasklet);
	dbg ("udsl_cancel_send done");
}

static int udsl_atm_send (struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;

	vdbg ("udsl_atm_send called (skb 0x%p, len %u)", skb, skb->len);

	if (!instance || !instance->usb_dev) {
		dbg ("udsl_atm_send: NULL data!");
		return -ENODEV;
	}

	if (!instance->firmware_loaded)
		return -EAGAIN;

	if (vcc->qos.aal != ATM_AAL5) {
		dbg ("udsl_atm_send: unsupported ATM type %d!", vcc->qos.aal);
		return -EINVAL;
	}

	if (skb->len > ATM_MAX_AAL5_PDU) {
		dbg ("udsl_atm_send: packet too long (%d vs %d)!", skb->len, ATM_MAX_AAL5_PDU);
		return -EINVAL;
	}

	PACKETDEBUG (skb->data, skb->len);

	udsl_groom_skb (vcc, skb);
	skb_queue_tail (&instance->sndqueue, skb);
	tasklet_schedule (&instance->send_tasklet);

	return 0;
}


/**********
**  ATM  **
**********/

static void udsl_atm_dev_close (struct atm_dev *dev)
{
	struct udsl_instance_data *instance = dev->dev_data;

	if (!instance) {
		dbg ("udsl_atm_dev_close: NULL instance!");
		return;
	}

	dbg ("udsl_atm_dev_close: queue has %u elements", instance->sndqueue.qlen);

	tasklet_kill (&instance->send_tasklet);
	kfree (instance);
	dev->dev_data = NULL;
}

static int udsl_atm_proc_read (struct atm_dev *atm_dev, loff_t *pos, char *page)
{
	struct udsl_instance_data *instance = atm_dev->dev_data;
	int left = *pos;

	if (!instance) {
		dbg ("udsl_atm_proc_read: NULL instance!");
		return -ENODEV;
	}

	if (!left--)
		return sprintf (page, "%s\n", instance->description);

	if (!left--)
		return sprintf (page, "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
				atm_dev->esi [0], atm_dev->esi [1], atm_dev->esi [2],
				atm_dev->esi [3], atm_dev->esi [4], atm_dev->esi [5]);

	if (!left--)
		return sprintf (page, "AAL5: tx %d ( %d err ), rx %d ( %d err, %d drop )\n",
				atomic_read (&atm_dev->stats.aal5.tx),
				atomic_read (&atm_dev->stats.aal5.tx_err),
				atomic_read (&atm_dev->stats.aal5.rx),
				atomic_read (&atm_dev->stats.aal5.rx_err),
				atomic_read (&atm_dev->stats.aal5.rx_drop));

	if (!left--) {
		switch (atm_dev->signal) {
		case ATM_PHY_SIG_FOUND:
			sprintf (page, "Line up");
			break;
		case ATM_PHY_SIG_LOST:
			sprintf (page, "Line down");
			break;
		default:
			sprintf (page, "Line state unknown");
			break;
		}

		if (instance->usb_dev) {
			if (!instance->firmware_loaded)
				strcat (page, ", no firmware\n");
			else
				strcat (page, ", firmware loaded\n");
		} else
			strcat (page, ", disconnected\n");

		return strlen (page);
	}

	return 0;
}

static int udsl_atm_open (struct atm_vcc *vcc, short vpi, int vci)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;
	struct udsl_vcc_data *new;

	dbg ("udsl_atm_open: vpi %hd, vci %d", vpi, vci);

	if (!instance || !instance->usb_dev) {
		dbg ("udsl_atm_open: NULL data!");
		return -ENODEV;
	}

	if ((vpi == ATM_VPI_ANY) || (vci == ATM_VCI_ANY))
		return -EINVAL;

	/* only support AAL5 */
	if (vcc->qos.aal != ATM_AAL5)
		return -EINVAL;

	if (!instance->firmware_loaded) {
		dbg ("udsl_atm_open: firmware not loaded!");
		return -EAGAIN;
	}

	down (&instance->serialize); /* vs self, udsl_atm_close */

	if (udsl_find_vcc (instance, vpi, vci)) {
		up (&instance->serialize);
		return -EADDRINUSE;
	}

	if (!(new = kmalloc (sizeof (struct udsl_vcc_data), GFP_KERNEL))) {
		up (&instance->serialize);
		return -ENOMEM;
	}

	memset (new, 0, sizeof (struct udsl_vcc_data));
	new->vcc = vcc;
	new->vpi = vpi;
	new->vci = vci;
	new->max_pdu = UDSL_MAX_AAL5_MRU;

	vcc->dev_data = new;
	vcc->vpi = vpi;
	vcc->vci = vci;

	tasklet_disable (&instance->receive_tasklet);
	list_add (&new->list, &instance->vcc_list);
	tasklet_enable (&instance->receive_tasklet);

	set_bit (ATM_VF_ADDR, &vcc->flags);
	set_bit (ATM_VF_PARTIAL, &vcc->flags);
	set_bit (ATM_VF_READY, &vcc->flags);

	up (&instance->serialize);

	udsl_fire_receivers (instance);

	dbg ("udsl_atm_open: allocated vcc data 0x%p (max_pdu: %u)", new, new->max_pdu);

	return 0;
}

static void udsl_atm_close (struct atm_vcc *vcc)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;
	struct udsl_vcc_data *vcc_data = vcc->dev_data;

	dbg ("udsl_atm_close called");

	if (!instance || !vcc_data) {
		dbg ("udsl_atm_close: NULL data!");
		return;
	}

	dbg ("udsl_atm_close: deallocating vcc 0x%p with vpi %d vci %d", vcc_data, vcc_data->vpi, vcc_data->vci);

	udsl_cancel_send (instance, vcc);

	down (&instance->serialize); /* vs self, udsl_atm_open */

	tasklet_disable (&instance->receive_tasklet);
	list_del (&vcc_data->list);
	tasklet_enable (&instance->receive_tasklet);

	if (vcc_data->skb)
		kfree_skb (vcc_data->skb);
	vcc_data->skb = NULL;

	kfree (vcc_data);
	vcc->dev_data = NULL;

	vcc->vpi = ATM_VPI_UNSPEC;
	vcc->vci = ATM_VCI_UNSPEC;
	clear_bit (ATM_VF_READY, &vcc->flags);
	clear_bit (ATM_VF_PARTIAL, &vcc->flags);
	clear_bit (ATM_VF_ADDR, &vcc->flags);

	up (&instance->serialize);

	dbg ("udsl_atm_close successful");
}

static int udsl_atm_ioctl (struct atm_dev *dev, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case ATM_QUERYLOOP:
		return put_user (ATM_LM_NONE, (int *) arg) ? -EFAULT : 0;
	default:
		return -ENOIOCTLCMD;
	}
}


/**********
**  USB  **
**********/

static int udsl_set_alternate (struct udsl_instance_data *instance)
{
	down (&instance->serialize); /* vs self */
	if (!instance->firmware_loaded) {
		int ret;

		if ((ret = usb_set_interface (instance->usb_dev, 1, 1)) < 0) {
			dbg ("udsl_set_alternate: usb_set_interface returned %d!", ret);
			up (&instance->serialize);
			return ret;
		}
		instance->firmware_loaded = 1;
	}
	up (&instance->serialize);
	udsl_fire_receivers (instance);
	return 0;
}

static int udsl_usb_ioctl (struct usb_interface *intf, unsigned int code, void *user_data)
{
	struct udsl_instance_data *instance = usb_get_intfdata (intf);

	dbg ("udsl_usb_ioctl entered");

	if (!instance) {
		dbg ("udsl_usb_ioctl: NULL instance!");
		return -ENODEV;
	}

	switch (code) {
	case UDSL_IOCTL_LINE_UP:
		instance->atm_dev->signal = ATM_PHY_SIG_FOUND;
		return udsl_set_alternate (instance);
	case UDSL_IOCTL_LINE_DOWN:
		instance->atm_dev->signal = ATM_PHY_SIG_LOST;
		return 0;
	default:
		return -ENOTTY;
	}
}

static int udsl_usb_probe (struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int ifnum = intf->altsetting->desc.bInterfaceNumber;
	struct udsl_instance_data *instance;
	unsigned char mac_str [13];
	int i, length;
	char *buf;

	dbg ("udsl_usb_probe: trying device with vendor=0x%x, product=0x%x, ifnum %d",
	     dev->descriptor.idVendor, dev->descriptor.idProduct, ifnum);

	if ((dev->descriptor.bDeviceClass != USB_CLASS_VENDOR_SPEC) ||
	    (dev->descriptor.idVendor != SPEEDTOUCH_VENDORID) ||
	    (dev->descriptor.idProduct != SPEEDTOUCH_PRODUCTID) || (ifnum != 1))
		return -ENODEV;

	dbg ("udsl_usb_probe: device accepted");

	/* instance init */
	if (!(instance = kmalloc (sizeof (struct udsl_instance_data), GFP_KERNEL))) {
		dbg ("udsl_usb_probe: no memory for instance data!");
		return -ENOMEM;
	}

	memset (instance, 0, sizeof (struct udsl_instance_data));

	init_MUTEX (&instance->serialize);

	instance->usb_dev = dev;

	INIT_LIST_HEAD (&instance->vcc_list);

	spin_lock_init (&instance->spare_receivers_lock);
	INIT_LIST_HEAD (&instance->spare_receivers);

	spin_lock_init (&instance->completed_receivers_lock);
	INIT_LIST_HEAD (&instance->completed_receivers);

	tasklet_init (&instance->receive_tasklet, udsl_process_receive, (unsigned long) instance);

	skb_queue_head_init (&instance->sndqueue);

	spin_lock_init (&instance->send_lock);
	INIT_LIST_HEAD (&instance->spare_senders);
	INIT_LIST_HEAD (&instance->spare_send_buffers);

	tasklet_init (&instance->send_tasklet, udsl_process_send, (unsigned long) instance);
	INIT_LIST_HEAD (&instance->filled_send_buffers);

	/* receive init */
	for (i = 0; i < UDSL_NUM_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->receivers [i]);

		if (!(rcv->skb = dev_alloc_skb (UDSL_RCV_BUF_SIZE * ATM_CELL_SIZE))) {
			dbg ("udsl_usb_probe: no memory for skb %d!", i);
			goto fail;
		}

		if (!(rcv->urb = usb_alloc_urb (0, GFP_KERNEL))) {
			dbg ("udsl_usb_probe: no memory for receive urb %d!", i);
			goto fail;
		}

		rcv->instance = instance;

		list_add (&rcv->list, &instance->spare_receivers);

		dbg ("udsl_usb_probe: skb->truesize = %d (asked for %d)", rcv->skb->truesize, UDSL_RCV_BUF_SIZE * ATM_CELL_SIZE);
	}

	/* send init */
	for (i = 0; i < UDSL_NUM_SND_URBS; i++) {
		struct udsl_sender *snd = &(instance->senders [i]);

		if (!(snd->urb = usb_alloc_urb (0, GFP_KERNEL))) {
			dbg ("udsl_usb_probe: no memory for send urb %d!", i);
			goto fail;
		}

		snd->instance = instance;

		list_add (&snd->list, &instance->spare_senders);
	}

	for (i = 0; i < UDSL_NUM_SND_BUFS; i++) {
		struct udsl_send_buffer *buf = &(instance->send_buffers [i]);

		if (!(buf->base = kmalloc (UDSL_SND_BUF_SIZE * ATM_CELL_SIZE, GFP_KERNEL))) {
			dbg ("udsl_usb_probe: no memory for send buffer %d!", i);
			goto fail;
		}

		list_add (&buf->list, &instance->spare_send_buffers);
	}

	/* ATM init */
	if (!(instance->atm_dev = atm_dev_register (udsl_driver_name, &udsl_atm_devops, -1, 0))) {
		dbg ("udsl_usb_probe: failed to register ATM device!");
		goto fail;
	}

	instance->atm_dev->ci_range.vpi_bits = ATM_CI_MAX;
	instance->atm_dev->ci_range.vci_bits = ATM_CI_MAX;
	instance->atm_dev->signal = ATM_PHY_SIG_UNKNOWN;

	/* temp init ATM device, set to 128kbit */
	instance->atm_dev->link_rate = 128 * 1000 / 424;

	/* set MAC address, it is stored in the serial number */
	memset (instance->atm_dev->esi, 0, sizeof (instance->atm_dev->esi));
	if (usb_string (dev, dev->descriptor.iSerialNumber, mac_str, sizeof (mac_str)) == 12)
		for (i = 0; i < 6; i++)
			instance->atm_dev->esi [i] = (hex2int (mac_str [i * 2]) * 16) + (hex2int (mac_str [i * 2 + 1]));

	/* device description */
	buf = instance->description;
	length = sizeof (instance->description);

	if ((i = usb_string (dev, dev->descriptor.iProduct, buf, length)) < 0)
		goto finish;

	buf += i;
	length -= i;

	i = snprintf (buf, length, " (");
	buf += i;
	length -= i;

	if (length <= 0 || (i = usb_make_path (dev, buf, length)) < 0)
		goto finish;

	buf += i;
	length -= i;

	snprintf (buf, length, ")");

finish:
	/* ready for ATM callbacks */
	wmb ();
	instance->atm_dev->dev_data = instance;

	usb_set_intfdata (intf, instance);

	return 0;

fail:
	for (i = 0; i < UDSL_NUM_SND_BUFS; i++)
		kfree (instance->send_buffers [i].base);

	for (i = 0; i < UDSL_NUM_SND_URBS; i++)
		usb_free_urb (instance->senders [i].urb);

	for (i = 0; i < UDSL_NUM_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->receivers [i]);

		usb_free_urb (rcv->urb);

		if (rcv->skb)
			kfree_skb (rcv->skb);
	}

	kfree (instance);

	return -ENOMEM;
}

static void udsl_usb_disconnect (struct usb_interface *intf)
{
	struct udsl_instance_data *instance = usb_get_intfdata (intf);
	struct list_head *pos;
	unsigned int count = 0;
	int result, i;

	dbg ("udsl_usb_disconnect entered");

	usb_set_intfdata (intf, NULL);

	if (!instance) {
		dbg ("udsl_usb_disconnect: NULL instance!");
		return;
	}

	tasklet_disable (&instance->receive_tasklet);

	/* receive finalize */
	down (&instance->serialize); /* vs udsl_fire_receivers */
	/* no need to take the spinlock */
	list_for_each (pos, &instance->spare_receivers)
		if (++count > UDSL_NUM_RCV_URBS)
			panic (__FILE__ ": memory corruption detected at line %d!\n", __LINE__);
	INIT_LIST_HEAD (&instance->spare_receivers);
	up (&instance->serialize);

	dbg ("udsl_usb_disconnect: flushed %u spare receivers", count);

	count = UDSL_NUM_RCV_URBS - count;

	for (i = 0; i < UDSL_NUM_RCV_URBS; i++)
		if ((result = usb_unlink_urb (instance->receivers [i].urb)) < 0)
			dbg ("udsl_usb_disconnect: usb_unlink_urb on receive urb %d returned %d", i, result);

	/* wait for completion handlers to finish */
	do {
		unsigned int completed = 0;

		spin_lock_irq (&instance->completed_receivers_lock);
		list_for_each (pos, &instance->completed_receivers)
			if (++completed > count)
				panic (__FILE__ ": memory corruption detected at line %d!\n", __LINE__);
		spin_unlock_irq (&instance->completed_receivers_lock);

		dbg ("udsl_usb_disconnect: found %u completed receivers", completed);

		if (completed == count)
			break;

		set_current_state (TASK_RUNNING);
		schedule ();
	} while (1);

	/* no need to take the spinlock */
	INIT_LIST_HEAD (&instance->completed_receivers);

	tasklet_enable (&instance->receive_tasklet);
	tasklet_kill (&instance->receive_tasklet);

	for (i = 0; i < UDSL_NUM_RCV_URBS; i++) {
		struct udsl_receiver *rcv = &(instance->receivers [i]);

		usb_free_urb (rcv->urb);
		kfree_skb (rcv->skb);
	}

	/* send finalize */
	tasklet_disable (&instance->send_tasklet);

	for (i = 0; i < UDSL_NUM_SND_URBS; i++)
		if ((result = usb_unlink_urb (instance->senders [i].urb)) < 0)
			dbg ("udsl_usb_disconnect: usb_unlink_urb on send urb %d returned %d", i, result);

	/* wait for completion handlers to finish */
	do {
		count = 0;
		spin_lock_irq (&instance->send_lock);
		list_for_each (pos, &instance->spare_senders)
			if (++count > UDSL_NUM_SND_URBS)
				panic (__FILE__ ": memory corruption detected at line %d!\n", __LINE__);
		spin_unlock_irq (&instance->send_lock);

		dbg ("udsl_usb_disconnect: found %u spare senders", count);

		if (count == UDSL_NUM_SND_URBS)
			break;

		set_current_state (TASK_RUNNING);
		schedule ();
	} while (1);

	/* no need to take the spinlock */
	INIT_LIST_HEAD (&instance->spare_senders);
	INIT_LIST_HEAD (&instance->spare_send_buffers);
	instance->current_buffer = NULL;

	tasklet_enable (&instance->send_tasklet);

	for (i = 0; i < UDSL_NUM_SND_URBS; i++)
		usb_free_urb (instance->senders [i].urb);

	for (i = 0; i < UDSL_NUM_SND_BUFS; i++)
		kfree (instance->send_buffers [i].base);

	wmb ();
	instance->usb_dev = NULL;

	/* ATM finalize */
	shutdown_atm_dev (instance->atm_dev); /* frees instance */
}


/***********
**  init  **
***********/

static int __init udsl_usb_init (void)
{
	struct sk_buff *skb; /* dummy for sizeof */

	dbg ("udsl_usb_init: driver version " DRIVER_VERSION);

	if (sizeof (struct udsl_control) > sizeof (skb->cb)) {
		printk (KERN_ERR __FILE__ ": unusable with this kernel!\n");
		return -EIO;
	}

	return usb_register (&udsl_usb_driver);
}

static void __exit udsl_usb_cleanup (void)
{
	dbg ("udsl_usb_cleanup entered");

	usb_deregister (&udsl_usb_driver);
}

module_init (udsl_usb_init);
module_exit (udsl_usb_cleanup);

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_LICENSE ("GPL");


/************
**  debug  **
************/

#ifdef VERBOSE_DEBUG
static int udsl_print_packet (const unsigned char *data, int len)
{
	unsigned char buffer [256];
	int i = 0, j = 0;

	for (i = 0; i < len;) {
		buffer [0] = '\0';
		sprintf (buffer, "%.3d :", i);
		for (j = 0; (j < 16) && (i < len); j++, i++) {
			sprintf (buffer, "%s %2.2x", buffer, data [i]);
		}
		dbg ("%s", buffer);
	}
	return i;
}
#endif
