/*
 * Copyright (c) 2001 by David Brownell
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
 * EHCI scheduled transaction support:  interrupt, iso, split iso
 * These are called "periodic" transactions in the EHCI spec.
 */

/*
 * Ceiling microseconds (typical) for that many bytes at high speed
 * ISO is a bit less, no ACK ... from USB 2.0 spec, 5.11.3 (and needed
 * to preallocate bandwidth)
 */
#define EHCI_HOST_DELAY	5	/* nsec, guess */
#define HS_USECS(bytes) NS_TO_US ( ((55 * 8 * 2083)/1000) \
	+ ((2083UL * (3167 + BitTime (bytes)))/1000) \
	+ EHCI_HOST_DELAY)
#define HS_USECS_ISO(bytes) NS_TO_US ( ((long)(38 * 8 * 2.083)) \
	+ ((2083UL * (3167 + BitTime (bytes)))/1000) \
	+ EHCI_HOST_DELAY)
	
static int ehci_get_frame (struct usb_hcd *hcd);

/*-------------------------------------------------------------------------*/

/*
 * periodic_next_shadow - return "next" pointer on shadow list
 * @periodic: host pointer to qh/itd/sitd
 * @tag: hardware tag for type of this record
 */
static union ehci_shadow *
periodic_next_shadow (union ehci_shadow *periodic, int tag)
{
	switch (tag) {
	case Q_TYPE_QH:
		return &periodic->qh->qh_next;
	case Q_TYPE_FSTN:
		return &periodic->fstn->fstn_next;
#ifdef have_iso
	case Q_TYPE_ITD:
		return &periodic->itd->itd_next;
	case Q_TYPE_SITD:
		return &periodic->sitd->sitd_next;
#endif /* have_iso */
	}
	dbg ("BAD shadow %p tag %d", periodic->ptr, tag);
	// BUG ();
	return 0;
}

/* returns true after successful unlink */
/* caller must hold ehci->lock */
static int periodic_unlink (struct ehci_hcd *ehci, unsigned frame, void *ptr)
{
	union ehci_shadow	*prev_p = &ehci->pshadow [frame];
	u32			*hw_p = &ehci->periodic [frame];
	union ehci_shadow	here = *prev_p;
	union ehci_shadow	*next_p;

	/* find predecessor of "ptr"; hw and shadow lists are in sync */
	while (here.ptr && here.ptr != ptr) {
		prev_p = periodic_next_shadow (prev_p, Q_NEXT_TYPE (*hw_p));
		hw_p = &here.qh->hw_next;
		here = *prev_p;
	}
	/* an interrupt entry (at list end) could have been shared */
	if (!here.ptr) {
		dbg ("entry %p no longer on frame [%d]", ptr, frame);
		return 0;
	}
	// vdbg ("periodic unlink %p from frame %d", ptr, frame);

	/* update hardware list ... HC may still know the old structure, so
	 * don't change hw_next until it'll have purged its cache
	 */
	next_p = periodic_next_shadow (&here, Q_NEXT_TYPE (*hw_p));
	*hw_p = here.qh->hw_next;

	/* unlink from shadow list; HCD won't see old structure again */
	*prev_p = *next_p;
	next_p->ptr = 0;

	return 1;
}

