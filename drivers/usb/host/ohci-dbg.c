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

#define edstring(ed_type) ({ char *temp; \
	switch (ed_type) { \
	case PIPE_CONTROL:	temp = "CTRL"; break; \
	case PIPE_BULK:		temp = "BULK"; break; \
	case PIPE_INTERRUPT:	temp = "INTR"; break; \
	default: 		temp = "ISOC"; break; \
	}; temp;})
#define pipestring(pipe) edstring(usb_pipetype(pipe))

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
	dbg("%s %p dev:%d,ep=%d-%c,%s,flags:%x,len:%d/%d,stat:%d", 
		    str,
		    urb,
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
	if (controller->hcca)
		dbg ("hcca frame #%04x", controller->hcca->frame_no);
	ohci_dump_roothub (controller, 1);
}

static const char data0 [] = "DATA0";
static const char data1 [] = "DATA1";

static void ohci_dump_td (char *label, struct td *td)
{
	u32	tmp = le32_to_cpup (&td->hwINFO);

	dbg ("%s td %p%s; urb %p index %d; hw next td %08x",
		label, td,
		(tmp & TD_DONE) ? " (DONE)" : "",
		td->urb, td->index,
		le32_to_cpup (&td->hwNextTD));
	if ((tmp & TD_ISO) == 0) {
		const char	*toggle, *pid;
		u32	cbp, be;

		switch (tmp & TD_T) {
		case TD_T_DATA0: toggle = data0; break;
		case TD_T_DATA1: toggle = data1; break;
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
		dbg ("     info %08x CC=%x FC=%d DI=%d SF=%04x", tmp,
			TD_CC_GET(tmp),
			(tmp >> 24) & 0x07,
			(tmp & TD_DI) >> 21,
			tmp & 0x0000ffff);
		dbg ("     bp0 %08x be %08x",
			le32_to_cpup (&td->hwCBP) & ~0x0fff,
			le32_to_cpup (&td->hwBE));
		for (i = 0; i < MAXPSW; i++) {
			u16	psw = le16_to_cpup (&td->hwPSW [i]);
			int	cc = (psw >> 12) & 0x0f;
			dbg ("       psw [%d] = %2x, CC=%x %s=%d", i,
				psw, cc,
				(cc >= 0x0e) ? "OFFSET" : "SIZE",
				psw & 0x0fff);
		}
	}
}

/* caller MUST own hcd spinlock if verbose is set! */
static void __attribute__((unused))
ohci_dump_ed (struct ohci_hcd *ohci, char *label, struct ed *ed, int verbose)
{
	u32	tmp = ed->hwINFO;
	char	*type = "";

	dbg ("%s: %s, ed %p state 0x%x type %s; next ed %08x",
		ohci->hcd.self.bus_name, label,
		ed, ed->state, edstring (ed->type),
		le32_to_cpup (&ed->hwNextED));
	switch (tmp & (ED_IN|ED_OUT)) {
	case ED_OUT: type = "-OUT"; break;
	case ED_IN: type = "-IN"; break;
	/* else from TDs ... control */
	}
	dbg ("  info %08x MAX=%d%s%s%s%s EP=%d%s DEV=%d", le32_to_cpu (tmp),
		0x03ff & (le32_to_cpu (tmp) >> 16),
		(tmp & ED_DEQUEUE) ? " DQ" : "",
		(tmp & ED_ISO) ? " ISO" : "",
		(tmp & ED_SKIP) ? " SKIP" : "",
		(tmp & ED_LOWSPEED) ? " LOW" : "",
		0x000f & (le32_to_cpu (tmp) >> 7),
		type,
		0x007f & le32_to_cpu (tmp));
	dbg ("  tds: head %08x %s%s tail %08x%s",
		tmp = le32_to_cpup (&ed->hwHeadP),
		(ed->hwHeadP & ED_C) ? data1 : data0,
		(ed->hwHeadP & ED_H) ? " HALT" : "",
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

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,32)
#	define DRIVERFS_DEBUG_FILES
#endif

#endif /* DEBUG */

/*-------------------------------------------------------------------------*/

#ifdef DRIVERFS_DEBUG_FILES

static ssize_t
show_list (struct ohci_hcd *ohci, char *buf, size_t count, struct ed *ed)
{
	unsigned		temp, size = count;

	if (!ed)
		return 0;

	/* print first --> last */
	while (ed->ed_prev)
		ed = ed->ed_prev;

	/* dump a snapshot of the bulk or control schedule */
	while (ed) {
		u32			info = ed->hwINFO;
		u32			scratch = cpu_to_le32p (&ed->hwINFO);
		struct list_head	*entry;
		struct td		*td;

		temp = snprintf (buf, size,
			"ed/%p %cs dev%d ep%d-%s max %d %08x%s%s %s",
			ed,
			(info & ED_LOWSPEED) ? 'l' : 'f',
			scratch & 0x7f,
			(scratch >> 7) & 0xf,
			(info & ED_IN) ? "in" : "out",
			0x03ff & (scratch >> 16),
			scratch,
			(info & ED_SKIP) ? " s" : "",
			(ed->hwHeadP & ED_H) ? " H" : "",
			(ed->hwHeadP & ED_C) ? data1 : data0);
		size -= temp;
		buf += temp;

		list_for_each (entry, &ed->td_list) {
			u32		cbp, be;

			td = list_entry (entry, struct td, td_list);
			scratch = cpu_to_le32p (&td->hwINFO);
			cbp = le32_to_cpup (&td->hwCBP);
			be = le32_to_cpup (&td->hwBE);
			temp = snprintf (buf, size,
					"\n\ttd %p %s %d cc=%x urb %p (%08x)",
					td,
					({ char *pid;
					switch (scratch & TD_DP) {
					case TD_DP_SETUP: pid = "setup"; break;
					case TD_DP_IN: pid = "in"; break;
					case TD_DP_OUT: pid = "out"; break;
					default: pid = "(?)"; break;
					 } pid;}),
					cbp ? (be + 1 - cbp) : 0,
					TD_CC_GET (scratch), td->urb, scratch);
			size -= temp;
			buf += temp;
		}

		temp = snprintf (buf, size, "\n");
		size -= temp;
		buf += temp;

		ed = ed->ed_next;
	}
	return count - size;
}

static ssize_t
show_async (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct pci_dev		*pdev;
	struct ohci_hcd		*ohci;
	size_t			temp;
	unsigned long		flags;

	if (off != 0)
		return 0;
	pdev = container_of (dev, struct pci_dev, dev);
	ohci = container_of (pci_get_drvdata (pdev), struct ohci_hcd, hcd);

	/* display control and bulk lists together, for simplicity */
	spin_lock_irqsave (&ohci->lock, flags);
	temp = show_list (ohci, buf, count, ohci->ed_controltail);
	count = show_list (ohci, buf + temp, count - temp, ohci->ed_bulktail);
	spin_unlock_irqrestore (&ohci->lock, flags);

	return temp + count;
}
static DEVICE_ATTR (async, S_IRUGO, show_async, NULL);


#define DBG_SCHED_LIMIT 64

static ssize_t
show_periodic (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct pci_dev		*pdev;
	struct ohci_hcd		*ohci;
	struct ed		**seen, *ed;
	unsigned long		flags;
	unsigned		temp, size, seen_count;
	char			*next;
	unsigned		i;

	if (off != 0)
		return 0;
	if (!(seen = kmalloc (DBG_SCHED_LIMIT * sizeof *seen, SLAB_ATOMIC)))
		return 0;
	seen_count = 0;

	pdev = container_of (dev, struct pci_dev, dev);
	ohci = container_of (pci_get_drvdata (pdev), struct ohci_hcd, hcd);
	next = buf;
	size = count;

	temp = snprintf (next, size, "size = %d\n", NUM_INTS);
	size -= temp;
	next += temp;

	/* dump a snapshot of the periodic schedule (and load) */
	spin_lock_irqsave (&ohci->lock, flags);
	for (i = 0; i < NUM_INTS; i++) {
		if (!(ed = ohci->periodic [i]))
			continue;

		temp = snprintf (next, size, "%2d [%3d]:", i, ohci->load [i]);
		size -= temp;
		next += temp;

		do {
			temp = snprintf (next, size, " ed%d/%p",
				ed->interval, ed);
			size -= temp;
			next += temp;
			for (temp = 0; temp < seen_count; temp++) {
				if (seen [temp] == ed)
					break;
			}

			/* show more info the first time around */
			if (temp == seen_count) {
				u32	info = ed->hwINFO;
				u32	scratch = cpu_to_le32p (&ed->hwINFO);

				temp = snprintf (next, size,
					" (%cs dev%d%s ep%d-%s"
					" max %d %08x%s%s)",
					(info & ED_LOWSPEED) ? 'l' : 'f',
					scratch & 0x7f,
					(info & ED_ISO) ? " iso" : "",
					(scratch >> 7) & 0xf,
					(info & ED_IN) ? "in" : "out",
					0x03ff & (scratch >> 16),
					scratch,
					(info & ED_SKIP) ? " s" : "",
					(ed->hwHeadP & ED_H) ? " H" : "");
				size -= temp;
				next += temp;

				// FIXME some TD info too

				if (seen_count < DBG_SCHED_LIMIT)
					seen [seen_count++] = ed;

				ed = ed->ed_next;

			} else {
				/* we've seen it and what's after */
				temp = 0;
				ed = 0;
			}

		} while (ed);

		temp = snprintf (next, size, "\n");
		size -= temp;
		next += temp;
	}
	spin_unlock_irqrestore (&ohci->lock, flags);
	kfree (seen);

	return count - size;
}
static DEVICE_ATTR (periodic, S_IRUGO, show_periodic, NULL);

#undef DBG_SCHED_LIMIT

static inline void create_debug_files (struct ohci_hcd *bus)
{
	device_create_file (&bus->hcd.pdev->dev, &dev_attr_async);
	device_create_file (&bus->hcd.pdev->dev, &dev_attr_periodic);
	// registers
	dbg ("%s: created debug files", bus->hcd.self.bus_name);
}

static inline void remove_debug_files (struct ohci_hcd *bus)
{
	device_remove_file (&bus->hcd.pdev->dev, &dev_attr_async);
	device_remove_file (&bus->hcd.pdev->dev, &dev_attr_periodic);
}

#else /* empty stubs for creating those files */

static inline void create_debug_files (struct ohci_hcd *bus) { }
static inline void remove_debug_files (struct ohci_hcd *bus) { }

#endif /* DRIVERFS_DEBUG_FILES */

/*-------------------------------------------------------------------------*/

