/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * 
 * [ Initialisation is based on Linus'  ]
 * [ uhci code and gregs ohci fragments ]
 * [ (C) Copyright 1999 Linus Torvalds  ]
 * [ (C) Copyright 1999 Gregory P. Smith]
 * 
 * 
 * OHCI is the main "non-Intel/VIA" standard for USB 1.1 host controller
 * interfaces (though some non-x86 Intel chips use it).  It supports
 * smarter hardware than UHCI.  A download link for the spec available
 * through the http://www.usb.org website.
 *
 * History:
 * 
 * 2002/09/03 get rid of ed hashtables, rework periodic scheduling and
 * 	bandwidth accounting; if debugging, show schedules in driverfs
 * 2002/07/19 fixes to management of ED and schedule state.
 * 2002/06/09 SA-1111 support (Christopher Hoover)
 * 2002/06/01 remember frame when HC won't see EDs any more; use that info
 *	to fix urb unlink races caused by interrupt latency assumptions;
 *	minor ED field and function naming updates
 * 2002/01/18 package as a patch for 2.5.3; this should match the
 *	2.4.17 kernel modulo some bugs being fixed.
 *
 * 2001/10/18 merge pmac cleanup (Benjamin Herrenschmidt) and bugfixes
 *	from post-2.4.5 patches.
 * 2001/09/20 USB_ZERO_PACKET support; hcca_dma portability, OPTi warning
 * 2001/09/07 match PCI PM changes, errnos from Linus' tree
 * 2001/05/05 fork 2.4.5 version into "hcd" framework, cleanup, simplify;
 *	pbook pci quirks gone (please fix pbook pci sw!) (db)
 *
 * 2001/04/08 Identify version on module load (gb)
 * 2001/03/24 td/ed hashing to remove bus_to_virt (Steve Longerbeam);
 	pci_map_single (db)
 * 2001/03/21 td and dev/ed allocation uses new pci_pool API (db)
 * 2001/03/07 hcca allocation uses pci_alloc_consistent (Steve Longerbeam)
 *
 * 2000/09/26 fixed races in removing the private portion of the urb
 * 2000/09/07 disable bulk and control lists when unlinking the last
 *	endpoint descriptor in order to avoid unrecoverable errors on
 *	the Lucent chips. (rwc@sgi)
 * 2000/08/29 use bandwidth claiming hooks (thanks Randy!), fix some
 *	urb unlink probs, indentation fixes
 * 2000/08/11 various oops fixes mostly affecting iso and cleanup from
 *	device unplugs.
 * 2000/06/28 use PCI hotplug framework, for better power management
 *	and for Cardbus support (David Brownell)
 * 2000/earlier:  fixes for NEC/Lucent chips; suspend/resume handling
 *	when the controller loses power; handle UE; cleanup; ...
 *
 * v5.2 1999/12/07 URB 3rd preview, 
 * v5.1 1999/11/30 URB 2nd preview, cpia, (usb-scsi)
 * v5.0 1999/11/22 URB Technical preview, Paul Mackerras powerbook susp/resume 
 * 	i386: HUB, Keyboard, Mouse, Printer 
 *
 * v4.3 1999/10/27 multiple HCs, bulk_request
 * v4.2 1999/09/05 ISO API alpha, new dev alloc, neg Error-codes
 * v4.1 1999/08/27 Randy Dunlap's - ISO API first impl.
 * v4.0 1999/08/18 
 * v3.0 1999/06/25 
 * v2.1 1999/05/09  code clean up
 * v2.0 1999/05/04 
 * v1.0 1999/04/27 initial release
 *
 * This file is licenced under the GPL.
 */
 
#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>  /* for in_interrupt () */

#ifdef CONFIG_USB_DEBUG
#	define DEBUG
#else
#	undef DEBUG
#endif

#include <linux/usb.h>
#include "../core/hcd.h"

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>

/*
 * TO DO:
 *
 *	- "disabled" and "sleeping" should be in hcd->state
 *	- bandwidth alloc to generic code
 *	- lots more testing!!
 */

#define DRIVER_VERSION "2002-Sep-03"
#define DRIVER_AUTHOR "Roman Weissgaerber, David Brownell"
#define DRIVER_DESC "USB 1.1 'Open' Host Controller (OHCI) Driver"

/*-------------------------------------------------------------------------*/

// #define OHCI_VERBOSE_DEBUG	/* not always helpful */

/* For initializing controller (mask in an HCFS mode too) */
#define	OHCI_CONTROL_INIT \
	 (OHCI_CTRL_CBSR & 0x3) | OHCI_CTRL_IE | OHCI_CTRL_PLE

#define OHCI_UNLINK_TIMEOUT	 (HZ / 10)

/*-------------------------------------------------------------------------*/

#include "ohci.h"

static inline void disable (struct ohci_hcd *ohci)
{
	ohci->disabled = 1;
	ohci->hcd.state = USB_STATE_HALT;
}

#include "ohci-hub.c"
#include "ohci-dbg.c"
#include "ohci-mem.c"
#include "ohci-q.c"

/*-------------------------------------------------------------------------*/

/*
 * queue up an urb for anything except the root hub
 */
static int ohci_urb_enqueue (
	struct usb_hcd	*hcd,
	struct urb	*urb,
	int		mem_flags
) {
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	struct ed	*ed;
	urb_priv_t	*urb_priv;
	unsigned int	pipe = urb->pipe;
	int		i, size = 0;
	unsigned long	flags;
	int		retval = 0;
	
#ifdef OHCI_VERBOSE_DEBUG
	urb_print (urb, "SUB", usb_pipein (pipe));
#endif
	
	/* every endpoint has a ed, locate and maybe (re)initialize it */
	if (! (ed = ed_get (ohci, urb->dev, pipe, urb->interval)))
		return -ENOMEM;

	/* for the private part of the URB we need the number of TDs (size) */
	switch (ed->type) {
		case PIPE_CONTROL:
			/* td_submit_urb() doesn't yet handle these */
			if (urb->transfer_buffer_length > 4096)
				return -EMSGSIZE;

			/* 1 TD for setup, 1 for ACK, plus ... */
			size = 2;
			/* FALLTHROUGH */
		// case PIPE_INTERRUPT:
		// case PIPE_BULK:
		default:
			/* one TD for every 4096 Bytes (can be upto 8K) */
			size += urb->transfer_buffer_length / 4096;
			/* ... and for any remaining bytes ... */
			if ((urb->transfer_buffer_length % 4096) != 0)
				size++;
			/* ... and maybe a zero length packet to wrap it up */
			if (size == 0)
				size++;
			else if ((urb->transfer_flags & USB_ZERO_PACKET) != 0
				&& (urb->transfer_buffer_length
					% usb_maxpacket (urb->dev, pipe,
						usb_pipeout (pipe))) == 0)
				size++;
			break;
		case PIPE_ISOCHRONOUS: /* number of packets from URB */
			size = urb->number_of_packets;
			if (size <= 0)
				return -EINVAL;
			for (i = 0; i < urb->number_of_packets; i++) {
  				urb->iso_frame_desc [i].actual_length = 0;
  				urb->iso_frame_desc [i].status = -EXDEV;
  			}
			break;
	}

	/* allocate the private part of the URB */
	urb_priv = kmalloc (sizeof (urb_priv_t) + size * sizeof (struct td *),
			mem_flags);
	if (!urb_priv)
		return -ENOMEM;
	memset (urb_priv, 0, sizeof (urb_priv_t) + size * sizeof (struct td *));
	
	spin_lock_irqsave (&ohci->lock, flags);

	/* don't submit to a dead HC */
	if (ohci->disabled || ohci->sleeping) {
		retval = -ENODEV;
		goto fail;
	}

	/* fill the private part of the URB */
	urb_priv->length = size;
	urb_priv->ed = ed;	

	/* allocate the TDs (updating hash chains) */
	for (i = 0; i < size; i++) {
		urb_priv->td [i] = td_alloc (ohci, SLAB_ATOMIC);
		if (!urb_priv->td [i]) {
			urb_priv->length = i;
			retval = -ENOMEM;
			goto fail;
		}
	}	

	/* schedule the ed if needed */
	if (ed->state == ED_IDLE) {
		retval = ed_schedule (ohci, ed);
		if (retval < 0)
			goto fail;
		if (ed->type == PIPE_ISOCHRONOUS) {
			u16	frame = le16_to_cpu (ohci->hcca->frame_no);

			/* delay a few frames before the first TD */
			frame += max_t (u16, 8, ed->interval);
			frame &= ~(ed->interval - 1);
			frame |= ed->branch;
			urb->start_frame = frame;

			/* yes, only USB_ISO_ASAP is supported, and
			 * urb->start_frame is never used as input.
			 */
		}
	} else if (ed->type == PIPE_ISOCHRONOUS)
		urb->start_frame = ed->last_iso + ed->interval;

	/* fill the TDs and link them to the ed; and
	 * enable that part of the schedule, if needed
	 * and update count of queued periodic urbs
	 */
	urb->hcpriv = urb_priv;
	td_submit_urb (ohci, urb);

fail:
	if (retval)
		urb_free_priv (ohci, urb_priv);
	spin_unlock_irqrestore (&ohci->lock, flags);
	return retval;
}

/*
 * decouple the URB from the HC queues (TDs, urb_priv); it's
 * already marked using urb->status.  reporting is always done
 * asynchronously, and we might be dealing with an urb that's
 * partially transferred, or an ED with other urbs being unlinked.
 */
static int ohci_urb_dequeue (struct usb_hcd *hcd, struct urb *urb)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	unsigned long		flags;
	
#ifdef DEBUG
	urb_print (urb, "UNLINK", 1);
#endif		  

	if (!ohci->disabled) {
		urb_priv_t  *urb_priv;

		/* Unless an IRQ completed the unlink while it was being
		 * handed to us, flag it for unlink and giveback, and force
		 * some upcoming INTR_SF to call finish_unlinks()
		 */
		spin_lock_irqsave (&ohci->lock, flags);
		urb_priv = urb->hcpriv;
		if (urb_priv) {
			urb_priv->state = URB_DEL; 
			if (urb_priv->ed->state == ED_OPER)
				start_urb_unlink (ohci, urb_priv->ed);
		}
		spin_unlock_irqrestore (&ohci->lock, flags);
	} else {
		/*
		 * with HC dead, we won't respect hc queue pointers
		 * any more ... just clean up every urb's memory.
		 */
		finish_urb (ohci, urb);
	}
	return 0;
}

