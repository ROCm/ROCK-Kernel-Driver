/*
 * Universal Host Controller Interface driver for USB.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2000 Johannes Erdfelt, johannes@erdfelt.com
 * (C) Copyright 1999 Randy Dunlap
 * (C) Copyright 1999 Georg Acher, acher@in.tum.de
 * (C) Copyright 1999 Deti Fliegl, deti@fliegl.de
 * (C) Copyright 1999 Thomas Sailer, sailer@ife.ee.ethz.ch
 * (C) Copyright 1999 Roman Weissgaerber, weissg@vienna.at
 * (C) Copyright 2000 Yggdrasil Computing, Inc. (port of new PCI interface
 *               support from usb-ohci.c by Adam Richter, adam@yggdrasil.com).
 * (C) Copyright 1999 Gregory P. Smith (from usb-ohci.c)
 *
 *
 * Intel documents this fairly well, and as far as I know there
 * are no royalties or anything like that, but even so there are
 * people who decided that they want to do the same thing in a
 * completely different way.
 *
 * WARNING! The USB documentation is downright evil. Most of it
 * is just crap, written by a committee. You're better off ignoring
 * most of it, the important stuff is:
 *  - the low-level protocol (fairly simple but lots of small details)
 *  - working around the horridness of the rest
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#define DEBUG
#include <linux/usb.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "uhci.h"
#include "uhci-debug.h"

#include <linux/pm.h>

static int debug = 1;
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug level");

static kmem_cache_t *uhci_td_cachep;
static kmem_cache_t *uhci_qh_cachep;
static kmem_cache_t *uhci_up_cachep;	/* urb_priv */

static int rh_submit_urb(struct urb *urb);
static int rh_unlink_urb(struct urb *urb);
static int uhci_get_current_frame_number(struct usb_device *dev);
static int uhci_unlink_generic(struct urb *urb);
static int uhci_unlink_urb(struct urb *urb);

#define min(a,b) (((a)<(b))?(a):(b))

/* If a transfer is still active after this much time, turn off FSBR */
#define IDLE_TIMEOUT	(HZ / 20)	/* 50 ms */

/*
 * Only the USB core should call uhci_alloc_dev and uhci_free_dev
 */
static int uhci_alloc_dev(struct usb_device *dev)
{
	return 0;
}

static int uhci_free_dev(struct usb_device *dev)
{
	struct uhci *uhci = (struct uhci *)dev->bus->hcpriv;
	struct list_head *tmp, *head = &uhci->urb_list;
	unsigned long flags;

	/* Walk through the entire URB list and forcefully remove any */
	/*  URBs that are still active for that device */
	nested_lock(&uhci->urblist_lock, flags);
	tmp = head->next;
	while (tmp != head) {
		struct urb *u = list_entry(tmp, struct urb, urb_list);

		tmp = tmp->next;

		if (u->dev == dev)
			uhci_unlink_urb(u);
	}
	nested_unlock(&uhci->urblist_lock, flags);

	return 0;
}

static void uhci_add_urb_list(struct uhci *uhci, struct urb *urb)
{
	unsigned long flags;

	nested_lock(&uhci->urblist_lock, flags);
	list_add(&urb->urb_list, &uhci->urb_list);
	nested_unlock(&uhci->urblist_lock, flags);
}

static void uhci_remove_urb_list(struct uhci *uhci, struct urb *urb)
{
	unsigned long flags;

	nested_lock(&uhci->urblist_lock, flags);
	if (!list_empty(&urb->urb_list)) {
		list_del(&urb->urb_list);
		INIT_LIST_HEAD(&urb->urb_list);
	}
	nested_unlock(&uhci->urblist_lock, flags);
}

void uhci_set_next_interrupt(struct uhci *uhci)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->framelist_lock, flags);
	uhci->skel_term_td.status |= TD_CTRL_IOC;
	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

void uhci_clear_next_interrupt(struct uhci *uhci)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->framelist_lock, flags);
	uhci->skel_term_td.status &= ~TD_CTRL_IOC;
	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

static struct uhci_td *uhci_alloc_td(struct usb_device *dev)
{
	struct uhci_td *td;

	td = kmem_cache_alloc(uhci_td_cachep, in_interrupt() ? SLAB_ATOMIC : SLAB_KERNEL);
	if (!td)
		return NULL;

	td->link = UHCI_PTR_TERM;
	td->buffer = 0;

	td->frameptr = NULL;
	td->nexttd = td->prevtd = NULL;
	td->dev = dev;
	INIT_LIST_HEAD(&td->list);

	usb_inc_dev_use(dev);

	return td;
}

static void inline uhci_fill_td(struct uhci_td *td, __u32 status,
		__u32 info, __u32 buffer)
{
	td->status = status;
	td->info = info;
	td->buffer = buffer;
}

static void uhci_insert_td(struct uhci *uhci, struct uhci_td *skeltd, struct uhci_td *td)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->framelist_lock, flags);

	/* Fix the linked list pointers */
	td->nexttd = skeltd->nexttd;
	td->prevtd = skeltd;
	if (skeltd->nexttd)
		skeltd->nexttd->prevtd = td;
	skeltd->nexttd = td;

	td->link = skeltd->link;
	skeltd->link = virt_to_bus(td);

	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

/*
 * We insert Isochronous transfers directly into the frame list at the
 * beginning
 * The layout looks as follows:
 * frame list pointer -> iso td's (if any) ->
 * periodic interrupt td (if frame 0) -> irq td's -> control qh -> bulk qh
 */