/* how many of the uframe's 125 usecs are allocated? */
static unsigned short
periodic_usecs (struct ehci_hcd *ehci, unsigned frame, unsigned uframe)
{
	u32			*hw_p = &ehci->periodic [frame];
	union ehci_shadow	*q = &ehci->pshadow [frame];
	unsigned		usecs = 0;
#ifdef have_iso
	u32			temp = 0;
#endif

	while (q->ptr) {
		switch (Q_NEXT_TYPE (*hw_p)) {
		case Q_TYPE_QH:
			/* is it in the S-mask? */
			if (q->qh->hw_info2 & cpu_to_le32 (1 << uframe))
				usecs += q->qh->usecs;
			q = &q->qh->qh_next;
			break;
		case Q_TYPE_FSTN:
			/* for "save place" FSTNs, count the relevant INTR
			 * bandwidth from the previous frame
			 */
			if (q->fstn->hw_prev != EHCI_LIST_END) {
				dbg ("not counting FSTN bandwidth yet ...");
			}
			q = &q->fstn->fstn_next;
			break;
#ifdef have_iso
		case Q_TYPE_ITD:
			temp = le32_to_cpu (q->itd->transaction [uframe]);
			temp >>= 16;
			temp &= 0x0fff;
			if (temp)
				usecs += HS_USECS_ISO (temp);
			q = &q->itd->itd_next;
			break;
		case Q_TYPE_SITD:
			temp = q->sitd->hw_fullspeed_ep &
				__constant_cpu_to_le32 (1 << 31);

			// FIXME:  this doesn't count data bytes right...

			/* is it in the S-mask?  (count SPLIT, DATA) */
			if (q->sitd->hw_uframe & cpu_to_le32 (1 << uframe)) {
				if (temp)
					usecs += HS_USECS (188);
				else
					usecs += HS_USECS (1);
			}

			/* ... C-mask?  (count CSPLIT, DATA) */
			if (q->sitd->hw_uframe &
					cpu_to_le32 (1 << (8 + uframe))) {
				if (temp)
					usecs += HS_USECS (0);
				else
					usecs += HS_USECS (188);
			}
			q = &q->sitd->sitd_next;
			break;
#endif /* have_iso */
		default:
			BUG ();
		}
	}
#ifdef	DEBUG
	if (usecs > 100)
		err ("overallocated uframe %d, periodic is %d usecs",
			frame * 8 + uframe, usecs);
#endif
	return usecs;
}

/*-------------------------------------------------------------------------*/

static void intr_deschedule (
	struct ehci_hcd	*ehci,
	unsigned	frame,
	struct ehci_qh	*qh,
	unsigned	period
) {
	unsigned long	flags;

	spin_lock_irqsave (&ehci->lock, flags);

	do {
		periodic_unlink (ehci, frame, qh);
		qh_unput (ehci, qh);
		frame += period;
	} while (frame < ehci->periodic_size);

	qh->qh_state = QH_STATE_UNLINK;
	qh->qh_next.ptr = 0;
	ehci->periodic_urbs--;

	/* maybe turn off periodic schedule */
	if (!ehci->periodic_urbs) {
		u32	cmd = readl (&ehci->regs->command);

		/* did setting PSE not take effect yet?
		 * takes effect only at frame boundaries...
		 */
		while (!(readl (&ehci->regs->status) & STS_PSS))
			udelay (20);

		cmd &= ~CMD_PSE;
		writel (cmd, &ehci->regs->command);
		/* posted write ... */

		ehci->next_frame = -1;
	} else
		vdbg ("periodic schedule still enabled");

	spin_unlock_irqrestore (&ehci->lock, flags);

	/*
	 * If the hc may be looking at this qh, then delay a uframe
	 * (yeech!) to be sure it's done.
	 * No other threads may be mucking with this qh.
	 */
	if (((ehci_get_frame (&ehci->hcd) - frame) % period) == 0)
		udelay (125);

	qh->qh_state = QH_STATE_IDLE;
	qh->hw_next = EHCI_LIST_END;

	vdbg ("descheduled qh %p, per = %d frame = %d count = %d, urbs = %d",
		qh, period, frame,
		atomic_read (&qh->refcount), ehci->periodic_urbs);
}

