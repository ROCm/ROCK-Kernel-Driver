/*
 * Copyright (c) 2001-2002 by David Brownell
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* this file is part of ehci-hcd.c */

/*-------------------------------------------------------------------------*/

/*
 * EHCI hardware queue manipulation ... the core.  QH/QTD manipulation.
 *
 * Control, bulk, and interrupt traffic all use "qh" lists.  They list "qtd"
 * entries describing USB transactions, max 16-20kB/entry (with 4kB-aligned
 * buffers needed for the larger number).  We use one QH per endpoint, queue
 * multiple urbs (all three types) per endpoint.  URBs may need several qtds.
 *
 * ISO traffic uses "ISO TD" (itd, and sitd) records, and (along with
 * interrupts) needs careful scheduling.  Performance improvements can be
 * an ongoing challenge.  That's in "ehci-sched.c".
 * 
 * USB 1.1 devices are handled (a) by "companion" OHCI or UHCI root hubs,
 * or otherwise through transaction translators (TTs) in USB 2.0 hubs using
 * (b) special fields in qh entries or (c) split iso entries.  TTs will
 * buffer low/full speed data so the host collects it at high speed.
 */

/*-------------------------------------------------------------------------*/

/* fill a qtd, returning how much of the buffer we were able to queue up */

static int
qtd_fill (struct ehci_qtd *qtd, dma_addr_t buf, size_t len, int token)
{
	int	i, count;
	u64	addr = buf;

	/* one buffer entry per 4K ... first might be short or unaligned */
	qtd->hw_buf [0] = cpu_to_le32 ((u32)addr);
	qtd->hw_buf_hi [0] = cpu_to_le32 ((u32)(addr >> 32));
	count = 0x1000 - (buf & 0x0fff);	/* rest of that page */
	if (likely (len < count))		/* ... iff needed */
		count = len;
	else {
		buf +=  0x1000;
		buf &= ~0x0fff;

		/* per-qtd limit: from 16K to 20K (best alignment) */
		for (i = 1; count < len && i < 5; i++) {
			addr = buf;
			qtd->hw_buf [i] = cpu_to_le32 ((u32)addr);
			qtd->hw_buf_hi [i] = cpu_to_le32 ((u32)(addr >> 32));
			buf += 0x1000;
			if ((count + 0x1000) < len)
				count += 0x1000;
			else
				count = len;
		}
	}
	qtd->hw_token = cpu_to_le32 ((count << 16) | token);
	qtd->length = count;

#if 0
	vdbg ("  qtd_fill %p, token %8x bytes %d dma %x",
		qtd, le32_to_cpu (qtd->hw_token), count, qtd->hw_buf [0]);
#endif

	return count;
}

/*-------------------------------------------------------------------------*/

/* update halted (but potentially linked) qh */

static inline void qh_update (struct ehci_qh *qh, struct ehci_qtd *qtd)
{
	qh->hw_current = 0;
	qh->hw_qtd_next = QTD_NEXT (qtd->qtd_dma);
	qh->hw_alt_next = EHCI_LIST_END;

	/* HC must see latest qtd and qh data before we clear ACTIVE+HALT */
	wmb ();
	qh->hw_token &= __constant_cpu_to_le32 (QTD_TOGGLE | QTD_STS_PING);
}

/*-------------------------------------------------------------------------*/

