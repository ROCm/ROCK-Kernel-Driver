/*  
    UHCI HCD (Host Controller Driver) for USB, UHCI transfer processing
    
    (c) 1999-2002 
    Georg Acher      +    Deti Fliegl    +    Thomas Sailer
    georg@acher.org      deti@fliegl.de   sailer@ife.ee.ethz.ch
   
    with the help of
    David Brownell, david-b@pacbell.net
    Adam Richter, adam@yggdrasil.com
    Roman Weissgaerber, weissg@vienna.at
    
    HW-initalization based on material of
    Randy Dunlap + Johannes Erdfelt + Gregory P. Smith + Linus Torvalds 

    $Id: usb-uhci-q.c,v 1.3 2002/05/25 16:42:41 acher Exp $
*/

/*-------------------------------------------------------------------*/
static inline void finish_urb (struct uhci_hcd *uhci, struct urb *urb)
{
	if (urb->hcpriv) 
		uhci_free_priv (uhci, urb, urb->hcpriv);
	
	usb_hcd_giveback_urb (&uhci->hcd, urb);
}
/*###########################################################################*/
//                        URB SUBMISSION STUFF
//     assembles QHs und TDs for control, bulk, interrupt  and isochronous
/*###########################################################################*/

// returns: 0 (no transfer queued), urb* (this urb already queued) 
static struct urb* search_dev_ep (struct uhci_hcd *uhci, struct urb *urb)
{
	struct list_head *p;
	struct urb *tmp;
	urb_priv_t *priv;
	unsigned int mask = usb_pipecontrol(urb->pipe) ? (~USB_DIR_IN) : (~0);

	p=uhci->urb_list.next;

	for (; p != &uhci->urb_list; p = p->next) {
		priv = list_entry (p,  urb_priv_t, urb_list);
		tmp = priv->urb;
		dbg("search_dev_ep urb: %p", tmp);
		// we can accept this urb if it is not queued at this time 
		// or if non-iso transfer requests should be scheduled for the same device and pipe
		if ((!usb_pipeisoc(urb->pipe) && (tmp->dev == urb->dev) && !((tmp->pipe ^ urb->pipe) & mask)) ||
		    (urb == tmp))
			return tmp;	// found another urb already queued for processing
	}
	return 0;
}
/*-------------------------------------------------------------------*/

static int uhci_submit_control_urb (struct uhci_hcd *uhci, struct urb *urb)
{
	uhci_desc_t *qh, *td;
	urb_priv_t *urb_priv = urb->hcpriv;
	unsigned long destination, status;
	int maxsze = usb_maxpacket (urb->dev, urb->pipe, usb_pipeout (urb->pipe));
	int depth_first = ctrl_depth;  // UHCI descriptor chasing method
	unsigned long len;
	char *data;

//	err("uhci_submit_control start, buf %p", urb->transfer_buffer);
	if (alloc_qh (uhci, &qh))		// alloc qh for this request
		return -ENOMEM;

	if (alloc_td (uhci, &td, UHCI_PTR_DEPTH * depth_first))		// get td for setup stage
		goto fail_unmap_enomem;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;
	
	status = TD_CTRL_ACTIVE
		| (urb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD)
		| (3 << 27);                      /* 3 errors */

	if (urb->dev->speed == USB_SPEED_LOW)
		status |= TD_CTRL_LS;

	/*  Build the TD for the control request, try forever, 8 bytes of data */
	fill_td (td, status, destination | (7 << 21), urb_priv->setup_packet_dma);

	insert_td (uhci, qh, td, 0);	// queue 'setup stage'-td in qh
#if 0
	{
		char *sp=urb->setup_packet;
		dbg("SETUP to pipe %x: %x %x %x %x %x %x %x %x", urb->pipe,
		    sp[0],sp[1],sp[2],sp[3],sp[4],sp[5],sp[6],sp[7]);
	}
	//uhci_show_td(td);
#endif

	len = urb->transfer_buffer_length;
	data = urb->transfer_buffer;

	/* If direction is "send", change the frame from SETUP (0x2D)
	   to OUT (0xE1). Else change it from SETUP to IN (0x69). */

	destination = (urb->pipe & PIPE_DEVEP_MASK) | (usb_pipeout (urb->pipe)?USB_PID_OUT:USB_PID_IN);

	while (len > 0) {
		int pktsze = len;

		if (alloc_td (uhci, &td, UHCI_PTR_DEPTH * depth_first))
			goto fail_unmap_enomem;

		if (pktsze > maxsze)
			pktsze = maxsze;

		destination ^= 1 << TD_TOKEN_TOGGLE;	// toggle DATA0/1

		// Status, pktsze bytes of data
		fill_td (td, status, destination | ((pktsze - 1) << 21),
			 urb_priv->transfer_buffer_dma + (data - (char *)urb->transfer_buffer));

		insert_td (uhci, qh, td, UHCI_PTR_DEPTH * depth_first);	// queue 'data stage'-td in qh

		data += pktsze;
		len -= pktsze;
	}

	/* Build the final TD for control status 
	   It's only IN if the pipe is out AND we aren't expecting data */

	destination &= ~UHCI_PID;

	if (usb_pipeout (urb->pipe) || (urb->transfer_buffer_length == 0))
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= 1 << TD_TOKEN_TOGGLE;	/* End in Data1 */

	if (alloc_td (uhci, &td, UHCI_PTR_DEPTH))
		goto fail_unmap_enomem;

	status &=~TD_CTRL_SPD;

	/* no limit on errors on final packet, 0 bytes of data */
	fill_td (td, status | TD_CTRL_IOC, destination | (UHCI_NULL_DATA_SIZE << 21), 0);

	insert_td (uhci, qh, td, UHCI_PTR_DEPTH * depth_first);	// queue status td

	list_add (&qh->desc_list, &urb_priv->desc_list);

	queue_urb (uhci, urb);	// queue _before_ inserting in desc chain

	qh->hw.qh.element &= cpu_to_le32(~UHCI_PTR_TERM);

	/* Start it up... put low speed first */
	if (urb->dev->speed == USB_SPEED_LOW)
		insert_qh (uhci, uhci->control_chain, qh, 0);
	else
		insert_qh (uhci, uhci->bulk_chain, qh, 0);

	return 0;

fail_unmap_enomem:
	delete_qh(uhci, qh);
	return -ENOMEM;
}
/*-------------------------------------------------------------------*/
// For queued bulk transfers, two additional QH helpers are allocated (nqh, bqh)
// Due to the linking with other bulk urbs, it has to be locked with urb_list_lock!

