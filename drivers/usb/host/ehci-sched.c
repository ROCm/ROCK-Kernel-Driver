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
 * EHCI scheduled transaction support:  interrupt, iso, split iso
 * These are called "periodic" transactions in the EHCI spec.
 *
 * Note that for interrupt transfers, the QH/QTD manipulation is shared
 * with the "asynchronous" transaction support (control/bulk transfers).
 * The only real difference is in how interrupt transfers are scheduled.
 * We get some funky API restrictions from the current URB model, which
 * works notably better for reading transfers than for writing.  (And
 * which accordingly needs to change before it'll work inside devices,
 * or with "USB On The Go" additions to USB 2.0 ...)
 */

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
	case Q_TYPE_ITD:
		return &periodic->itd->itd_next;
#ifdef have_split_iso
	case Q_TYPE_SITD:
		return &periodic->sitd->sitd_next;
#endif /* have_split_iso */
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

	while (q->ptr) {
		switch (Q_NEXT_TYPE (*hw_p)) {
		case Q_TYPE_QH:
			/* is it in the S-mask? */
			if (q->qh->hw_info2 & cpu_to_le32 (1 << uframe))
				usecs += q->qh->usecs;
			/* ... or C-mask? */
			if (q->qh->hw_info2 & cpu_to_le32 (1 << (8 + uframe)))
				usecs += q->qh->c_usecs;
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
		case Q_TYPE_ITD:
			/* NOTE the "one uframe per itd" policy */
			if (q->itd->hw_transaction [uframe] != 0)
				usecs += q->itd->usecs;
			q = &q->itd->itd_next;
			break;
#ifdef have_split_iso
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
#endif /* have_split_iso */
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

static int enable_periodic (struct ehci_hcd *ehci)
{
	u32	cmd;
	int	status;

	/* did clearing PSE did take effect yet?
	 * takes effect only at frame boundaries...
	 */
	status = handshake (&ehci->regs->status, STS_PSS, 0, 9 * 125);
	if (status != 0) {
		ehci->hcd.state = USB_STATE_HALT;
		return status;
	}

	cmd = readl (&ehci->regs->command) | CMD_PSE;
	writel (cmd, &ehci->regs->command);
	/* posted write ... PSS happens later */
	ehci->hcd.state = USB_STATE_RUNNING;

	/* make sure ehci_work scans these */
	ehci->next_uframe = readl (&ehci->regs->frame_index)
				% (ehci->periodic_size << 3);
	return 0;
}

static int disable_periodic (struct ehci_hcd *ehci)
{
	u32	cmd;
	int	status;

	/* did setting PSE not take effect yet?
	 * takes effect only at frame boundaries...
	 */
	status = handshake (&ehci->regs->status, STS_PSS, STS_PSS, 9 * 125);
	if (status != 0) {
		ehci->hcd.state = USB_STATE_HALT;
		return status;
	}

	cmd = readl (&ehci->regs->command) & ~CMD_PSE;
	writel (cmd, &ehci->regs->command);
	/* posted write ... */

	ehci->next_uframe = -1;
	return 0;
}

/*-------------------------------------------------------------------------*/

// FIXME microframe periods not yet handled

static void intr_deschedule (
	struct ehci_hcd	*ehci,
	struct ehci_qh	*qh,
	int		wait
) {
	int		status;
	unsigned	frame = qh->start;

	do {
		periodic_unlink (ehci, frame, qh);
		qh_put (ehci, qh);
		frame += qh->period;
	} while (frame < ehci->periodic_size);

	qh->qh_state = QH_STATE_UNLINK;
	qh->qh_next.ptr = 0;
	ehci->periodic_sched--;

	/* maybe turn off periodic schedule */
	if (!ehci->periodic_sched)
		status = disable_periodic (ehci);
	else {
		status = 0;
		vdbg ("periodic schedule still enabled");
	}

	/*
	 * If the hc may be looking at this qh, then delay a uframe
	 * (yeech!) to be sure it's done.
	 * No other threads may be mucking with this qh.
	 */
	if (((ehci_get_frame (&ehci->hcd) - frame) % qh->period) == 0) {
		if (wait) {
			udelay (125);
			qh->hw_next = EHCI_LIST_END;
		} else {
			/* we may not be IDLE yet, but if the qh is empty
			 * the race is very short.  then if qh also isn't
			 * rescheduled soon, it won't matter.  otherwise...
			 */
			vdbg ("intr_deschedule...");
		}
	} else
		qh->hw_next = EHCI_LIST_END;

	qh->qh_state = QH_STATE_IDLE;

	/* update per-qh bandwidth utilization (for usbfs) */
	hcd_to_bus (&ehci->hcd)->bandwidth_allocated -= 
		(qh->usecs + qh->c_usecs) / qh->period;

	dbg ("descheduled qh %p, period = %d frame = %d count = %d, urbs = %d",
		qh, qh->period, frame,
		atomic_read (&qh->refcount), ehci->periodic_sched);
}

static int check_period (
	struct ehci_hcd *ehci, 
	unsigned	frame,
	unsigned	uframe,
	unsigned	period,
	unsigned	usecs
) {
	/* complete split running into next frame?
	 * given FSTN support, we could sometimes check...
	 */
	if (uframe >= 8)
		return 0;

	/*
	 * 80% periodic == 100 usec/uframe available
	 * convert "usecs we need" to "max already claimed" 
	 */
	usecs = 100 - usecs;

	do {
		int	claimed;

// FIXME delete when intr_submit handles non-empty queues
// this gives us a one intr/frame limit (vs N/uframe)
// ... and also lets us avoid tracking split transactions
// that might collide at a given TT/hub.
		if (ehci->pshadow [frame].ptr)
			return 0;

		claimed = periodic_usecs (ehci, frame, uframe);
		if (claimed > usecs)
			return 0;

// FIXME update to handle sub-frame periods
	} while ((frame += period) < ehci->periodic_size);

	// success!
	return 1;
}

static int check_intr_schedule (
	struct ehci_hcd		*ehci, 
	unsigned		frame,
	unsigned		uframe,
	const struct ehci_qh	*qh,
	u32			*c_maskp
)
{
    	int		retval = -ENOSPC;

	if (!check_period (ehci, frame, uframe, qh->period, qh->usecs))
		goto done;
	if (!qh->c_usecs) {
		retval = 0;
		*c_maskp = cpu_to_le32 (0);
		goto done;
	}

	/* This is a split transaction; check the bandwidth available for
	 * the completion too.  Check both worst and best case gaps: worst
	 * case is SPLIT near uframe end, and CSPLIT near start ... best is
	 * vice versa.  Difference can be almost two uframe times, but we
	 * reserve unnecessary bandwidth (waste it) this way.  (Actually
	 * even better cases exist, like immediate device NAK.)
	 *
	 * FIXME don't even bother unless we know this TT is idle in that
	 * range of uframes ... for now, check_period() allows only one
	 * interrupt transfer per frame, so needn't check "TT busy" status
	 * when scheduling a split (QH, SITD, or FSTN).
	 *
	 * FIXME ehci 0.96 and above can use FSTNs
	 */
	if (!check_period (ehci, frame, uframe + qh->gap_uf + 1,
				qh->period, qh->c_usecs))
		goto done;
	if (!check_period (ehci, frame, uframe + qh->gap_uf,
				qh->period, qh->c_usecs))
		goto done;

	*c_maskp = cpu_to_le32 (0x03 << (8 + uframe + qh->gap_uf));
	retval = 0;
done:
	return retval;
}

static int qh_schedule (struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	int 		status;
	unsigned	uframe;
	u32		c_mask;
	unsigned	frame;		/* 0..(qh->period - 1), or NO_FRAME */

	qh->hw_next = EHCI_LIST_END;
	frame = qh->start;

	/* reuse the previous schedule slots, if we can */
	if (frame < qh->period) {
		uframe = ffs (le32_to_cpup (&qh->hw_info2) & 0x00ff);
		status = check_intr_schedule (ehci, frame, --uframe,
				qh, &c_mask);
	} else {
		uframe = 0;
		c_mask = 0;
		status = -ENOSPC;
	}

	/* else scan the schedule to find a group of slots such that all
	 * uframes have enough periodic bandwidth available.
	 */
	if (status) {
		frame = qh->period - 1;
		do {
			for (uframe = 0; uframe < 8; uframe++) {
				status = check_intr_schedule (ehci,
						frame, uframe, qh,
						&c_mask);
				if (status == 0)
					break;
			}
		} while (status && frame--);
		if (status)
			goto done;
		qh->start = frame;

		/* reset S-frame and (maybe) C-frame masks */
		qh->hw_info2 &= ~0xffff;
		qh->hw_info2 |= cpu_to_le32 (1 << uframe) | c_mask;
	} else
		dbg ("reused previous qh %p schedule", qh);

	/* stuff into the periodic schedule */
	qh->qh_state = QH_STATE_LINKED;
	dbg ("scheduled qh %p usecs %d/%d period %d.0 starting %d.%d (gap %d)",
		qh, qh->usecs, qh->c_usecs,
		qh->period, frame, uframe, qh->gap_uf);
	do {
		if (unlikely (ehci->pshadow [frame].ptr != 0)) {

// FIXME -- just link toward the end, before any qh with a shorter period,
// AND accommodate it already having been linked here (after some other qh)
// AS WELL AS updating the schedule checking logic

			BUG ();
		} else {
			ehci->pshadow [frame].qh = qh_get (qh);
			ehci->periodic [frame] =
				QH_NEXT (qh->qh_dma);
		}
		wmb ();
		frame += qh->period;
	} while (frame < ehci->periodic_size);

	/* update per-qh bandwidth for usbfs */
	hcd_to_bus (&ehci->hcd)->bandwidth_allocated += 
		(qh->usecs + qh->c_usecs) / qh->period;

	/* maybe enable periodic schedule processing */
	if (!ehci->periodic_sched++)
		status = enable_periodic (ehci);
done:
	return status;
}

static int intr_submit (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	struct list_head	*qtd_list,
	int			mem_flags
) {
	unsigned		epnum;
	unsigned long		flags;
	struct ehci_qh		*qh;
	struct hcd_dev		*dev;
	int			is_input;
	int			status = 0;
	struct list_head	empty;

	/* get endpoint and transfer/schedule data */
	epnum = usb_pipeendpoint (urb->pipe);
	is_input = usb_pipein (urb->pipe);
	if (is_input)
		epnum |= 0x10;

	spin_lock_irqsave (&ehci->lock, flags);
	dev = (struct hcd_dev *)urb->dev->hcpriv;

	/* get qh and force any scheduling errors */
	INIT_LIST_HEAD (&empty);
	qh = qh_append_tds (ehci, urb, &empty, epnum, &dev->ep [epnum]);
	if (qh == 0) {
		status = -ENOMEM;
		goto done;
	}
	if (qh->qh_state == QH_STATE_IDLE) {
		if ((status = qh_schedule (ehci, qh)) != 0)
			goto done;
	}

	/* then queue the urb's tds to the qh */
	qh = qh_append_tds (ehci, urb, qtd_list, epnum, &dev->ep [epnum]);
	BUG_ON (qh == 0);

	/* ... update usbfs periodic stats */
	hcd_to_bus (&ehci->hcd)->bandwidth_int_reqs++;

done:
	spin_unlock_irqrestore (&ehci->lock, flags);
	if (status)
		qtd_list_free (ehci, urb, qtd_list);

	return status;
}

static unsigned
intr_complete (
	struct ehci_hcd	*ehci,
	unsigned	frame,
	struct ehci_qh	*qh,
	struct pt_regs	*regs
) {
	unsigned	count;

	/* nothing to report? */
	if (likely ((qh->hw_token & __constant_cpu_to_le32 (QTD_STS_ACTIVE))
			!= 0))
		return 0;
	if (unlikely (list_empty (&qh->qtd_list))) {
		dbg ("intr qh %p no TDs?", qh);
		return 0;
	}
	
	/* handle any completions */
	count = qh_completions (ehci, qh, regs);

	if (unlikely (list_empty (&qh->qtd_list)))
		intr_deschedule (ehci, qh, 0);

	return count;
}

/*-------------------------------------------------------------------------*/

static void
itd_free_list (struct ehci_hcd *ehci, struct urb *urb)
{
	struct ehci_itd *first_itd = urb->hcpriv;

	while (!list_empty (&first_itd->itd_list)) {
		struct ehci_itd	*itd;

		itd = list_entry (
			first_itd->itd_list.next,
			struct ehci_itd, itd_list);
		list_del (&itd->itd_list);
		pci_pool_free (ehci->itd_pool, itd, itd->itd_dma);
	}
	pci_pool_free (ehci->itd_pool, first_itd, first_itd->itd_dma);
	urb->hcpriv = 0;
}

static int
itd_fill (
	struct ehci_hcd	*ehci,
	struct ehci_itd	*itd,
	struct urb	*urb,
	unsigned	index,		// urb->iso_frame_desc [index]
	dma_addr_t	dma		// mapped transfer buffer
) {
	u64		temp;
	u32		buf1;
	unsigned	i, epnum, maxp, multi;
	unsigned	length;
	int		is_input;

	itd->hw_next = EHCI_LIST_END;
	itd->urb = urb;
	itd->index = index;

	/* tell itd about its transfer buffer, max 2 pages */
	length = urb->iso_frame_desc [index].length;
	dma += urb->iso_frame_desc [index].offset;
	temp = dma & ~0x0fff;
	for (i = 0; i < 2; i++) {
		itd->hw_bufp [i] = cpu_to_le32 ((u32) temp);
		itd->hw_bufp_hi [i] = cpu_to_le32 ((u32)(temp >> 32));
		temp += 0x1000;
	}
	itd->buf_dma = dma;

	/*
	 * this might be a "high bandwidth" highspeed endpoint,
	 * as encoded in the ep descriptor's maxpacket field
	 */
	epnum = usb_pipeendpoint (urb->pipe);
	is_input = usb_pipein (urb->pipe);
	if (is_input) {
		maxp = urb->dev->epmaxpacketin [epnum];
		buf1 = (1 << 11);
	} else {
		maxp = urb->dev->epmaxpacketout [epnum];
		buf1 = 0;
	}
	buf1 |= (maxp & 0x03ff);
	multi = 1;
	multi += (maxp >> 11) & 0x03;
	maxp &= 0x03ff;
	maxp *= multi;

	/* transfer can't fit in any uframe? */ 
	if (length < 0 || maxp < length) {
		dbg ("BAD iso packet: %d bytes, max %d, urb %p [%d] (of %d)",
			length, maxp, urb, index,
			urb->iso_frame_desc [index].length);
		return -ENOSPC;
	}
	itd->usecs = usb_calc_bus_time (USB_SPEED_HIGH, is_input, 1, length);

	/* "plus" info in low order bits of buffer pointers */
	itd->hw_bufp [0] |= cpu_to_le32 ((epnum << 8) | urb->dev->devnum);
	itd->hw_bufp [1] |= cpu_to_le32 (buf1);
	itd->hw_bufp [2] |= cpu_to_le32 (multi);

	/* figure hw_transaction[] value (it's scheduled later) */
	itd->transaction = EHCI_ISOC_ACTIVE;
	itd->transaction |= dma & 0x0fff;		/* offset; buffer=0 */
	if ((index + 1) == urb->number_of_packets)
		itd->transaction |= EHCI_ITD_IOC; 	/* end-of-urb irq */
	itd->transaction |= length << 16;
	cpu_to_le32s (&itd->transaction);

	return 0;
}

static int
itd_urb_transaction (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	int			mem_flags
) {
	int			frame_index;
	struct ehci_itd		*first_itd, *itd;
	int			status;
	dma_addr_t		itd_dma;

	/* allocate/init ITDs */
	for (frame_index = 0, first_itd = 0;
			frame_index < urb->number_of_packets;
			frame_index++) {
		itd = pci_pool_alloc (ehci->itd_pool, mem_flags, &itd_dma);
		if (!itd) {
			status = -ENOMEM;
			goto fail;
		}
		memset (itd, 0, sizeof *itd);
		itd->itd_dma = itd_dma;

		status = itd_fill (ehci, itd, urb, frame_index,
				urb->transfer_dma);
		if (status != 0)
			goto fail;

		if (first_itd)
			list_add_tail (&itd->itd_list,
					&first_itd->itd_list);
		else {
			INIT_LIST_HEAD (&itd->itd_list);
			urb->hcpriv = first_itd = itd;
		}
	}
	urb->error_count = 0;
	return 0;

fail:
	if (urb->hcpriv)
		itd_free_list (ehci, urb);
	return status;
}

/*-------------------------------------------------------------------------*/

static inline void
itd_link (struct ehci_hcd *ehci, unsigned frame, struct ehci_itd *itd)
{
	/* always prepend ITD/SITD ... only QH tree is order-sensitive */
	itd->itd_next = ehci->pshadow [frame];
	itd->hw_next = ehci->periodic [frame];
	ehci->pshadow [frame].itd = itd;
	ehci->periodic [frame] = cpu_to_le32 (itd->itd_dma) | Q_TYPE_ITD;
}

/*
 * return zero on success, else -errno
 * - start holds first uframe to start scheduling into
 * - max is the first uframe it's NOT (!) OK to start scheduling into
 * math to be done modulo "mod" (ehci->periodic_size << 3)
 */
static int get_iso_range (
	struct ehci_hcd		*ehci,
	struct urb		*urb,
	unsigned		*start,
	unsigned		*max,
	unsigned		mod
) {
	struct list_head	*lh;
	struct hcd_dev		*dev = urb->dev->hcpriv;
	int			last = -1;
	unsigned		now, span, end;

	span = urb->interval * urb->number_of_packets;

	/* first see if we know when the next transfer SHOULD happen */
	list_for_each (lh, &dev->urb_list) {
		struct urb	*u;
		struct ehci_itd	*itd;
		unsigned	s;

		u = list_entry (lh, struct urb, urb_list);
		if (u == urb || u->pipe != urb->pipe)
			continue;
		if (u->interval != urb->interval) {	/* must not change! */ 
			dbg ("urb %p interval %d ... != %p interval %d",
				u, u->interval, urb, urb->interval);
			return -EINVAL;
		}
		
		/* URB for this endpoint... covers through when?  */
		itd = urb->hcpriv;
		s = itd->uframe + u->interval * u->number_of_packets;
		if (last < 0)
			last = s;
		else {
			/*
			 * So far we can only queue two ISO URBs...
			 *
			 * FIXME do interval math, figure out whether
			 * this URB is "before" or not ... also, handle
			 * the case where the URB might have completed,
			 * but hasn't yet been processed.
			 */
			dbg ("NYET: queue >2 URBs per ISO endpoint");
			return -EDOM;
		}
	}

	/* calculate the legal range [start,max) */
	now = readl (&ehci->regs->frame_index) + 1;	/* next uframe */
	if (!ehci->periodic_sched)
		now += 8;				/* startup delay */
	now %= mod;
	end = now + mod;
	if (last < 0) {
		*start = now + ehci->i_thresh + /* paranoia */ 1;
		*max = end - span;
		if (*max < *start + 1)
			*max = *start + 1;
	} else {
		*start = last % mod;
		*max = (last + 1) % mod;
	}

	/* explicit start frame? */
	if (!(urb->transfer_flags & URB_ISO_ASAP)) {
		unsigned	temp;

		/* sanity check: must be in range */
		urb->start_frame %= ehci->periodic_size;
		temp = urb->start_frame << 3;
		if (temp < *start)
			temp += mod;
		if (temp > *max)
			return -EDOM;

		/* use that explicit start frame */
		*start = urb->start_frame << 3;
		temp += 8;
		if (temp < *max)
			*max = temp;
	}

	// FIXME minimize wraparound to "now" ... insist max+span
	// (and start+span) remains a few frames short of "end"

	*max %= ehci->periodic_size;
	if ((*start + span) < end)
		return 0;
	return -EFBIG;
}

static int
itd_schedule (struct ehci_hcd *ehci, struct urb *urb)
{
	unsigned	start, max, i;
	int		status;
	unsigned	mod = ehci->periodic_size << 3;

	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc [i].status = -EINPROGRESS;
		urb->iso_frame_desc [i].actual_length = 0;
	}

	if ((status = get_iso_range (ehci, urb, &start, &max, mod)) != 0)
		return status;

	do {
		unsigned	uframe;
		unsigned	usecs;
		struct ehci_itd	*itd;

		/* check schedule: enough space? */
		itd = urb->hcpriv;
		uframe = start;
		for (i = 0, uframe = start;
				i < urb->number_of_packets;
				i++, uframe += urb->interval) {
			uframe %= mod;

			/* can't commit more than 80% periodic == 100 usec */
			if (periodic_usecs (ehci, uframe >> 3, uframe & 0x7)
					> (100 - itd->usecs)) {
				itd = 0;
				break;
			}
			itd = list_entry (itd->itd_list.next,
				struct ehci_itd, itd_list);
		}
		if (!itd)
			continue;
		
		/* that's where we'll schedule this! */
		itd = urb->hcpriv;
		urb->start_frame = start >> 3;
		vdbg ("ISO urb %p (%d packets period %d) starting %d.%d",
			urb, urb->number_of_packets, urb->interval,
			urb->start_frame, start & 0x7);
		for (i = 0, uframe = start, usecs = 0;
				i < urb->number_of_packets;
				i++, uframe += urb->interval) {
			uframe %= mod;

			itd->uframe = uframe;
			itd->hw_transaction [uframe & 0x07] = itd->transaction;
			itd_link (ehci, (uframe >> 3) % ehci->periodic_size,
				itd);
			wmb ();
			usecs += itd->usecs;

			itd = list_entry (itd->itd_list.next,
				struct ehci_itd, itd_list);
		}

		/* update bandwidth utilization records (for usbfs)
		 *
		 * FIXME This claims each URB queued to an endpoint, as if
		 * transfers were concurrent, not sequential.  So bandwidth
		 * typically gets double-billed ... comes from tying it to
		 * URBs rather than endpoints in the schedule.  Luckily we
		 * don't use this usbfs data for serious decision making.
		 */
		usecs /= urb->number_of_packets;
		usecs /= urb->interval;
		usecs >>= 3;
		if (usecs < 1)
			usecs = 1;
		usb_claim_bandwidth (urb->dev, urb, usecs, 1);

		/* maybe enable periodic schedule processing */
		if (!ehci->periodic_sched++) {
			if ((status =  enable_periodic (ehci)) != 0) {
				// FIXME deschedule right away
				err ("itd_schedule, enable = %d", status);
			}
		}

		return 0;

	} while ((start = ++start % mod) != max);

	/* no room in the schedule */
	dbg ("urb %p, CAN'T SCHEDULE", urb);
	return -ENOSPC;
}

/*-------------------------------------------------------------------------*/

#define	ISO_ERRS (EHCI_ISOC_BUF_ERR | EHCI_ISOC_BABBLE | EHCI_ISOC_XACTERR)

static unsigned
itd_complete (
	struct ehci_hcd	*ehci,
	struct ehci_itd	*itd,
	unsigned	uframe,
	struct pt_regs	*regs
) {
	struct urb				*urb = itd->urb;
	struct usb_iso_packet_descriptor	*desc;
	u32					t;

	/* update status for this uframe's transfers */
	desc = &urb->iso_frame_desc [itd->index];

	t = itd->hw_transaction [uframe];
	itd->hw_transaction [uframe] = 0;
	if (t & EHCI_ISOC_ACTIVE)
		desc->status = -EXDEV;
	else if (t & ISO_ERRS) {
		urb->error_count++;
		if (t & EHCI_ISOC_BUF_ERR)
			desc->status = usb_pipein (urb->pipe)
				? -ENOSR  /* couldn't read */
				: -ECOMM; /* couldn't write */
		else if (t & EHCI_ISOC_BABBLE)
			desc->status = -EOVERFLOW;
		else /* (t & EHCI_ISOC_XACTERR) */
			desc->status = -EPROTO;

		/* HC need not update length with this error */
		if (!(t & EHCI_ISOC_BABBLE))
			desc->actual_length += EHCI_ITD_LENGTH (t);
	} else {
		desc->status = 0;
		desc->actual_length += EHCI_ITD_LENGTH (t);
	}

	vdbg ("itd %p urb %p packet %d/%d trans %x status %d len %d",
		itd, urb, itd->index + 1, urb->number_of_packets,
		t, desc->status, desc->actual_length);

	/* handle completion now? */
	if ((itd->index + 1) != urb->number_of_packets)
		return 0;

	/*
	 * Always give the urb back to the driver ... expect it to submit
	 * a new urb (or resubmit this), and to have another already queued
	 * when un-interrupted transfers are needed.
	 *
	 * NOTE that for now we don't accelerate ISO unlinks; they just
	 * happen according to the current schedule.  Means a delay of
	 * up to about a second (max).
	 */
	itd_free_list (ehci, urb);
	if (urb->status == -EINPROGRESS)
		urb->status = 0;

	/* complete() can reenter this HCD */
	spin_unlock (&ehci->lock);
	usb_hcd_giveback_urb (&ehci->hcd, urb, regs);
	spin_lock (&ehci->lock);

	/* defer stopping schedule; completion can submit */
	ehci->periodic_sched--;
	if (!ehci->periodic_sched)
		(void) disable_periodic (ehci);

	return 1;
}

/*-------------------------------------------------------------------------*/

static int itd_submit (struct ehci_hcd *ehci, struct urb *urb, int mem_flags)
{
	int		status;
	unsigned long	flags;

	dbg ("itd_submit urb %p", urb);

	/* allocate ITDs w/o locking anything */
	status = itd_urb_transaction (ehci, urb, mem_flags);
	if (status < 0)
		return status;

	/* schedule ... need to lock */
	spin_lock_irqsave (&ehci->lock, flags);
	status = itd_schedule (ehci, urb);
	spin_unlock_irqrestore (&ehci->lock, flags);
	if (status < 0)
		itd_free_list (ehci, urb);

	return status;
}

#ifdef have_split_iso

/*-------------------------------------------------------------------------*/

/*
 * "Split ISO TDs" ... used for USB 1.1 devices going through
 * the TTs in USB 2.0 hubs.
 *
 * FIXME not yet implemented
 */

#endif /* have_split_iso */

/*-------------------------------------------------------------------------*/

static void
scan_periodic (struct ehci_hcd *ehci, struct pt_regs *regs)
{
	unsigned	frame, clock, now_uframe, mod;
	unsigned	count = 0;

	mod = ehci->periodic_size << 3;

	/*
	 * When running, scan from last scan point up to "now"
	 * else clean up by scanning everything that's left.
	 * Touches as few pages as possible:  cache-friendly.
	 * Don't scan ISO entries more than once, though.
	 */
	frame = ehci->next_uframe >> 3;
	if (HCD_IS_RUNNING (ehci->hcd.state))
		now_uframe = readl (&ehci->regs->frame_index);
	else
		now_uframe = (frame << 3) - 1;
	now_uframe %= mod;
	clock = now_uframe >> 3;

	for (;;) {
		union ehci_shadow	q, *q_p;
		u32			type, *hw_p;
		unsigned		uframes;

restart:
		/* scan schedule to _before_ current frame index */
		if (frame == clock)
			uframes = now_uframe & 0x07;
		else
			uframes = 8;

		q_p = &ehci->pshadow [frame];
		hw_p = &ehci->periodic [frame];
		q.ptr = q_p->ptr;
		type = Q_NEXT_TYPE (*hw_p);

		/* scan each element in frame's queue for completions */
		while (q.ptr != 0) {
			int			last;
			unsigned		uf;
			union ehci_shadow	temp;

			switch (type) {
			case Q_TYPE_QH:
				last = (q.qh->hw_next == EHCI_LIST_END);
				temp = q.qh->qh_next;
				type = Q_NEXT_TYPE (q.qh->hw_next);
				count += intr_complete (ehci, frame,
						qh_get (q.qh), regs);
				qh_put (ehci, q.qh);
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
				q = q.fstn->fstn_next;
				break;
			case Q_TYPE_ITD:
				last = (q.itd->hw_next == EHCI_LIST_END);

				/* Unlink each (S)ITD we see, since the ISO
				 * URB model forces constant rescheduling.
				 * That complicates sharing uframes in ITDs,
				 * and means we need to skip uframes the HC
				 * hasn't yet processed.
				 */
				for (uf = 0; uf < uframes; uf++) {
					if (q.itd->hw_transaction [uf] != 0) {
						temp = q;
						*q_p = q.itd->itd_next;
						*hw_p = q.itd->hw_next;
						type = Q_NEXT_TYPE (*hw_p);

						/* might free q.itd ... */
						count += itd_complete (ehci,
							temp.itd, uf, regs);
						break;
					}
				}
				/* we might skip this ITD's uframe ... */
				if (uf == uframes) {
					q_p = &q.itd->itd_next;
					hw_p = &q.itd->hw_next;
					type = Q_NEXT_TYPE (q.itd->hw_next);
				}

				q = *q_p;
				break;
#ifdef have_split_iso
			case Q_TYPE_SITD:
				last = (q.sitd->hw_next == EHCI_LIST_END);
				sitd_complete (ehci, q.sitd);
				type = Q_NEXT_TYPE (q.sitd->hw_next);

				// FIXME unlink SITD after split completes
				q = q.sitd->sitd_next;
				break;
#endif /* have_split_iso */
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
			ehci->next_uframe = now_uframe;
			now = readl (&ehci->regs->frame_index) % mod;
			if (now_uframe == now)
				break;

			/* rescan the rest of this frame, then ... */
			now_uframe = now;
			clock = now_uframe >> 3;
		} else
			frame = (frame + 1) % ehci->periodic_size;
	} 
}