static inline void qtd_copy_status (struct urb *urb, size_t length, u32 token)
{
	/* count IN/OUT bytes, not SETUP (even short packets) */
	if (likely (QTD_PID (token) != 2))
		urb->actual_length += length - QTD_LENGTH (token);

	/* don't modify error codes */
	if (unlikely (urb->status == -EINPROGRESS && (token & QTD_STS_HALT))) {
		if (token & QTD_STS_BABBLE) {
			/* FIXME "must" disable babbling device's port too */
			urb->status = -EOVERFLOW;
		} else if (token & QTD_STS_MMF) {
			/* fs/ls interrupt xfer missed the complete-split */
			urb->status = -EPROTO;
		} else if (token & QTD_STS_DBE) {
			urb->status = (QTD_PID (token) == 1) /* IN ? */
				? -ENOSR  /* hc couldn't read data */
				: -ECOMM; /* hc couldn't write data */
		} else if (token & QTD_STS_XACT) {
			/* timeout, bad crc, wrong PID, etc; retried */
			if (QTD_CERR (token))
				urb->status = -EPIPE;
			else {
				dbg ("3strikes");
				urb->status = -EPROTO;
			}
		/* CERR nonzero + no errors + halt --> stall */
		} else if (QTD_CERR (token))
			urb->status = -EPIPE;
		else	/* unknown */
			urb->status = -EPROTO;

		dbg ("ep %d-%s qtd token %08x --> status %d",
			/* devpath */
			usb_pipeendpoint (urb->pipe),
			usb_pipein (urb->pipe) ? "in" : "out",
			token, urb->status);

		/* stall indicates some recovery action is needed */
		if (urb->status == -EPIPE) {
			int	pipe = urb->pipe;

			if (!usb_pipecontrol (pipe))
				usb_endpoint_halt (urb->dev,
					usb_pipeendpoint (pipe),
					usb_pipeout (pipe));
			if (urb->dev->tt && !usb_pipeint (pipe)) {
#ifdef DEBUG
				struct usb_device *tt = urb->dev->tt->hub;
				dbg ("clear tt %s-%s p%d buffer, a%d ep%d",
					tt->bus->bus_name, tt->devpath,
    					urb->dev->ttport, urb->dev->devnum,
    					usb_pipeendpoint (pipe));
#endif /* DEBUG */
				usb_hub_tt_clear_buffer (urb->dev, pipe);
			}
		}
	}
}

/* urb->lock ignored from here on (hcd is done with urb) */

static unsigned long ehci_urb_done (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	unsigned long		flags
) {
#ifdef	INTR_AUTOMAGIC
	struct urb		*resubmit = 0;
	struct usb_device	*dev = 0;
#endif

	if (likely (urb->hcpriv != 0)) {
		struct ehci_qh	*qh = (struct ehci_qh *) urb->hcpriv;

		/* S-mask in a QH means it's an interrupt urb */
		if ((qh->hw_info2 & cpu_to_le32 (0x00ff)) != 0) {

			/* ... update hc-wide periodic stats (for usbfs) */
			hcd_to_bus (&ehci->hcd)->bandwidth_int_reqs--;

#ifdef	INTR_AUTOMAGIC
			if (!((urb->status == -ENOENT)
					|| (urb->status == -ECONNRESET))) {
				resubmit = usb_get_urb (urb);
				dev = urb->dev;
			}
#endif
		}
		qh_put (ehci, qh);
		urb->hcpriv = 0;
	}

	if (likely (urb->status == -EINPROGRESS)) {
		if (urb->actual_length != urb->transfer_buffer_length
				&& (urb->transfer_flags & URB_SHORT_NOT_OK))
			urb->status = -EREMOTEIO;
		else
			urb->status = 0;
	}

	/* complete() can reenter this HCD */
	spin_unlock_irqrestore (&ehci->lock, flags);
	usb_hcd_giveback_urb (&ehci->hcd, urb);

#ifdef	INTR_AUTOMAGIC
	if (resubmit && ((urb->status == -ENOENT)
				|| (urb->status == -ECONNRESET))) {
		usb_put_urb (resubmit);
		resubmit = 0;
	}
	// device drivers will soon be doing something like this
	if (resubmit) {
		int	status;

		resubmit->dev = dev;
		status = SUBMIT_URB (resubmit, SLAB_KERNEL);
		if (status != 0)
			err ("can't resubmit interrupt urb %p: status %d",
					resubmit, status);
		usb_put_urb (resubmit);
	}
#endif

	spin_lock_irqsave (&ehci->lock, flags);
	return flags;
}


/*
 * Process and free completed qtds for a qh, returning URBs to drivers.
 * Chases up to qh->hw_current, returns irqsave flags (maybe modified).
 */