static int uhci_submit_bulk_urb (struct uhci_hcd *uhci, struct urb *urb, struct urb *bulk_urb)
{
	urb_priv_t *urb_priv = urb->hcpriv, *upriv, *bpriv=NULL;
	uhci_desc_t *qh, *td, *nqh=NULL, *bqh=NULL, *first_td=NULL;
	unsigned long destination, status;
	char *data;
	unsigned int pipe = urb->pipe;
	int maxsze = usb_maxpacket (urb->dev, pipe, usb_pipeout (pipe));
	int info, len, last;
	int depth_first = bulk_depth;  // UHCI descriptor chasing method

	if (usb_endpoint_halted (urb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)))
		return -EPIPE;

	queue_dbg("uhci_submit_bulk_urb: urb %p, old %p, pipe %08x, len %i",
		  urb,bulk_urb,urb->pipe,urb->transfer_buffer_length);

	upriv = (urb_priv_t*)urb->hcpriv;

	if (!bulk_urb) {
		if (alloc_qh (uhci, &qh))		// get qh for this request
			return -ENOMEM;

		if (urb->transfer_flags & USB_QUEUE_BULK) {
			if (alloc_qh(uhci, &nqh)) // placeholder for clean unlink
				goto fail_unmap_enomem;
			upriv->next_qh = nqh;
			queue_dbg("new next qh %p",nqh);
		}
	}
	else { 
		bpriv = (urb_priv_t*)bulk_urb->hcpriv;
		qh = bpriv->bottom_qh;  // re-use bottom qh and next qh
		nqh = bpriv->next_qh;
		upriv->next_qh=nqh;	
		upriv->prev_queued_urb=bulk_urb;
	}

	if (urb->transfer_flags & USB_QUEUE_BULK) {
		if (alloc_qh (uhci, &bqh))  // "bottom" QH
			goto fail_unmap_enomem;

		set_qh_element(bqh, UHCI_PTR_TERM);
		set_qh_head(bqh, nqh->dma_addr | UHCI_PTR_QH); // element
		upriv->bottom_qh = bqh;
	}
	queue_dbg("uhci_submit_bulk: qh %p bqh %p nqh %p",qh, bqh, nqh);

	/* The "pipe" thing contains the destination in bits 8--18. */
	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid (pipe);
	
	status = TD_CTRL_ACTIVE
		| ((urb->transfer_flags & USB_DISABLE_SPD) ? 0 : TD_CTRL_SPD)
		| (3 << 27);                   /* 3 errors */

	if (urb->dev->speed == USB_SPEED_LOW)
		status |= TD_CTRL_LS;

	/* Build the TDs for the bulk request */
	len = urb->transfer_buffer_length;
	data = urb->transfer_buffer;
	
	do {					// TBD: Really allow zero-length packets?
		int pktsze = len;

		if (alloc_td (uhci, &td, UHCI_PTR_DEPTH * depth_first))
			goto fail_unmap_enomem;

		if (pktsze > maxsze)
			pktsze = maxsze;

		// pktsze bytes of data 
		info = destination | (((pktsze - 1)&UHCI_NULL_DATA_SIZE) << 21) |
			(uhci_get_toggle (urb) << TD_TOKEN_TOGGLE);

		fill_td (td, status, info,
			 urb_priv->transfer_buffer_dma + (data - (char *)urb->transfer_buffer));

		data += pktsze;
		len -= pktsze;
		// Use USB_ZERO_PACKET to finish bulk OUTs always with a zero length packet
		last = (len == 0 && (usb_pipein(pipe) || pktsze < maxsze || !(urb->transfer_flags & USB_ZERO_PACKET)));

		if (last)
			set_td_ioc(td);	// last one generates INT

		insert_td (uhci, qh, td, UHCI_PTR_DEPTH * depth_first);
		if (!first_td)
			first_td=td;
		uhci_do_toggle (urb);

	} while (!last);

	if (bulk_urb && bpriv)   // everything went OK, link with old bulk URB
		bpriv->next_queued_urb=urb;

	list_add (&qh->desc_list, &urb_priv->desc_list);

	if (urb->transfer_flags & USB_QUEUE_BULK)
		append_qh(uhci, td, bqh, UHCI_PTR_DEPTH * depth_first);

	queue_urb_unlocked (uhci, urb);
	
	if (urb->transfer_flags & USB_QUEUE_BULK)
		set_qh_element(qh, first_td->dma_addr);
	else
		qh->hw.qh.element &= cpu_to_le32(~UHCI_PTR_TERM);    // arm QH

	if (!bulk_urb) { 					// new bulk queue	
		if (urb->transfer_flags & USB_QUEUE_BULK) {
			spin_lock (&uhci->td_lock);		// both QHs in one go
			insert_qh (uhci, uhci->chain_end, qh, 0);	// Main QH
			insert_qh (uhci, uhci->chain_end, nqh, 0);	// Helper QH
			spin_unlock (&uhci->td_lock);
		}
		else
			insert_qh (uhci, uhci->chain_end, qh, 0);
	}
	
	//dbg("uhci_submit_bulk_urb: exit\n");
	return 0;

fail_unmap_enomem:
	delete_qh(uhci, qh);
	if (bqh) 
		delete_qh(uhci, bqh);
	if (!bulk_urb && nqh)
		delete_qh(uhci, nqh);

	return -ENOMEM;
}
/*---------------------------------------------------------------------------*/
// submits USB interrupt (ie. polling ;-) 
// ASAP-flag set implicitely
// if period==0, the transfer is only done once