static void uhci_insert_td_frame_list(struct uhci *uhci, struct uhci_td *td, unsigned framenum)
{
	unsigned long flags;
	struct uhci_td *nexttd;

	framenum %= UHCI_NUMFRAMES;

	spin_lock_irqsave(&uhci->framelist_lock, flags);

	td->frameptr = &uhci->fl->frame[framenum];
	td->link = uhci->fl->frame[framenum];
	if (!(td->link & (UHCI_PTR_TERM | UHCI_PTR_QH))) {
		nexttd = (struct uhci_td *)uhci_ptr_to_virt(td->link);
		td->nexttd = nexttd;
		nexttd->prevtd = td;
		nexttd->frameptr = NULL;
	}
	uhci->fl->frame[framenum] = virt_to_bus(td);

	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

static void uhci_remove_td(struct uhci *uhci, struct uhci_td *td)
{
	unsigned long flags;

	/* If it's not inserted, don't remove it */
	if (!td->frameptr && !td->prevtd && !td->nexttd)
		return;

	spin_lock_irqsave(&uhci->framelist_lock, flags);
	if (td->frameptr) {
		*(td->frameptr) = td->link;
		if (td->nexttd) {
			td->nexttd->frameptr = td->frameptr;
			td->nexttd->prevtd = NULL;
			td->nexttd = NULL;
		}
		td->frameptr = NULL;
	} else {
		if (td->prevtd) {
			td->prevtd->nexttd = td->nexttd;
			td->prevtd->link = td->link;
		}
		if (td->nexttd)
			td->nexttd->prevtd = td->prevtd;
		td->prevtd = td->nexttd = NULL;
	}
	td->link = UHCI_PTR_TERM;
	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

/*
 * Inserts a td into qh list at the top.
 */
static void uhci_insert_tds_in_qh(struct uhci_qh *qh, struct urb *urb, int breadth)
{
	struct list_head *tmp, *head;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td, *prevtd;

	if (!urbp)
		return;

	head = &urbp->list;
	tmp = head->next;
	if (head == tmp)
		return;

	td = list_entry(tmp, struct uhci_td, list);

	/* Add the first TD to the QH element pointer */
	qh->element = virt_to_bus(td) | (breadth ? 0 : UHCI_PTR_DEPTH);

	prevtd = td;

	/* Then link the rest of the TD's */
	tmp = tmp->next;
	while (tmp != head) {
		td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

		prevtd->link = virt_to_bus(td) | (breadth ? 0 : UHCI_PTR_DEPTH);

		prevtd = td;
	}

	prevtd->link = UHCI_PTR_TERM;
}

static void uhci_free_td(struct uhci_td *td)
{
	if (!list_empty(&td->list))
		dbg("td is still in URB list!");

	if (td->dev)
		usb_dec_dev_use(td->dev);

	kmem_cache_free(uhci_td_cachep, td);
}

static struct uhci_qh *uhci_alloc_qh(struct usb_device *dev)
{
	struct uhci_qh *qh;

	qh = kmem_cache_alloc(uhci_qh_cachep, in_interrupt() ? SLAB_ATOMIC : SLAB_KERNEL);
	if (!qh)
		return NULL;

	qh->element = UHCI_PTR_TERM;
	qh->link = UHCI_PTR_TERM;

	qh->dev = dev;
	qh->prevqh = qh->nextqh = NULL;

	INIT_LIST_HEAD(&qh->remove_list);

	usb_inc_dev_use(dev);

	return qh;
}

static void uhci_free_qh(struct uhci_qh *qh)
{
	if (qh->dev)
		usb_dec_dev_use(qh->dev);

	kmem_cache_free(uhci_qh_cachep, qh);
}

static void uhci_insert_qh(struct uhci *uhci, struct uhci_qh *skelqh, struct uhci_qh *qh)
{
	unsigned long flags;

	spin_lock_irqsave(&uhci->framelist_lock, flags);

	/* Fix the linked list pointers */
	qh->nextqh = skelqh->nextqh;
	qh->prevqh = skelqh;
	if (skelqh->nextqh)
		skelqh->nextqh->prevqh = qh;
	skelqh->nextqh = qh;

	qh->link = skelqh->link;
	skelqh->link = virt_to_bus(qh) | UHCI_PTR_QH;

	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

static void uhci_remove_qh(struct uhci *uhci, struct uhci_qh *qh)
{
	unsigned long flags;
	int delayed;

	/* If the QH isn't queued, then we don't need to delay unlink it */
	delayed = (qh->prevqh || qh->nextqh);

	spin_lock_irqsave(&uhci->framelist_lock, flags);
	if (qh->prevqh) {
		qh->prevqh->nextqh = qh->nextqh;
		qh->prevqh->link = qh->link;
	}
	if (qh->nextqh)
		qh->nextqh->prevqh = qh->prevqh;
	qh->prevqh = qh->nextqh = NULL;
	qh->element = qh->link = UHCI_PTR_TERM;
	spin_unlock_irqrestore(&uhci->framelist_lock, flags);

	if (delayed) {
		spin_lock_irqsave(&uhci->qh_remove_lock, flags);

		/* Check to see if the remove list is empty */
		/* Set the IOC bit to force an interrupt so we can remove the QH */
		if (list_empty(&uhci->qh_remove_list))
			uhci_set_next_interrupt(uhci);

		/* Add it */
		list_add(&qh->remove_list, &uhci->qh_remove_list);

		spin_unlock_irqrestore(&uhci->qh_remove_lock, flags);
	} else
		uhci_free_qh(qh);
}

static spinlock_t uhci_append_urb_lock = SPIN_LOCK_UNLOCKED;

/* This function will append one URB's QH to another URB's QH. This is for */
/*  USB_QUEUE_BULK support */
static void uhci_append_queued_urb(struct uhci *uhci, struct urb *eurb, struct urb *urb)
{
	struct urb_priv *eurbp, *urbp, *furbp, *lurbp;
	struct list_head *tmp;
	struct uhci_td *td, *ltd;
	unsigned long flags;

	eurbp = eurb->hcpriv;
	urbp = urb->hcpriv;

	spin_lock_irqsave(&uhci_append_urb_lock, flags);

	/* Find the beginning URB in the queue */
	if (eurbp->queued) {
		struct list_head *head = &eurbp->urb_queue_list;

		tmp = head->next;
		while (tmp != head) {
			struct urb_priv *turbp =
				list_entry(tmp, struct urb_priv, urb_queue_list);

			tmp = tmp->next;

			if (!turbp->queued)
				break;
		}
	} else
		tmp = &eurbp->urb_queue_list;

	furbp = list_entry(tmp, struct urb_priv, urb_queue_list);

	tmp = furbp->urb_queue_list.prev;
	lurbp = list_entry(tmp, struct urb_priv, urb_queue_list);

	/* Add this one to the end */
	list_add_tail(&urbp->urb_queue_list, &furbp->urb_queue_list);

	/* Grab the last TD from the last URB */
	ltd = list_entry(lurbp->list.prev, struct uhci_td, list);

	/* Grab the first TD from the first URB */
	td = list_entry(urbp->list.next, struct uhci_td, list);

	/* No breadth since this will only be called for bulk transfers */
	ltd->link = virt_to_bus(td);

	spin_unlock_irqrestore(&uhci_append_urb_lock, flags);
}

static void uhci_delete_queued_urb(struct uhci *uhci, struct urb *urb)
{
	struct urb_priv *urbp, *nurbp;
	unsigned long flags;

	urbp = urb->hcpriv;

	spin_lock_irqsave(&uhci_append_urb_lock, flags);

	nurbp = list_entry(urbp->urb_queue_list.next, struct urb_priv,
			urb_queue_list);

	if (!urbp->queued) {
		/* We're the head, so just insert the QH for the next URB */
		uhci_insert_qh(uhci, &uhci->skel_bulk_qh, nurbp->qh);
		nurbp->queued = 0;
	} else {
		struct urb_priv *purbp;
		struct uhci_td *ptd;

		/* We're somewhere in the middle (or end). A bit trickier */
		/*  than the head scenario */
		purbp = list_entry(urbp->urb_queue_list.prev, struct urb_priv,
				urb_queue_list);

		ptd = list_entry(purbp->list.prev, struct uhci_td, list);
		if (nurbp->queued)
			/* Close the gap between the two */
			ptd->link = virt_to_bus(list_entry(nurbp->list.next,
					struct uhci_td, list));
		else
			/* The next URB happens to be the beggining, so */
			/*  we're the last, end the chain */
			ptd->link = UHCI_PTR_TERM;
		
	}

	list_del(&urbp->urb_queue_list);

	spin_unlock_irqrestore(&uhci_append_urb_lock, flags);
}

struct urb_priv *uhci_alloc_urb_priv(struct urb *urb)
{
	struct urb_priv *urbp;

	urbp = kmem_cache_alloc(uhci_up_cachep, in_interrupt() ? SLAB_ATOMIC : SLAB_KERNEL);
	if (!urbp)
		return NULL;

	memset((void *)urbp, 0, sizeof(*urbp));

	urbp->inserttime = jiffies;
	urbp->urb = urb;
	
	INIT_LIST_HEAD(&urbp->list);
	INIT_LIST_HEAD(&urbp->urb_queue_list);

	urb->hcpriv = urbp;

	return urbp;
}

static void uhci_add_td_to_urb(struct urb *urb, struct uhci_td *td)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	td->urb = urb;

	list_add_tail(&td->list, &urbp->list);
}

static void uhci_remove_td_from_urb(struct urb *urb, struct uhci_td *td)
{
	urb = NULL;	/* No warnings */

	if (list_empty(&td->list))
		return;

	list_del(&td->list);
	INIT_LIST_HEAD(&td->list);

	td->urb = NULL;
}

static void uhci_destroy_urb_priv(struct urb *urb)
{
	struct list_head *tmp, *head;
	struct urb_priv *urbp;
	struct uhci *uhci;
	struct uhci_td *td;
	unsigned long flags;

	spin_lock_irqsave(&urb->lock, flags);

	urbp = (struct urb_priv *)urb->hcpriv;
	if (!urbp)
		goto unlock;

	if (!urb->dev || !urb->dev->bus || !urb->dev->bus->hcpriv)
		goto unlock;

	uhci = urb->dev->bus->hcpriv;

	head = &urbp->list;
	tmp = head->next;
	while (tmp != head) {
		td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

		uhci_remove_td_from_urb(urb, td);

		uhci_remove_td(uhci, td);

		uhci_free_td(td);
	}

	urb->hcpriv = NULL;
	kmem_cache_free(uhci_up_cachep, urbp);

unlock:
	spin_unlock_irqrestore(&urb->lock, flags);
}

static void uhci_inc_fsbr(struct uhci *uhci, struct urb *urb)
{
	unsigned long flags;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	if (!urbp)
		return;

	spin_lock_irqsave(&uhci->framelist_lock, flags);

	if ((!(urb->transfer_flags & USB_NO_FSBR)) && (!urbp->fsbr)) {
		urbp->fsbr = 1;
		if (!uhci->fsbr++)
			uhci->skel_term_qh.link = virt_to_bus(&uhci->skel_hs_control_qh) | UHCI_PTR_QH;
	}

	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

static void uhci_dec_fsbr(struct uhci *uhci, struct urb *urb)
{
	unsigned long flags;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	if (!urbp)
		return;

	spin_lock_irqsave(&uhci->framelist_lock, flags);

	if ((!(urb->transfer_flags & USB_NO_FSBR)) && urbp->fsbr) {
		urbp->fsbr = 0;
		if (!--uhci->fsbr)
			uhci->skel_term_qh.link = UHCI_PTR_TERM;
	}

	spin_unlock_irqrestore(&uhci->framelist_lock, flags);
}

/*
 * Map status to standard result codes
 *
 * <status> is (td->status & 0xFE0000) [a.k.a. uhci_status_bits(td->status)]
 * <dir_out> is True for output TDs and False for input TDs.
 */
static int uhci_map_status(int status, int dir_out)
{
	if (!status)
		return 0;
	if (status & TD_CTRL_BITSTUFF)			/* Bitstuff error */
		return -EPROTO;
	if (status & TD_CTRL_CRCTIMEO) {		/* CRC/Timeout */
		if (dir_out)
			return -ETIMEDOUT;
		else
			return -EILSEQ;
	}
	if (status & TD_CTRL_NAK)			/* NAK */
		return -ETIMEDOUT;
	if (status & TD_CTRL_BABBLE)			/* Babble */
		return -EPIPE;
	if (status & TD_CTRL_DBUFERR)			/* Buffer error */
		return -ENOSR;
	if (status & TD_CTRL_STALLED)			/* Stalled */
		return -EPIPE;
	if (status & TD_CTRL_ACTIVE)			/* Active */
		return 0;

	return -EINVAL;
}

/*
 * Control transfers
 */
static int uhci_submit_control(struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	struct uhci_td *td;
	struct uhci_qh *qh;
	unsigned long destination, status;
	int maxsze = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	int len = urb->transfer_buffer_length;
	unsigned char *data = urb->transfer_buffer;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;

	/* 3 errors */
	status = (urb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | (3 << 27);

	/*
	 * Build the TD for the control request
	 */
	td = uhci_alloc_td(urb->dev);
	if (!td)
		return -ENOMEM;

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status, destination | (7 << 21),
		virt_to_bus(urb->setup_packet));

	/*
	 * If direction is "send", change the frame from SETUP (0x2D)
	 * to OUT (0xE1). Else change it from SETUP to IN (0x69).
	 */
	destination ^= (USB_PID_SETUP ^ usb_packetid(urb->pipe));

	if (!(urb->transfer_flags & USB_DISABLE_SPD))
		status |= TD_CTRL_SPD;

	/*
	 * Build the DATA TD's
	 */
	while (len > 0) {
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		td = uhci_alloc_td(urb->dev);
		if (!td)
			return -ENOMEM;

		/* Alternate Data0/1 (start with Data1) */
		destination ^= 1 << TD_TOKEN_TOGGLE;
	
		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | ((pktsze - 1) << 21),
			virt_to_bus(data));

		data += pktsze;
		len -= pktsze;
	}

	/*
	 * Build the final TD for control status 
	 */
	td = uhci_alloc_td(urb->dev);
	if (!td)
		return -ENOMEM;

	/*
	 * It's IN if the pipe is an output pipe or we're not expecting
	 * data back.
	 */
	destination &= ~TD_PID;
	if (usb_pipeout(urb->pipe) || !urb->transfer_buffer_length)
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= 1 << TD_TOKEN_TOGGLE;		/* End in Data1 */

	status &= ~TD_CTRL_SPD;

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status | TD_CTRL_IOC,
		destination | (UHCI_NULL_DATA_SIZE << 21), 0);

	qh = uhci_alloc_qh(urb->dev);
	if (!qh)
		return -ENOMEM;

	/* Low speed or small transfers gets a different queue and treatment */
	if (urb->pipe & TD_CTRL_LS) {
		uhci_insert_tds_in_qh(qh, urb, 0);
		uhci_insert_qh(uhci, &uhci->skel_ls_control_qh, qh);
	} else {
		uhci_insert_tds_in_qh(qh, urb, 1);
		uhci_insert_qh(uhci, &uhci->skel_hs_control_qh, qh);
		uhci_inc_fsbr(uhci, urb);
	}

	urbp->qh = qh;

	uhci_add_urb_list(uhci, urb);

	return -EINPROGRESS;
}

static int usb_control_retrigger_status(struct urb *urb);

static int uhci_result_control(struct urb *urb)
{
	struct list_head *tmp, *head;
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	unsigned int status;
	int ret = 0;

	if (!urbp)
		return -EINVAL;

	head = &urbp->list;
	if (head->next == head)
		return -EINVAL;

	if (urbp->short_control_packet) {
		tmp = head->prev;
		goto status_phase;
	}

	tmp = head->next;
	td = list_entry(tmp, struct uhci_td, list);

	/* The first TD is the SETUP phase, check the status, but skip */
	/*  the count */
	status = uhci_status_bits(td->status);
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

	urb->actual_length = 0;

	/* The rest of the TD's (but the last) are data */
	tmp = tmp->next;
	while (tmp != head && tmp->next != head) {
		td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

		if (urbp->fsbr_timeout && (td->status & TD_CTRL_IOC) &&
		    !(td->status & TD_CTRL_ACTIVE)) {
			uhci_inc_fsbr(urb->dev->bus->hcpriv, urb);
			urbp->fsbr_timeout = 0;
			td->status &= ~TD_CTRL_IOC;
		}

		status = uhci_status_bits(td->status);
		if (status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		urb->actual_length += uhci_actual_length(td->status);

		if (status)
			goto td_error;

		/* Check to see if we received a short packet */
		if (uhci_actual_length(td->status) < uhci_expected_length(td->info)) {
			if (urb->transfer_flags & USB_DISABLE_SPD) {
				ret = -EREMOTEIO;
				goto err;
			}

			if (uhci_packetid(td->info) == USB_PID_IN)
				return usb_control_retrigger_status(urb);
			else
				return 0;
		}
	}

status_phase:
	td = list_entry(tmp, struct uhci_td, list);

	/* Control status phase */
	status = uhci_status_bits(td->status);

#ifdef I_HAVE_BUGGY_APC_BACKUPS
	/* APC BackUPS Pro kludge */
	/* It tries to send all of the descriptor instead of the amount */
	/*  we requested */
	if (td->status & TD_CTRL_IOC &&	/* IOC is masked out by uhci_status_bits */
	    status & TD_CTRL_ACTIVE &&
	    status & TD_CTRL_NAK)
		return 0;
#endif

	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

	return 0;

td_error:
	ret = uhci_map_status(status, uhci_packetout(td->info));
	if (ret == -EPIPE)
		/* endpoint has stalled - mark it halted */
		usb_endpoint_halt(urb->dev, uhci_endpoint(td->info),
	    			uhci_packetout(td->info));

err:
	if (debug && ret != -EPIPE) {
		/* Some debugging code */
		dbg("uhci_result_control() failed with status %x", status);

		/* Print the chain for debugging purposes */
		uhci_show_urb_queue(urb);
	}

	return ret;
}

static int usb_control_retrigger_status(struct urb *urb)
{
	struct list_head *tmp, *head;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci *uhci = urb->dev->bus->hcpriv;

	urbp->short_control_packet = 1;

	/* Create a new QH to avoid pointer overwriting problems */
	uhci_remove_qh(uhci, urbp->qh);

	/* Delete all of the TD's except for the status TD at the end */
	head = &urbp->list;
	tmp = head->next;
	while (tmp != head && tmp->next != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

		uhci_remove_td_from_urb(urb, td);

		uhci_remove_td(uhci, td);

		uhci_free_td(td);
	}

	urbp->qh = uhci_alloc_qh(urb->dev);
	if (!urbp->qh) {
		err("unable to allocate new QH for control retrigger");
		return -ENOMEM;
	}

	/* One TD, who cares about Breadth first? */
	uhci_insert_tds_in_qh(urbp->qh, urb, 0);

	/* Low speed or small transfers gets a different queue and treatment */
	if (urb->pipe & TD_CTRL_LS)
		uhci_insert_qh(uhci, &uhci->skel_ls_control_qh, urbp->qh);
	else
		uhci_insert_qh(uhci, &uhci->skel_hs_control_qh, urbp->qh);

	return -EINPROGRESS;
}

/*
 * Interrupt transfers
 */
static int uhci_submit_interrupt(struct urb *urb)
{
	struct uhci_td *td;
	unsigned long destination, status;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;

	if (urb->transfer_buffer_length > usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)))
		return -EINVAL;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	status = (urb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_IOC;

	td = uhci_alloc_td(urb->dev);
	if (!td)
		return -ENOMEM;

	destination |= (usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE);
	destination |= ((urb->transfer_buffer_length - 1) << 21);

	usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe));

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status, destination,
		virt_to_bus(urb->transfer_buffer));

	uhci_insert_td(uhci, &uhci->skeltd[__interval_to_skel(urb->interval)], td);

	uhci_add_urb_list(uhci, urb);

	return -EINPROGRESS;
}

