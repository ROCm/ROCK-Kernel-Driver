/*
 * OHCI HCD (Host Controller Driver) for USB.
 * 
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2001 David Brownell <dbrownell@users.sourceforge.net>
 * 
 * This file is licenced under GPL
 * $Id: ohci-q.c,v 1.6 2002/01/19 00:23:15 dbrownell Exp $
 */
 
static void urb_free_priv (struct ohci_hcd *hc, urb_priv_t *urb_priv)
{
	int		last = urb_priv->length - 1;

	if (last >= 0) {
		int		i;
		struct td	*td = urb_priv->td [0];
#ifdef CONFIG_PCI
		int		len = td->urb->transfer_buffer_length;
		int		dir = usb_pipeout (td->urb->pipe)
					? PCI_DMA_TODEVICE
					: PCI_DMA_FROMDEVICE;

		/* unmap CTRL URB setup buffer (always td 0) */
		if (usb_pipecontrol (td->urb->pipe)) {
			pci_unmap_single (hc->hcd.pdev, 
					td->data_dma, 8, PCI_DMA_TODEVICE);
			
			/* CTRL data buffer starts at td 1 if len > 0 */
			if (len && last > 0)
				td = urb_priv->td [1]; 		
		}
		/* else:  ISOC, BULK, INTR data buffer starts at td 0 */

		/* unmap data buffer */
		if (len && td->data_dma)
			pci_unmap_single (hc->hcd.pdev,
					td->data_dma, len, dir);
#else
#	warning "assuming no buffer unmapping is needed"
#endif

		for (i = 0; i <= last; i++) {
			td = urb_priv->td [i];
			if (td)
				td_free (hc, td);
		}
	}

	kfree (urb_priv);
}

/*-------------------------------------------------------------------------*/

/*
 * URB goes back to driver, and isn't reissued.
 * It's completely gone from HC data structures, so no locking
 * is needed ... or desired! (Giveback can call back to hcd.)
 */
static inline void finish_urb (struct ohci_hcd *ohci, struct urb *urb)
{
	if (urb->hcpriv) {
		urb_free_priv (ohci, urb->hcpriv);
		urb->hcpriv = NULL;
	}
	usb_hcd_giveback_urb (&ohci->hcd, urb);
}

static void td_submit_urb (struct urb *urb);

/*
 * URB is reported to driver, is reissued if it's periodic.
 */
static int return_urb (struct ohci_hcd *hc, struct urb *urb)
{
	urb_priv_t	*urb_priv = urb->hcpriv;
	struct urb	*urbt;
	unsigned long	flags;
	int		i;

#ifdef DEBUG
	if (!urb_priv) {
		err ("already unlinked!");
		BUG ();
	}

	/* just to be sure */
	if (!urb->complete) {
		err ("no completion!");
		BUG ();
	}
#endif

#ifdef OHCI_VERBOSE_DEBUG
	urb_print (urb, "RET", usb_pipeout (urb->pipe));
#endif

// FIXME:  but if urb->status says it was was unlinked ...

	switch (usb_pipetype (urb->pipe)) {
  		case PIPE_INTERRUPT:
#ifdef CONFIG_PCI
			pci_unmap_single (hc->hcd.pdev,
				urb_priv->td [0]->data_dma,
				urb->transfer_buffer_length,
				usb_pipeout (urb->pipe)
					? PCI_DMA_TODEVICE
					: PCI_DMA_FROMDEVICE);
#endif
			urb->complete (urb);

			/* implicitly requeued */
  			urb->actual_length = 0;
  			urb->status = -EINPROGRESS;
  			if (urb_priv->state != URB_DEL) {
				spin_lock_irqsave (&hc->lock, flags);
  				td_submit_urb (urb);
  				spin_unlock_irqrestore (&hc->lock, flags);
			}
  			break;

		case PIPE_ISOCHRONOUS:
			for (urbt = urb->next;
					urbt && (urbt != urb);
					urbt = urbt->next)
				continue;
			if (urbt) { /* send the reply and requeue URB */	
#ifdef CONFIG_PCI
// FIXME this style unmap is only done on this route ...
				pci_unmap_single (hc->hcd.pdev,
					urb_priv->td [0]->data_dma,
					urb->transfer_buffer_length,
					usb_pipeout (urb->pipe)
						? PCI_DMA_TODEVICE
						: PCI_DMA_FROMDEVICE);
#endif
				urb->complete (urb);
				spin_lock_irqsave (&hc->lock, flags);
				urb->actual_length = 0;
  				urb->status = -EINPROGRESS;
  				urb->start_frame = urb_priv->ed->last_iso + 1;
  				if (urb_priv->state != URB_DEL) {
  					for (i = 0; i < urb->number_of_packets;
							i++) {
  						urb->iso_frame_desc [i]
							.actual_length = 0;
  						urb->iso_frame_desc [i]
							.status = -EXDEV;
  					}
  					td_submit_urb (urb);
  				}
// FIXME if not deleted, should have been "finished"
  				spin_unlock_irqrestore (&hc->lock, flags);

  			} else { /* not reissued */
				finish_urb (hc, urb);
			}		
			break;

		/*
		 * C/B requests that get here are never reissued.
		 */
		case PIPE_BULK:
		case PIPE_CONTROL:
			finish_urb (hc, urb);
			break;
	}
	return 0;
}