static int uhci_submit_int_urb (struct uhci_hcd *uhci, struct urb *urb)
{
	urb_priv_t *urb_priv = urb->hcpriv;
	int nint;
	uhci_desc_t *td;
	int status, destination;
	int info;
	unsigned int pipe = urb->pipe;
	
	if (urb->interval == 0)
		nint = 0;
	else {  
		// log2-function (urb->interval already 2^n)
		nint = ffs(urb->interval);
		if (nint>7)
			nint=7;
	}

	dbg("INT-interval %i, chain  %i", urb->interval, nint);
	// remember start frame, just in case...
	urb->start_frame = UHCI_GET_CURRENT_FRAME (uhci) & 1023;	

	urb->number_of_packets = 1;  // INT allows only one packet
	
	if (alloc_td (uhci, &td, UHCI_PTR_DEPTH))
		return -ENOMEM;

	status = TD_CTRL_ACTIVE | TD_CTRL_IOC
		| (urb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD)
		| (3 << 27);

	if (urb->dev->speed == USB_SPEED_LOW)
		status |= TD_CTRL_LS;

	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid (pipe) |
		(((urb->transfer_buffer_length - 1) & 0x7ff) << 21);

	info = destination | (uhci_get_toggle (urb) << TD_TOKEN_TOGGLE);
	fill_td (td, status, info, urb_priv->transfer_buffer_dma);
	list_add_tail (&td->desc_list, &urb_priv->desc_list);
	queue_urb (uhci, urb);
	insert_td_horizontal (uhci, uhci->int_chain[nint], td);	// store in INT-TDs
	uhci_do_toggle (urb);

	return 0;
}
/*###########################################################################*/
//                  ISOCHRONOUS TRANSFERS
/*###########################################################################*/

// In case of ASAP iso transfer, search the URB-list for already queued URBs
// for this EP and calculate the earliest start frame for the new
// URB (easy seamless URB continuation!)
static int find_iso_limits (struct uhci_hcd *uhci, struct urb *urb, unsigned int *start, unsigned int *end)
{
	struct urb *u, *last_urb = NULL;
	urb_priv_t *priv;
	struct list_head *p;
	int ret=-1;
	unsigned long flags;
	
	spin_lock_irqsave (&uhci->urb_list_lock, flags);
	p=uhci->urb_list.prev;

	for (; p != &uhci->urb_list; p = p->prev) {
		priv = list_entry (p, urb_priv_t, urb_list);
		u = priv->urb;
		// look for pending URBs with identical pipe handle
		// works only because iso doesn't toggle the data bit!
		if ((urb->pipe == u->pipe) && (urb->dev == u->dev) && (u->status == -EINPROGRESS)) {
			if (!last_urb)
				*start = u->start_frame;
			last_urb = u;
		}
	}
	
	if (last_urb) {
		*end = (last_urb->start_frame + last_urb->number_of_packets*last_urb->interval) & 1023;
		ret=0;
	}
	
	spin_unlock_irqrestore(&uhci->urb_list_lock, flags);
	
	return ret;
}
/*-------------------------------------------------------------------*/
// adjust start_frame according to scheduling constraints (ASAP etc)

static int iso_find_start (struct uhci_hcd *uhci, struct urb *urb)
{
	unsigned int now;
	unsigned int start_limit = 0, stop_limit = 0, queued_size, number_of_frames;
	int limits;

	now = UHCI_GET_CURRENT_FRAME (uhci) & 1023;

	number_of_frames = (unsigned) (urb->number_of_packets*urb->interval);

	if ( number_of_frames > 900)
		return -EFBIG;
	
	limits = find_iso_limits (uhci, urb, &start_limit, &stop_limit);
	queued_size = (stop_limit - start_limit) & 1023;

	if (urb->transfer_flags & USB_ISO_ASAP) {
		// first iso
		if (limits) {
			// 10ms setup should be enough //FIXME!
			urb->start_frame = (now + 10) & 1023;
		}
		else {
			urb->start_frame = stop_limit;		// seamless linkage

			if (((now - urb->start_frame) & 1023) <= (unsigned) number_of_frames) {
				info("iso_find_start: gap in seamless isochronous scheduling");
				dbg("iso_find_start: now %u start_frame %u number_of_packets %u interval %u pipe 0x%08x",
					now, urb->start_frame, urb->number_of_packets, urb->interval, urb->pipe);
				urb->start_frame = (now + 5) & 1023;	// 5ms setup should be enough
			}
		}
	}
	else {
		urb->start_frame &= 1023;
		if (((now - urb->start_frame) & 1023) < number_of_frames) {
			dbg("iso_find_start: now between start_frame and end");
			return -EAGAIN;
		}
	}

	/* check if either start_frame or start_frame+number_of_packets-1 lies between start_limit and stop_limit */
	if (limits)
		return 0;

	if (((urb->start_frame - start_limit) & 1023) < queued_size ||
	    ((urb->start_frame + number_of_frames - 1 - start_limit) & 1023) < queued_size) {
		dbg("iso_find_start: start_frame %u number_of_packets %u start_limit %u stop_limit %u",
			urb->start_frame, urb->number_of_packets, start_limit, stop_limit);
		return -EAGAIN;
	}

	return 0;
}
/*-------------------------------------------------------------------*/
static int uhci_submit_iso_urb (struct uhci_hcd *uhci, struct urb *urb, int mem_flags)
{
	urb_priv_t *urb_priv = urb->hcpriv;
	int n=0, i, ret, last=0;
	uhci_desc_t *td, **tdm;
	int status, destination;
	unsigned long flags;

	tdm = (uhci_desc_t **) kmalloc (urb->number_of_packets * sizeof (uhci_desc_t*), mem_flags);

	if (!tdm) 
		return -ENOMEM;

	memset(tdm, 0, urb->number_of_packets * sizeof (uhci_desc_t*));

	// First try to get all TDs. Cause: Removing already inserted TDs can only be done 
	// racefree in three steps: unlink TDs, wait one frame, delete TDs. 
	// So, this solutions seems simpler...

	for (n = 0; n < urb->number_of_packets; n++) {
		dbg("n:%d urb->iso_frame_desc[n].length:%d", n, urb->iso_frame_desc[n].length);
		if (!urb->iso_frame_desc[n].length)
			continue;  // allows ISO striping by setting length to zero in iso_descriptor
		
		if (alloc_td (uhci, &td, UHCI_PTR_DEPTH)) {
			ret = -ENOMEM;
			goto fail_unmap_tds;
		}
		
		last=n;
		tdm[n] = td;
	}

	__save_flags(flags);
	__cli();		      // Disable IRQs to schedule all ISO-TDs in time
	ret = iso_find_start (uhci, urb);	// adjusts urb->start_frame for later use
	
	if (ret) {
		__restore_flags(flags);
		n = urb->number_of_packets;
		goto fail_unmap_tds;
	}

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;

	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid (urb->pipe);

	// Queue all allocated TDs
	for (n = 0; n < urb->number_of_packets; n++) {
		td = tdm[n];
		if (!td)
			continue;
			
		if (n  == last) {
			status |= TD_CTRL_IOC;
			queue_urb (uhci, urb);
		}

		fill_td (td, status, destination | (((urb->iso_frame_desc[n].length - 1) & 0x7ff) << 21),
			 urb_priv->transfer_buffer_dma + urb->iso_frame_desc[n].offset);
		list_add_tail (&td->desc_list, &urb_priv->desc_list);
	
		insert_td_horizontal (uhci, uhci->iso_td[(urb->start_frame + n*urb->interval) & 1023], td);	// store in iso-tds
	}

	kfree (tdm);
	dbg("ISO-INT# %i, start %i, now %i", urb->number_of_packets, urb->start_frame, UHCI_GET_CURRENT_FRAME (uhci) & 1023);
	ret = 0;

	__restore_flags(flags);
	return ret;	

	// Cleanup allocated TDs
fail_unmap_tds:
	dbg("ISO failed, free %i, ret %i",n,ret);
	for (i = 0; i < n; i++)
		if (tdm[i])
			delete_desc(uhci, tdm[i]);
	kfree (tdm);
	return ret;
}
/*###########################################################################*/
//                        URB UNLINK PROCESSING
/*###########################################################################*/