static int intr_submit (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list,
	int			mem_flags
) {
	unsigned		epnum, period;
	unsigned		temp;
	unsigned short		mult, usecs;
	unsigned long		flags;
	struct ehci_qh		*qh;
	struct hcd_dev		*dev;
	int			status = 0;

	/* get endpoint and transfer data */
	epnum = usb_pipeendpoint (urb->pipe);
	if (usb_pipein (urb->pipe)) {
		temp = urb->dev->epmaxpacketin [epnum];
		epnum |= 0x10;
	} else
		temp = urb->dev->epmaxpacketout [epnum];
	mult = 1;
	if (urb->dev->speed == USB_SPEED_HIGH) {
		/* high speed "high bandwidth" is coded in ep maxpacket */
		mult += (temp >> 11) & 0x03;
		temp &= 0x03ff;
	} else {
		dbg ("no intr/tt scheduling yet"); 
		status = -ENOSYS;
		goto done;
	}

	/*
	 * NOTE: current completion/restart logic doesn't handle more than
	 * one qtd in a periodic qh ... 16-20 KB/urb is pretty big for this.
	 * such big requests need many periods to transfer.
	 */
	if (unlikely (qtd_list->next != qtd_list->prev)) {
		dbg ("only one intr qtd per urb allowed"); 
		status = -EINVAL;
		goto done;
	}

	usecs = HS_USECS (urb->transfer_buffer_length);

	/*
	 * force a power-of-two (frames) sized polling interval
	 *
	 * NOTE: endpoint->bInterval for highspeed is measured in uframes,
	 * while for full/low speeds it's in frames.  Here we "know" that
	 * urb->interval doesn't give acccess to high interrupt rates.
	 */
	period = ehci->periodic_size;
	temp = period;
	if (unlikely (urb->interval < 1))
		urb->interval = 1;
	while (temp > urb->interval)
		temp >>= 1;
	period = urb->interval = temp;

	spin_lock_irqsave (&ehci->lock, flags);

	/* get the qh (must be empty and idle) */
	dev = (struct hcd_dev *)urb->dev->hcpriv;
	qh = (struct ehci_qh *) dev->ep [epnum];
	if (qh) {
		/* only allow one queued interrupt urb per EP */
		if (unlikely (qh->qh_state != QH_STATE_IDLE
				|| !list_empty (&qh->qtd_list))) {
			dbg ("interrupt urb already queued");
			status = -EBUSY;
		} else {
			/* maybe reset hardware's data toggle in the qh */
			if (unlikely (!usb_gettoggle (urb->dev, epnum & 0x0f,
					!(epnum & 0x10)))) {
				qh->hw_token |=
					__constant_cpu_to_le32 (QTD_TOGGLE);
				usb_settoggle (urb->dev, epnum & 0x0f,
					!(epnum & 0x10), 1);
			}
			/* trust the QH was set up as interrupt ... */
			list_splice (qtd_list, &qh->qtd_list);
			qh_update (qh, list_entry (qtd_list->next,
						struct ehci_qtd, qtd_list));
		}
	} else {
		/* can't sleep here, we have ehci->lock... */
		qh = ehci_qh_make (ehci, urb, qtd_list, SLAB_ATOMIC);
		qtd_list = &qh->qtd_list;
		if (likely (qh != 0)) {
			// dbg ("new INTR qh %p", qh);
			dev->ep [epnum] = qh;
		} else
			status = -ENOMEM;
	}

	/* Schedule this periodic QH. */
	if (likely (status == 0)) {
		unsigned	frame = urb->interval;

		qh->hw_next = EHCI_LIST_END;
		qh->hw_info2 |= cpu_to_le32 (mult << 30);
		qh->usecs = usecs;

		urb->hcpriv = qh_put (qh);
		status = -ENOSPC;

		/* pick a set of schedule slots, link the QH into them */
		do {
			int	uframe;

			/* Select some frame 0..(urb->interval - 1) with a
			 * microframe that can hold this transaction.
			 *
			 * FIXME for TT splits, need uframes for start and end.
			 * FSTNs can put end into next frame (uframes 0 or 1).
			 */
			frame--;
			for (uframe = 0; uframe < 8; uframe++) {
				int	claimed;
				claimed = periodic_usecs (ehci, frame, uframe);
				/* 80% periodic == 100 usec max committed */
				if ((claimed + usecs) <= 100) {
					vdbg ("frame %d.%d: %d usecs, plus %d",
						frame, uframe, claimed, usecs);
					break;
				}
			}
			if (uframe == 8)
				continue;
// FIXME delete when code below handles non-empty queues
			if (ehci->pshadow [frame].ptr)
				continue;

			/* QH will run once each period, starting there  */
			urb->start_frame = frame;
			status = 0;

			/* set S-frame mask */
			qh->hw_info2 |= cpu_to_le32 (1 << uframe);
			// dbg_qh ("Schedule INTR qh", ehci, qh);

			/* stuff into the periodic schedule */
			qh->qh_state = QH_STATE_LINKED;
			vdbg ("qh %p usecs %d period %d starting frame %d.%d",
				qh, qh->usecs, period, frame, uframe);
			do {
				if (unlikely (ehci->pshadow [frame].ptr != 0)) {
// FIXME -- just link to the end, before any qh with a shorter period,
// AND handle it already being (implicitly) linked into this frame
					BUG ();
				} else {
					ehci->pshadow [frame].qh = qh_put (qh);
					ehci->periodic [frame] =
						QH_NEXT (qh->qh_dma);
				}
				frame += period;
			} while (frame < ehci->periodic_size);

			/* update bandwidth utilization records (for usbfs) */
			usb_claim_bandwidth (urb->dev, urb, usecs, 0);

			/* maybe enable periodic schedule processing */
			if (!ehci->periodic_urbs++) {
				u32	cmd;

				/* did clearing PSE did take effect yet?
				 * takes effect only at frame boundaries...
				 */
				while (readl (&ehci->regs->status) & STS_PSS)
					udelay (20);

				cmd = readl (&ehci->regs->command) | CMD_PSE;
				writel (cmd, &ehci->regs->command);
				/* posted write ... PSS happens later */
				ehci->hcd.state = USB_STATE_RUNNING;

				/* make sure tasklet scans these */
				ehci->next_frame = ehci_get_frame (&ehci->hcd);
			}
			break;

		} while (frame);
	}
	spin_unlock_irqrestore (&ehci->lock, flags);
done:
	if (status) {
		usb_complete_t	complete = urb->complete;

		urb->complete = 0;
		urb->status = status;
		qh_completions (ehci, qtd_list, 1);
		urb->complete = complete;
	}
	return status;
}