/*-------------------------------------------------------------------------*
 * ED handling functions
 *-------------------------------------------------------------------------*/  

/* search for the right branch to insert an interrupt ed into the int tree 
 * do some load balancing;
 * returns the branch and 
 * sets the interval to interval = 2^integer (ld (interval))
 */
static int ep_int_balance (struct ohci_hcd *ohci, int interval, int load)
{
	int	i, branch = 0;

	/* search for the least loaded interrupt endpoint branch */
	for (i = 0; i < NUM_INTS ; i++) 
		if (ohci->ohci_int_load [branch] > ohci->ohci_int_load [i])
			branch = i; 

	branch = branch % interval;
	for (i = branch; i < NUM_INTS; i += interval)
		ohci->ohci_int_load [i] += load;

	return branch;
}

/*-------------------------------------------------------------------------*/

/*  2^int ( ld (inter)) */

static int ep_2_n_interval (int inter)
{	
	int	i;

	for (i = 0; ((inter >> i) > 1 ) && (i < 5); i++)
		continue;
	return 1 << i;
}

/*-------------------------------------------------------------------------*/

/* the int tree is a binary tree 
 * in order to process it sequentially the indexes of the branches have
 * to be mapped the mapping reverses the bits of a word of num_bits length
 */
static int ep_rev (int num_bits, int word)
{
	int			i, wout = 0;

	for (i = 0; i < num_bits; i++)
		wout |= (( (word >> i) & 1) << (num_bits - i - 1));
	return wout;
}

/*-------------------------------------------------------------------------*/

/* link an ed into one of the HC chains */

static int ep_link (struct ohci_hcd *ohci, struct ed *edi)
{	 
	int			int_branch, i;
	int			inter, interval, load;
	__u32			*ed_p;
	volatile struct ed	*ed = edi;

	ed->state = ED_OPER;

	switch (ed->type) {
	case PIPE_CONTROL:
		ed->hwNextED = 0;
		if (ohci->ed_controltail == NULL) {
			writel (ed->dma, &ohci->regs->ed_controlhead);
		} else {
			ohci->ed_controltail->hwNextED = cpu_to_le32 (ed->dma);
		}
		ed->ed_prev = ohci->ed_controltail;
		if (!ohci->ed_controltail
				&& !ohci->ed_rm_list [0]
				&& !ohci->ed_rm_list [1]
				&& !ohci->sleeping
				) {
			ohci->hc_control |= OHCI_CTRL_CLE;
			writel (ohci->hc_control, &ohci->regs->control);
		}
		ohci->ed_controltail = edi;	  
		break;

	case PIPE_BULK:
		ed->hwNextED = 0;
		if (ohci->ed_bulktail == NULL) {
			writel (ed->dma, &ohci->regs->ed_bulkhead);
		} else {
			ohci->ed_bulktail->hwNextED = cpu_to_le32 (ed->dma);
		}
		ed->ed_prev = ohci->ed_bulktail;
		if (!ohci->ed_bulktail
				&& !ohci->ed_rm_list [0]
				&& !ohci->ed_rm_list [1]
				&& !ohci->sleeping
				) {
			ohci->hc_control |= OHCI_CTRL_BLE;
			writel (ohci->hc_control, &ohci->regs->control);
		}
		ohci->ed_bulktail = edi;	  
		break;

	case PIPE_INTERRUPT:
		load = ed->int_load;
		interval = ep_2_n_interval (ed->int_period);
		ed->int_interval = interval;
		int_branch = ep_int_balance (ohci, interval, load);
		ed->int_branch = int_branch;

		for (i = 0; i < ep_rev (6, interval); i += inter) {
			inter = 1;
			for (ed_p = & (ohci->hcca->int_table [ep_rev (5, i) + int_branch]); 
				 (*ed_p != 0) && ((dma_to_ed (ohci, le32_to_cpup (ed_p)))->int_interval >= interval); 
				ed_p = & ((dma_to_ed (ohci, le32_to_cpup (ed_p)))->hwNextED)) 
					inter = ep_rev (6, (dma_to_ed (ohci, le32_to_cpup (ed_p)))->int_interval);
			ed->hwNextED = *ed_p; 
			*ed_p = cpu_to_le32 (ed->dma);
		}
#ifdef DEBUG
		ep_print_int_eds (ohci, "LINK_INT");
#endif
		break;

	case PIPE_ISOCHRONOUS:
		ed->hwNextED = 0;
		ed->int_interval = 1;
		if (ohci->ed_isotail != NULL) {
			ohci->ed_isotail->hwNextED = cpu_to_le32 (ed->dma);
			ed->ed_prev = ohci->ed_isotail;
		} else {
			for ( i = 0; i < NUM_INTS; i += inter) {
				inter = 1;
				for (ed_p = & (ohci->hcca->int_table [ep_rev (5, i)]); 
					*ed_p != 0; 
					ed_p = & ((dma_to_ed (ohci, le32_to_cpup (ed_p)))->hwNextED)) 
						inter = ep_rev (6, (dma_to_ed (ohci, le32_to_cpup (ed_p)))->int_interval);
				*ed_p = cpu_to_le32 (ed->dma);	
			}	
			ed->ed_prev = NULL;
		}	
		ohci->ed_isotail = edi;  
#ifdef DEBUG
		ep_print_int_eds (ohci, "LINK_ISO");
#endif
		break;
	}	 	
	return 0;
}