static unsigned long
qh_completions (struct ehci_hcd *ehci, struct ehci_qh *qh, unsigned long flags)
{
	struct ehci_qtd		*qtd, *last;
	struct list_head	*next, *qtd_list = &qh->qtd_list;
	int			unlink = 0, halted = 0;

	if (unlikely (list_empty (qtd_list)))
		return flags;

	/* scan QTDs till end of list, or we reach an active one */
	for (qtd = list_entry (qtd_list->next, struct ehci_qtd, qtd_list),
			    	last = 0, next = 0;
			next != qtd_list;
			last = qtd, qtd = list_entry (next,
						struct ehci_qtd, qtd_list)) {
		struct urb	*urb = qtd->urb;
		u32		token = 0;

		/* clean up any state from previous QTD ...*/
		if (last) {
			if (likely (last->urb != urb))
				flags = ehci_urb_done (ehci, last->urb, flags);

			/* qh overlays can have HC's old cached copies of
			 * next qtd ptrs, if an URB was queued afterwards.
			 */
			if (cpu_to_le32 (last->qtd_dma) == qh->hw_current
					&& last->hw_next != qh->hw_qtd_next) {
				qh->hw_alt_next = last->hw_alt_next;
				qh->hw_qtd_next = last->hw_next;
			}

			ehci_qtd_free (ehci, last);
			last = 0;
		}
		next = qtd->qtd_list.next;

		/* QTDs at tail may be active if QH+HC are running,
		 * or when unlinking some urbs queued to this QH
		 */
		token = le32_to_cpu (qtd->hw_token);
		halted = halted
			|| (__constant_cpu_to_le32 (QTD_STS_HALT)
				& qh->hw_token) != 0
			|| (ehci->hcd.state == USB_STATE_HALT)
			|| (qh->qh_state == QH_STATE_IDLE);

		// FIXME Remove the automagic unlink mode.
		// Drivers can now clean up safely; it's their job.
		//
		// FIXME Removing it should fix the short read scenarios
		// with "huge" urb data (more than one 16+KByte td) with
		// the short read someplace other than the last data TD.
		// Except the control case: 'retrigger' status ACKs.

		/* fault: unlink the rest, since this qtd saw an error? */
		if (unlikely ((token & QTD_STS_HALT) != 0)) {
			unlink = 1;
			/* status copied below */

		/* QH halts only because of fault (above) or unlink (here). */
		} else if (unlikely (halted != 0)) {

			/* unlinking everything because of HC shutdown? */
			if (ehci->hcd.state == USB_STATE_HALT) {
				unlink = 1;

			/* explicit unlink, maybe starting here? */
			} else if (qh->qh_state == QH_STATE_IDLE
					&& (urb->status == -ECONNRESET
						|| urb->status == -ESHUTDOWN
						|| urb->status == -ENOENT)) {
				unlink = 1;

			/* QH halted to unlink urbs _after_ this?  */
			} else if (!unlink && (token & QTD_STS_ACTIVE) != 0) {
				qtd = 0;
				continue;
			}

			/* unlink the rest?  once we start unlinking, after
			 * a fault or explicit unlink, we unlink all later
			 * urbs.  usb spec requires that for faults...
			 */
			if (unlink && urb->status == -EINPROGRESS)
				urb->status = -ECONNRESET;

		/* Else QH is active, so we must not modify QTDs
		 * that HC may be working on.  No more qtds to check.
		 */
		} else if (unlikely ((token & QTD_STS_ACTIVE) != 0)) {
			next = qtd_list;
			qtd = 0;
			continue;
		}

		spin_lock (&urb->lock);
		qtd_copy_status (urb, qtd->length, token);
		spin_unlock (&urb->lock);

		list_del (&qtd->qtd_list);

#if 0
		if (urb->status == -EINPROGRESS)
			vdbg ("  qtd %p ok, urb %p, token %8x, len %d",
				qtd, urb, token, urb->actual_length);
		else
			vdbg ("urb %p status %d, qtd %p, token %8x, len %d",
				urb, urb->status, qtd, token,
				urb->actual_length);
#endif
	}

	/* last urb's completion might still need calling */
	if (likely (last != 0)) {
		flags = ehci_urb_done (ehci, last->urb, flags);
		ehci_qtd_free (ehci, last);
	}

	/* reactivate queue after error and driver's cleanup */
	if (unlikely (halted && !list_empty (qtd_list))) {
		qh_update (qh, list_entry (qtd_list->next,
				struct ehci_qtd, qtd_list));
	}

	return flags;
}

/*-------------------------------------------------------------------------*/

/*
 * reverse of qh_urb_transaction:  free a list of TDs.
 * used for cleanup after errors, before HC sees an URB's TDs.
 */
static void qtd_list_free (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list
) {
	struct list_head	*entry, *temp;

	list_for_each_safe (entry, temp, qtd_list) {
		struct ehci_qtd	*qtd;

		qtd = list_entry (entry, struct ehci_qtd, qtd_list);
		list_del (&qtd->qtd_list);
		ehci_qtd_free (ehci, qtd);
	}
}

/*
 * create a list of filled qtds for this URB; won't link into qh.
 */