static int uhci_result_interrupt(struct urb *urb)
{
	struct list_head *tmp, *head;
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	unsigned int status;
	int ret = 0;

	if (!urbp)
		return -EINVAL;

	urb->actual_length = 0;

	head = &urbp->list;
	tmp = head->next;
	while (tmp != head) {
		td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

		if (urbp->fsbr_timeout && (td->status & TD_CTRL_IOC) &&
		    !(td->status & TD_CTRL_ACTIVE)) {
			uhci_inc_fsbr(urb->dev->bus->hcpriv, urb);
			urbp->fsbr_timeout = 0;
			td->status &= ~TD_CTRL_IOC;
		}

		status = uhci_status_bits(td->status);
		if (status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		urb->actual_length += uhci_actual_length(td->status);

		if (status)
			goto td_error;

		if (uhci_actual_length(td->status) < uhci_expected_length(td->info)) {
			usb_settoggle(urb->dev, uhci_endpoint(td->info),
				uhci_packetout(td->info),
				uhci_toggle(td->info) ^ 1);

			if (urb->transfer_flags & USB_DISABLE_SPD) {
				ret = -EREMOTEIO;
				goto err;
			} else
				return 0;
		}
	}

	return 0;

td_error:
	ret = uhci_map_status(status, uhci_packetout(td->info));
	if (ret == -EPIPE)
		/* endpoint has stalled - mark it halted */
		usb_endpoint_halt(urb->dev, uhci_endpoint(td->info),
	    			uhci_packetout(td->info));

err:
	if (debug && ret != -EPIPE) {
		/* Some debugging code */
		dbg("uhci_result_interrupt/bulk() failed with status %x",
			status);

		/* Print the chain for debugging purposes */
		if (urbp->qh)
			uhci_show_urb_queue(urb);
		else
			uhci_show_td(td);
	}

	return ret;
}

static void uhci_reset_interrupt(struct urb *urb)
{
	struct list_head *tmp;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;

	if (!urbp)
		return;

	tmp = urbp->list.next;
	td = list_entry(tmp, struct uhci_td, list);
	if (!td)
		return;

	td->status = (td->status & 0x2F000000) | TD_CTRL_ACTIVE | TD_CTRL_IOC;
	td->info &= ~(1 << TD_TOKEN_TOGGLE);
	td->info |= (usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE);
	usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe));

	urb->status = -EINPROGRESS;
}