/*-------------------------------------------------------------------------*/

/* unlink an ed from one of the HC chains. 
 * just the link to the ed is unlinked.
 * the link from the ed still points to another operational ed or 0
 * so the HC can eventually finish the processing of the unlinked ed
 */
static int ep_unlink (struct ohci_hcd *ohci, struct ed *ed) 
{
	int	int_branch;
	int	i;
	int	inter;
	int	interval;
	__u32	*ed_p;

	ed->hwINFO |= __constant_cpu_to_le32 (OHCI_ED_SKIP);

	switch (ed->type) {
	case PIPE_CONTROL:
		if (ed->ed_prev == NULL) {
			if (!ed->hwNextED) {
				ohci->hc_control &= ~OHCI_CTRL_CLE;
				writel (ohci->hc_control, &ohci->regs->control);
			}
			writel (le32_to_cpup (&ed->hwNextED),
				&ohci->regs->ed_controlhead);
		} else {
			ed->ed_prev->hwNextED = ed->hwNextED;
		}
		if (ohci->ed_controltail == ed) {
			ohci->ed_controltail = ed->ed_prev;
		} else {
			 (dma_to_ed (ohci, le32_to_cpup (&ed->hwNextED)))
			 	->ed_prev = ed->ed_prev;
		}
		break;

	case PIPE_BULK:
		if (ed->ed_prev == NULL) {
			if (!ed->hwNextED) {
				ohci->hc_control &= ~OHCI_CTRL_BLE;
				writel (ohci->hc_control, &ohci->regs->control);
			}
			writel (le32_to_cpup (&ed->hwNextED),
				&ohci->regs->ed_bulkhead);
		} else {
			ed->ed_prev->hwNextED = ed->hwNextED;
		}
		if (ohci->ed_bulktail == ed) {
			ohci->ed_bulktail = ed->ed_prev;
		} else {
			 (dma_to_ed (ohci, le32_to_cpup (&ed->hwNextED)))
			 	->ed_prev = ed->ed_prev;
		}
		break;

	case PIPE_INTERRUPT:
		int_branch = ed->int_branch;
		interval = ed->int_interval;

		for (i = 0; i < ep_rev (6, interval); i += inter) {
			for (ed_p = & (ohci->hcca->int_table [ep_rev (5, i) + int_branch]), inter = 1; 
				 (*ed_p != 0) && (*ed_p != ed->hwNextED); 
				ed_p = & ((dma_to_ed (ohci, le32_to_cpup (ed_p)))->hwNextED), 
				inter = ep_rev (6, (dma_to_ed (ohci, le32_to_cpup (ed_p)))->int_interval)) {				
					if ((dma_to_ed (ohci, le32_to_cpup (ed_p))) == ed) {
			  			*ed_p = ed->hwNextED;		
			  			break;
			  		}
			  }
		}
		for (i = int_branch; i < NUM_INTS; i += interval)
		    ohci->ohci_int_load [i] -= ed->int_load;
#ifdef DEBUG
		ep_print_int_eds (ohci, "UNLINK_INT");
#endif
		break;

	case PIPE_ISOCHRONOUS:
		if (ohci->ed_isotail == ed)
			ohci->ed_isotail = ed->ed_prev;
		if (ed->hwNextED != 0) 
			(dma_to_ed (ohci, le32_to_cpup (&ed->hwNextED)))
		    		->ed_prev = ed->ed_prev;

		if (ed->ed_prev != NULL) {
			ed->ed_prev->hwNextED = ed->hwNextED;
		} else {
			for (i = 0; i < NUM_INTS; i++) {
				for (ed_p = & (ohci->hcca->int_table [ep_rev (5, i)]); 
						*ed_p != 0; 
						ed_p = & ((dma_to_ed (ohci, le32_to_cpup (ed_p)))->hwNextED)) {
					// inter = ep_rev (6, (dma_to_ed (ohci, le32_to_cpup (ed_p)))->int_interval);
					if ((dma_to_ed (ohci, le32_to_cpup (ed_p))) == ed) {
						*ed_p = ed->hwNextED;		
						break;
					}
				}
			}	
		}	
#ifdef DEBUG
		ep_print_int_eds (ohci, "UNLINK_ISO");
#endif
		break;
	}
	ed->state = ED_UNLINK;
	return 0;
}