static struct list_head *
qh_urb_transaction (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*head,
	int			flags
) {
	struct ehci_qtd		*qtd, *qtd_prev;
	dma_addr_t		buf;
	int			len, maxpacket;
	int			is_input, status_patch = 0;
	u32			token;

	/*
	 * URBs map to sequences of QTDs:  one logical transaction
	 */
	qtd = ehci_qtd_alloc (ehci, flags);
	if (unlikely (!qtd))
		return 0;
	qtd_prev = 0;
	list_add_tail (&qtd->qtd_list, head);
	qtd->urb = urb;

	token = QTD_STS_ACTIVE;
	token |= (EHCI_TUNE_CERR << 10);
	/* for split transactions, SplitXState initialized to zero */

	if (usb_pipecontrol (urb->pipe)) {
		/* SETUP pid */
		qtd_fill (qtd, urb->setup_dma, sizeof (struct usb_ctrlrequest),
			token | (2 /* "setup" */ << 8));

		/* ... and always at least one more pid */
		token ^= QTD_TOGGLE;
		qtd_prev = qtd;
		qtd = ehci_qtd_alloc (ehci, flags);
		if (unlikely (!qtd))
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT (qtd->qtd_dma);
		list_add_tail (&qtd->qtd_list, head);

		if (!(urb->transfer_flags & URB_SHORT_NOT_OK))
			status_patch = 1;
	} 

	/*
	 * data transfer stage:  buffer setup
	 */
	len = urb->transfer_buffer_length;
	is_input = usb_pipein (urb->pipe);
	if (likely (len > 0))
		buf = urb->transfer_dma;
	else
		buf = 0;

	if (!buf || is_input)
		token |= (1 /* "in" */ << 8);
	/* else it's already initted to "out" pid (0 << 8) */

	maxpacket = usb_maxpacket (urb->dev, urb->pipe, !is_input) & 0x03ff;

	/*
	 * buffer gets wrapped in one or more qtds;
	 * last one may be "short" (including zero len)
	 * and may serve as a control status ack
	 */
	for (;;) {
		int this_qtd_len;

		qtd->urb = urb;
		this_qtd_len = qtd_fill (qtd, buf, len, token);
		len -= this_qtd_len;
		buf += this_qtd_len;

		/* qh makes control packets use qtd toggle; maybe switch it */
		if ((maxpacket & (this_qtd_len + (maxpacket - 1))) == 0)
			token ^= QTD_TOGGLE;

		if (likely (len <= 0))
			break;

		qtd_prev = qtd;
		qtd = ehci_qtd_alloc (ehci, flags);
		if (unlikely (!qtd))
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT (qtd->qtd_dma);
		list_add_tail (&qtd->qtd_list, head);
	}

	/*
	 * control requests may need a terminating data "status" ack;
	 * bulk ones may need a terminating short packet (zero length).
	 */
	if (likely (buf != 0)) {
		int	one_more = 0;

		if (usb_pipecontrol (urb->pipe)) {
			one_more = 1;
			token ^= 0x0100;	/* "in" <--> "out"  */
			token |= QTD_TOGGLE;	/* force DATA1 */
		} else if (usb_pipebulk (urb->pipe)
				&& (urb->transfer_flags & USB_ZERO_PACKET)
				&& !(urb->transfer_buffer_length % maxpacket)) {
			one_more = 1;
		}
		if (one_more) {
			qtd_prev = qtd;
			qtd = ehci_qtd_alloc (ehci, flags);
			if (unlikely (!qtd))
				goto cleanup;
			qtd->urb = urb;
			qtd_prev->hw_next = QTD_NEXT (qtd->qtd_dma);
			list_add_tail (&qtd->qtd_list, head);

			/* never any data in such packets */
			qtd_fill (qtd, 0, 0, token);
		}
	}

	/* if we're permitting a short control read, we want the hardware to
	 * just continue after short data and send the status ack.  it can do
	 * that on the last data packet (typically the only one).  for other
	 * packets, software fixup is needed (in qh_completions).
	 */
	if (status_patch) {
		struct ehci_qtd		*prev;

		prev = list_entry (qtd->qtd_list.prev,
				struct ehci_qtd, qtd_list);
		prev->hw_alt_next = QTD_NEXT (qtd->qtd_dma);
	}

	/* by default, enable interrupt on urb completion */
	if (likely (!(urb->transfer_flags & URB_NO_INTERRUPT)))
		qtd->hw_token |= __constant_cpu_to_le32 (QTD_IOC);
	return head;

cleanup:
	qtd_list_free (ehci, urb, head);
	return 0;
}

