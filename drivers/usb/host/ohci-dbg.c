/*
 * OHCI HCD (Host Controller Driver) for USB.
 * 
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * 
 * This file is licenced under the GPL.
 * $Id: ohci-dbg.c,v 1.4 2002/03/27 20:40:40 dbrownell Exp $
 */
 
/*-------------------------------------------------------------------------*/
 
#ifdef DEBUG

#define pipestring(pipe) ({ char *temp; \
	switch (usb_pipetype (pipe)) { \
	case PIPE_CONTROL:	temp = "CTRL"; break; \
	case PIPE_BULK:		temp = "BULK"; break; \
	case PIPE_INTERRUPT:	temp = "INTR"; break; \
	default: 		temp = "ISOC"; break; \
	}; temp;})

/* debug| print the main components of an URB     
 * small: 0) header + data packets 1) just header
 */
static void urb_print (struct urb * urb, char * str, int small)
{
	unsigned int pipe= urb->pipe;
	
	if (!urb->dev || !urb->dev->bus) {
		dbg("%s URB: no dev", str);
		return;
	}
	
#ifndef	OHCI_VERBOSE_DEBUG
	if (urb->status != 0)
#endif
	dbg("%s:[%4x] dev:%d,ep=%d-%c,%s,flags:%4x,len:%d/%d,stat:%d", 
		    str,
		    usb_get_current_frame_number (urb->dev), 
		    usb_pipedevice (pipe),
		    usb_pipeendpoint (pipe), 
		    usb_pipeout (pipe)? 'O': 'I',
		    pipestring (pipe),
		    urb->transfer_flags, 
		    urb->actual_length, 
		    urb->transfer_buffer_length,
		    urb->status);

#ifdef	OHCI_VERBOSE_DEBUG
	if (!small) {
		int i, len;

		if (usb_pipecontrol (pipe)) {
			printk (KERN_DEBUG __FILE__ ": setup(8):");
			for (i = 0; i < 8 ; i++) 
				printk (" %02x", ((__u8 *) urb->setup_packet) [i]);
			printk ("\n");
		}
		if (urb->transfer_buffer_length > 0 && urb->transfer_buffer) {
			printk (KERN_DEBUG __FILE__ ": data(%d/%d):", 
				urb->actual_length, 
				urb->transfer_buffer_length);
			len = usb_pipeout (pipe)? 
						urb->transfer_buffer_length: urb->actual_length;
			for (i = 0; i < 16 && i < len; i++) 
				printk (" %02x", ((__u8 *) urb->transfer_buffer) [i]);
			printk ("%s stat:%d\n", i < len? "...": "", urb->status);
		}
	} 
#endif
}

static inline struct ed *
dma_to_ed (struct ohci_hcd *hc, dma_addr_t ed_dma);

/* print non-empty branches of the periodic ed tree */
static void __attribute__ ((unused))
ohci_dump_periodic (struct ohci_hcd *ohci, char *label)
{
	int i, j;
	u32 *ed_p;
	int printed = 0;

	for (i= 0; i < 32; i++) {
		j = 5;
		ed_p = &(ohci->hcca->int_table [i]);
		if (*ed_p == 0)
			continue;
		printed = 1;
		printk (KERN_DEBUG "%s, ohci %s frame %2d:",
				label, ohci->hcd.self.bus_name, i);
		while (*ed_p != 0 && j--) {
			struct ed *ed = dma_to_ed (ohci, le32_to_cpup(ed_p));
			printk (" %p/%08x;", ed, ed->hwINFO);
			ed_p = &ed->hwNextED;
		}
		printk ("\n");
	}
	if (!printed)
		printk (KERN_DEBUG "%s, ohci %s, empty periodic schedule\n",
				label, ohci->hcd.self.bus_name);
}

static void ohci_dump_intr_mask (char *label, __u32 mask)
{
	dbg ("%s: 0x%08x%s%s%s%s%s%s%s%s%s",
		label,
		mask,
		(mask & OHCI_INTR_MIE) ? " MIE" : "",
		(mask & OHCI_INTR_OC) ? " OC" : "",
		(mask & OHCI_INTR_RHSC) ? " RHSC" : "",
		(mask & OHCI_INTR_FNO) ? " FNO" : "",
		(mask & OHCI_INTR_UE) ? " UE" : "",
		(mask & OHCI_INTR_RD) ? " RD" : "",
		(mask & OHCI_INTR_SF) ? " SF" : "",
		(mask & OHCI_INTR_WDH) ? " WDH" : "",
		(mask & OHCI_INTR_SO) ? " SO" : ""
		);
}