/*-------------------------------------------------------------------------*/

/* (re)init an endpoint; this _should_ be done once at the
 * usb_set_configuration command, but the USB stack is a bit stateless
 * so we do it at every transaction.
 * if the state of the ed is ED_NEW then a dummy td is added and the
 * state is changed to ED_UNLINK
 * in all other cases the state is left unchanged
 * the ed info fields are set even though most of them should
 * not change
 */
static struct ed *ep_add_ed (
	struct usb_device	*udev,
	unsigned int		pipe,
	int			interval,
	int			load,
	int			mem_flags
) {
   	struct ohci_hcd		*ohci = hcd_to_ohci (udev->bus->hcpriv);
	struct hcd_dev		*dev = (struct hcd_dev *) udev->hcpriv;
	struct td		*td;
	struct ed		*ed; 
	unsigned		ep;
	unsigned long		flags;

	spin_lock_irqsave (&ohci->lock, flags);

	ep = usb_pipeendpoint (pipe) << 1;
	if (!usb_pipecontrol (pipe) && usb_pipeout (pipe))
		ep |= 1;
	if (!(ed = dev->ep [ep])) {
		ed = ed_alloc (ohci, SLAB_ATOMIC);
		if (!ed) {
			/* out of memory */
			spin_unlock_irqrestore (&ohci->lock, flags);
			return NULL;
		}
		dev->ep [ep] = ed;
	}

	if (ed->state & ED_URB_DEL) {
		/* pending unlink request */
		spin_unlock_irqrestore (&ohci->lock, flags);
		return NULL;
	}

	if (ed->state == ED_NEW) {
		ed->hwINFO = __constant_cpu_to_le32 (OHCI_ED_SKIP);
  		/* dummy td; end of td list for ed */
		td = td_alloc (ohci, SLAB_ATOMIC);
 		if (!td) {
			/* out of memory */
			spin_unlock_irqrestore (&ohci->lock, flags);
			return NULL;
		}
		ed->hwTailP = cpu_to_le32 (td->td_dma);
		ed->hwHeadP = ed->hwTailP;	
		ed->state = ED_UNLINK;
		ed->type = usb_pipetype (pipe);
	}

	ohci->dev [usb_pipedevice (pipe)] = udev;

// FIXME:  don't do this if it's linked to the HC,
// we might clobber data toggle or other state ...

	ed->hwINFO = cpu_to_le32 (usb_pipedevice (pipe)
			| usb_pipeendpoint (pipe) << 7
			| (usb_pipeisoc (pipe)? 0x8000: 0)
			| (usb_pipecontrol (pipe)
				? 0: (usb_pipeout (pipe)? 0x800: 0x1000)) 
			| (udev->speed == USB_SPEED_LOW) << 13
			| usb_maxpacket (udev, pipe, usb_pipeout (pipe))
				<< 16);

  	if (ed->type == PIPE_INTERRUPT && ed->state == ED_UNLINK) {
  		ed->int_period = interval;
  		ed->int_load = load;
  	}

	spin_unlock_irqrestore (&ohci->lock, flags);
	return ed; 
}