/*-------------------------------------------------------------------------*/

/*
 * Hardware maintains data toggle (like OHCI) ... here we (re)initialize
 * the hardware data toggle in the QH, and set the pseudo-toggle in udev
 * so we can see if usb_clear_halt() was called.  NOP for control, since
 * we set up qh->hw_info1 to always use the QTD toggle bits. 
 */
static inline void
clear_toggle (struct usb_device *udev, int ep, int is_out, struct ehci_qh *qh)
{
	vdbg ("clear toggle, dev %d ep 0x%x-%s",
		udev->devnum, ep, is_out ? "out" : "in");
	qh->hw_token &= ~__constant_cpu_to_le32 (QTD_TOGGLE);
	usb_settoggle (udev, ep, is_out, 1);
}

// Would be best to create all qh's from config descriptors,
// when each interface/altsetting is established.  Unlink
// any previous qh and cancel its urbs first; endpoints are
// implicitly reset then (data toggle too).
// That'd mean updating how usbcore talks to HCDs. (2.5?)


// high bandwidth multiplier, as encoded in highspeed endpoint descriptors
#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))
// ... and packet size, for any kind of endpoint descriptor
#define max_packet(wMaxPacketSize) ((wMaxPacketSize) & 0x03ff)

/*
 * Each QH holds a qtd list; a QH is used for everything except iso.
 *
 * For interrupt urbs, the scheduler must set the microframe scheduling
 * mask(s) each time the QH gets scheduled.  For highspeed, that's
 * just one microframe in the s-mask.  For split interrupt transactions
 * there are additional complications: c-mask, maybe FSTNs.
 */
static struct ehci_qh *
ehci_qh_make (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list,
	int			flags
) {
	struct ehci_qh		*qh = ehci_qh_alloc (ehci, flags);
	u32			info1 = 0, info2 = 0;
	int			is_input, type;
	int			maxp = 0;

	if (!qh)
		return qh;

	/*
	 * init endpoint/device data for this QH
	 */
	info1 |= usb_pipeendpoint (urb->pipe) << 8;
	info1 |= usb_pipedevice (urb->pipe) << 0;

	is_input = usb_pipein (urb->pipe);
	type = usb_pipetype (urb->pipe);
	maxp = usb_maxpacket (urb->dev, urb->pipe, !is_input);

	/* Compute interrupt scheduling parameters just once, and save.
	 * - allowing for high bandwidth, how many nsec/uframe are used?
	 * - split transactions need a second CSPLIT uframe; same question
	 * - splits also need a schedule gap (for full/low speed I/O)
	 * - qh has a polling interval
	 *
	 * For control/bulk requests, the HC or TT handles these.
	 */
	if (type == PIPE_INTERRUPT) {
		qh->usecs = usb_calc_bus_time (USB_SPEED_HIGH, is_input, 0,
				hb_mult (maxp) * max_packet (maxp));
		qh->start = NO_FRAME;

		if (urb->dev->speed == USB_SPEED_HIGH) {
			qh->c_usecs = 0;
			qh->gap_uf = 0;

			/* FIXME handle HS periods of less than 1 frame. */
			qh->period = urb->interval >> 3;
			if (qh->period < 1) {
				dbg ("intr period %d uframes, NYET!",
						urb->interval);
				qh = 0;
				goto done;
			}
		} else {
			/* gap is f(FS/LS transfer times) */
			qh->gap_uf = 1 + usb_calc_bus_time (urb->dev->speed,
					is_input, 0, maxp) / (125 * 1000);

			/* FIXME this just approximates SPLIT/CSPLIT times */
			if (is_input) {		// SPLIT, gap, CSPLIT+DATA
				qh->c_usecs = qh->usecs + HS_USECS (0);
				qh->usecs = HS_USECS (1);
			} else {		// SPLIT+DATA, gap, CSPLIT
				qh->usecs += HS_USECS (1);
				qh->c_usecs = HS_USECS (0);
			}

			qh->period = urb->interval;
		}
	}

	/* using TT? */
	switch (urb->dev->speed) {
	case USB_SPEED_LOW:
		info1 |= (1 << 12);	/* EPS "low" */
		/* FALL THROUGH */

	case USB_SPEED_FULL:
		/* EPS 0 means "full" */
		info1 |= (EHCI_TUNE_RL_TT << 28);
		if (type == PIPE_CONTROL) {
			info1 |= (1 << 27);	/* for TT */
			info1 |= 1 << 14;	/* toggle from qtd */
		}
		info1 |= maxp << 16;

		info2 |= (EHCI_TUNE_MULT_TT << 30);
		info2 |= urb->dev->ttport << 23;
		info2 |= urb->dev->tt->hub->devnum << 16;

		/* NOTE:  if (PIPE_INTERRUPT) { scheduler sets c-mask } */

		break;

	case USB_SPEED_HIGH:		/* no TT involved */
		info1 |= (2 << 12);	/* EPS "high" */
		info1 |= (EHCI_TUNE_RL_HS << 28);
		if (type == PIPE_CONTROL) {
			info1 |= 64 << 16;	/* usb2 fixed maxpacket */
			info1 |= 1 << 14;	/* toggle from qtd */
			info2 |= (EHCI_TUNE_MULT_HS << 30);
		} else if (type == PIPE_BULK) {
			info1 |= 512 << 16;	/* usb2 fixed maxpacket */
			info2 |= (EHCI_TUNE_MULT_HS << 30);
		} else {		/* PIPE_INTERRUPT */
			info1 |= max_packet (maxp) << 16;
			info2 |= hb_mult (maxp) << 30;
		}
		break;
	default:
 		dbg ("bogus dev %p speed %d", urb->dev, urb->dev->speed);
 		return 0;
	}

	/* NOTE:  if (PIPE_INTERRUPT) { scheduler sets s-mask } */

	qh->qh_state = QH_STATE_IDLE;
	qh->hw_info1 = cpu_to_le32 (info1);
	qh->hw_info2 = cpu_to_le32 (info2);

	/* initialize sw and hw queues with these qtds */
	if (!list_empty (qtd_list)) {
		list_splice (qtd_list, &qh->qtd_list);
		qh_update (qh, list_entry (qtd_list->next, struct ehci_qtd, qtd_list));
	} else {
		qh->hw_qtd_next = qh->hw_alt_next = EHCI_LIST_END;
	}

	/* initialize data toggle state */
	clear_toggle (urb->dev, usb_pipeendpoint (urb->pipe), !is_input, qh);

done:
	return qh;
}
#undef hb_mult
#undef hb_packet