static void maybe_print_eds (char *label, __u32 value)
{
	if (value)
		dbg ("%s %08x", label, value);
}

static char *hcfs2string (int state)
{
	switch (state) {
		case OHCI_USB_RESET:	return "reset";
		case OHCI_USB_RESUME:	return "resume";
		case OHCI_USB_OPER:	return "operational";
		case OHCI_USB_SUSPEND:	return "suspend";
	}
	return "?";
}

// dump control and status registers
static void ohci_dump_status (struct ohci_hcd *controller)
{
	struct ohci_regs	*regs = controller->regs;
	__u32			temp;

	temp = readl (&regs->revision) & 0xff;
	dbg ("OHCI %d.%d, %s legacy support registers",
		0x03 & (temp >> 4), (temp & 0x0f),
		(temp & 0x10) ? "with" : "NO");

	temp = readl (&regs->control);
	dbg ("control: 0x%08x%s%s%s HCFS=%s%s%s%s%s CBSR=%d", temp,
		(temp & OHCI_CTRL_RWE) ? " RWE" : "",
		(temp & OHCI_CTRL_RWC) ? " RWC" : "",
		(temp & OHCI_CTRL_IR) ? " IR" : "",
		hcfs2string (temp & OHCI_CTRL_HCFS),
		(temp & OHCI_CTRL_BLE) ? " BLE" : "",
		(temp & OHCI_CTRL_CLE) ? " CLE" : "",
		(temp & OHCI_CTRL_IE) ? " IE" : "",
		(temp & OHCI_CTRL_PLE) ? " PLE" : "",
		temp & OHCI_CTRL_CBSR
		);

	temp = readl (&regs->cmdstatus);
	dbg ("cmdstatus: 0x%08x SOC=%d%s%s%s%s", temp,
		(temp & OHCI_SOC) >> 16,
		(temp & OHCI_OCR) ? " OCR" : "",
		(temp & OHCI_BLF) ? " BLF" : "",
		(temp & OHCI_CLF) ? " CLF" : "",
		(temp & OHCI_HCR) ? " HCR" : ""
		);

	ohci_dump_intr_mask ("intrstatus", readl (&regs->intrstatus));
	ohci_dump_intr_mask ("intrenable", readl (&regs->intrenable));
	// intrdisable always same as intrenable
	// ohci_dump_intr_mask ("intrdisable", readl (&regs->intrdisable));

	maybe_print_eds ("ed_periodcurrent", readl (&regs->ed_periodcurrent));

	maybe_print_eds ("ed_controlhead", readl (&regs->ed_controlhead));
	maybe_print_eds ("ed_controlcurrent", readl (&regs->ed_controlcurrent));

	maybe_print_eds ("ed_bulkhead", readl (&regs->ed_bulkhead));
	maybe_print_eds ("ed_bulkcurrent", readl (&regs->ed_bulkcurrent));

	maybe_print_eds ("donehead", readl (&regs->donehead));
}

static void ohci_dump_roothub (struct ohci_hcd *controller, int verbose)
{
	__u32			temp, ndp, i;

	temp = roothub_a (controller);
	if (temp == ~(u32)0)
		return;
	ndp = (temp & RH_A_NDP);

	if (verbose) {
		dbg ("roothub.a: %08x POTPGT=%d%s%s%s%s%s NDP=%d", temp,
			((temp & RH_A_POTPGT) >> 24) & 0xff,
			(temp & RH_A_NOCP) ? " NOCP" : "",
			(temp & RH_A_OCPM) ? " OCPM" : "",
			(temp & RH_A_DT) ? " DT" : "",
			(temp & RH_A_NPS) ? " NPS" : "",
			(temp & RH_A_PSM) ? " PSM" : "",
			ndp
			);
		temp = roothub_b (controller);
		dbg ("roothub.b: %08x PPCM=%04x DR=%04x",
			temp,
			(temp & RH_B_PPCM) >> 16,
			(temp & RH_B_DR)
			);
		temp = roothub_status (controller);
		dbg ("roothub.status: %08x%s%s%s%s%s%s",
			temp,
			(temp & RH_HS_CRWE) ? " CRWE" : "",
			(temp & RH_HS_OCIC) ? " OCIC" : "",
			(temp & RH_HS_LPSC) ? " LPSC" : "",
			(temp & RH_HS_DRWE) ? " DRWE" : "",
			(temp & RH_HS_OCI) ? " OCI" : "",
			(temp & RH_HS_LPS) ? " LPS" : ""
			);
	}
	
	for (i = 0; i < ndp; i++) {
		temp = roothub_portstatus (controller, i);
		dbg_port (controller, "", i, temp);
	}
}