/*-------------------------------------------------------------------------*/

/* frees config/altsetting state for endpoints,
 * including ED memory, dummy TD, and bulk/intr data toggle
 */

static void
ohci_free_config (struct usb_hcd *hcd, struct usb_device *udev)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	struct hcd_dev		*dev = (struct hcd_dev *) udev->hcpriv;
	int			i;
	unsigned long		flags;
#ifdef DEBUG
	int			rescans = 0;
#endif

rescan:
	/* free any eds, and dummy tds, still hanging around */
	spin_lock_irqsave (&ohci->lock, flags);
	for (i = 0; i < 32; i++) {
		struct ed	*ed = dev->ep [i];

		if (!ed)
			continue;

		if (ohci->disabled && ed->state != ED_IDLE)
			ed->state = ED_IDLE;
		switch (ed->state) {
		case ED_UNLINK:		/* wait a frame? */
			goto do_rescan;
		case ED_IDLE:		/* fully unlinked */
			td_free (ohci, ed->dummy);
			break;
		default:
#ifdef DEBUG
			err ("illegal ED %d state in free_config, %d",
				i, ed->state);
#endif
			/* ED_OPER: some driver disconnect() is broken,
			 * it didn't even start its unlinks much less wait
			 * for their completions.
			 * OTHERWISE:  hcd bug, ed is garbage
			 */
			BUG ();
		}
		ed_free (ohci, ed);
	}
	spin_unlock_irqrestore (&ohci->lock, flags);
	return;