static unsigned long
intr_complete (
	struct ehci_hcd	*ehci,
	unsigned	frame,
	struct ehci_qh	*qh,
	unsigned long	flags		/* caller owns ehci->lock ... */
) {
	struct ehci_qtd	*qtd;
	struct urb	*urb;
	int		unlinking;

	/* nothing to report? */
	if (likely ((qh->hw_token & __constant_cpu_to_le32 (QTD_STS_ACTIVE))
			!= 0))
		return flags;
	
	qtd = list_entry (qh->qtd_list.next, struct ehci_qtd, qtd_list);
	urb = qtd->urb;
	unlinking = (urb->status == -ENOENT) || (urb->status == -ECONNRESET);

	/* call any completions, after patching for reactivation */
	spin_unlock_irqrestore (&ehci->lock, flags);
	/* NOTE:  currently restricted to one qtd per qh! */
	if (qh_completions (ehci, &qh->qtd_list, 0) == 0)
		urb = 0;
	spin_lock_irqsave (&ehci->lock, flags);

	/* never reactivate requests that were unlinked ... */
	if (likely (urb != 0)) {
		if (unlinking
				|| urb->status == -ECONNRESET
				|| urb->status == -ENOENT
				// || (urb->dev == null)
				|| ehci->hcd.state == USB_STATE_HALT)
			urb = 0;
		// FIXME look at all those unlink cases ... we always
		// need exactly one completion that reports unlink.
		// the one above might not have been it!
	}

	/* normally reactivate */
	if (likely (urb != 0)) {
		if (usb_pipeout (urb->pipe))
			pci_dma_sync_single (ehci->hcd.pdev,
				qtd->buf_dma,
				urb->transfer_buffer_length,
				PCI_DMA_TODEVICE);
		urb->status = -EINPROGRESS;
		urb->actual_length = 0;

		/* patch qh and restart */
		qh_update (qh, qtd);
	}
	return flags;
}

/*-------------------------------------------------------------------------*/

#ifdef	have_iso

static inline void itd_free (struct ehci_hcd *ehci, struct ehci_itd *itd)
{
	pci_pool_free (ehci->itd_pool, itd, itd->itd_dma);
}

/*
 * Create itd and allocate into uframes within specified frame.
 * Caller must update the resulting uframe links.
 */