static void uhci_clean_iso_step1(struct uhci_hcd *uhci, urb_priv_t *urb_priv)
{
	struct list_head *p;
	uhci_desc_t *td;
	dbg("uhci_clean_iso_step1");
	for (p = urb_priv->desc_list.next; p != &urb_priv->desc_list; p = p->next) {
				td = list_entry (p, uhci_desc_t, desc_list);
				unlink_td (uhci, td, 1);
	}
}
/*-------------------------------------------------------------------*/
/* mode: CLEAN_TRANSFER_NO_DELETION: unlink but no deletion mark (step 1 of async_unlink)
         CLEAN_TRANSFER_REGULAR: regular (unlink/delete-mark)
         CLEAN_TRANSFER_DELETION_MARK: deletion mark for QH (step 2 of async_unlink)
 looks a bit complicated because of all the bulk queueing goodies
*/

static void uhci_clean_transfer (struct uhci_hcd *uhci, struct urb *urb, uhci_desc_t *qh, int mode)
{
	uhci_desc_t *bqh, *nqh, *prevqh, *prevtd;
	urb_priv_t *priv=(urb_priv_t*)urb->hcpriv;
	int now=UHCI_GET_CURRENT_FRAME(uhci);

	bqh=priv->bottom_qh;	
	
	if (!priv->next_queued_urb)  { // no more appended bulk queues

		queue_dbg("uhci_clean_transfer: No more bulks for urb %p, qh %p, bqh %p, nqh %p", 
			  urb, qh, bqh, priv->next_qh);	
	
		if (priv->prev_queued_urb && mode != CLEAN_TRANSFER_DELETION_MARK) {  // qh not top of the queue
				unsigned long flags; 
				urb_priv_t* ppriv=(urb_priv_t*)priv->prev_queued_urb->hcpriv;

				spin_lock_irqsave (&uhci->qh_lock, flags);
				prevqh = list_entry (ppriv->desc_list.next, uhci_desc_t, desc_list);
				prevtd = list_entry (prevqh->vertical.prev, uhci_desc_t, vertical);
				set_td_link(prevtd, priv->bottom_qh->dma_addr | UHCI_PTR_QH); // skip current qh
				mb();
				queue_dbg("uhci_clean_transfer: relink pqh %p, ptd %p",prevqh, prevtd);
				spin_unlock_irqrestore (&uhci->qh_lock, flags);

				ppriv->bottom_qh = priv->bottom_qh;
				ppriv->next_queued_urb = NULL;
			}
		else {   // queue is dead, qh is top of the queue
			
			if (mode != CLEAN_TRANSFER_DELETION_MARK) 				
				unlink_qh(uhci, qh); // remove qh from horizontal chain

			if (bqh) {  // remove remainings of bulk queue
				nqh=priv->next_qh;

				if (mode != CLEAN_TRANSFER_DELETION_MARK) 
					unlink_qh(uhci, nqh);  // remove nqh from horizontal chain
				
				if (mode != CLEAN_TRANSFER_NO_DELETION) {  // add helper QHs to free desc list
					nqh->last_used = bqh->last_used = now;
					list_add_tail (&nqh->horizontal, &uhci->free_desc_qh);
					list_add_tail (&bqh->horizontal, &uhci->free_desc_qh);
				}			
			}
		}
	}
	else { // there are queued urbs following
	
	  queue_dbg("uhci_clean_transfer: urb %p, prevurb %p, nexturb %p, qh %p, bqh %p, nqh %p",
		       urb, priv->prev_queued_urb,  priv->next_queued_urb, qh, bqh, priv->next_qh);	
       	
		if (mode != CLEAN_TRANSFER_DELETION_MARK) {	// no work for cleanup at unlink-completion
			struct urb *nurb;
			unsigned long flags;

			nurb = priv->next_queued_urb;
			spin_lock_irqsave (&uhci->qh_lock, flags);		

			if (!priv->prev_queued_urb) { // top QH
				
				prevqh = list_entry (qh->horizontal.prev, uhci_desc_t, horizontal);
				set_qh_head(prevqh, bqh->dma_addr | UHCI_PTR_QH);
				list_del (&qh->horizontal);  // remove this qh from horizontal chain
				list_add (&bqh->horizontal, &prevqh->horizontal); // insert next bqh in horizontal chain
			}
			else {		// intermediate QH
				urb_priv_t* ppriv=(urb_priv_t*)priv->prev_queued_urb->hcpriv;
				urb_priv_t* npriv=(urb_priv_t*)nurb->hcpriv;
				uhci_desc_t * bnqh;
				
				bnqh = list_entry (npriv->desc_list.next, uhci_desc_t, desc_list);
				ppriv->bottom_qh = bnqh;
				ppriv->next_queued_urb = nurb;				
				prevqh = list_entry (ppriv->desc_list.next, uhci_desc_t, desc_list);
				set_qh_head(prevqh, bqh->dma_addr | UHCI_PTR_QH);
			}

			mb();
			((urb_priv_t*)nurb->hcpriv)->prev_queued_urb=priv->prev_queued_urb;
			spin_unlock_irqrestore (&uhci->qh_lock, flags);
		}		
	}

	if (mode != CLEAN_TRANSFER_NO_DELETION) {
		qh->last_used = now;	
		list_add_tail (&qh->horizontal, &uhci->free_desc_qh); // mark qh for later deletion/kfree
	}
}

