/*
 * OHCI HCD (Host Controller Driver) for USB.
 * 
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * 
 * This file is licenced under GPL
 */

/*-------------------------------------------------------------------------*/

/*
 * OHCI Root Hub ... the nonsharable stuff
 *
 * Registers don't need cpu_to_le32, that happens transparently
 */

/* AMD-756 (D2 rev) reports corrupt register contents in some cases.
 * The erratum (#4) description is incorrect.  AMD's workaround waits
 * till some bits (mostly reserved) are clear; ok for all revs.
 */
#define read_roothub(hc, register, mask) ({ \
	u32 temp = readl (&hc->regs->roothub.register); \
	if (temp == -1) \
		disable (hc); \
	else if (hc->flags & OHCI_QUIRK_AMD756) \
		while (temp & mask) \
			temp = readl (&hc->regs->roothub.register); \
	temp; })

static u32 roothub_a (struct ohci_hcd *hc)
	{ return read_roothub (hc, a, 0xfc0fe000); }
static inline u32 roothub_b (struct ohci_hcd *hc)
	{ return readl (&hc->regs->roothub.b); }
static inline u32 roothub_status (struct ohci_hcd *hc)
	{ return readl (&hc->regs->roothub.status); }
static u32 roothub_portstatus (struct ohci_hcd *hc, int i)
	{ return read_roothub (hc, portstatus [i], 0xffe0fce0); }

/*-------------------------------------------------------------------------*/

#define dbg_port(hc,label,num,value) \
	ohci_dbg (hc, \
		"%s roothub.portstatus [%d] " \
		"= 0x%08x%s%s%s%s%s%s%s%s%s%s%s%s\n", \
		label, num, temp, \
		(temp & RH_PS_PRSC) ? " PRSC" : "", \
		(temp & RH_PS_OCIC) ? " OCIC" : "", \
		(temp & RH_PS_PSSC) ? " PSSC" : "", \
		(temp & RH_PS_PESC) ? " PESC" : "", \
		(temp & RH_PS_CSC) ? " CSC" : "", \
 		\
		(temp & RH_PS_LSDA) ? " LSDA" : "", \
		(temp & RH_PS_PPS) ? " PPS" : "", \
		(temp & RH_PS_PRS) ? " PRS" : "", \
		(temp & RH_PS_POCI) ? " POCI" : "", \
		(temp & RH_PS_PSS) ? " PSS" : "", \
 		\
		(temp & RH_PS_PES) ? " PES" : "", \
		(temp & RH_PS_CCS) ? " CCS" : "" \
		);

/*-------------------------------------------------------------------------*/

#if	defined(CONFIG_USB_SUSPEND) || defined(CONFIG_PM)

#define OHCI_SCHED_ENABLES \
	(OHCI_CTRL_CLE|OHCI_CTRL_BLE|OHCI_CTRL_PLE|OHCI_CTRL_IE)

static void dl_done_list (struct ohci_hcd *, struct pt_regs *);
static void finish_unlinks (struct ohci_hcd *, u16 , struct pt_regs *);

static int ohci_hub_suspend (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	struct usb_device	*root = hcd_to_bus (&ohci->hcd)->root_hub;
	int			status = 0;

	if (root->dev.power.power_state != 0)
		return 0;
	if (time_before (jiffies, ohci->next_statechange))
		return -EAGAIN;

	spin_lock_irq (&ohci->lock);

	ohci->hc_control = readl (&ohci->regs->control);
	switch (ohci->hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_RESUME:
		ohci_dbg (ohci, "resume/suspend?\n");
		ohci->hc_control &= ~OHCI_CTRL_HCFS;
		ohci->hc_control |= OHCI_USB_RESET;
		writel (ohci->hc_control, &ohci->regs->control);
		(void) readl (&ohci->regs->control);
		/* FALL THROUGH */
	case OHCI_USB_RESET:
		status = -EBUSY;
		ohci_dbg (ohci, "needs reinit!\n");
		goto done;
	case OHCI_USB_SUSPEND:
		ohci_dbg (ohci, "already suspended?\n");
		goto succeed;
	}
	ohci_dbg (ohci, "suspend root hub\n");

	/* First stop any processing */
	ohci->hcd.state = USB_STATE_QUIESCING;
	if (ohci->hc_control & OHCI_SCHED_ENABLES) {
		int		limit;

		ohci->hc_control &= ~OHCI_SCHED_ENABLES;
		writel (ohci->hc_control, &ohci->regs->control);
		ohci->hc_control = readl (&ohci->regs->control);
		writel (OHCI_INTR_SF, &ohci->regs->intrstatus);

		/* sched disables take effect on the next frame,
		 * then the last WDH could take 6+ msec
		 */
		ohci_dbg (ohci, "stopping schedules ...\n");
		limit = 2000;
		while (limit > 0) {
			udelay (250);
			limit =- 250;
			if (readl (&ohci->regs->intrstatus) & OHCI_INTR_SF)
				break;
		}
		dl_done_list (ohci, 0);
		mdelay (7);
	}
	dl_done_list (ohci, 0);
	finish_unlinks (ohci, OHCI_FRAME_NO(ohci->hcca), 0);
	writel (readl (&ohci->regs->intrstatus), &ohci->regs->intrstatus);

	/* maybe resume can wake root hub */
	if (ohci->hcd.remote_wakeup)
		ohci->hc_control |= OHCI_CTRL_RWE;
	else
		ohci->hc_control &= ~OHCI_CTRL_RWE;

	/* Suspend hub */
	ohci->hc_control &= ~OHCI_CTRL_HCFS;
	ohci->hc_control |= OHCI_USB_SUSPEND;
	writel (ohci->hc_control, &ohci->regs->control);
	(void) readl (&ohci->regs->control);

	/* no resumes until devices finish suspending */
	ohci->next_statechange = jiffies + MSEC_TO_JIFFIES (5);

succeed:
	/* it's not USB_STATE_SUSPENDED unless access to this
	 * hub from the non-usb side (PCI, SOC, etc) stopped 
	 */
	root->dev.power.power_state = 3;
done:
	spin_unlock_irq (&ohci->lock);
	return status;
}