/*
 * Bulk transfers
 */
static int uhci_submit_bulk(struct urb *urb, struct urb *eurb)
{
	struct uhci_td *td;
	struct uhci_qh *qh;
	unsigned long destination, status;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	int maxsze = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	int len = urb->transfer_buffer_length;
	unsigned char *data = urb->transfer_buffer;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	if (len < 0)
		return -EINVAL;

	/* Can't have low speed bulk transfers */
	if (urb->pipe & TD_CTRL_LS)
		return -EINVAL;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	/* 3 errors */
	status = TD_CTRL_ACTIVE | (3 << TD_CTRL_C_ERR_SHIFT);

	if (!(urb->transfer_flags & USB_DISABLE_SPD))
		status |= TD_CTRL_SPD;

	/*
	 * Build the DATA TD's
	 */
	do {	/* Allow zero length packets */
		int pktsze = len;

		if (pktsze > maxsze)
			pktsze = maxsze;

		td = uhci_alloc_td(urb->dev);
		if (!td)
			return -ENOMEM;

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | ((pktsze - 1) << 21) |
			(usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			 usb_pipeout(urb->pipe)) << TD_TOKEN_TOGGLE),
			virt_to_bus(data));

		data += pktsze;
		len -= maxsze;

		if (len <= 0)
			td->status |= TD_CTRL_IOC;

		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			usb_pipeout(urb->pipe));
	} while (len > 0);

	qh = uhci_alloc_qh(urb->dev);
	if (!qh)
		return -ENOMEM;

	urbp->qh = qh;

	/* Always assume depth first */
	uhci_insert_tds_in_qh(qh, urb, 1);

	if (urb->transfer_flags & USB_QUEUE_BULK && eurb) {
		urbp->queued = 1;
		uhci_append_queued_urb(uhci, eurb, urb);
	} else
		uhci_insert_qh(uhci, &uhci->skel_bulk_qh, qh);

	uhci_add_urb_list(uhci, urb);

	uhci_inc_fsbr(uhci, urb);

	return -EINPROGRESS;
}

/* We can use the result interrupt since they're identical */
#define uhci_result_bulk uhci_result_interrupt

/*
 * Isochronous transfers
 */
static int isochronous_find_limits(struct urb *urb, unsigned int *start, unsigned int *end)
{
	struct urb *last_urb = NULL;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	struct list_head *tmp, *head = &uhci->urb_list;
	int ret = 0;
	unsigned long flags;

	nested_lock(&uhci->urblist_lock, flags);
	tmp = head->next;
	while (tmp != head) {
		struct urb *u = list_entry(tmp, struct urb, urb_list);

		tmp = tmp->next;

		/* look for pending URB's with identical pipe handle */
		if ((urb->pipe == u->pipe) && (urb->dev == u->dev) &&
		    (u->status == -EINPROGRESS) && (u != urb)) {
			if (!last_urb)
				*start = u->start_frame;
			last_urb = u;
		}
	}

	if (last_urb) {
		*end = (last_urb->start_frame + last_urb->number_of_packets) & 1023;
		ret = 0;
	} else
		ret = -1;	/* no previous urb found */

	nested_unlock(&uhci->urblist_lock, flags);

	return ret;
}

static int isochronous_find_start(struct urb *urb)
{
	int limits;
	unsigned int start = 0, end = 0;

	if (urb->number_of_packets > 900)	/* 900? Why? */
		return -EFBIG;

	limits = isochronous_find_limits(urb, &start, &end);

	if (urb->transfer_flags & USB_ISO_ASAP) {
		if (limits) {
			int curframe;

			curframe = uhci_get_current_frame_number(urb->dev) % UHCI_NUMFRAMES;
			urb->start_frame = (curframe + 10) % UHCI_NUMFRAMES;
		} else
			urb->start_frame = end;
	} else {
		urb->start_frame %= UHCI_NUMFRAMES;
		/* FIXME: Sanity check */
	}

	return 0;
}

static int uhci_submit_isochronous(struct urb *urb)
{
	struct uhci_td *td;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	int i, ret, framenum;
	int status, destination;

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	ret = isochronous_find_start(urb);
	if (ret)
		return ret;

	framenum = urb->start_frame;
	for (i = 0; i < urb->number_of_packets; i++, framenum++) {
		if (!urb->iso_frame_desc[i].length)
			continue;

		td = uhci_alloc_td(urb->dev);
		if (!td)
			return -ENOMEM;

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | ((urb->iso_frame_desc[i].length - 1) << 21),
			virt_to_bus(urb->transfer_buffer + urb->iso_frame_desc[i].offset));

		if (i + 1 >= urb->number_of_packets)
			td->status |= TD_CTRL_IOC;

		uhci_insert_td_frame_list(uhci, td, framenum);
	}

	uhci_add_urb_list(uhci, urb);

	return -EINPROGRESS;
}

static int uhci_result_isochronous(struct urb *urb)
{
	struct list_head *tmp, *head;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	int status;
	int i, ret = 0;

	if (!urbp)
		return -EINVAL;

	urb->actual_length = 0;

	i = 0;
	head = &urbp->list;
	tmp = head->next;
	while (tmp != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, list);
		int actlength;

		tmp = tmp->next;

		if (td->status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		actlength = uhci_actual_length(td->status);
		urb->iso_frame_desc[i].actual_length = actlength;
		urb->actual_length += actlength;

		status = uhci_map_status(uhci_status_bits(td->status), usb_pipeout(urb->pipe));
		urb->iso_frame_desc[i].status = status;
		if (status != 0) {
			urb->error_count++;
			ret = status;
		}

		i++;
	}

	return ret;
}

static struct urb *uhci_find_urb_ep(struct uhci *uhci, struct urb *urb)
{
	struct list_head *tmp, *head = &uhci->urb_list;
	unsigned long flags;
	struct urb *u = NULL;

	if (usb_pipeisoc(urb->pipe))
		return NULL;

	nested_lock(&uhci->urblist_lock, flags);
	tmp = head->next;
	while (tmp != head) {
		u = list_entry(tmp, struct urb, urb_list);

		tmp = tmp->next;

		if (u->dev == urb->dev &&
		    u->pipe == urb->pipe)
			goto found;
	}
	u = NULL;

found:
	nested_unlock(&uhci->urblist_lock, flags);

	return u;
}

static int uhci_submit_urb(struct urb *urb)
{
	int ret = -EINVAL;
	struct uhci *uhci;
	unsigned long flags;
	struct urb *u;
	int bustime;

	if (!urb)
		return -EINVAL;

	if (!urb->dev || !urb->dev->bus || !urb->dev->bus->hcpriv)
		return -ENODEV;

	uhci = (struct uhci *)urb->dev->bus->hcpriv;

	/* Short circuit the virtual root hub */
	if (usb_pipedevice(urb->pipe) == uhci->rh.devnum)
		return rh_submit_urb(urb);

	u = uhci_find_urb_ep(uhci, urb);
	if (u && !(urb->transfer_flags & USB_QUEUE_BULK))
		return -ENXIO;

	usb_inc_dev_use(urb->dev);
	spin_lock_irqsave(&urb->lock, flags);

	if (!uhci_alloc_urb_priv(urb)) {
		spin_unlock_irqrestore(&urb->lock, flags);
		usb_dec_dev_use(urb->dev);

		return -ENOMEM;
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ret = uhci_submit_control(urb);
		break;
	case PIPE_INTERRUPT:
		if (urb->bandwidth == 0) {	/* not yet checked/allocated */
			bustime = usb_check_bandwidth(urb->dev, urb);
			if (bustime < 0)
				ret = bustime;
			else {
				ret = uhci_submit_interrupt(urb);
				if (ret == -EINPROGRESS)
					usb_claim_bandwidth(urb->dev, urb, bustime, 0);
			}
		} else		/* bandwidth is already set */
			ret = uhci_submit_interrupt(urb);
		break;
	case PIPE_BULK:
		ret = uhci_submit_bulk(urb, u);
		break;
	case PIPE_ISOCHRONOUS:
		if (urb->bandwidth == 0) {	/* not yet checked/allocated */
			if (urb->number_of_packets <= 0) {
				ret = -EINVAL;
				break;
			}
			bustime = usb_check_bandwidth(urb->dev, urb);
			if (bustime < 0) {
				ret = bustime;
				break;
			}

			ret = uhci_submit_isochronous(urb);
			if (ret == -EINPROGRESS)
				usb_claim_bandwidth(urb->dev, urb, bustime, 1);
		} else		/* bandwidth is already set */
			ret = uhci_submit_isochronous(urb);
		break;
	}

	urb->status = ret;

	spin_unlock_irqrestore(&urb->lock, flags);

	if (ret == -EINPROGRESS)
		ret = 0;
	else {
		uhci_unlink_generic(urb);
		usb_dec_dev_use(urb->dev);
	}

	return ret;
}