static struct ehci_itd *
itd_make (
	struct ehci_hcd	*ehci,
	struct urb	*urb,
	unsigned	index,		// urb->iso_frame_desc [index]
	unsigned	frame,		// scheduled start
	dma_addr_t	dma,		// mapped transfer buffer
	int		mem_flags
) {
	struct ehci_itd	*itd;
	u64		temp;
	u32		buf1;
	unsigned	epnum, maxp, multi, usecs;
	unsigned	length;
	unsigned	i, bufnum;

	/* allocate itd, start to fill it */
	itd = pci_pool_alloc (ehci->itd_pool, mem_flags, &dma);
	if (!itd)
		return itd;

	itd->hw_next = EHCI_LIST_END;
	itd->urb = urb;
	itd->index = index;
	INIT_LIST_HEAD (&itd->itd_list);
	itd->uframe = (frame * 8) % ehci->periodic_size;

	/* tell itd about the buffer its transfers will consume */
	length = urb->iso_frame_desc [index].length;
	dma += urb->iso_frame_desc [index].offset;
	temp = dma & ~0x0fff;
	for (i = 0; i < 7; i++) {
		itd->hw_bufp [i] = cpu_to_le32 ((u32) temp);
		itd->hw_bufp_hi [i] = cpu_to_le32 ((u32)(temp >> 32));
		temp += 0x0fff;
	}

	/*
	 * this might be a "high bandwidth" highspeed endpoint,
	 * as encoded in the ep descriptor's maxpacket field
	 */
	epnum = usb_pipeendpoint (urb->pipe);
	if (usb_pipein (urb->pipe)) {
		maxp = urb->dev->epmaxpacketin [epnum];
		buf1 = (1 << 11) | maxp;
	} else {
		maxp = urb->dev->epmaxpacketout [epnum];
		buf1 = maxp;
	}
	multi = 1;
	multi += (temp >> 11) & 0x03;
	maxp &= 0x03ff;

	/* "plus" info in low order bits of buffer pointers */
	itd->hw_bufp [0] |= cpu_to_le32 ((epnum << 8) | urb->dev->devnum);
	itd->hw_bufp [1] |= cpu_to_le32 (buf1);
	itd->hw_bufp [2] |= cpu_to_le32 (multi);

	/* schedule as many uframes as needed */
	maxp *= multi;
	usecs = HS_USECS_ISO (maxp);
	bufnum = 0;
	for (i = 0; i < 8; i++) {
		unsigned	t, offset, scratch;

		if (length <= 0) {
			itd->hw_transaction [i] = 0;
			continue;
		}

		/* don't commit more than 80% periodic == 100 usec */
		if ((periodic_usecs (ehci, itd->uframe, i) + usecs) > 100)
			continue;

		/* we'll use this uframe; figure hw_transaction */
		t = EHCI_ISOC_ACTIVE;
		t |= bufnum << 12;		// which buffer?
		offset = temp & 0x0fff;		// offset therein
		t |= offset;
		if ((offset + maxp) >= 4096) 	// hc auto-wraps end-of-"page"
			bufnum++;
		if (length <= maxp) {
			// interrupt only needed at end-of-urb
			if ((index + 1) == urb->number_of_packets)
				t |= EHCI_ITD_IOC;
			scratch = length;
		} else
			scratch = maxp;
		t |= scratch << 16;
		t = cpu_to_le32 (t);

		itd->hw_transaction [i] = itd->transaction [i] = t;
		length -= scratch;
	}
	if (length > 0) {
		dbg ("iso frame too big, urb %p [%d], %d extra (of %d)",
			urb, index, length, urb->iso_frame_desc [index].length);
		itd_free (ehci, itd);
		itd = 0;
	}
	return itd;
}

static inline void
itd_link (struct ehci_hcd *ehci, unsigned frame, struct ehci_itd *itd)
{
	u32		ptr;

	ptr = cpu_to_le32 (itd->itd_dma);	// type 0 == itd
	if (ehci->pshadow [frame].ptr) {
		if (!itd->itd_next.ptr) {
			itd->itd_next = ehci->pshadow [frame];
			itd->hw_next = ehci->periodic [frame];
		} else if (itd->itd_next.ptr != ehci->pshadow [frame].ptr) {
			dbg ("frame %d itd link goof", frame);
			BUG ();
		}
	}
	ehci->pshadow [frame].itd = itd;
	ehci->periodic [frame] = ptr;
}

#define	ISO_ERRS (EHCI_ISOC_BUF_ERR | EHCI_ISOC_BABBLE | EHCI_ISOC_XACTERR)