/*-------------------------------------------------------------------*/
// async unlink_urb completion/cleanup work
// has to be protected by urb_list_lock!
// features: if set in transfer_flags, the resulting status of the killed
// transaction is not overwritten

static void uhci_cleanup_unlink(struct uhci_hcd *uhci, int force)
{
	struct list_head *q;
	struct urb *urb;
	urb_priv_t *urb_priv;
	int type, now = UHCI_GET_CURRENT_FRAME(uhci);

	q = uhci->urb_unlinked.next;

	while (q != &uhci->urb_unlinked) {
		urb_priv = list_entry (q, urb_priv_t, urb_list);
		urb = urb_priv->urb;

		q = urb_priv->urb_list.next;
					
		if (force || ((urb_priv->started != ~0) && (urb_priv->started != now))) {
			async_dbg("async cleanup %p",urb);
			type=usb_pipetype (urb->pipe);

			switch (type) { // process descriptors
			case PIPE_CONTROL:
//				usb_show_device(urb->dev);
				process_transfer (uhci, urb, CLEAN_TRANSFER_DELETION_MARK);  // don't unlink (already done)
//				usb_show_device(urb->dev);
				break;
			case PIPE_BULK:
				if (!uhci->avoid_bulk.counter)
					process_transfer (uhci, urb, CLEAN_TRANSFER_DELETION_MARK); // don't unlink (already done)
				else
					continue;
				break;
			case PIPE_ISOCHRONOUS:
				process_iso (uhci, urb, PROCESS_ISO_FORCE); // force, don't unlink
				break;
			case PIPE_INTERRUPT:
				process_interrupt (uhci, urb, PROCESS_INT_REMOVE);
				break;
			}
			
			list_del (&urb_priv->urb_list);			
			uhci_urb_dma_sync(uhci, urb, urb_priv);
			// clean up descriptors for INT/ISO
//			if (type==PIPE_ISOCHRONOUS || type==PIPE_INTERRUPT) 
//				uhci_clean_iso_step2(uhci, urb_priv);
	
			uhci_free_priv(uhci, urb, urb_priv);		

			if (!(urb->transfer_flags & USB_TIMEOUT_KILLED))
				urb->status = -ENOENT;  // now the urb is really dead

			spin_unlock(&uhci->urb_list_lock);
			usb_hcd_giveback_urb(&uhci->hcd, urb);
			spin_lock(&uhci->urb_list_lock);
		}
	}
}
/*-------------------------------------------------------------------*/
/* needs urb_list_lock!
   mode: UNLINK_ASYNC_STORE_URB: unlink and move URB into unlinked list
         UNLINK_ASYNC_DONT_STORE: unlink, don't move URB into unlinked list
*/
static int uhci_unlink_urb_async (struct uhci_hcd *uhci, struct urb *urb, int mode)
{
	uhci_desc_t *qh;
	urb_priv_t *urb_priv;
	
	async_dbg("unlink_urb_async called %p",urb);
	urb_priv = (urb_priv_t*)urb->hcpriv;
	if (urb_priv==0) {
		err("hc_priv for URB %p is zero!",urb);
		return -EINVAL;
	}
	urb_priv->started = ~0;  // mark
	dequeue_urb (uhci, urb);

	if (mode==UNLINK_ASYNC_STORE_URB)
		list_add_tail (&urb_priv->urb_list, &uhci->urb_unlinked); // store urb

	uhci_switch_timer_int(uhci);
	uhci->unlink_urb_done = 1;

	switch (usb_pipetype (urb->pipe)) {
	case PIPE_INTERRUPT:
		urb_priv->flags = 0; // mark as deleted (if called from completion)
		uhci_do_toggle (urb);

	case PIPE_ISOCHRONOUS:
		uhci_clean_iso_step1 (uhci, urb_priv);
		break;

	case PIPE_BULK:
	case PIPE_CONTROL:
		qh = list_entry (urb_priv->desc_list.next, uhci_desc_t, desc_list);
		uhci_clean_transfer (uhci, urb, qh, CLEAN_TRANSFER_NO_DELETION);
		break;
	}
	urb_priv->started = UHCI_GET_CURRENT_FRAME(uhci);
	return 0;  // completion will follow
}
/*-------------------------------------------------------------------*/
// unlink urbs for specific device or all devices
static void uhci_unlink_urbs(struct uhci_hcd *uhci, struct usb_device *usb_dev, int remove_all)
{
	struct list_head *p;
	struct list_head *p2;
	struct urb *urb;
	urb_priv_t *priv;
	unsigned long flags;

	spin_lock_irqsave (&uhci->urb_list_lock, flags);
	p = uhci->urb_list.prev;	
	while (p != &uhci->urb_list) {
		p2 = p;
		p = p->prev;
		priv = list_entry (p2, urb_priv_t, urb_list);
		urb = priv->urb;

//		err("unlink urb: %p, dev %p, ud %p", urb, usb_dev,urb->dev);
		
		if (remove_all || (usb_dev == urb->dev)) {
			spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
			err("forced removing of queued URB %p due to disconnect",urb);
			uhci_urb_dequeue(&uhci->hcd, urb);
			urb->dev = NULL; // avoid further processing of this URB
			spin_lock_irqsave (&uhci->urb_list_lock, flags);
			p = uhci->urb_list.prev;	
		}
	}
	spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
} 
/*-------------------------------------------------------------------*/