/*-------------------------------------------------------------------------*/

/* move qh (and its qtds) onto async queue; maybe enable queue.  */

static void qh_link_async (struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	u32		dma = QH_NEXT (qh->qh_dma);
	struct ehci_qh	*q;

	if (unlikely (!(q = ehci->async))) {
		u32	cmd = readl (&ehci->regs->command);

		/* in case a clear of CMD_ASE didn't take yet */
		(void) handshake (&ehci->regs->status, STS_ASS, 0, 150);

		qh->hw_info1 |= __constant_cpu_to_le32 (QH_HEAD); /* [4.8] */
		qh->qh_next.qh = qh;
		qh->hw_next = dma;
		wmb ();
		ehci->async = qh;
		writel ((u32)qh->qh_dma, &ehci->regs->async_next);
		cmd |= CMD_ASE | CMD_RUN;
		writel (cmd, &ehci->regs->command);
		ehci->hcd.state = USB_STATE_RUNNING;
		/* posted write need not be known to HC yet ... */
	} else {
		/* splice right after "start" of ring */
		qh->hw_info1 &= ~__constant_cpu_to_le32 (QH_HEAD); /* [4.8] */
		qh->qh_next = q->qh_next;
		qh->hw_next = q->hw_next;
		wmb ();
		q->qh_next.qh = qh;
		q->hw_next = dma;
	}
	qh->qh_state = QH_STATE_LINKED;
	/* qtd completions reported later by interrupt */
}

/*-------------------------------------------------------------------------*/

/*
 * For control/bulk/interrupt, return QH with these TDs appended.
 * Allocates and initializes the QH if necessary.
 * Returns null if it can't allocate a QH it needs to.
 * If the QH has TDs (urbs) already, that's great.
 */