static inline struct ed *find_head (struct ed *ed)
{
	/* for bulk and control lists */
	while (ed->ed_prev)
		ed = ed->ed_prev;
	return ed;
}

static int hc_restart (struct ohci_hcd *ohci);

/* caller owns root->serialize */
static int ohci_hub_resume (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	struct usb_device	*root = hcd_to_bus (&ohci->hcd)->root_hub;
	u32			temp, enables;
	int			status = -EINPROGRESS;

	if (!root->dev.power.power_state)
		return 0;
	if (time_before (jiffies, ohci->next_statechange))
		return -EAGAIN;

	spin_lock_irq (&ohci->lock);
	ohci->hc_control = readl (&ohci->regs->control);
	switch (ohci->hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_SUSPEND:
		ohci->hc_control &= ~(OHCI_CTRL_HCFS|OHCI_SCHED_ENABLES);
		ohci->hc_control |= OHCI_USB_RESUME;
		writel (ohci->hc_control, &ohci->regs->control);
		(void) readl (&ohci->regs->control);
		ohci_dbg (ohci, "resume root hub\n");
		break;
	case OHCI_USB_RESUME:
		/* HCFS changes sometime after INTR_RD */
		ohci_info (ohci, "remote wakeup\n");
		break;
	case OHCI_USB_OPER:
		ohci_dbg (ohci, "odd resume\n");
		root->dev.power.power_state = 0;
		status = 0;
		break;
	default:		/* RESET, we lost power */
		ohci_dbg (ohci, "root hub hardware reset\n");
		status = -EBUSY;
	}
	spin_unlock_irq (&ohci->lock);
	if (status == -EBUSY)
		return hc_restart (ohci);
	if (status != -EINPROGRESS)
		return status;

	temp = roothub_a (ohci) & RH_A_NDP;
	enables = 0;
	while (temp--) {
		u32 stat = readl (&ohci->regs->roothub.portstatus [temp]);

		/* force global, not selective, resume */
		if (!(stat & RH_PS_PSS))
			continue;
		writel (RH_PS_POCI, &ohci->regs->roothub.portstatus [temp]);
	}

	/* Some controllers (lucent) need extra-long delays */
	ohci->hcd.state = USB_STATE_RESUMING;
	mdelay (20 /* usb 11.5.1.10 */ + 15);

	temp = readl (&ohci->regs->control);
	temp &= OHCI_CTRL_HCFS;
	if (temp != OHCI_USB_RESUME) {
		ohci_err (ohci, "controller won't resume\n");
		return -EBUSY;
	}

	/* disable old schedule state, reinit from scratch */
	writel (0, &ohci->regs->ed_controlhead);
	writel (0, &ohci->regs->ed_controlcurrent);
	writel (0, &ohci->regs->ed_bulkhead);
	writel (0, &ohci->regs->ed_bulkcurrent);
	writel (0, &ohci->regs->ed_periodcurrent);
	writel ((u32) ohci->hcca_dma, &ohci->regs->hcca);

	periodic_reinit (ohci);

	/* interrupts might have been disabled */
	writel (OHCI_INTR_INIT, &ohci->regs->intrenable);
	if (ohci->ed_rm_list)
		writel (OHCI_INTR_SF, &ohci->regs->intrenable);
	writel (readl (&ohci->regs->intrstatus), &ohci->regs->intrstatus);

	/* Then re-enable operations */
	writel (OHCI_USB_OPER, &ohci->regs->control);
	(void) readl (&ohci->regs->control);
	msec_delay (3);

	temp = OHCI_CONTROL_INIT | OHCI_USB_OPER;
	if (ohci->hcd.can_wakeup)
		temp |= OHCI_CTRL_RWC;
	ohci->hc_control = temp;
	writel (temp, &ohci->regs->control);
	(void) readl (&ohci->regs->control);

	/* TRSMRCY */
	msec_delay (10);
	root->dev.power.power_state = 0;

	/* keep it alive for ~5x suspend + resume costs */
	ohci->next_statechange = jiffies + MSEC_TO_JIFFIES (250);

	/* maybe turn schedules back on */
	enables = 0;
	temp = 0;
	if (!ohci->ed_rm_list) {
		if (ohci->ed_controltail) {
			writel (find_head (ohci->ed_controltail)->dma,
				&ohci->regs->ed_controlhead);
			enables |= OHCI_CTRL_CLE;
			temp |= OHCI_CLF;
		}
		if (ohci->ed_bulktail) {
			writel (find_head (ohci->ed_bulktail)->dma,
				&ohci->regs->ed_bulkhead);
			enables |= OHCI_CTRL_BLE;
			temp |= OHCI_BLF;
		}
	}
	if (hcd_to_bus (&ohci->hcd)->bandwidth_isoc_reqs
			|| hcd_to_bus (&ohci->hcd)->bandwidth_int_reqs)
		enables |= OHCI_CTRL_PLE|OHCI_CTRL_IE;
	if (enables) {
		ohci_dbg (ohci, "restarting schedules ... %08x\n", enables);
		ohci->hc_control |= enables;
		writel (ohci->hc_control, &ohci->regs->control);
		if (temp)
			writel (status, &ohci->regs->cmdstatus);
		(void) readl (&ohci->regs->control);
	}

	ohci->hcd.state = USB_STATE_RUNNING;
	return 0;
}