/*-------------------------------------------------------------------------*/

/* request unlinking of an endpoint from an operational HC.
 * put the ep on the rm_list and stop the bulk or ctrl list 
 * real work is done at the next start frame (SF) hardware interrupt
 */
static void ed_unlink (struct usb_device *usb_dev, struct ed *ed)
{    
	unsigned int		frame;
   	struct ohci_hcd		*ohci = hcd_to_ohci (usb_dev->bus->hcpriv);

	/* already pending? */
	if (ed->state & ED_URB_DEL)
		return;
	ed->state |= ED_URB_DEL;

	ed->hwINFO |= __constant_cpu_to_le32 (OHCI_ED_SKIP);

	switch (ed->type) {
		case PIPE_CONTROL: /* stop control list */
			ohci->hc_control &= ~OHCI_CTRL_CLE;
			writel (ohci->hc_control,
				&ohci->regs->control); 
			break;
		case PIPE_BULK: /* stop bulk list */
			ohci->hc_control &= ~OHCI_CTRL_BLE;
			writel (ohci->hc_control,
				&ohci->regs->control); 
			break;
	}

	frame = le16_to_cpu (ohci->hcca->frame_no) & 0x1;
	ed->ed_rm_list = ohci->ed_rm_list [frame];
	ohci->ed_rm_list [frame] = ed;

	/* enable SOF interrupt */
	if (!ohci->sleeping) {
		writel (OHCI_INTR_SF, &ohci->regs->intrstatus);
		writel (OHCI_INTR_SF, &ohci->regs->intrenable);
	}
}

/*-------------------------------------------------------------------------*
 * TD handling functions
 *-------------------------------------------------------------------------*/

/* enqueue next TD for this URB (OHCI spec 5.2.8.2) */

static void
td_fill (struct ohci_hcd *ohci, unsigned int info,
	dma_addr_t data, int len,
	struct urb *urb, int index)
{
	volatile struct td	*td, *td_pt;
	urb_priv_t		*urb_priv = urb->hcpriv;

	if (index >= urb_priv->length) {
		err ("internal OHCI error: TD index > length");
		return;
	}

	/* use this td as the next dummy */
	td_pt = urb_priv->td [index];
	td_pt->hwNextTD = 0;

	/* fill the old dummy TD */
	td = urb_priv->td [index] = dma_to_td (ohci,
			le32_to_cpup (&urb_priv->ed->hwTailP) & ~0xf);

	td->ed = urb_priv->ed;
	td->next_dl_td = NULL;
	td->index = index;
	td->urb = urb; 
	td->data_dma = data;
	if (!len)
		data = 0;

	td->hwINFO = cpu_to_le32 (info);
	if ((td->ed->type) == PIPE_ISOCHRONOUS) {
		td->hwCBP = cpu_to_le32 (data & 0xFFFFF000);
		td->ed->last_iso = info & 0xffff;
	} else {
		td->hwCBP = cpu_to_le32 (data); 
	}			
	if (data)
		td->hwBE = cpu_to_le32 (data + len - 1);
	else
		td->hwBE = 0;
	td->hwNextTD = cpu_to_le32 (td_pt->td_dma);
	td->hwPSW [0] = cpu_to_le16 ((data & 0x0FFF) | 0xE000);

	/* append to queue */
	td->ed->hwTailP = td->hwNextTD;
}

/*-------------------------------------------------------------------------*/

/* prepare all TDs of a transfer */