/*
 * Return the result of a transfer
 *
 * Must be called with urblist_lock acquired
 */
static void uhci_transfer_result(struct urb *urb)
{
	struct usb_device *dev = urb->dev;
	struct urb *turb;
	int proceed = 0, is_ring = 0;
	int ret = -EINVAL;
	unsigned long flags;

	spin_lock_irqsave(&urb->lock, flags);

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ret = uhci_result_control(urb);
		break;
	case PIPE_INTERRUPT:
		ret = uhci_result_interrupt(urb);
		break;
	case PIPE_BULK:
		ret = uhci_result_bulk(urb);
		break;
	case PIPE_ISOCHRONOUS:
		ret = uhci_result_isochronous(urb);
		break;
	}

	urb->status = ret;

	spin_unlock_irqrestore(&urb->lock, flags);

	if (ret == -EINPROGRESS)
		return;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
	case PIPE_ISOCHRONOUS:
		/* Release bandwidth for Interrupt or Isoc. transfers */
		/* Spinlock needed ? */
		if (urb->bandwidth)
			usb_release_bandwidth(urb->dev, urb, 1);
		uhci_unlink_generic(urb);
		break;
	case PIPE_INTERRUPT:
		/* Interrupts are an exception */
		if (urb->interval) {
			urb->complete(urb);
			uhci_reset_interrupt(urb);
			return;
		}

		/* Release bandwidth for Interrupt or Isoc. transfers */
		/* Spinlock needed ? */
		if (urb->bandwidth)
			usb_release_bandwidth(urb->dev, urb, 0);
		uhci_unlink_generic(urb);
		break;
	}

	if (urb->next) {
		turb = urb->next;
		do {
			if (turb->status != -EINPROGRESS) {
				proceed = 1;
				break;
			}

			turb = turb->next;
		} while (turb && turb != urb && turb != urb->next);

		if (turb == urb || turb == urb->next)
			is_ring = 1;
	}

	if (urb->complete && !proceed) {
		urb->complete(urb);
		if (!proceed && is_ring)
			uhci_submit_urb(urb);
	}

	if (proceed && urb->next) {
		turb = urb->next;
		do {
			if (turb->status != -EINPROGRESS &&
			    uhci_submit_urb(turb) != 0)

			turb = turb->next;
		} while (turb && turb != urb->next);

		if (urb->complete)
			urb->complete(urb);
	}

	/* We decrement the usage count after we're done with everything */
	usb_dec_dev_use(dev);
}

static int uhci_unlink_generic(struct urb *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;

	if (!urbp)
		return -EINVAL;

	uhci_dec_fsbr(uhci, urb);	/* Safe since it checks */

	uhci_remove_urb_list(uhci, urb);

	if (urbp->qh)
		/* The interrupt loop will reclaim the QH's */
		uhci_remove_qh(uhci, urbp->qh);

	if (!list_empty(&urbp->urb_queue_list))
		uhci_delete_queued_urb(uhci, urb);

	uhci_destroy_urb_priv(urb);

	urb->dev = NULL;

	return 0;
}

static int uhci_unlink_urb(struct urb *urb)
{
	struct uhci *uhci;
	int ret = 0;
	unsigned long flags;

	if (!urb)
		return -EINVAL;

	if (!urb->dev || !urb->dev->bus)
		return -ENODEV;

	uhci = (struct uhci *)urb->dev->bus->hcpriv;

	/* Short circuit the virtual root hub */
	if (usb_pipedevice(urb->pipe) == uhci->rh.devnum)
		return rh_unlink_urb(urb);

	/* Release bandwidth for Interrupt or Isoc. transfers */
	/* Spinlock needed ? */
	if (urb->bandwidth) {
		switch (usb_pipetype(urb->pipe)) {
		case PIPE_INTERRUPT:
			usb_release_bandwidth(urb->dev, urb, 0);
			break;
		case PIPE_ISOCHRONOUS:
			usb_release_bandwidth(urb->dev, urb, 1);
			break;
		default:
			break;
		}
	}

	if (urb->status == -EINPROGRESS) {
		uhci_unlink_generic(urb);

		if (urb->transfer_flags & USB_ASYNC_UNLINK) {
			urb->status = -ECONNABORTED;

			spin_lock_irqsave(&uhci->urb_remove_lock, flags);

			/* Check to see if the remove list is empty */
			if (list_empty(&uhci->urb_remove_list))
				uhci_set_next_interrupt(uhci);
			
			list_add(&urb->urb_list, &uhci->urb_remove_list);

			spin_unlock_irqrestore(&uhci->urb_remove_lock, flags);
		} else {
			urb->status = -ENOENT;

			if (in_interrupt()) {	/* wait at least 1 frame */
				static int errorcount = 10;

				if (errorcount--)
					dbg("uhci_unlink_urb called from interrupt for urb %p", urb);
				udelay(1000);
			} else
				schedule_timeout(1+1*HZ/1000); 

			if (urb->complete)
				urb->complete(urb);
		}
	}

	return ret;
}

static int uhci_fsbr_timeout(struct uhci *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct list_head *head, *tmp;

	uhci_dec_fsbr(uhci, urb);

	/* There is a race with updating IOC in here, but it's not worth */
	/*  trying to fix since this is merely an optimization. The only */
	/*  time we'd lose is if the status of the packet got updated */
	/*  and we'd be turning on FSBR next frame anyway, so it's a wash */
	urbp->fsbr_timeout = 1;

	head = &urbp->list;
	tmp = head->next;
	while (tmp != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

		if (td->status & TD_CTRL_ACTIVE) {
			td->status |= TD_CTRL_IOC;
			break;
		}
	}

	return 0;
}

/*
 * uhci_get_current_frame_number()
 *
 * returns the current frame number for a USB bus/controller.
 */
static int uhci_get_current_frame_number(struct usb_device *dev)
{
	struct uhci *uhci = (struct uhci *)dev->bus->hcpriv;

	return inw(uhci->io_addr + USBFRNUM);
}

struct usb_operations uhci_device_operations = {
	uhci_alloc_dev,
	uhci_free_dev,
	uhci_get_current_frame_number,
	uhci_submit_urb,
	uhci_unlink_urb
};

/* -------------------------------------------------------------------
   Virtual Root Hub
   ------------------------------------------------------------------- */

static __u8 root_hub_dev_des[] =
{
 	0x12,			/*  __u8  bLength; */
	0x01,			/*  __u8  bDescriptorType; Device */
	0x00,			/*  __u16 bcdUSB; v1.0 */
	0x01,
	0x09,			/*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,			/*  __u8  bDeviceSubClass; */
	0x00,			/*  __u8  bDeviceProtocol; */
	0x08,			/*  __u8  bMaxPacketSize0; 8 Bytes */
	0x00,			/*  __u16 idVendor; */
	0x00,
	0x00,			/*  __u16 idProduct; */
	0x00,
	0x00,			/*  __u16 bcdDevice; */
	0x00,
	0x00,			/*  __u8  iManufacturer; */
	0x02,			/*  __u8  iProduct; */
	0x01,			/*  __u8  iSerialNumber; */
	0x01			/*  __u8  bNumConfigurations; */
};


/* Configuration descriptor */
static __u8 root_hub_config_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x02,			/*  __u8  bDescriptorType; Configuration */
	0x19,			/*  __u16 wTotalLength; */
	0x00,
	0x01,			/*  __u8  bNumInterfaces; */
	0x01,			/*  __u8  bConfigurationValue; */
	0x00,			/*  __u8  iConfiguration; */
	0x40,			/*  __u8  bmAttributes;
					Bit 7: Bus-powered, 6: Self-powered,
					Bit 5 Remote-wakeup, 4..0: resvd */
	0x00,			/*  __u8  MaxPower; */

	/* interface */
	0x09,			/*  __u8  if_bLength; */
	0x04,			/*  __u8  if_bDescriptorType; Interface */
	0x00,			/*  __u8  if_bInterfaceNumber; */
	0x00,			/*  __u8  if_bAlternateSetting; */
	0x01,			/*  __u8  if_bNumEndpoints; */
	0x09,			/*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,			/*  __u8  if_bInterfaceSubClass; */
	0x00,			/*  __u8  if_bInterfaceProtocol; */
	0x00,			/*  __u8  if_iInterface; */

	/* endpoint */
	0x07,			/*  __u8  ep_bLength; */
	0x05,			/*  __u8  ep_bDescriptorType; Endpoint */
	0x81,			/*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
	0x03,			/*  __u8  ep_bmAttributes; Interrupt */
	0x08,			/*  __u16 ep_wMaxPacketSize; 8 Bytes */
	0x00,
	0xff			/*  __u8  ep_bInterval; 255 ms */
};

static __u8 root_hub_hub_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x29,			/*  __u8  bDescriptorType; Hub-descriptor */
	0x02,			/*  __u8  bNbrPorts; */
	0x00,			/* __u16  wHubCharacteristics; */
	0x00,
	0x01,			/*  __u8  bPwrOn2pwrGood; 2ms */
	0x00,			/*  __u8  bHubContrCurrent; 0 mA */
	0x00,			/*  __u8  DeviceRemovable; *** 7 Ports max *** */
	0xff			/*  __u8  PortPwrCtrlMask; *** 7 ports max *** */
};