static void ohci_dump (struct ohci_hcd *controller, int verbose)
{
	dbg ("OHCI controller %s state", controller->hcd.self.bus_name);

	// dumps some of the state we know about
	ohci_dump_status (controller);
#ifdef OHCI_VERBOSE_DEBUG
	if (verbose)
		ohci_dump_periodic (controller, "hcca");
#endif
	dbg ("hcca frame #%04x", controller->hcca->frame_no);
	ohci_dump_roothub (controller, 1);
}

static void ohci_dump_td (char *label, struct td *td)
{
	u32	tmp = le32_to_cpup (&td->hwINFO);

	dbg ("%s td %p; urb %p index %d; hw next td %08x",
		label, td,
		td->urb, td->index,
		le32_to_cpup (&td->hwNextTD));
	if ((tmp & TD_ISO) == 0) {
		char	*toggle, *pid;
		u32	cbp, be;

		switch (tmp & TD_T) {
		case TD_T_DATA0: toggle = "DATA0"; break;
		case TD_T_DATA1: toggle = "DATA1"; break;
		case TD_T_TOGGLE: toggle = "(CARRY)"; break;
		default: toggle = "(?)"; break;
		}
		switch (tmp & TD_DP) {
		case TD_DP_SETUP: pid = "SETUP"; break;
		case TD_DP_IN: pid = "IN"; break;
		case TD_DP_OUT: pid = "OUT"; break;
		default: pid = "(bad pid)"; break;
		}
		dbg ("     info %08x CC=%x %s DI=%d %s %s", tmp,
			TD_CC_GET(tmp), /* EC, */ toggle,
			(tmp & TD_DI) >> 21, pid,
			(tmp & TD_R) ? "R" : "");
		cbp = le32_to_cpup (&td->hwCBP);
		be = le32_to_cpup (&td->hwBE);
		dbg ("     cbp %08x be %08x (len %d)", cbp, be,
			cbp ? (be + 1 - cbp) : 0);
	} else {
		unsigned	i;
		dbg ("     info %08x CC=%x DI=%d START=%04x", tmp,
			TD_CC_GET(tmp), /* FC, */
			(tmp & TD_DI) >> 21,
			tmp & 0x0000ffff);
		dbg ("     bp0 %08x be %08x",
			le32_to_cpup (&td->hwCBP) & ~0x0fff,
			le32_to_cpup (&td->hwBE));
		for (i = 0; i < MAXPSW; i++) {
			dbg ("       psw [%d] = %2x", i,
				le16_to_cpu (td->hwPSW [i]));
		}
	}
}

/* caller MUST own hcd spinlock if verbose is set! */
static void __attribute__((unused))
ohci_dump_ed (struct ohci_hcd *ohci, char *label, struct ed *ed, int verbose)
{
	u32	tmp = ed->hwINFO;
	char	*type = "";

	dbg ("%s: %s, ed %p state 0x%x type %d; next ed %08x",
		ohci->hcd.self.bus_name, label,
		ed, ed->state, ed->type,
		le32_to_cpup (&ed->hwNextED));
	switch (tmp & (ED_IN|ED_OUT)) {
	case ED_OUT: type = "-OUT"; break;
	case ED_IN: type = "-IN"; break;
	/* else from TDs ... control */
	}
	dbg ("  info %08x MAX=%d%s%s%s EP=%d%s DEV=%d", le32_to_cpu (tmp),
		0x0fff & (le32_to_cpu (tmp) >> 16),
		(tmp & ED_ISO) ? " ISO" : "",
		(tmp & ED_SKIP) ? " SKIP" : "",
		(tmp & ED_LOWSPEED) ? " LOW" : "",
		0x000f & (le32_to_cpu (tmp) >> 7),
		type,
		0x007f & le32_to_cpu (tmp));
	dbg ("  tds: head %08x%s%s tail %08x%s",
		tmp = le32_to_cpup (&ed->hwHeadP),
		(ed->hwHeadP & ED_H) ? " HALT" : "",
		(ed->hwHeadP & ED_C) ? " CARRY" : "",
		le32_to_cpup (&ed->hwTailP),
		verbose ? "" : " (not listing)");
	if (verbose) {
		struct list_head	*tmp;

		/* use ed->td_list because HC concurrently modifies
		 * hwNextTD as it accumulates ed_donelist.
		 */
		list_for_each (tmp, &ed->td_list) {
			struct td		*td;
			td = list_entry (tmp, struct td, td_list);
			ohci_dump_td ("  ->", td);
		}
	}
}

#endif