static unsigned long
itd_complete (struct ehci_hcd *ehci, struct ehci_itd *itd, unsigned long flags)
{
	struct urb		*urb = itd->urb;

	/* if not unlinking: */
	if (!(urb->transfer_flags & EHCI_STATE_UNLINK)
			&& ehci->hcd.state != USB_STATE_HALT) {
		int			i;
		struct usb_iso_packet_descriptor	*desc;
		struct ehci_itd		*first_itd = urb->hcpriv;

		/* update status for this frame's transfers */
		desc = &urb->iso_frame_desc [itd->index];
		desc->status = 0;
		desc->actual_length = 0;
		for (i = 0; i < 8; i++) {
			u32	 t = itd->hw_transaction [i];
			if (t & (ISO_ERRS | EHCI_ISOC_ACTIVE)) {
				if (t & EHCI_ISOC_ACTIVE)
					desc->status = -EXDEV;
				else if (t & EHCI_ISOC_BUF_ERR)
					desc->status = usb_pipein (urb->pipe)
						? -ENOSR  /* couldn't read */
						: -ECOMM; /* couldn't write */
				else if (t & EHCI_ISOC_BABBLE)
					desc->status = -EOVERFLOW;
				else /* (t & EHCI_ISOC_XACTERR) */
					desc->status = -EPROTO;
				break;
			}
			desc->actual_length += EHCI_ITD_LENGTH (t);
		}

		/* handle completion now? */
		if ((itd->index + 1) != urb->number_of_packets)
			return flags;

		i = usb_pipein (urb->pipe);
		if (i)
			pci_dma_sync_single (ehci->hcd.pdev,
				first_itd->buf_dma,
				urb->transfer_buffer_length,
				PCI_DMA_FROMDEVICE);

		/* call completion with no locks; it can unlink ... */
		spin_unlock_irqrestore (&ehci->lock, flags);
		urb->complete (urb);
		spin_lock_irqsave (&ehci->lock, flags);

		/* re-activate this URB? or unlink? */
		if (!(urb->transfer_flags & EHCI_STATE_UNLINK)
				&& ehci->hcd.state != USB_STATE_HALT) {
			if (!i)
				pci_dma_sync_single (ehci->hcd.pdev,
					first_itd->buf_dma,
					urb->transfer_buffer_length,
					PCI_DMA_TODEVICE);

			itd = urb->hcpriv;
			do {
				for (i = 0; i < 8; i++)
					itd->hw_transaction [i]
						= itd->transaction [i];
				itd = list_entry (itd->itd_list.next,
						struct ehci_itd, itd_list);
			} while (itd != urb->hcpriv);
			return flags;
		}

	/* unlink done only on the last itd */
	} else if ((itd->index + 1) != urb->number_of_packets)
		return flags;

	/* we're unlinking ... */

	/* decouple urb from the hcd */
	spin_unlock_irqrestore (&ehci->lock, flags);
	if (ehci->hcd.state == USB_STATE_HALT)
		urb->status = -ESHUTDOWN;
	itd = urb->hcpriv;
	urb->hcpriv = 0;
	ehci_urb_done (ehci, itd->buf_dma, urb);
	spin_lock_irqsave (&ehci->lock, flags);

	/* take itds out of the hc's periodic schedule */
	list_entry (itd->itd_list.prev, struct ehci_itd, itd_list)
		->itd_list.next = 0;
	do {
		struct ehci_itd	*next;

		if (itd->itd_list.next)
			next = list_entry (itd->itd_list.next,
				struct ehci_itd, itd_list);
		else
			next = 0;

		// FIXME:  hc WILL (!) lap us here, if we get behind
		// by 128 msec (or less, with smaller periodic_size).
		// Reading/caching these itds will cause trouble...

		periodic_unlink (ehci, itd->uframe, itd);
		itd_free (ehci, itd);
		itd = next;
	} while (itd);
	return flags;
}

/*-------------------------------------------------------------------------*/