/*-------------------------------------------------------------------------*/
/* prepare Interrupt pipe transaction data; HUB INTERRUPT ENDPOINT */
static int rh_send_irq(struct urb *urb)
{
	int i, len = 1;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	unsigned int io_addr = uhci->io_addr;
	__u16 data = 0;

	for (i = 0; i < uhci->rh.numports; i++) {
		data |= ((inw(io_addr + USBPORTSC1 + i * 2) & 0xa) > 0 ? (1 << (i + 1)) : 0);
		len = (i + 1) / 8 + 1;
	}

	*(__u16 *) urb->transfer_buffer = cpu_to_le16(data);
	urb->actual_length = len;
	urb->status = USB_ST_NOERROR;

	if ((data > 0) && (uhci->rh.send != 0)) {
		dbg("root-hub INT complete: port1: %x port2: %x data: %x",
			inw(io_addr + USBPORTSC1), inw(io_addr + USBPORTSC2), data);
		urb->complete(urb);
	}

	return USB_ST_NOERROR;
}

/*-------------------------------------------------------------------------*/
/* Virtual Root Hub INTs are polled by this timer every "interval" ms */
static int rh_init_int_timer(struct urb *urb);

static void rh_int_timer_do(unsigned long ptr)
{
	struct urb *urb = (struct urb *)ptr;
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	struct list_head *tmp, *head = &uhci->urb_list;
	struct urb_priv *urbp;
	int len;
	unsigned long flags;

	if (uhci->rh.send) {
		len = rh_send_irq(urb);
		if (len > 0) {
			urb->actual_length = len;
			if (urb->complete)
				urb->complete(urb);
		}
	}

	nested_lock(&uhci->urblist_lock, flags);
	tmp = head->next;
	while (tmp != head) {
		struct urb *u = list_entry(tmp, urb_t, urb_list);

		tmp = tmp->next;

		urbp = (struct urb_priv *)u->hcpriv;
		if (urbp) {
			/* Check if the FSBR timed out */
			if (urbp->fsbr && time_after_eq(jiffies, urbp->inserttime + IDLE_TIMEOUT))
				uhci_fsbr_timeout(uhci, u);

			/* Check if the URB timed out */
			if (u->timeout && time_after_eq(jiffies, u->timeout)) {
				u->transfer_flags |= USB_ASYNC_UNLINK | USB_TIMEOUT_KILLED;
				uhci_unlink_urb(u);
			}
		}
	}
	nested_unlock(&uhci->urblist_lock, flags);

	rh_init_int_timer(urb);
}

/*-------------------------------------------------------------------------*/
/* Root Hub INTs are polled by this timer */
static int rh_init_int_timer(struct urb *urb)
{
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;

	uhci->rh.interval = urb->interval;
	init_timer(&uhci->rh.rh_int_timer);
	uhci->rh.rh_int_timer.function = rh_int_timer_do;
	uhci->rh.rh_int_timer.data = (unsigned long)urb;
	uhci->rh.rh_int_timer.expires = jiffies + (HZ * (urb->interval < 30 ? 30 : urb->interval)) / 1000;
	add_timer(&uhci->rh.rh_int_timer);

	return 0;
}

/*-------------------------------------------------------------------------*/
#define OK(x)			len = (x); break

#define CLR_RH_PORTSTAT(x) \
	status = inw(io_addr + USBPORTSC1 + 2 * (wIndex-1)); \
	status = (status & 0xfff5) & ~(x); \
	outw(status, io_addr + USBPORTSC1 + 2 * (wIndex-1))

#define SET_RH_PORTSTAT(x) \
	status = inw(io_addr + USBPORTSC1 + 2 * (wIndex-1)); \
	status = (status & 0xfff5) | (x); \
	outw(status, io_addr + USBPORTSC1 + 2 * (wIndex-1))


/*-------------------------------------------------------------------------*/
/*************************
 ** Root Hub Control Pipe
 *************************/

static int rh_submit_urb(struct urb *urb)
{
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;
	unsigned int pipe = urb->pipe;
	devrequest *cmd = (devrequest *)urb->setup_packet;
	void *data = urb->transfer_buffer;
	int leni = urb->transfer_buffer_length;
	int len = 0;
	int status = 0;
	int stat = USB_ST_NOERROR;
	int i;
	unsigned int io_addr = uhci->io_addr;
	__u16 cstatus;
	__u16 bmRType_bReq;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;

	if (usb_pipetype(pipe) == PIPE_INTERRUPT) {
		uhci->rh.urb = urb;
		uhci->rh.send = 1;
		uhci->rh.interval = urb->interval;
		rh_init_int_timer(urb);

		return USB_ST_NOERROR;
	}

	bmRType_bReq = cmd->requesttype | cmd->request << 8;
	wValue = le16_to_cpu(cmd->value);
	wIndex = le16_to_cpu(cmd->index);
	wLength = le16_to_cpu(cmd->length);

	for (i = 0; i < 8; i++)
		uhci->rh.c_p_r[i] = 0;

	switch (bmRType_bReq) {
		/* Request Destination:
		   without flags: Device,
		   RH_INTERFACE: interface,
		   RH_ENDPOINT: endpoint,
		   RH_CLASS means HUB here,
		   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here
		*/

	case RH_GET_STATUS:
		*(__u16 *)data = cpu_to_le16(1);
		OK(2);
	case RH_GET_STATUS | RH_INTERFACE:
		*(__u16 *)data = cpu_to_le16(0);
		OK(2);
	case RH_GET_STATUS | RH_ENDPOINT:
		*(__u16 *)data = cpu_to_le16(0);
		OK(2);
	case RH_GET_STATUS | RH_CLASS:
		*(__u32 *)data = cpu_to_le32(0);
		OK(4);		/* hub power */
	case RH_GET_STATUS | RH_OTHER | RH_CLASS:
		status = inw(io_addr + USBPORTSC1 + 2 * (wIndex - 1));
		cstatus = ((status & USBPORTSC_CSC) >> (1 - 0)) |
			((status & USBPORTSC_PEC) >> (3 - 1)) |
			(uhci->rh.c_p_r[wIndex - 1] << (0 + 4));
			status = (status & USBPORTSC_CCS) |
			((status & USBPORTSC_PE) >> (2 - 1)) |
			((status & USBPORTSC_SUSP) >> (12 - 2)) |
			((status & USBPORTSC_PR) >> (9 - 4)) |
			(1 << 8) |      /* power on */
			((status & USBPORTSC_LSDA) << (-8 + 9));

		*(__u16 *)data = cpu_to_le16(status);
		*(__u16 *)(data + 2) = cpu_to_le16(cstatus);
		OK(4);
	case RH_CLEAR_FEATURE | RH_ENDPOINT:
		switch (wValue) {
		case RH_ENDPOINT_STALL:
			OK(0);
		}
		break;
	case RH_CLEAR_FEATURE | RH_CLASS:
		switch (wValue) {
		case RH_C_HUB_OVER_CURRENT:
			OK(0);	/* hub power over current */
		}
		break;
	case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case RH_PORT_ENABLE:
			CLR_RH_PORTSTAT(USBPORTSC_PE);
			OK(0);
		case RH_PORT_SUSPEND:
			CLR_RH_PORTSTAT(USBPORTSC_SUSP);
			OK(0);
		case RH_PORT_POWER:
			OK(0);	/* port power */
		case RH_C_PORT_CONNECTION:
			SET_RH_PORTSTAT(USBPORTSC_CSC);
			OK(0);
		case RH_C_PORT_ENABLE:
			SET_RH_PORTSTAT(USBPORTSC_PEC);
			OK(0);
		case RH_C_PORT_SUSPEND:
			/*** WR_RH_PORTSTAT(RH_PS_PSSC); */
			OK(0);
		case RH_C_PORT_OVER_CURRENT:
			OK(0);	/* port power over current */
		case RH_C_PORT_RESET:
			uhci->rh.c_p_r[wIndex - 1] = 0;
			OK(0);
		}
		break;
	case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case RH_PORT_SUSPEND:
			SET_RH_PORTSTAT(USBPORTSC_SUSP);
			OK(0);
		case RH_PORT_RESET:
			SET_RH_PORTSTAT(USBPORTSC_PR);
			wait_ms(50);	/* USB v1.1 7.1.7.3 */
			uhci->rh.c_p_r[wIndex - 1] = 1;
			CLR_RH_PORTSTAT(USBPORTSC_PR);
			udelay(10);
			SET_RH_PORTSTAT(USBPORTSC_PE);
			wait_ms(10);
			SET_RH_PORTSTAT(0xa);
			OK(0);
		case RH_PORT_POWER:
			OK(0); /* port power ** */
		case RH_PORT_ENABLE:
			SET_RH_PORTSTAT(USBPORTSC_PE);
			OK(0);
		}
		break;
	case RH_SET_ADDRESS:
		uhci->rh.devnum = wValue;
		OK(0);
	case RH_GET_DESCRIPTOR:
		switch ((wValue & 0xff00) >> 8) {
		case 0x01:	/* device descriptor */
			len = min(leni, min(sizeof(root_hub_dev_des), wLength));
			memcpy(data, root_hub_dev_des, len);
			OK(len);
		case 0x02:	/* configuration descriptor */
			len = min(leni, min(sizeof(root_hub_config_des), wLength));
			memcpy (data, root_hub_config_des, len);
			OK(len);
		case 0x03:	/* string descriptors */
			len = usb_root_hub_string (wValue & 0xff,
				uhci->io_addr, "UHCI-alt",
				data, wLength);
			if (len > 0) {
				OK (min (leni, len));
			} else 
				stat = -EPIPE;
		}
		break;
	case RH_GET_DESCRIPTOR | RH_CLASS:
		root_hub_hub_des[2] = uhci->rh.numports;
		len = min(leni, min(sizeof(root_hub_hub_des), wLength));
		memcpy(data, root_hub_hub_des, len);
		OK(len);
	case RH_GET_CONFIGURATION:
		*(__u8 *)data = 0x01;
		OK(1);
	case RH_SET_CONFIGURATION:
		OK(0);
	case RH_GET_INTERFACE | RH_INTERFACE:
		*(__u8 *)data = 0x00;
		OK(1);
	case RH_SET_INTERFACE | RH_INTERFACE:
		OK(0);
	default:
		stat = -EPIPE;
	}

	urb->actual_length = len;
	urb->status = stat;
	if (urb->complete)
		urb->complete(urb);

	return USB_ST_NOERROR;
}
/*-------------------------------------------------------------------------*/