static void td_submit_urb (struct urb *urb)
{ 
	urb_priv_t	*urb_priv = urb->hcpriv;
	struct ohci_hcd *ohci = hcd_to_ohci (urb->dev->bus->hcpriv);
	dma_addr_t	data;
	int		data_len = urb->transfer_buffer_length;
	int		cnt = 0; 
	__u32		info = 0;
  	unsigned int	toggle = 0;

	/* OHCI handles the DATA-toggles itself, we just use the
	 * USB-toggle bits for resetting
	 */
  	if (usb_gettoggle (urb->dev, usb_pipeendpoint (urb->pipe),
			usb_pipeout (urb->pipe))) {
  		toggle = TD_T_TOGGLE;
	} else {
  		toggle = TD_T_DATA0;
		usb_settoggle (urb->dev, usb_pipeendpoint (urb->pipe),
			usb_pipeout (urb->pipe), 1);
	}

	urb_priv->td_cnt = 0;

	if (data_len) {
#ifdef CONFIG_PCI
		data = pci_map_single (ohci->hcd.pdev,
			urb->transfer_buffer, data_len,
			usb_pipeout (urb->pipe)
				? PCI_DMA_TODEVICE
				: PCI_DMA_FROMDEVICE
			);
#else
#	error "what dma addr to use"
#endif
	} else
		data = 0;

	switch (usb_pipetype (urb->pipe)) {
		case PIPE_BULK:
			info = usb_pipeout (urb->pipe)
				? TD_CC | TD_DP_OUT
				: TD_CC | TD_DP_IN ;
			while (data_len > 4096) {		
				td_fill (ohci,
					info | (cnt? TD_T_TOGGLE:toggle),
					data, 4096, urb, cnt);
				data += 4096; data_len -= 4096; cnt++;
			}
			info = usb_pipeout (urb->pipe)?
				TD_CC | TD_DP_OUT : TD_CC | TD_R | TD_DP_IN ;
			td_fill (ohci, info | (cnt? TD_T_TOGGLE:toggle),
				data, data_len, urb, cnt);
			cnt++;
			if ((urb->transfer_flags & USB_ZERO_PACKET)
					&& cnt < urb_priv->length) {
				td_fill (ohci, info | (cnt? TD_T_TOGGLE:toggle),
					0, 0, urb, cnt);
				cnt++;
			}
			/* start bulk list */
			if (!ohci->sleeping)
				writel (OHCI_BLF, &ohci->regs->cmdstatus);
			break;

		case PIPE_INTERRUPT:
			info = TD_CC | toggle;
			info |= usb_pipeout (urb->pipe) 
				?  TD_DP_OUT
				:  TD_R | TD_DP_IN;
			td_fill (ohci, info, data, data_len, urb, cnt++);
			break;

		case PIPE_CONTROL:
			info = TD_CC | TD_DP_SETUP | TD_T_DATA0;
			td_fill (ohci, info,
#ifdef CONFIG_PCI
				pci_map_single (ohci->hcd.pdev,
					urb->setup_packet, 8,
					PCI_DMA_TODEVICE),
#else
#	error "what dma addr to use"				
#endif
				8, urb, cnt++); 
			if (data_len > 0) {  
				info = TD_CC | TD_R | TD_T_DATA1;
				info |= usb_pipeout (urb->pipe)
				    ? TD_DP_OUT
				    : TD_DP_IN;
				/* NOTE:  mishandles transfers >8K, some >4K */
				td_fill (ohci, info, data, data_len,
						urb, cnt++);  
			} 
			info = usb_pipeout (urb->pipe)
				? TD_CC | TD_DP_IN | TD_T_DATA1
				: TD_CC | TD_DP_OUT | TD_T_DATA1;
			td_fill (ohci, info, data, 0, urb, cnt++);
			/* start control list */
			if (!ohci->sleeping)
				writel (OHCI_CLF, &ohci->regs->cmdstatus);
			break;

		case PIPE_ISOCHRONOUS:
			for (cnt = 0; cnt < urb->number_of_packets; cnt++) {
				td_fill (ohci, TD_CC | TD_ISO
					| ((urb->start_frame + cnt) & 0xffff), 
				    data + urb->iso_frame_desc [cnt].offset, 
				    urb->iso_frame_desc [cnt].length, urb, cnt); 
			}
			break;
	} 
	if (urb_priv->length != cnt) 
		dbg ("TD LENGTH %d != CNT %d", urb_priv->length, cnt);
}

/*-------------------------------------------------------------------------*
 * Done List handling functions
 *-------------------------------------------------------------------------*/

/* calculate the transfer length and update the urb */