static int itd_submit (struct ehci_hcd *ehci, struct urb *urb)
{
	struct ehci_itd		*first_itd = 0, *itd;
	unsigned		frame_index;
	dma_addr_t		dma;
	unsigned long		flags;

	dbg ("itd_submit");

	/* set up one dma mapping for this urb */
	dma = pci_map_single (ehci->hcd.pdev,
		urb->transfer_buffer, urb->transfer_buffer_length,
		usb_pipein (urb->pipe)
		    ? PCI_DMA_FROMDEVICE
		    : PCI_DMA_TODEVICE);
	if (dma == 0)
		return -ENOMEM;

	/*
	 * Schedule as needed.  This is VERY optimistic about free
	 * bandwidth!  But the API assumes drivers can pick frames
	 * intelligently (how?), so there's no other good option.
	 *
	 * FIXME  this doesn't handle urb->next rings, or try to
	 * use the iso periodicity.
	 */
	if (urb->transfer_flags & USB_ISO_ASAP) { 
		urb->start_frame = ehci_get_frame (&ehci->hcd);
		urb->start_frame++;
	}
	urb->start_frame %= ehci->periodic_size;

	/* create and populate itds (doing uframe scheduling) */
	spin_lock_irqsave (&ehci->lock, flags);
	for (frame_index = 0;
			frame_index < urb->number_of_packets;
			frame_index++) {
		itd = itd_make (ehci, urb, frame_index,
			urb->start_frame + frame_index,
			dma, SLAB_ATOMIC);
		if (itd) {
			if (first_itd)
				list_add_tail (&itd->itd_list,
						&first_itd->itd_list);
			else
				first_itd = itd;
		} else {
			spin_unlock_irqrestore (&ehci->lock, flags);
			if (first_itd) {
				while (!list_empty (&first_itd->itd_list)) {
					itd = list_entry (
						first_itd->itd_list.next,
						struct ehci_itd, itd_list);
					list_del (&itd->itd_list);
					itd_free (ehci, itd);
				}
				itd_free (ehci, first_itd);
			}
			pci_unmap_single (ehci->hcd.pdev,
				dma, urb->transfer_buffer_length,
				usb_pipein (urb->pipe)
				    ? PCI_DMA_FROMDEVICE
				    : PCI_DMA_TODEVICE);
			return -ENOMEM;
		}
	}

	/* stuff into the schedule */
	itd = first_itd;
	do {
		unsigned	i;

		for (i = 0; i < 8; i++) {
			if (!itd->hw_transaction [i])
				continue;
			itd_link (ehci, itd->uframe + i, itd);
		}
		itd = list_entry (itd->itd_list.next,
			struct ehci_itd, itd_list);
	} while (itd != first_itd);
	urb->hcpriv = first_itd;

	spin_unlock_irqrestore (&ehci->lock, flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

/*
 * "Split ISO TDs" ... used for USB 1.1 devices going through
 * the TTs in USB 2.0 hubs.
 */

static inline void
sitd_free (struct ehci_hcd *ehci, struct ehci_sitd *sitd)
{
	pci_pool_free (ehci->sitd_pool, sitd, sitd->sitd_dma);
}

static struct ehci_sitd *
sitd_make (
	struct ehci_hcd	*ehci,
	struct urb	*urb,
	unsigned	index,		// urb->iso_frame_desc [index]
	unsigned	uframe,		// scheduled start
	dma_addr_t	dma,		// mapped transfer buffer
	int		mem_flags
) {
	struct ehci_sitd	*sitd;
	unsigned		length;

	sitd = pci_pool_alloc (ehci->sitd_pool, mem_flags, &dma);
	if (!sitd)
		return sitd;
	sitd->urb = urb;
	length = urb->iso_frame_desc [index].length;
	dma += urb->iso_frame_desc [index].offset;

#if 0
	// FIXME:  do the rest!
#else
	sitd_free (ehci, sitd);
	return 0;
#endif

}

static inline void
sitd_link (struct ehci_hcd *ehci, unsigned frame, struct ehci_sitd *sitd)
{
	u32		ptr;

	ptr = cpu_to_le32 (sitd->sitd_dma | 2);	// type 2 == sitd
	if (ehci->pshadow [frame].ptr) {
		if (!sitd->sitd_next.ptr) {
			sitd->sitd_next = ehci->pshadow [frame];
			sitd->hw_next = ehci->periodic [frame];
		} else if (sitd->sitd_next.ptr != ehci->pshadow [frame].ptr) {
			dbg ("frame %d sitd link goof", frame);
			BUG ();
		}
	}
	ehci->pshadow [frame].sitd = sitd;
	ehci->periodic [frame] = ptr;
}

static unsigned long
sitd_complete (
	struct ehci_hcd *ehci,
	struct ehci_sitd	*sitd,
	unsigned long		flags
) {
	// FIXME -- implement!

	dbg ("NYI -- sitd_complete");
	return flags;
}

/*-------------------------------------------------------------------------*/

static int sitd_submit (struct ehci_hcd *ehci, struct urb *urb)
{
	// struct ehci_sitd	*first_sitd = 0;
	unsigned		frame_index;
	dma_addr_t		dma;
	int			mem_flags;

	dbg ("NYI -- sitd_submit");

	// FIXME -- implement!

	// FIXME:  setup one big dma mapping
	dma = 0;

	mem_flags = SLAB_ATOMIC;

	for (frame_index = 0;
			frame_index < urb->number_of_packets;
			frame_index++) {
		struct ehci_sitd	*sitd;
		unsigned		uframe;

		// FIXME:  use real arguments, schedule this!
		uframe = -1;

		sitd = sitd_make (ehci, urb, frame_index,
				uframe, dma, mem_flags);

		if (sitd) {
    /*
			if (first_sitd)
				list_add_tail (&sitd->sitd_list,
						&first_sitd->sitd_list);
			else
				first_sitd = sitd;
    */
		} else {
			// FIXME:  clean everything up
		}
	}

	// if we have a first sitd, then
		// store them all into the periodic schedule!
		// urb->hcpriv = first sitd in sitd_list

	return -ENOSYS;
}

#endif	/* have_iso */

/*-------------------------------------------------------------------------*/

static void scan_periodic (struct ehci_hcd *ehci)
{
	unsigned	frame;
	unsigned	clock;
	unsigned long	flags;

	spin_lock_irqsave (&ehci->lock, flags);

	/*
	 * When running, scan from last scan point up to "now"
	 * Touches as few pages as possible:  cache-friendly.
	 * It's safe to scan entries more than once, though.
	 */
	if (HCD_IS_RUNNING (ehci->hcd.state)) {
		frame = ehci->next_frame;
		clock = ehci_get_frame (&ehci->hcd);

	/* when shutting down, scan everything for thoroughness */
	} else {
		frame = 0;
		clock = ehci->periodic_size - 1;
	}
	for (;;) {
		union ehci_shadow	 q;
		u32	type;

restart:
		q.ptr = ehci->pshadow [frame].ptr;
		type = Q_NEXT_TYPE (ehci->periodic [frame]);

		/* scan each element in frame's queue for completions */
		while (q.ptr != 0) {
			int			last;
			union ehci_shadow	temp;

			switch (type) {
			case Q_TYPE_QH:
				last = (q.qh->hw_next == EHCI_LIST_END);
				flags = intr_complete (ehci, frame,
						qh_put (q.qh), flags);
				type = Q_NEXT_TYPE (q.qh->hw_next);
				temp = q.qh->qh_next;
				qh_unput (ehci, q.qh);
				q = temp;
				break;
			case Q_TYPE_FSTN:
				last = (q.fstn->hw_next == EHCI_LIST_END);
				/* for "save place" FSTNs, look at QH entries
				 * in the previous frame for completions.
				 */
				if (q.fstn->hw_prev != EHCI_LIST_END) {
					dbg ("ignoring completions from FSTNs");
				}
				type = Q_NEXT_TYPE (q.fstn->hw_next);
				temp = q.fstn->fstn_next;
				break;
#ifdef have_iso
			case Q_TYPE_ITD:
				last = (q.itd->hw_next == EHCI_LIST_END);
				flags = itd_complete (ehci, q.itd, flags);
				type = Q_NEXT_TYPE (q.itd->hw_next);
				q = q.itd->itd_next;
				break;
			case Q_TYPE_SITD:
				last = (q.sitd->hw_next == EHCI_LIST_END);
				flags = sitd_complete (ehci, q.sitd, flags);
				type = Q_NEXT_TYPE (q.sitd->hw_next);
				q = q.sitd->sitd_next;
				break;
#endif /* have_iso */
			default:
				dbg ("corrupt type %d frame %d shadow %p",
					type, frame, q.ptr);
				// BUG ();
				last = 1;
				q.ptr = 0;
			}

			/* did completion remove an interior q entry? */
			if (unlikely (q.ptr == 0 && !last))
				goto restart;
		}

		/* stop when we catch up to the HC */

		// FIXME:  this assumes we won't get lapped when
		// latencies climb; that should be rare, but...
		// detect it, and just go all the way around.
		// FLR might help detect this case, so long as latencies
		// don't exceed periodic_size msec (default 1.024 sec).

		// FIXME:  likewise assumes HC doesn't halt mid-scan

		if (frame == clock) {
			unsigned	now;

			if (!HCD_IS_RUNNING (ehci->hcd.state))
				break;
			ehci->next_frame = clock;
			now = ehci_get_frame (&ehci->hcd);
			if (clock == now)
				break;
			clock = now;
		} else if (++frame >= ehci->periodic_size)
			frame = 0;
	} 
	spin_unlock_irqrestore (&ehci->lock, flags);
 }