// Checks for URB timeout and removes bandwidth reclamation if URB idles too long
static void uhci_check_timeouts(struct uhci_hcd *uhci)
{
	struct list_head *p,*p2;
	struct urb *urb;
	int type;	
	
	p = uhci->urb_list.prev;	

	while (p != &uhci->urb_list) {
		urb_priv_t *hcpriv;

		p2 = p;
		p = p->prev;
		hcpriv = list_entry (p2,  urb_priv_t, urb_list);
		urb = hcpriv->urb;
		type = usb_pipetype (urb->pipe);

		if ( urb->timeout && time_after(jiffies, hcpriv->started + urb->timeout)) {
			urb->transfer_flags |= USB_TIMEOUT_KILLED;
			async_dbg("uhci_check_timeout: timeout for %p",urb);
			uhci_unlink_urb_async(uhci, urb, UNLINK_ASYNC_STORE_URB);
		}
		else if (high_bw && ((type == PIPE_BULK) || (type == PIPE_CONTROL)) &&  
			 (hcpriv->use_loop) && time_after(jiffies, hcpriv->started + IDLE_TIMEOUT))
			disable_desc_loop(uhci, urb);
	}
	uhci->timeout_check=jiffies;
}
/*###########################################################################*/
//                        INTERRUPT PROCESSING ROUTINES
/*###########################################################################*/
/*
 * Map status to standard result codes
 *
 * <status> is (td->status & 0xFE0000) [a.k.a. uhci_status_bits(td->status)
 * <dir_out> is True for output TDs and False for input TDs.
 */
static int uhci_map_status (int status, int dir_out)
{
	if (!status)
		return 0;
	if (status & TD_CTRL_BITSTUFF)	/* Bitstuff error */
		return -EPROTO;
	if (status & TD_CTRL_CRCTIMEO) {	/* CRC/Timeout */
		if (dir_out)
			return -ETIMEDOUT;
		else
			return -EILSEQ;
	}
	if (status & TD_CTRL_NAK)	/* NAK */
		return -ETIMEDOUT;
	if (status & TD_CTRL_BABBLE)	/* Babble */
		return -EOVERFLOW;
	if (status & TD_CTRL_DBUFERR)	/* Buffer error */
		return -ENOSR;
	if (status & TD_CTRL_STALLED)	/* Stalled */
		return -EPIPE;
	if (status & TD_CTRL_ACTIVE)	/* Active */
		return 0;

	return -EPROTO;
}
/*-------------------------------------------------------------------*/
static void correct_data_toggles(struct urb *urb)
{
	usb_settoggle (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe), 
		       !uhci_get_toggle (urb));

	while(urb) {
		urb_priv_t *priv=urb->hcpriv;		
		uhci_desc_t *qh = list_entry (priv->desc_list.next, uhci_desc_t, desc_list);
		struct list_head *p = qh->vertical.next;
		uhci_desc_t *td;
		dbg("URB to correct %p\n", urb);
	
		for (; p != &qh->vertical; p = p->next) {
			td = list_entry (p, uhci_desc_t, vertical);
			td->hw.td.info^=cpu_to_le32(1<<TD_TOKEN_TOGGLE);
		}
		urb=priv->next_queued_urb;
	}
}
/*-------------------------------------------------------------------*/
/* 
 * For IN-control transfers, process_transfer gets a bit more complicated,
 * since there are devices that return less data (eg. strings) than they
 * have announced. This leads to a queue abort due to the short packet,
 * the status stage is not executed. If this happens, the status stage
 * is manually re-executed.
 * mode: PROCESS_TRANSFER_REGULAR: regular (unlink QH)
 *       PROCESS_TRANSFER_DONT_UNLINK: QHs already unlinked (for async unlink_urb)
 */