do_rescan:
#ifdef DEBUG
	/* a driver->disconnect() returned before its unlinks completed? */
	if (in_interrupt ()) {
		dbg ("WARNING: spin in interrupt; driver->disconnect() bug");
		dbg ("dev usb-%s-%s ep 0x%x", 
			ohci->hcd.self.bus_name, udev->devpath, i);
	}
	BUG_ON (!(readl (&ohci->regs->intrenable) & OHCI_INTR_SF));
	BUG_ON (rescans >= 2);	/* HWBUG */
	rescans++;
#endif

	spin_unlock_irqrestore (&ohci->lock, flags);
	wait_ms (1);
	goto rescan;
}

static int ohci_get_frame (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);

	return le16_to_cpu (ohci->hcca->frame_no);
}

/*-------------------------------------------------------------------------*
 * HC functions
 *-------------------------------------------------------------------------*/

/* reset the HC and BUS */

static int hc_reset (struct ohci_hcd *ohci)
{
	u32 temp;

	/* SMM owns the HC?  not for long! */
	if (readl (&ohci->regs->control) & OHCI_CTRL_IR) {
		temp = 50;	/* arbitrary: half second */
		writel (OHCI_INTR_OC, &ohci->regs->intrenable);
		writel (OHCI_OCR, &ohci->regs->cmdstatus);
		dbg ("USB HC TakeOver from SMM");
		while (readl (&ohci->regs->control) & OHCI_CTRL_IR) {
			wait_ms (10);
			if (--temp == 0) {
				err ("USB HC TakeOver failed!");
				return -1;
			}
		}
	}

	/* Disable HC interrupts */
	writel (OHCI_INTR_MIE, &ohci->regs->intrdisable);

	dbg ("USB HC reset_hc %s: ctrl = 0x%x ;",
		ohci->hcd.self.bus_name,
		readl (&ohci->regs->control));

  	/* Reset USB (needed by some controllers); RemoteWakeupConnected
	 * saved if boot firmware (BIOS/SMM/...) told us it's connected
	 */
	ohci->hc_control = readl (&ohci->regs->control);
	ohci->hc_control &= OHCI_CTRL_RWC;	/* hcfs 0 = RESET */
	writel (ohci->hc_control, &ohci->regs->control);
	wait_ms (50);

	/* HC Reset requires max 10 us delay */
	writel (OHCI_HCR,  &ohci->regs->cmdstatus);
	temp = 30;	/* ... allow extra time */
	while ((readl (&ohci->regs->cmdstatus) & OHCI_HCR) != 0) {
		if (--temp == 0) {
			err ("USB HC reset timed out!");
			return -1;
		}
		udelay (1);
	}

	/* now we're in the SUSPEND state ... must go OPERATIONAL
	 * within 2msec else HC enters RESUME
	 *
	 * ... but some hardware won't init fmInterval "by the book"
	 * (SiS, OPTi ...), so reset again instead.  SiS doesn't need
	 * this if we write fmInterval after we're OPERATIONAL.
	 */
	writel (ohci->hc_control, &ohci->regs->control);

	return 0;
}