static int rh_unlink_urb(struct urb *urb)
{
	struct uhci *uhci = (struct uhci *)urb->dev->bus->hcpriv;

	if (uhci->rh.urb == urb) {
		uhci->rh.send = 0;
		del_timer(&uhci->rh.rh_int_timer);
	}
	return 0;
}
/*-------------------------------------------------------------------*/

void uhci_free_pending_qhs(struct uhci *uhci)
{
	struct list_head *tmp, *head;
	unsigned long flags;

	/* Free any pending QH's */
	spin_lock_irqsave(&uhci->qh_remove_lock, flags);
	head = &uhci->qh_remove_list;
	tmp = head->next;
	while (tmp != head) {
		struct uhci_qh *qh = list_entry(tmp, struct uhci_qh, remove_list);

		tmp = tmp->next;

		list_del(&qh->remove_list);

		uhci_free_qh(qh);
	}
	spin_unlock_irqrestore(&uhci->qh_remove_lock, flags);
}

static void uhci_interrupt(int irq, void *__uhci, struct pt_regs *regs)
{
	struct uhci *uhci = __uhci;
	unsigned int io_addr = uhci->io_addr;
	unsigned short status;
	unsigned long flags;
	struct list_head *tmp, *head;

	/*
	 * Read the interrupt status, and write it back to clear the
	 * interrupt cause
	 */
	status = inw(io_addr + USBSTS);
	if (!status)	/* shared interrupt, not mine */
		return;
	outw(status, io_addr + USBSTS);

	if (status & ~(USBSTS_USBINT | USBSTS_ERROR)) {
		if (status & USBSTS_RD)
			printk(KERN_INFO "uhci: resume detected, not implemented\n");
		if (status & USBSTS_HSE)
			printk(KERN_ERR "uhci: host system error, PCI problems?\n");
		if (status & USBSTS_HCPE)
			printk(KERN_ERR "uhci: host controller process error. something bad happened\n");
		if (status & USBSTS_HCH) {
			printk(KERN_ERR "uhci: host controller halted. very bad\n");
			/* FIXME: Reset the controller, fix the offending TD */
		}
	}

	uhci_free_pending_qhs(uhci);

	spin_lock(&uhci->urb_remove_lock);
	head = &uhci->urb_remove_list;
	tmp = head->next;
	while (tmp != head) {
		struct urb *urb = list_entry(tmp, struct urb, urb_list);

		tmp = tmp->next;

		list_del(&urb->urb_list);

		if (urb->complete)
			urb->complete(urb);
	}
	spin_unlock(&uhci->urb_remove_lock);

	uhci_clear_next_interrupt(uhci);

	/* Walk the list of pending TD's to see which ones completed */
	nested_lock(&uhci->urblist_lock, flags);
	head = &uhci->urb_list;
	tmp = head->next;
	while (tmp != head) {
		struct urb *urb = list_entry(tmp, struct urb, urb_list);

		tmp = tmp->next;

		/* Checks the status and does all of the magic necessary */
		uhci_transfer_result(urb);
	}
	nested_unlock(&uhci->urblist_lock, flags);
}

static void reset_hc(struct uhci *uhci)
{
	unsigned int io_addr = uhci->io_addr;

	/* Global reset for 50ms */
	outw(USBCMD_GRESET, io_addr + USBCMD);
	wait_ms(50);
	outw(0, io_addr + USBCMD);
	wait_ms(10);
}

static void start_hc(struct uhci *uhci)
{
	unsigned int io_addr = uhci->io_addr;
	int timeout = 1000;

	/*
	 * Reset the HC - this will force us to get a
	 * new notification of any already connected
	 * ports due to the virtual disconnect that it
	 * implies.
	 */
	outw(USBCMD_HCRESET, io_addr + USBCMD);
	while (inw(io_addr + USBCMD) & USBCMD_HCRESET) {
		if (!--timeout) {
			printk(KERN_ERR "uhci: USBCMD_HCRESET timed out!\n");
			break;
		}
	}

	/* Turn on all interrupts */
	outw(USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC | USBINTR_SP,
		io_addr + USBINTR);

	/* Start at frame 0 */
	outw(0, io_addr + USBFRNUM);
	outl(virt_to_bus(uhci->fl), io_addr + USBFLBASEADD);

	/* Run and mark it configured with a 64-byte max packet */
	outw(USBCMD_RS | USBCMD_CF | USBCMD_MAXP, io_addr + USBCMD);
}

/*
 * Allocate a frame list, and then setup the skeleton
 *
 * The hardware doesn't really know any difference
 * in the queues, but the order does matter for the
 * protocols higher up. The order is:
 *
 *  - any isochronous events handled before any
 *    of the queues. We don't do that here, because
 *    we'll create the actual TD entries on demand.
 *  - The first queue is the "interrupt queue".
 *  - The second queue is the "control queue", split into low and high speed
 *  - The third queue is "bulk data".
 */
static struct uhci *alloc_uhci(unsigned int io_addr, unsigned int io_size)
{
	int i, port;
	struct uhci *uhci;
	struct usb_bus *bus;

	uhci = kmalloc(sizeof(*uhci), GFP_KERNEL);
	if (!uhci)
		return NULL;

	memset(uhci, 0, sizeof(*uhci));

	uhci->irq = -1;
	uhci->io_addr = io_addr;
	uhci->io_size = io_size;

	spin_lock_init(&uhci->qh_remove_lock);
	INIT_LIST_HEAD(&uhci->qh_remove_list);

	spin_lock_init(&uhci->urb_remove_lock);
	INIT_LIST_HEAD(&uhci->urb_remove_list);

	nested_init(&uhci->urblist_lock);
	INIT_LIST_HEAD(&uhci->urb_list);

	spin_lock_init(&uhci->framelist_lock);

	/* We need exactly one page (per UHCI specs), how convenient */
	/* We assume that one page is atleast 4k (1024 frames * 4 bytes) */
	uhci->fl = (void *)__get_free_page(GFP_KERNEL);
	if (!uhci->fl)
		goto au_free_uhci;

	bus = usb_alloc_bus(&uhci_device_operations);
	if (!bus)
		goto au_free_fl;

	uhci->bus = bus;
	bus->hcpriv = uhci;

	/* Initialize the root hub */

	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/*  they may have more but give no way to determine how many they */
	/*  have. However, according to the UHCI spec, Bit 7 is always set */
	/*  to 1. So we try to use this to our advantage */
	for (port = 0; port < (io_size - 0x10) / 2; port++) {
		unsigned int portstatus;

		portstatus = inw(io_addr + 0x10 + (port * 2));
		if (!(portstatus & 0x0080))
			break;
	}
	if (debug)
		info("detected %d ports", port);

	/* This is experimental so anything less than 2 or greater than 8 is */
	/*  something weird and we'll ignore it */
	if (port < 2 || port > 8) {
		info("port count misdetected? forcing to 2 ports");
		port = 2;
	}

	uhci->rh.numports = port;

	/*
	 * 9 Interrupt queues; link int2 to int1, int4 to int2, etc
	 * then link int1 to control and control to bulk
	 */
	for (i = 1; i < 9; i++) {
		struct uhci_td *td = &uhci->skeltd[i];

		uhci_fill_td(td, 0, (UHCI_NULL_DATA_SIZE << 21) | (0x7f << 8) | USB_PID_IN, 0);
		td->link = virt_to_bus(&uhci->skeltd[i - 1]);
	}


	uhci_fill_td(&uhci->skel_int1_td, 0, (UHCI_NULL_DATA_SIZE << 21) | (0x7f << 8) | USB_PID_IN, 0);
	uhci->skel_int1_td.link = virt_to_bus(&uhci->skel_ls_control_qh) | UHCI_PTR_QH;