static struct ehci_qh *qh_append_tds (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list,
	int			epnum,
	void			**ptr
)
{
	struct ehci_qh		*qh = 0;

	qh = (struct ehci_qh *) *ptr;
	if (likely (qh != 0)) {
		struct ehci_qtd	*qtd;

		if (unlikely (list_empty (qtd_list)))
			qtd = 0;
		else
			qtd = list_entry (qtd_list->next, struct ehci_qtd,
					qtd_list);

		/* maybe patch the qh used for set_address */
		if (unlikely (epnum == 0
				&& le32_to_cpu (qh->hw_info1 & 0x7f) == 0))
			qh->hw_info1 |= cpu_to_le32 (usb_pipedevice(urb->pipe));

		/* append to tds already queued to this qh? */
		if (unlikely (!list_empty (&qh->qtd_list) && qtd)) {
			struct ehci_qtd		*last_qtd;
			int			short_rx = 0;
			u32			hw_next;

			/* update the last qtd's "next" pointer */
			// dbg_qh ("non-empty qh", ehci, qh);
			last_qtd = list_entry (qh->qtd_list.prev,
					struct ehci_qtd, qtd_list);
			hw_next = QTD_NEXT (qtd->qtd_dma);
			last_qtd->hw_next = hw_next;

			/* previous urb allows short rx? maybe optimize. */
			if (!(last_qtd->urb->transfer_flags & URB_SHORT_NOT_OK)
					&& (epnum & 0x10)) {
				// only the last QTD for now
				last_qtd->hw_alt_next = hw_next;
				short_rx = 1;
			}

			/* Adjust any old copies in qh overlay too.
			 * Interrupt code must cope with case of HC having it
			 * cached, and clobbering these updates.
			 * ... complicates getting rid of extra interrupts!
			 * (Or:  use dummy td, so cache always stays valid.)
			 */
			if (qh->hw_current == cpu_to_le32 (last_qtd->qtd_dma)) {
				wmb ();
				qh->hw_qtd_next = hw_next;
				if (short_rx)
					qh->hw_alt_next = hw_next
				    		| (qh->hw_alt_next & 0x1e);
				vdbg ("queue to qh %p, patch", qh);
			}

		/* no URB queued */
		} else {
			// dbg_qh ("empty qh", ehci, qh);

			/* NOTE: we already canceled any queued URBs
			 * when the endpoint halted.
			 */

			/* usb_clear_halt() means qh data toggle gets reset */
			if (unlikely (!usb_gettoggle (urb->dev,
						(epnum & 0x0f),
						!(epnum & 0x10)))) {
				clear_toggle (urb->dev,
					epnum & 0x0f, !(epnum & 0x10), qh);
			}
			if (qtd)
				qh_update (qh, qtd);
		}
		list_splice (qtd_list, qh->qtd_list.prev);

	} else {
		/* can't sleep here, we have ehci->lock... */
		qh = ehci_qh_make (ehci, urb, qtd_list, SLAB_ATOMIC);
		// if (qh) dbg_qh ("new qh", ehci, qh);
		*ptr = qh;
	}
	if (qh)
		urb->hcpriv = qh_get (qh);
	return qh;
}

/*-------------------------------------------------------------------------*/

static int
submit_async (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list,
	int			mem_flags
) {
	struct ehci_qtd		*qtd;
	struct hcd_dev		*dev;
	int			epnum;
	unsigned long		flags;
	struct ehci_qh		*qh = 0;

	qtd = list_entry (qtd_list->next, struct ehci_qtd, qtd_list);
	dev = (struct hcd_dev *)urb->dev->hcpriv;
	epnum = usb_pipeendpoint (urb->pipe);
	if (usb_pipein (urb->pipe) && !usb_pipecontrol (urb->pipe))
		epnum |= 0x10;

	vdbg ("%s: submit_async urb %p len %d ep %d-%s qtd %p [qh %p]",
		hcd_to_bus (&ehci->hcd)->bus_name,
		urb, urb->transfer_buffer_length,
		epnum & 0x0f, (epnum & 0x10) ? "in" : "out",
		qtd, dev ? dev->ep [epnum] : (void *)~0);

	spin_lock_irqsave (&ehci->lock, flags);
	qh = qh_append_tds (ehci, urb, qtd_list, epnum, &dev->ep [epnum]);

	/* Control/bulk operations through TTs don't need scheduling,
	 * the HC and TT handle it when the TT has a buffer ready.
	 */
	if (likely (qh != 0)) {
		if (likely (qh->qh_state == QH_STATE_IDLE))
			qh_link_async (ehci, qh_get (qh));
	}
	spin_unlock_irqrestore (&ehci->lock, flags);
	if (unlikely (qh == 0)) {
		qtd_list_free (ehci, urb, qtd_list);
		return -ENOMEM;
	}
	return 0;
}