static void dl_transfer_length (struct td *td)
{
	__u32		tdINFO, tdBE, tdCBP;
 	__u16		tdPSW;
 	struct urb	*urb = td->urb;
 	urb_priv_t	*urb_priv = urb->hcpriv;
	int		dlen = 0;
	int		cc = 0;

	tdINFO = le32_to_cpup (&td->hwINFO);
  	tdBE   = le32_to_cpup (&td->hwBE);
  	tdCBP  = le32_to_cpup (&td->hwCBP);


  	if (tdINFO & TD_ISO) {
 		tdPSW = le16_to_cpu (td->hwPSW [0]);
 		cc = (tdPSW >> 12) & 0xF;
		if (cc < 0xE)  {
			if (usb_pipeout (urb->pipe)) {
				dlen = urb->iso_frame_desc [td->index].length;
			} else {
				dlen = tdPSW & 0x3ff;
			}
			urb->actual_length += dlen;
			urb->iso_frame_desc [td->index].actual_length = dlen;
			if (! (urb->transfer_flags & USB_DISABLE_SPD)
					&& (cc == TD_DATAUNDERRUN))
				cc = TD_CC_NOERROR;

			urb->iso_frame_desc [td->index].status
				= cc_to_error [cc];
		}
	} else { /* BULK, INT, CONTROL DATA */
		if (! (usb_pipetype (urb->pipe) == PIPE_CONTROL && 
				 ((td->index == 0)
				 || (td->index == urb_priv->length - 1)))) {
 			if (tdBE != 0) {
				urb->actual_length += (td->hwCBP == 0)
					? (tdBE - td->data_dma + 1)
					: (tdCBP - td->data_dma);
			}
  		}
  	}
}

/*-------------------------------------------------------------------------*/

/* replies to the request have to be on a FIFO basis so
 * we unreverse the hc-reversed done-list
 */
static struct td *dl_reverse_done_list (struct ohci_hcd *ohci)
{
	__u32		td_list_hc;
	struct td	*td_rev = NULL;
	struct td	*td_list = NULL;
  	urb_priv_t	*urb_priv = NULL;
  	unsigned long	flags;

  	spin_lock_irqsave (&ohci->lock, flags);

	td_list_hc = le32_to_cpup (&ohci->hcca->done_head) & 0xfffffff0;
	ohci->hcca->done_head = 0;

	while (td_list_hc) {		
		td_list = dma_to_td (ohci, td_list_hc);

		if (TD_CC_GET (le32_to_cpup (&td_list->hwINFO))) {
			urb_priv = (urb_priv_t *) td_list->urb->hcpriv;
			dbg (" USB-error/status: %x : %p", 
				TD_CC_GET (le32_to_cpup (&td_list->hwINFO)),
				td_list);
			if (td_list->ed->hwHeadP
					& __constant_cpu_to_le32 (0x1)) {
				if (urb_priv && ((td_list->index + 1)
						< urb_priv->length)) {
					td_list->ed->hwHeadP = 
			    (urb_priv->td [urb_priv->length - 1]->hwNextTD
				    & __constant_cpu_to_le32 (0xfffffff0))
			    | (td_list->ed->hwHeadP
				    & __constant_cpu_to_le32 (0x2));
					urb_priv->td_cnt += urb_priv->length
						- td_list->index - 1;
				} else 
					td_list->ed->hwHeadP &=
					__constant_cpu_to_le32 (0xfffffff2);
			}
		}

		td_list->next_dl_td = td_rev;	
		td_rev = td_list;
		td_list_hc = le32_to_cpup (&td_list->hwNextTD) & 0xfffffff0;	
	}	
	spin_unlock_irqrestore (&ohci->lock, flags);
	return td_list;
}

/*-------------------------------------------------------------------------*/

/* there are some pending requests to unlink 
 * - some URBs/TDs if urb_priv->state == URB_DEL
 */