static void ohci_rh_resume (void *_hcd)
{
	struct usb_hcd	*hcd = _hcd;

	down (&hcd->self.root_hub->serialize);
	(void) ohci_hub_resume (hcd);
	up (&hcd->self.root_hub->serialize);
}

#else

static void ohci_rh_resume (void *_hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (_hcd);
	ohci_dbg(ohci, "rh_resume ??\n");
}

#endif	/* CONFIG_USB_SUSPEND || CONFIG_PM */

/*-------------------------------------------------------------------------*/

/* build "status change" packet (one or two bytes) from HC registers */

static int
ohci_hub_status_data (struct usb_hcd *hcd, char *buf)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ports, i, changed = 0, length = 1;
	int		can_suspend = 1;

	ports = roothub_a (ohci) & RH_A_NDP; 
	if (ports > MAX_ROOT_PORTS) {
		if (!HCD_IS_RUNNING(ohci->hcd.state))
			return -ESHUTDOWN;
		ohci_err (ohci, "bogus NDP=%d, rereads as NDP=%d\n",
			ports, readl (&ohci->regs->roothub.a) & RH_A_NDP);
		/* retry later; "should not happen" */
		return 0;
	}

	/* init status */
	if (roothub_status (ohci) & (RH_HS_LPSC | RH_HS_OCIC))
		buf [0] = changed = 1;
	else
		buf [0] = 0;
	if (ports > 7) {
		buf [1] = 0;
		length++;
	}

	/* look at each port */
	for (i = 0; i < ports; i++) {
		u32	status = roothub_portstatus (ohci, i);

		if (status & (RH_PS_CSC | RH_PS_PESC | RH_PS_PSSC
				| RH_PS_OCIC | RH_PS_PRSC)) {
			changed = 1;
			if (i < 7)
			    buf [0] |= 1 << (i + 1);
			else
			    buf [1] |= 1 << (i - 7);
			continue;
		}

		/* can suspend if no ports are enabled; or if all all
		 * enabled ports are suspended AND remote wakeup is on.
		 */
		if (!(status & RH_PS_CCS))
			continue;
		if ((status & RH_PS_PSS) && ohci->hcd.remote_wakeup)
			continue;
		can_suspend = 0;
	}