	uhci->skel_ls_control_qh.link = virt_to_bus(&uhci->skel_hs_control_qh) | UHCI_PTR_QH;
	uhci->skel_ls_control_qh.element = UHCI_PTR_TERM;

	uhci->skel_hs_control_qh.link = virt_to_bus(&uhci->skel_bulk_qh) | UHCI_PTR_QH;
	uhci->skel_hs_control_qh.element = UHCI_PTR_TERM;

	uhci->skel_bulk_qh.link = virt_to_bus(&uhci->skel_term_qh) | UHCI_PTR_QH;
	uhci->skel_bulk_qh.element = UHCI_PTR_TERM;

	/* This dummy TD is to work around a bug in Intel PIIX controllers */
	uhci_fill_td(&uhci->skel_term_td, 0, (UHCI_NULL_DATA_SIZE << 21) | (0x7f << 8) | USB_PID_IN, 0);
	uhci->skel_term_td.link = UHCI_PTR_TERM;

	uhci->skel_term_qh.link = UHCI_PTR_TERM;
	uhci->skel_term_qh.element = virt_to_bus(&uhci->skel_term_td);

	/*
	 * Fill the frame list: make all entries point to
	 * the proper interrupt queue.
	 *
	 * This is probably silly, but it's a simple way to
	 * scatter the interrupt queues in a way that gives
	 * us a reasonable dynamic range for irq latencies.
	 */
	for (i = 0; i < 1024; i++) {
		struct uhci_td *irq = &uhci->skel_int1_td;

		if (i & 1) {
			irq++;
			if (i & 2) {
				irq++;
				if (i & 4) { 
					irq++;
					if (i & 8) { 
						irq++;
						if (i & 16) {
							irq++;
							if (i & 32) {
								irq++;
								if (i & 64)
									irq++;
							}
						}
					}
				}
			}
		}

		/* Only place we don't use the frame list routines */
		uhci->fl->frame[i] =  virt_to_bus(irq);
	}

	return uhci;

/*
 * error exits:
 */
au_free_fl:
	free_page((unsigned long)uhci->fl);
au_free_uhci:
	kfree(uhci);

	return NULL;
}

/*
 * De-allocate all resources..
 */
static void release_uhci(struct uhci *uhci)
{
	if (uhci->irq >= 0) {
		free_irq(uhci->irq, uhci);
		uhci->irq = -1;
	}

	if (uhci->fl) {
		free_page((unsigned long)uhci->fl);
		uhci->fl = NULL;
	}

	usb_free_bus(uhci->bus);
	kfree(uhci);
}

int uhci_start_root_hub(struct uhci *uhci)
{
	struct usb_device *dev;

	dev = usb_alloc_dev(NULL, uhci->bus);
	if (!dev)
		return -1;

	uhci->bus->root_hub = dev;
	usb_connect(dev);

	if (usb_new_device(dev) != 0) {
		usb_free_dev(dev);

		return -1;
	}

	return 0;
}

/*
 * If we've successfully found a UHCI, now is the time to increment the
 * module usage count, and return success..
 */
static int setup_uhci(struct pci_dev *dev, int irq, unsigned int io_addr, unsigned int io_size)
{
	int retval;
	struct uhci *uhci;
	char buf[8], *bufp = buf;

#ifndef __sparc__
	sprintf(buf, "%d", irq);
#else
	bufp = __irq_itoa(irq);
#endif
	printk(KERN_INFO __FILE__ ": USB UHCI at I/O 0x%x, IRQ %s\n",
		io_addr, bufp);

	uhci = alloc_uhci(io_addr, io_size);
	if (!uhci)
		return -ENOMEM;
	dev->driver_data = uhci;

	request_region(uhci->io_addr, io_size, "usb-uhci");

	reset_hc(uhci);

	usb_register_bus(uhci->bus);
	start_hc(uhci);

	retval = -EBUSY;
	if (request_irq(irq, uhci_interrupt, SA_SHIRQ, "usb-uhci", uhci) == 0) {
		uhci->irq = irq;

		pci_write_config_word(dev, USBLEGSUP, USBLEGSUP_DEFAULT);

		if (!uhci_start_root_hub(uhci))
			return 0;
	}

	/* Couldn't allocate IRQ if we got here */

	reset_hc(uhci);
	release_region(uhci->io_addr, uhci->io_size);
	release_uhci(uhci);

	return retval;
}

static int __devinit uhci_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int i;

	/* disable legacy emulation */
	pci_write_config_word(dev, USBLEGSUP, 0);

	if (pci_enable_device(dev) < 0)
		return -ENODEV;

	if (!dev->irq) {
		err("found UHCI device with no IRQ assigned. check BIOS settings!");
		return -ENODEV;
	}

	/* Search for the IO base address.. */
	for (i = 0; i < 6; i++) {
		unsigned int io_addr = pci_resource_start(dev, i);
		unsigned int io_size = pci_resource_len(dev, i);

		/* IO address? */
		if (!(pci_resource_flags(dev, i) & IORESOURCE_IO))
			continue;

		/* Is it already in use? */
		if (check_region(io_addr, io_size))
			break;

		pci_set_master(dev);
		return setup_uhci(dev, dev->irq, io_addr, io_size);
	}

	return -ENODEV;
}

static void __devexit uhci_pci_remove(struct pci_dev *dev)
{
	struct uhci *uhci = dev->driver_data;

	if (uhci->bus->root_hub)
		usb_disconnect(&uhci->bus->root_hub);

	usb_deregister_bus(uhci->bus);

	reset_hc(uhci);
	release_region(uhci->io_addr, uhci->io_size);

	uhci_free_pending_qhs(uhci);

	release_uhci(uhci);
}

static void uhci_pci_suspend(struct pci_dev *dev)
{
	reset_hc((struct uhci *) dev->driver_data);
}

static void uhci_pci_resume(struct pci_dev *dev)
{
	reset_hc((struct uhci *) dev->driver_data);
	start_hc((struct uhci *) dev->driver_data);
}

/*-------------------------------------------------------------------------*/

static const struct pci_device_id __devinitdata uhci_pci_ids [] = { {

	/* handle any USB UHCI controller */
	class: 		((PCI_CLASS_SERIAL_USB << 8) | 0x00),
	class_mask: 	~0,

	/* no matter who makes it */
	vendor:		PCI_ANY_ID,
	device:		PCI_ANY_ID,
	subvendor:	PCI_ANY_ID,
	subdevice:	PCI_ANY_ID,

	}, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE (pci, uhci_pci_ids);

static struct pci_driver uhci_pci_driver = {
	name:		"usb-uhci",
	id_table:	&uhci_pci_ids [0],

	probe:		uhci_pci_probe,
	remove:		uhci_pci_remove,

#ifdef	CONFIG_PM
	suspend:	uhci_pci_suspend,
	resume:		uhci_pci_resume,
#endif	/* PM */
};

 
static int __init uhci_hcd_init(void)
{
	int retval;

	retval = -ENOMEM;

	/* We throw all of the TD's and QH's into a kmem cache */
	/* TD's and QH's need to be 16 byte aligned and SLAB_HWCACHE_ALIGN */
	/*  does this for us */
	uhci_td_cachep = kmem_cache_create("uhci_td",
		sizeof(struct uhci_td), 0,
		SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!uhci_td_cachep)
		goto td_failed;

	uhci_qh_cachep = kmem_cache_create("uhci_qh",
		sizeof(struct uhci_qh), 0,
		SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!uhci_qh_cachep)
		goto qh_failed;

	uhci_up_cachep = kmem_cache_create("uhci_urb_priv",
		sizeof(struct urb_priv), 0, 0, NULL, NULL);

	if (!uhci_up_cachep)
		goto up_failed;

	retval = pci_module_init (&uhci_pci_driver);
	if (retval)
		goto init_failed;

	return 0;

init_failed:
	if (kmem_cache_destroy(uhci_up_cachep))
		printk(KERN_INFO "uhci: not all urb_priv's were freed\n");

up_failed:
	if (kmem_cache_destroy(uhci_qh_cachep))
		printk(KERN_INFO "uhci: not all QH's were freed\n");

qh_failed:
	if (kmem_cache_destroy(uhci_td_cachep))
		printk(KERN_INFO "uhci: not all TD's were freed\n");

td_failed:
	return retval;
}

static void __exit uhci_hcd_cleanup (void) 
{
	pci_unregister_driver (&uhci_pci_driver);
	
	if (kmem_cache_destroy(uhci_up_cachep))
		printk(KERN_INFO "uhci: not all urb_priv's were freed\n");

	if (kmem_cache_destroy(uhci_qh_cachep))
		printk(KERN_INFO "uhci: not all QH's were freed\n");

	if (kmem_cache_destroy(uhci_td_cachep))
		printk(KERN_INFO "uhci: not all TD's were freed\n");
}

module_init(uhci_hcd_init);
module_exit(uhci_hcd_cleanup);

MODULE_AUTHOR("Linus Torvalds, Johannes Erdfelt, Randy Dunlap, Georg Acher, Deti Fliegl, Thomas Sailer, Roman Weissgaerber");
MODULE_DESCRIPTION("USB Universal Host Controller Interface driver");