/*-------------------------------------------------------------------------*/

#define	FI		0x2edf		/* 12000 bits per frame (-1) */
#define LSTHRESH	0x628		/* lowspeed bit threshold */

/* Start an OHCI controller, set the BUS operational
 * enable interrupts 
 * connect the virtual root hub
 */
static int hc_start (struct ohci_hcd *ohci)
{
  	u32			mask;
  	struct usb_device	*udev;

	spin_lock_init (&ohci->lock);
	ohci->disabled = 1;
	ohci->sleeping = 0;

	/* Tell the controller where the control and bulk lists are
	 * The lists are empty now. */
	writel (0, &ohci->regs->ed_controlhead);
	writel (0, &ohci->regs->ed_bulkhead);

	/* a reset clears this */
	writel ((u32) ohci->hcca_dma, &ohci->regs->hcca);

	/* force default fmInterval (we won't adjust it); init thresholds
	 * for last FS and LS packets, reserve 90% for periodic.
	 */
	writel ((((6 * (FI - 210)) / 7) << 16) | FI, &ohci->regs->fminterval);
	writel (((9 * FI) / 10) & 0x3fff, &ohci->regs->periodicstart);
	writel (LSTHRESH, &ohci->regs->lsthresh);

	/* some OHCI implementations are finicky about how they init.
	 * bogus values here mean not even enumeration could work.
	 */
	if ((readl (&ohci->regs->fminterval) & 0x3fff0000) == 0
			|| !readl (&ohci->regs->periodicstart)) {
		err ("%s init err", ohci->hcd.self.bus_name);
		return -EOVERFLOW;
	}

 	/* start controller operations */
	ohci->hc_control &= OHCI_CTRL_RWC;
 	ohci->hc_control |= OHCI_CONTROL_INIT | OHCI_USB_OPER;
	ohci->disabled = 0;
 	writel (ohci->hc_control, &ohci->regs->control);

	/* Choose the interrupts we care about now, others later on demand */
	mask = OHCI_INTR_MIE | OHCI_INTR_UE | OHCI_INTR_WDH;
	writel (mask, &ohci->regs->intrstatus);
	writel (mask, &ohci->regs->intrenable);

	/* hub power always on: required for AMD-756 and some Mac platforms */
	writel ((roothub_a (ohci) | RH_A_NPS) & ~(RH_A_PSM | RH_A_OCPM),
		&ohci->regs->roothub.a);
	writel (RH_HS_LPSC, &ohci->regs->roothub.status);
	writel (0, &ohci->regs->roothub.b);

	// POTPGT delay is bits 24-31, in 2 ms units.
	mdelay ((roothub_a (ohci) >> 23) & 0x1fe);
 
	/* connect the virtual root hub */
	ohci->hcd.self.root_hub = udev = usb_alloc_dev (NULL, &ohci->hcd.self);
	ohci->hcd.state = USB_STATE_READY;
	if (!udev) {
		disable (ohci);
		ohci->hc_control &= ~OHCI_CTRL_HCFS;
		writel (ohci->hc_control, &ohci->regs->control);
		return -ENOMEM;
	}

	usb_connect (udev);
	udev->speed = USB_SPEED_FULL;
	if (usb_register_root_hub (udev, ohci->parent_dev) != 0) {
		usb_free_dev (udev);
		ohci->hcd.self.root_hub = NULL;
		disable (ohci);
		ohci->hc_control &= ~OHCI_CTRL_HCFS;
		writel (ohci->hc_control, &ohci->regs->control);
		return -ENODEV;
	}

	create_debug_files (ohci);
	return 0;
}