#ifdef CONFIG_PM
	/* save power by suspending idle root hubs;
	 * INTR_RD wakes us when there's work
	 */
	if (can_suspend
			&& !changed
			&& !ohci->ed_rm_list
			&& ((OHCI_CTRL_HCFS | OHCI_SCHED_ENABLES)
					& ohci->hc_control)
				== OHCI_USB_OPER
			&& down_trylock (&hcd->self.root_hub->serialize) == 0
			) {
		ohci_vdbg (ohci, "autosuspend\n");
		(void) ohci_hub_suspend (&ohci->hcd);
		ohci->hcd.state = USB_STATE_RUNNING;
		up (&hcd->self.root_hub->serialize);
	}
#endif

	return changed ? length : 0;
}

/*-------------------------------------------------------------------------*/

static void
ohci_hub_descriptor (
	struct ohci_hcd			*ohci,
	struct usb_hub_descriptor	*desc
) {
	u32		rh = roothub_a (ohci);
	int		ports = rh & RH_A_NDP; 
	u16		temp;

	desc->bDescriptorType = 0x29;
	desc->bPwrOn2PwrGood = (rh & RH_A_POTPGT) >> 24;
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = ports;
	temp = 1 + (ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	temp = 0;
	if (rh & RH_A_NPS)		/* no power switching? */
	    temp |= 0x0002;
	if (rh & RH_A_PSM) 		/* per-port power switching? */
	    temp |= 0x0001;
	if (rh & RH_A_NOCP)		/* no overcurrent reporting? */
	    temp |= 0x0010;
	else if (rh & RH_A_OCPM)	/* per-port overcurrent reporting? */
	    temp |= 0x0008;
	desc->wHubCharacteristics = cpu_to_le16 (temp);

	/* two bitmaps:  ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	rh = roothub_b (ohci);
	desc->bitmap [0] = rh & RH_B_DR;
	if (ports > 7) {
		desc->bitmap [1] = (rh & RH_B_DR) >> 8;
		desc->bitmap [2] = desc->bitmap [3] = 0xff;
	} else
		desc->bitmap [1] = 0xff;
}

/*-------------------------------------------------------------------------*/

static int ohci_hub_control (
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
) {
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ports = hcd_to_bus (hcd)->root_hub->maxchild;
	u32		temp;
	int		retval = 0;

	switch (typeReq) {
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
			writel (RH_HS_OCIC, &ohci->regs->roothub.status);
		case C_HUB_LOCAL_POWER:
			break;
		default:
			goto error;
		}
		break;
	case ClearPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			temp = RH_PS_CCS;
			break;
		case USB_PORT_FEAT_C_ENABLE:
			temp = RH_PS_PESC;
			break;
		case USB_PORT_FEAT_SUSPEND:
			temp = RH_PS_POCI;
			if ((ohci->hc_control & OHCI_CTRL_HCFS)
					!= OHCI_USB_OPER)
				schedule_work (&ohci->rh_resume);
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			temp = RH_PS_PSSC;
			break;
		case USB_PORT_FEAT_POWER:
			temp = RH_PS_LSDA;
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			temp = RH_PS_CSC;
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			temp = RH_PS_OCIC;
			break;
		case USB_PORT_FEAT_C_RESET:
			temp = RH_PS_PRSC;
			break;
		default:
			goto error;
		}
		writel (temp, &ohci->regs->roothub.portstatus [wIndex]);
		// readl (&ohci->regs->roothub.portstatus [wIndex]);
		break;
	case GetHubDescriptor:
		ohci_hub_descriptor (ohci, (struct usb_hub_descriptor *) buf);
		break;
	case GetHubStatus:
		temp = roothub_status (ohci) & ~(RH_HS_CRWE | RH_HS_DRWE);
		*(u32 *) buf = cpu_to_le32 (temp);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		temp = roothub_portstatus (ohci, wIndex);
		*(u32 *) buf = cpu_to_le32 (temp);

#ifndef	OHCI_VERBOSE_DEBUG
	if (*(u16*)(buf+2))	/* only if wPortChange is interesting */
#endif
		dbg_port (ohci, "GetStatus", wIndex + 1, temp);
		break;
	case SetHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
			// FIXME:  this can be cleared, yes?
		case C_HUB_LOCAL_POWER:
			break;
		default:
			goto error;
		}
		break;
	case SetPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			writel (RH_PS_PSS,
				&ohci->regs->roothub.portstatus [wIndex]);
			break;
		case USB_PORT_FEAT_POWER:
			writel (RH_PS_PPS,
				&ohci->regs->roothub.portstatus [wIndex]);
			break;
		case USB_PORT_FEAT_RESET:
			temp = readl (&ohci->regs->roothub.portstatus [wIndex]);
			if (temp & RH_PS_CCS)
				writel (RH_PS_PRS,
				    &ohci->regs->roothub.portstatus [wIndex]);
			break;
		default:
			goto error;
		}
		break;

	default:
error:
		/* "protocol stall" on error */
		retval = -EPIPE;
	}
	return retval;
}