/*-------------------------------------------------------------------------*/

/* the async qh for the qtds being reclaimed are now unlinked from the HC */
/* caller must not own ehci->lock */

static unsigned long
end_unlink_async (struct ehci_hcd *ehci, unsigned long flags)
{
	struct ehci_qh		*qh = ehci->reclaim;

	del_timer (&ehci->watchdog);

	qh->qh_state = QH_STATE_IDLE;
	qh->qh_next.qh = 0;
	qh_put (ehci, qh);			// refcount from reclaim 
	ehci->reclaim = 0;
	ehci->reclaim_ready = 0;

	flags = qh_completions (ehci, qh, flags);

	if (!list_empty (&qh->qtd_list)
			&& HCD_IS_RUNNING (ehci->hcd.state))
		qh_link_async (ehci, qh);
	else
		qh_put (ehci, qh);		// refcount from async list

	return flags;
}


/* makes sure the async qh will become idle */
/* caller must own ehci->lock */

static void start_unlink_async (struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	int		cmd = readl (&ehci->regs->command);
	struct ehci_qh	*prev;

#ifdef DEBUG
	if (ehci->reclaim
			|| !ehci->async
			|| qh->qh_state != QH_STATE_LINKED
#ifdef CONFIG_SMP
// this macro lies except on SMP compiles
			|| !spin_is_locked (&ehci->lock)
#endif
			)
		BUG ();
#endif

	qh->qh_state = QH_STATE_UNLINK;
	ehci->reclaim = qh = qh_get (qh);

	// dbg_qh ("start unlink", ehci, qh);

	/* Remove the last QH (qhead)?  Stop async schedule first. */
	if (unlikely (qh == ehci->async && qh->qh_next.qh == qh)) {
		/* can't get here without STS_ASS set */
		if (ehci->hcd.state != USB_STATE_HALT) {
			if (cmd & CMD_PSE) {
				writel (cmd & ~CMD_ASE, &ehci->regs->command);
				(void) handshake (&ehci->regs->status,
						  STS_ASS, 0, 150);
			} else
				ehci_ready (ehci);
		}
		qh->qh_next.qh = ehci->async = 0;

		ehci->reclaim_ready = 1;
		tasklet_schedule (&ehci->tasklet);
		return;
	} 

	if (unlikely (ehci->hcd.state == USB_STATE_HALT)) {
		ehci->reclaim_ready = 1;
		tasklet_schedule (&ehci->tasklet);
		return;
	}

	prev = ehci->async;
	while (prev->qh_next.qh != qh && prev->qh_next.qh != ehci->async)
		prev = prev->qh_next.qh;

	if (qh->hw_info1 & __constant_cpu_to_le32 (QH_HEAD)) {
		ehci->async = prev;
		prev->hw_info1 |= __constant_cpu_to_le32 (QH_HEAD);
	}
	prev->hw_next = qh->hw_next;
	prev->qh_next = qh->qh_next;
	wmb ();

	ehci->reclaim_ready = 0;
	cmd |= CMD_IAAD;
	writel (cmd, &ehci->regs->command);
	/* posted write need not be known to HC yet ... */

	mod_timer (&ehci->watchdog, jiffies + EHCI_WATCHDOG_JIFFIES);
}

/*-------------------------------------------------------------------------*/

static unsigned long
scan_async (struct ehci_hcd *ehci, unsigned long flags)
{
	struct ehci_qh		*qh;

rescan:
	qh = ehci->async;
	if (likely (qh != 0)) {
		do {
			/* clean any finished work for this qh */
			if (!list_empty (&qh->qtd_list)) {
				// dbg_qh ("scan_async", ehci, qh);
				qh = qh_get (qh);

				/* concurrent unlink could happen here */
				flags = qh_completions (ehci, qh, flags);
				qh_put (ehci, qh);
			}

			/* unlink idle entries, reducing HC PCI usage as
			 * well as HCD schedule-scanning costs
			 */
			if (list_empty (&qh->qtd_list) && !ehci->reclaim) {
				if (qh->qh_next.qh != qh) {
					// dbg ("irq/empty");
					start_unlink_async (ehci, qh);
				} else {
					// FIXME:  arrange to stop
					// after it's been idle a while.
					// stop/restart isn't free...
				}
			}
			qh = qh->qh_next.qh;
			if (!qh)		/* unlinked? */
				goto rescan;
		} while (qh != ehci->async);
	}
	return flags;
}