static int process_transfer (struct uhci_hcd *uhci, struct urb *urb, int mode)
{
	urb_priv_t *urb_priv = urb->hcpriv;
	struct list_head *qhl = urb_priv->desc_list.next;
	uhci_desc_t *qh = list_entry (qhl, uhci_desc_t, desc_list);
	struct list_head *p = qh->vertical.next;
	uhci_desc_t *desc= list_entry (urb_priv->desc_list.prev, uhci_desc_t, desc_list);
	uhci_desc_t *last_desc = list_entry (desc->vertical.prev, uhci_desc_t, vertical);
	int data_toggle = uhci_get_toggle (urb);	// save initial data_toggle
	int maxlength; 	// extracted and remapped info from TD
	int actual_length;
	int status = 0, ret = 0;

	//dbg("process_transfer: urb %p, urb_priv %p, qh %p last_desc %p\n",urb,urb_priv, qh, last_desc);

	/* if the status phase has been retriggered and the
	   queue is empty or the last status-TD is inactive, the retriggered
	   status stage is completed
	 */

	if (urb_priv->flags && 
	    ((qh->hw.qh.element == cpu_to_le32(UHCI_PTR_TERM)) || !is_td_active(desc)))
		goto transfer_finished;

	urb->actual_length=0;

	for (; p != &qh->vertical; p = p->next) {
		desc = list_entry (p, uhci_desc_t, vertical);

		if (is_td_active(desc)) {	// do not process active TDs
			if (mode == CLEAN_TRANSFER_DELETION_MARK) // if called from async_unlink
				uhci_clean_transfer(uhci, urb, qh, CLEAN_TRANSFER_DELETION_MARK);
			return ret;
		}
	
		actual_length = uhci_actual_length(desc);		// extract transfer parameters from TD
		maxlength = (((le32_to_cpu(desc->hw.td.info) >> 21) & 0x7ff) + 1) & 0x7ff;
		status = uhci_map_status (uhci_status_bits (le32_to_cpu(desc->hw.td.status)), usb_pipeout (urb->pipe));

		if (status == -EPIPE) { 		// see if EP is stalled
			// set up stalled condition
			usb_endpoint_halt (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe));
		}

		if (status && (status != -EPIPE) && (status != -EOVERFLOW)) {	
			// if any error occurred stop processing of further TDs
			// only set ret if status returned an error
			ret = status;
			urb->error_count++;
			break;
		}
		else if ((le32_to_cpu(desc->hw.td.info) & 0xff) != USB_PID_SETUP)
			urb->actual_length += actual_length;

		// got less data than requested
		if ( (actual_length < maxlength)) {
			if (urb->transfer_flags & USB_DISABLE_SPD) {
				status = -EREMOTEIO;	// treat as real error
				dbg("process_transfer: SPD!!");
				break;	// exit after this TD because SP was detected
			}

			// short read during control-IN: re-start status stage
			if ((usb_pipetype (urb->pipe) == PIPE_CONTROL)) {
				if (uhci_packetid(le32_to_cpu(last_desc->hw.td.info)) == USB_PID_OUT) {
			
					set_qh_element(qh, last_desc->dma_addr);  // re-trigger status stage
					dbg("short packet during control transfer, retrigger status stage @ %p",last_desc);
					urb_priv->flags = 1; // mark as short control packet
					return 0;
				}
			}
			// all other cases: short read is OK
			data_toggle = uhci_toggle (le32_to_cpu(desc->hw.td.info));
			break;
		}
		else if (status) {
			ret = status;
			urb->error_count++;
			break;
		}

		data_toggle = uhci_toggle (le32_to_cpu(desc->hw.td.info));
		queue_dbg("process_transfer: len:%d status:%x mapped:%x toggle:%d", 
			  actual_length, le32_to_cpu(desc->hw.td.status),status, data_toggle);      

	}

	/* toggle correction for short bulk transfers (nonqueued/queued) */
	if (usb_pipetype (urb->pipe) == PIPE_BULK ) {  

		urb_priv_t *priv=(urb_priv_t*)urb->hcpriv;
		struct urb *next_queued_urb=priv->next_queued_urb;

		if (next_queued_urb) {
			urb_priv_t *next_priv=(urb_priv_t*)next_queued_urb->hcpriv;
			uhci_desc_t *qh = list_entry (next_priv->desc_list.next, uhci_desc_t, desc_list);
			uhci_desc_t *first_td=list_entry (qh->vertical.next, uhci_desc_t, vertical);

			if (data_toggle == uhci_toggle (le32_to_cpu(first_td->hw.td.info))) {
				err("process_transfer: fixed toggle");
				correct_data_toggles(next_queued_urb);
			}						
		}
		else
			usb_settoggle (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe), !data_toggle);		
	}

 transfer_finished:
    
	uhci_clean_transfer(uhci, urb, qh, mode);
	urb->status = status;

	if (high_bw)
		disable_desc_loop(uhci,urb);

	dbg("process_transfer: (end) urb %p, wanted len %d, len %d status %x err %d",
		urb,urb->transfer_buffer_length,urb->actual_length, urb->status, urb->error_count);
	return ret;
}
/*-------------------------------------------------------------------*/
static int process_interrupt (struct uhci_hcd *uhci, struct urb *urb, int mode)
{
	urb_priv_t *urb_priv = urb->hcpriv;
	struct list_head *p = urb_priv->desc_list.next;
	uhci_desc_t *desc = list_entry (urb_priv->desc_list.prev, uhci_desc_t, desc_list);
	int actual_length, status = 0, i, ret = -EINPROGRESS;

	//dbg("urb contains interrupt request");

	for (i = 0; p != &urb_priv->desc_list; p = p->next, i++)	// Maybe we allow more than one TD later ;-)
	{
		desc = list_entry (p, uhci_desc_t, desc_list);

		if (is_td_active(desc) || !(desc->hw.td.status & cpu_to_le32(TD_CTRL_IOC))) {
			// do not process active TDs or one-shot TDs (->no recycling)
			//dbg("TD ACT Status @%p %08x",desc,le32_to_cpu(desc->hw.td.status));
			break;
		}
	
		// extract transfer parameters from TD
		actual_length = uhci_actual_length(desc);
		status = uhci_map_status (uhci_status_bits (le32_to_cpu(desc->hw.td.status)), usb_pipeout (urb->pipe));

		// see if EP is stalled
		if (status == -EPIPE) {
			// set up stalled condition
			usb_endpoint_halt (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe));
		}

		// if any error occurred: ignore this td, and continue
		if (status != 0) {
			//uhci_show_td (desc);
			urb->error_count++;
			goto recycle;
		}
		else
			urb->actual_length = actual_length;

	recycle:
		((urb_priv_t*)urb->hcpriv)->flags=1; // set to detect unlink during completion

		uhci_urb_dma_sync(uhci, urb, urb->hcpriv);
		if (urb->complete) {
			//dbg("process_interrupt: calling completion, status %i",status);
			urb->status = status;
			spin_unlock(&uhci->urb_list_lock);
			urb->complete ((struct urb *) urb);
			spin_lock(&uhci->urb_list_lock);
		}
		
		if ((urb->status != -ECONNABORTED) && (urb->status != ECONNRESET) &&
			    (urb->status != -ENOENT) && ((urb_priv_t*)urb->hcpriv)->flags) {

			urb->status = -EINPROGRESS;

			// Recycle INT-TD if interval!=0, else mark TD as one-shot
			if (urb->interval) {
				
				desc->hw.td.info &= cpu_to_le32(~(1 << TD_TOKEN_TOGGLE));
				if (status==0) {
					((urb_priv_t*)urb->hcpriv)->started=jiffies;
					desc->hw.td.info |= cpu_to_le32((uhci_get_toggle (urb) << TD_TOKEN_TOGGLE));
					uhci_do_toggle (urb);
				} else {
					desc->hw.td.info |= cpu_to_le32((!uhci_get_toggle (urb) << TD_TOKEN_TOGGLE));
				}
				desc->hw.td.status= cpu_to_le32(TD_CTRL_ACTIVE | TD_CTRL_IOC |
					(urb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (3 << 27));
				if (urb->dev->speed == USB_SPEED_LOW)
					desc->hw.td.status |=
					    __constant_cpu_to_le32 (TD_CTRL_LS);
				mb();
			}
			else {
				uhci_unlink_urb_async(uhci, urb, UNLINK_ASYNC_STORE_URB);
				uhci_do_toggle (urb); // correct toggle after unlink
				clr_td_ioc(desc);    // inactivate TD
			}
		}
		if (mode == PROCESS_INT_REMOVE) {
			INIT_LIST_HEAD(&desc->horizontal);
			list_add_tail (&desc->horizontal, &uhci->free_desc_td);
			desc->last_used=UHCI_GET_CURRENT_FRAME(uhci);
		}
	}

	return ret;
}
/*-------------------------------------------------------------------*/
// mode: PROCESS_ISO_REGULAR: processing only for done TDs, unlink TDs
// mode: PROCESS_ISO_FORCE: force processing, don't unlink TDs (already unlinked)