/*-------------------------------------------------------------------------*/

/* an interrupt happens */

static void ohci_irq (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	struct ohci_regs	*regs = ohci->regs;
 	int			ints; 

	/* we can eliminate a (slow) readl() if _only_ WDH caused this irq */
	if ((ohci->hcca->done_head != 0)
			&& ! (le32_to_cpup (&ohci->hcca->done_head) & 0x01)) {
		ints =  OHCI_INTR_WDH;

	/* cardbus/... hardware gone before remove() */
	} else if ((ints = readl (&regs->intrstatus)) == ~(u32)0) {
		disable (ohci);
		dbg ("%s device removed!", hcd->self.bus_name);
		return;

	/* interrupt for some other device? */
	} else if ((ints &= readl (&regs->intrenable)) == 0) {
		return;
	} 


	// dbg ("Interrupt: %x frame: %x", ints, le16_to_cpu (ohci->hcca->frame_no));

	if (ints & OHCI_INTR_UE) {
		disable (ohci);
		err ("OHCI Unrecoverable Error, %s disabled",
				hcd->self.bus_name);
		// e.g. due to PCI Master/Target Abort

#ifdef	DEBUG
		ohci_dump (ohci, 1);
#endif
		hc_reset (ohci);
	}
  
	if (ints & OHCI_INTR_WDH) {
		writel (OHCI_INTR_WDH, &regs->intrdisable);	
		dl_done_list (ohci, dl_reverse_done_list (ohci));
		writel (OHCI_INTR_WDH, &regs->intrenable); 
	}
  
	/* could track INTR_SO to reduce available PCI/... bandwidth */

	/* handle any pending URB/ED unlinks, leaving INTR_SF enabled
	 * when there's still unlinking to be done (next frame).
	 */
	spin_lock (&ohci->lock);
	if (ohci->ed_rm_list)
		finish_unlinks (ohci, le16_to_cpu (ohci->hcca->frame_no));
	if ((ints & OHCI_INTR_SF) != 0 && !ohci->ed_rm_list)
		writel (OHCI_INTR_SF, &regs->intrdisable);	
	spin_unlock (&ohci->lock);

	writel (ints, &regs->intrstatus);
	writel (OHCI_INTR_MIE, &regs->intrenable);	
}