static void dl_del_list (struct ohci_hcd *ohci, unsigned int frame)
{
	unsigned long	flags;
	struct ed	*ed;
	__u32		edINFO;
	__u32		tdINFO;
	struct td	*td = NULL, *td_next = NULL,
			*tdHeadP = NULL, *tdTailP;
	__u32		*td_p;
	int		ctrl = 0, bulk = 0;

	spin_lock_irqsave (&ohci->lock, flags);

	for (ed = ohci->ed_rm_list [frame]; ed != NULL; ed = ed->ed_rm_list) {

		tdTailP = dma_to_td (ohci,
			le32_to_cpup (&ed->hwTailP) & 0xfffffff0);
		tdHeadP = dma_to_td (ohci,
			le32_to_cpup (&ed->hwHeadP) & 0xfffffff0);
		edINFO = le32_to_cpup (&ed->hwINFO);
		td_p = &ed->hwHeadP;

		for (td = tdHeadP; td != tdTailP; td = td_next) {
			struct urb *urb = td->urb;
			urb_priv_t *urb_priv = td->urb->hcpriv;

			td_next = dma_to_td (ohci,
				le32_to_cpup (&td->hwNextTD) & 0xfffffff0);
			if ((urb_priv->state == URB_DEL)) {
				tdINFO = le32_to_cpup (&td->hwINFO);
				if (TD_CC_GET (tdINFO) < 0xE)
					dl_transfer_length (td);
				*td_p = td->hwNextTD | (*td_p
					& __constant_cpu_to_le32 (0x3));

				/* URB is done; clean up */
				if (++ (urb_priv->td_cnt) == urb_priv->length)
// FIXME:  we shouldn't hold ohci->lock here, else the
// completion function can't talk to this hcd ...
					finish_urb (ohci, urb);
			} else {
				td_p = &td->hwNextTD;
			}
		}

		ed->state &= ~ED_URB_DEL;
		tdHeadP = dma_to_td (ohci,
			le32_to_cpup (&ed->hwHeadP) & 0xfffffff0);

		if (tdHeadP == tdTailP) {
			if (ed->state == ED_OPER)
				ep_unlink (ohci, ed);
			td_free (ohci, tdTailP);
			ed->hwINFO = __constant_cpu_to_le32 (OHCI_ED_SKIP);
			ed->state = ED_NEW;
		} else
			ed->hwINFO &= ~__constant_cpu_to_le32 (OHCI_ED_SKIP);

		switch (ed->type) {
			case PIPE_CONTROL:
				ctrl = 1;
				break;
			case PIPE_BULK:
				bulk = 1;
				break;
		}
   	}

	/* maybe reenable control and bulk lists */ 
	if (!ohci->disabled) {
		if (ctrl) 	/* reset control list */
			writel (0, &ohci->regs->ed_controlcurrent);
		if (bulk)	/* reset bulk list */
			writel (0, &ohci->regs->ed_bulkcurrent);
		if (!ohci->ed_rm_list [!frame]) {
			if (ohci->ed_controltail)
				ohci->hc_control |= OHCI_CTRL_CLE;
			if (ohci->ed_bulktail)
				ohci->hc_control |= OHCI_CTRL_BLE;
			writel (ohci->hc_control, &ohci->regs->control);   
		}
	}

   	ohci->ed_rm_list [frame] = NULL;
   	spin_unlock_irqrestore (&ohci->lock, flags);
}



/*-------------------------------------------------------------------------*/

/*
 * process normal completions (error or success) and some unlinked eds
 * this is the main path for handing urbs back to drivers
 */
static void dl_done_list (struct ohci_hcd *ohci, struct td *td_list)
{
  	struct td	*td_list_next = NULL;
	struct ed	*ed;
	int		cc = 0;
	struct urb	*urb;
	urb_priv_t	*urb_priv;
 	__u32		tdINFO, edHeadP, edTailP;

 	unsigned long flags;

  	while (td_list) {
   		td_list_next = td_list->next_dl_td;

  		urb = td_list->urb;
  		urb_priv = urb->hcpriv;
  		tdINFO = le32_to_cpup (&td_list->hwINFO);

   		ed = td_list->ed;

   		dl_transfer_length (td_list);

  		/* error code of transfer */
  		cc = TD_CC_GET (tdINFO);
  		if (cc == TD_CC_STALL)
			usb_endpoint_halt (urb->dev,
				usb_pipeendpoint (urb->pipe),
				usb_pipeout (urb->pipe));

  		if (! (urb->transfer_flags & USB_DISABLE_SPD)
				&& (cc == TD_DATAUNDERRUN))
			cc = TD_CC_NOERROR;

  		if (++ (urb_priv->td_cnt) == urb_priv->length) {
			/*
			 * Except for periodic transfers, both branches do
			 * the same thing.  Periodic urbs get reissued until
			 * they're "deleted" with usb_unlink_urb.
			 */
			if ((ed->state & (ED_OPER | ED_UNLINK))
					&& (urb_priv->state != URB_DEL)) {
				spin_lock (&urb->lock);
				if (urb->status == -EINPROGRESS)
  					urb->status = cc_to_error [cc];
				spin_unlock (&urb->lock);
  				return_urb (ohci, urb);
  			} else
  				finish_urb (ohci, urb);
  		}

  		spin_lock_irqsave (&ohci->lock, flags);
  		if (ed->state != ED_NEW) { 
  			edHeadP = le32_to_cpup (&ed->hwHeadP) & 0xfffffff0;
  			edTailP = le32_to_cpup (&ed->hwTailP);

// FIXME:  ED_UNLINK is very fuzzy w.r.t. what the hc knows...

			/* unlink eds if they are not busy */
     			if ((edHeadP == edTailP) && (ed->state == ED_OPER)) 
     				ep_unlink (ohci, ed);
     		}	
     		spin_unlock_irqrestore (&ohci->lock, flags);

    		td_list = td_list_next;
  	}  
}