static int process_iso (struct uhci_hcd *uhci, struct urb *urb, int mode)
{
       	urb_priv_t *urb_priv = urb->hcpriv;
	struct list_head *p = urb_priv->desc_list.next, *p_tmp;
	uhci_desc_t *desc = list_entry (urb_priv->desc_list.prev, uhci_desc_t, desc_list);
	int i, ret = 0;
	int now=UHCI_GET_CURRENT_FRAME(uhci);

	dbg("urb contains iso request");
	if (is_td_active(desc) && mode==PROCESS_ISO_REGULAR)
		return -EXDEV;	// last TD not finished

	urb->error_count = 0;
	urb->actual_length = 0;
	urb->status = 0;
	dbg("process iso urb %p, %li, %i, %i, %i %08x",urb,jiffies,UHCI_GET_CURRENT_FRAME(s),
	    urb->number_of_packets,mode,le32_to_cpu(desc->hw.td.status));

	for (i = 0; p != &urb_priv->desc_list;  i++) {
		desc = list_entry (p, uhci_desc_t, desc_list);
		
		//uhci_show_td(desc);
		if (is_td_active(desc)) {
			// means we have completed the last TD, but not the TDs before
			desc->hw.td.status &= cpu_to_le32(~TD_CTRL_ACTIVE);
			dbg("TD still active (%x)- grrr. paranoia!", le32_to_cpu(desc->hw.td.status));
			ret = -EXDEV;
			urb->iso_frame_desc[i].status = ret;
			unlink_td (uhci, desc, 1); 
			goto err;
		}

		if (mode == PROCESS_ISO_REGULAR)
			unlink_td (uhci, desc, 1);

		if (urb->number_of_packets <= i) {
			dbg("urb->number_of_packets (%d)<=(%d)", urb->number_of_packets, i);
			ret = -EINVAL;
			goto err;
		}

		urb->iso_frame_desc[i].actual_length = uhci_actual_length(desc);
		urb->iso_frame_desc[i].status = uhci_map_status (uhci_status_bits (le32_to_cpu(desc->hw.td.status)), usb_pipeout (urb->pipe));
		urb->actual_length += urb->iso_frame_desc[i].actual_length;

	err:
		if (urb->iso_frame_desc[i].status != 0) {
			urb->error_count++;
			urb->status = urb->iso_frame_desc[i].status;
		}
		dbg("process_iso: %i: len:%d %08x status:%x",
		     i, urb->iso_frame_desc[i].actual_length, le32_to_cpu(desc->hw.td.status),urb->iso_frame_desc[i].status);

		p_tmp = p;
		p = p->next;
		list_del (p_tmp);

		// add to cool down pool
		INIT_LIST_HEAD(&desc->horizontal);
		list_add_tail (&desc->horizontal, &uhci->free_desc_td);
		desc->last_used=now;
	}
	
	dbg("process_iso: exit %i (%d), actual_len %i", i, ret,urb->actual_length);
	return ret;
}

/*-------------------------------------------------------------------*/
// called with urb_list_lock set
static int process_urb (struct uhci_hcd *uhci, struct list_head *p)
{
	struct urb *urb;	
	urb_priv_t *priv;
	int type, ret = 0;

	priv=list_entry (p,  urb_priv_t, urb_list);
	urb=priv->urb;
//	dbg("process_urb p %p, udev %p",urb, urb->dev);
	type=usb_pipetype (urb->pipe);

	switch (type) {
	case PIPE_CONTROL:
		ret = process_transfer (uhci, urb, CLEAN_TRANSFER_REGULAR);
		break;
	case PIPE_BULK:
		// if a submit is fiddling with bulk queues, ignore it for now
		if (!uhci->avoid_bulk.counter)
			ret = process_transfer (uhci, urb, CLEAN_TRANSFER_REGULAR);
		else
			return 0;
		break;
	case PIPE_ISOCHRONOUS:
		ret = process_iso (uhci, urb, PROCESS_ISO_REGULAR);
		break;
	case PIPE_INTERRUPT:
		ret = process_interrupt (uhci, urb, PROCESS_INT_REGULAR);
		break;
	}

	if (urb->status != -EINPROGRESS && type != PIPE_INTERRUPT) {

		dequeue_urb (uhci, urb);
 		uhci_free_priv(uhci, urb, urb->hcpriv);

		spin_unlock(&uhci->urb_list_lock);
		dbg("giveback urb %p, status %i, length %i\n", 
		    urb, urb->status, urb->transfer_buffer_length);
		
		usb_hcd_giveback_urb(&uhci->hcd, urb);
		spin_lock(&uhci->urb_list_lock);

	}
	return ret;
}
/*###########################################################################*/
//                        EMERGENCY ROOM
/*###########################################################################*/
/* used to reanimate a halted hostcontroller which signals no interrupts anymore.
   This is a shortcut for unloading and reloading the module, and should be only
   used as the last resort, but some VIA chips need it.
*/
static int hc_defibrillate(struct uhci_hcd *uhci)
{
	int ret;

	err("Watchdog timeout, host controller obviously clinically dead, defibrillating...\n"
	    "Expect disconnections for all devices on this controller!");

	uhci->running=0;
	outw (USBCMD_HCRESET, (int)uhci->hcd.regs + USBCMD);

	uhci_stop(&uhci->hcd);

	ret=init_skel(uhci);
	if (ret)
		return -ENOMEM;

	set_td_ioc(uhci->td128ms); // enable watchdog interrupt
	hc_irq_run(uhci);
	uhci->reanimations++;
	err("Host controller restart done...");
	return 0;
}