/*-------------------------------------------------------------------------*/

static void ohci_stop (struct usb_hcd *hcd)
{	
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);

	dbg ("%s: stop %s controller%s",
		hcd->self.bus_name,
		hcfs2string (ohci->hc_control & OHCI_CTRL_HCFS),
		ohci->disabled ? " (disabled)" : ""
		);
#ifdef	DEBUG
	ohci_dump (ohci, 1);
#endif

	if (!ohci->disabled)
		hc_reset (ohci);
	
	remove_debug_files (ohci);
	ohci_mem_cleanup (ohci);
	if (ohci->hcca) {
		pci_free_consistent (ohci->hcd.pdev, sizeof *ohci->hcca,
					ohci->hcca, ohci->hcca_dma);
		ohci->hcca = NULL;
		ohci->hcca_dma = 0;
	}
}

/*-------------------------------------------------------------------------*/

// FIXME:  this restart logic should be generic,
// and handle full hcd state cleanup

/* controller died; cleanup debris, then restart */
/* must not be called from interrupt context */

#ifdef CONFIG_PM
static int hc_restart (struct ohci_hcd *ohci)
{
	int temp;
	int i;

	ohci->disabled = 1;
	ohci->sleeping = 0;
	if (ohci->hcd.self.root_hub)
		usb_disconnect (&ohci->hcd.self.root_hub);
	
	/* empty the interrupt branches */
	for (i = 0; i < NUM_INTS; i++) ohci->load [i] = 0;
	for (i = 0; i < NUM_INTS; i++) ohci->hcca->int_table [i] = 0;
	
	/* no EDs to remove */
	ohci->ed_rm_list = NULL;

	/* empty control and bulk lists */	 
	ohci->ed_controltail = NULL;
	ohci->ed_bulktail    = NULL;

	if ((temp = hc_reset (ohci)) < 0 || (temp = hc_start (ohci)) < 0) {
		err ("can't restart %s, %d", ohci->hcd.self.bus_name, temp);
		return temp;
	} else
		dbg ("restart %s completed", ohci->hcd.self.bus_name);
	return 0;
}
#endif

/*-------------------------------------------------------------------------*/

static const char	hcd_name [] = "ohci-hcd";

#define DRIVER_INFO DRIVER_VERSION " " DRIVER_DESC

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_INFO);
MODULE_LICENSE ("GPL");

#ifdef CONFIG_PCI
#include "ohci-pci.c"
#endif

#ifdef CONFIG_SA1111
#include "ohci-sa1111.c"
#endif

#if !(defined(CONFIG_PCI) || defined(CONFIG_SA1111))
#error "missing bus glue for ohci-hcd"
#endif
