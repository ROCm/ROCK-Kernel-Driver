/*
 * Copyright (c) 2000-2002 by David Brownell
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
#include <linux/interrupt.h>

#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/usb.h>
#include "../core/hcd.h"

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>


/*-------------------------------------------------------------------------*/

/*
 * EHCI hc_driver implementation ... experimental, incomplete.
 * Based on the final 1.0 register interface specification.
 *
 * USB 2.0 shows up in upcoming www.pcmcia.org technology.
 * First was PCMCIA, like ISA; then CardBus, which is PCI.
 * Next comes "CardBay", using USB 2.0 signals.
 *
 * Contains additional contributions by: Brad Hards, Rory Bolt, ...
 *
 * Special thanks to Intel and VIA for providing host controllers to
 * test this driver on, and Cypress (including In-System Design) for
 * providing early devices for those host controllers to talk to!
 *
 * HISTORY:
 *
 * 2002-05-24	Preliminary FS/LS interrupts, using scheduling shortcuts
 * 2002-05-11	Clear TT errors for FS/LS ctrl/bulk.  Fill in some other
 *	missing pieces:  enabling 64bit dma, handoff from BIOS/SMM.
 * 2002-05-07	Some error path cleanups to report better errors; wmb();
 *	use non-CVS version id; better iso bandwidth claim.
 * 2002-04-19	Control/bulk/interrupt submit no longer uses giveback() on
 *	errors in submit path.  Bugfixes to interrupt scheduling/processing.
 * 2002-03-05	Initial high-speed ISO support; reduce ITD memory; shift
 *	more checking to generic hcd framework (db).  Make it work with
 *	Philips EHCI; reduce PCI traffic; shorten IRQ path (Rory Bolt).
 * 2002-01-14	Minor cleanup; version synch.
 * 2002-01-08	Fix roothub handoff of FS/LS to companion controllers.
 * 2002-01-04	Control/Bulk queuing behaves.
 *
 * 2001-12-12	Initial patch version for Linux 2.5.1 kernel.
 * 2001-June	Works with usb-storage and NEC EHCI on 2.4
 */

#define DRIVER_VERSION "2002-May-24"
#define DRIVER_AUTHOR "David Brownell"
#define DRIVER_DESC "USB 2.0 'Enhanced' Host Controller (EHCI) Driver"


// #define EHCI_VERBOSE_DEBUG
// #define have_split_iso

/* magic numbers that can affect system performance */
#define	EHCI_TUNE_CERR		3	/* 0-3 qtd retries; 0 == don't stop */
#define	EHCI_TUNE_RL_HS		0	/* nak throttle; see 4.9 */
#define	EHCI_TUNE_RL_TT		0
#define	EHCI_TUNE_MULT_HS	1	/* 1-3 transactions/uframe; 4.10.3 */
#define	EHCI_TUNE_MULT_TT	1

/* Initial IRQ latency:  lower than default */
static int log2_irq_thresh = 0;		// 0 to 6
MODULE_PARM (log2_irq_thresh, "i");
MODULE_PARM_DESC (log2_irq_thresh, "log2 IRQ latency, 1-64 microframes");

#define	INTR_MASK (STS_IAA | STS_FATAL | STS_ERR | STS_INT)

/*-------------------------------------------------------------------------*/

#include "ehci.h"
#include "ehci-dbg.c"

/*-------------------------------------------------------------------------*/

/*
 * hc states include: unknown, halted, ready, running
 * transitional states are messy just now
 * trying to avoid "running" unless urbs are active
 * a "ready" hc can be finishing prefetched work
 */

/* halt a non-running controller */
static void ehci_reset (struct ehci_hcd *ehci)
{
	u32	command = readl (&ehci->regs->command);

	command |= CMD_RESET;
	dbg_cmd (ehci, "reset", command);
	writel (command, &ehci->regs->command);
	while (readl (&ehci->regs->command) & CMD_RESET)
		continue;
	ehci->hcd.state = USB_STATE_HALT;
}

/* idle the controller (from running) */
static void ehci_ready (struct ehci_hcd *ehci)
{
	u32	command;

#ifdef DEBUG
	if (!HCD_IS_RUNNING (ehci->hcd.state))
		BUG ();
#endif

	while (!(readl (&ehci->regs->status) & (STS_ASS | STS_PSS)))
		udelay (100);
	command = readl (&ehci->regs->command);
	command &= ~(CMD_ASE | CMD_IAAD | CMD_PSE);
	writel (command, &ehci->regs->command);

	// hardware can take 16 microframes to turn off ...
	ehci->hcd.state = USB_STATE_READY;
}

/*-------------------------------------------------------------------------*/

#include "ehci-hub.c"
#include "ehci-mem.c"
#include "ehci-q.c"
#include "ehci-sched.c"

/*-------------------------------------------------------------------------*/

static void ehci_tasklet (unsigned long param);

/* EHCI 0.96 (and later) section 5.1 says how to kick BIOS/SMM/...
 * off the controller (maybe it can boot from highspeed USB disks).
 */
static int bios_handoff (struct ehci_hcd *ehci, int where, u32 cap)
{
	if (cap & (1 << 16)) {
		int msec = 500;

		/* request handoff to OS */
		cap &= 1 << 24;
		pci_write_config_dword (ehci->hcd.pdev, where, cap);

		/* and wait a while for it to happen */
		do {
			wait_ms (10);
			msec -= 10;
			pci_read_config_dword (ehci->hcd.pdev, where, &cap);
		} while ((cap & (1 << 16)) && msec);
		if (cap & (1 << 16)) {
			info ("BIOS handoff failed (%d, %04x)", where, cap);
			return 1;
		} 
		dbg ("BIOS handoff succeeded");
	} else
		dbg ("BIOS handoff not needed");
	return 0;
}

/* called by khubd or root hub init threads */

static int ehci_start (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	u32			temp;
	struct usb_device	*udev;
	int			retval;
	u32			hcc_params;
	u8                      tempbyte;

	spin_lock_init (&ehci->lock);

	ehci->caps = (struct ehci_caps *) hcd->regs;
	ehci->regs = (struct ehci_regs *) (hcd->regs + ehci->caps->length);
	dbg_hcs_params (ehci, "ehci_start");
	dbg_hcc_params (ehci, "ehci_start");

	hcc_params = readl (&ehci->caps->hcc_params);

	/* EHCI 0.96 and later may have "extended capabilities" */
	temp = HCC_EXT_CAPS (hcc_params);
	while (temp) {
		u32		cap;

		pci_read_config_dword (ehci->hcd.pdev, temp, &cap);
		dbg ("capability %04x at %02x", cap, temp);
		switch (cap & 0xff) {
		case 1:			/* BIOS/SMM/... handoff */
			if (bios_handoff (ehci, temp, cap) != 0)
				return -EOPNOTSUPP;
			break;
		case 0:			/* illegal reserved capability */
			warn ("illegal capability!");
			cap = 0;
			/* FALLTHROUGH */
		default:		/* unknown */
			break;
		}
		temp = (cap >> 8) & 0xff;
	}

	/* cache this readonly data; minimize PCI reads */
	ehci->hcs_params = readl (&ehci->caps->hcs_params);

	/*
	 * hw default: 1K periodic list heads, one per frame.
	 * periodic_size can shrink by USBCMD update if hcc_params allows.
	 */
	ehci->periodic_size = DEFAULT_I_TDPS;
	if ((retval = ehci_mem_init (ehci, SLAB_KERNEL)) < 0)
		return retval;

	/* controllers may cache some of the periodic schedule ... */
	if (HCC_ISOC_CACHE (hcc_params)) 	// full frame cache
		ehci->i_thresh = 8;
	else					// N microframes cached
		ehci->i_thresh = 2 + HCC_ISOC_THRES (hcc_params);

	ehci->async = 0;
	ehci->reclaim = 0;
	ehci->next_uframe = -1;

	/* controller state:  unknown --> reset */

	/* EHCI spec section 4.1 */
	// FIXME require STS_HALT before reset...
	ehci_reset (ehci);
	writel (INTR_MASK, &ehci->regs->intr_enable);
	writel (ehci->periodic_dma, &ehci->regs->frame_list);

	/*
	 * hcc_params controls whether ehci->regs->segment must (!!!)
	 * be used; it constrains QH/ITD/SITD and QTD locations.
	 * pci_pool consistent memory always uses segment zero.
	 * streaming mappings for I/O buffers, like pci_map_single(),
	 * can return segments above 4GB, if the device allows.
	 *
	 * NOTE:  layered drivers can't yet tell when we enable that,
	 * so they can't pass this info along (like NETIF_F_HIGHDMA)
	 */
	if (HCC_64BIT_ADDR (hcc_params)) {
		writel (0, &ehci->regs->segment);
		if (!pci_set_dma_mask (ehci->hcd.pdev, 0xffffffffffffffffULL))
			info ("enabled 64bit PCI DMA (DAC)");
	}

	/* clear interrupt enables, set irq latency */
	temp = readl (&ehci->regs->command) & 0xff;
	if (log2_irq_thresh < 0 || log2_irq_thresh > 6)
		log2_irq_thresh = 0;
	temp |= 1 << (16 + log2_irq_thresh);
	// if hc can park (ehci >= 0.96), default is 3 packets per async QH 
	// keeping default periodic framelist size
	temp &= ~(CMD_IAAD | CMD_ASE | CMD_PSE),
	// Philips, Intel, and maybe others need CMD_RUN before the
	// root hub will detect new devices (why?); NEC doesn't
	temp |= CMD_RUN;
	writel (temp, &ehci->regs->command);
	dbg_cmd (ehci, "init", temp);

	/* set async sleep time = 10 us ... ? */

	ehci->tasklet.func = ehci_tasklet;
	ehci->tasklet.data = (unsigned long) ehci;

	/* wire up the root hub */
	hcd->self.root_hub = udev = usb_alloc_dev (NULL, &hcd->self);
	if (!udev) {
done2:
		ehci_mem_cleanup (ehci);
		return -ENOMEM;
	}

	/*
	 * Start, enabling full USB 2.0 functionality ... usb 1.1 devices
	 * are explicitly handed to companion controller(s), so no TT is
	 * involved with the root hub.
	 */
	ehci->hcd.state = USB_STATE_READY;
	writel (FLAG_CF, &ehci->regs->configured_flag);
	readl (&ehci->regs->command);	/* unblock posted write */

        /* PCI Serial Bus Release Number is at 0x60 offset */
	pci_read_config_byte (hcd->pdev, 0x60, &tempbyte);
	temp = readw (&ehci->caps->hci_version);
	info ("USB %x.%x support enabled, EHCI rev %x.%2x",
	      ((tempbyte & 0xf0)>>4),
	      (tempbyte & 0x0f),
	       temp >> 8,
	       temp & 0xff);

	/*
	 * From here on, khubd concurrently accesses the root
	 * hub; drivers will be talking to enumerated devices.
	 *
	 * Before this point the HC was idle/ready.  After, khubd
	 * and device drivers may start it running.
	 */
	usb_connect (udev);
	udev->speed = USB_SPEED_HIGH;
	if (usb_register_root_hub (udev, &ehci->hcd.pdev->dev) != 0) {
		if (hcd->state == USB_STATE_RUNNING)
			ehci_ready (ehci);
		while (readl (&ehci->regs->status) & (STS_ASS | STS_PSS))
			udelay (100);
		ehci_reset (ehci);
		hcd->self.root_hub = 0;
		usb_free_dev (udev); 
		retval = -ENODEV;
		goto done2;
	}

	return 0;
}

/* always called by thread; normally rmmod */

static void ehci_stop (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);

	dbg ("%s: stop", hcd->self.bus_name);

	if (hcd->state == USB_STATE_RUNNING)
		ehci_ready (ehci);
	while (readl (&ehci->regs->status) & (STS_ASS | STS_PSS))
		udelay (100);
	ehci_reset (ehci);

	// root hub is shut down separately (first, when possible)
	scan_async (ehci);
	if (ehci->next_uframe != -1)
		scan_periodic (ehci);
	ehci_mem_cleanup (ehci);

	dbg_status (ehci, "ehci_stop completed", readl (&ehci->regs->status));
}

static int ehci_get_frame (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	return (readl (&ehci->regs->frame_index) >> 3) % ehci->periodic_size;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

/* suspend/resume, section 4.3 */

static int ehci_suspend (struct usb_hcd *hcd, u32 state)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	int			ports;
	int			i;

	dbg ("%s: suspend to %d", hcd->self.bus_name, state);

	ports = HCS_N_PORTS (ehci->hcs_params);

	// FIXME:  This assumes what's probably a D3 level suspend...

	// FIXME:  usb wakeup events on this bus should resume the machine.
	// pci config register PORTWAKECAP controls which ports can do it;
	// bios may have initted the register...

	/* suspend each port, then stop the hc */
	for (i = 0; i < ports; i++) {
		int	temp = readl (&ehci->regs->port_status [i]);

		if ((temp & PORT_PE) == 0
				|| (temp & PORT_OWNER) != 0)
			continue;
dbg ("%s: suspend port %d", hcd->self.bus_name, i);
		temp |= PORT_SUSPEND;
		writel (temp, &ehci->regs->port_status [i]);
	}

	if (hcd->state == USB_STATE_RUNNING)
		ehci_ready (ehci);
	while (readl (&ehci->regs->status) & (STS_ASS | STS_PSS))
		udelay (100);
	writel (readl (&ehci->regs->command) & ~CMD_RUN, &ehci->regs->command);

// save pci FLADJ value

	/* who tells PCI to reduce power consumption? */

	return 0;
}

static int ehci_resume (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	int			ports;
	int			i;

	dbg ("%s: resume", hcd->self.bus_name);

	ports = HCS_N_PORTS (ehci->hcs_params);

	// FIXME:  if controller didn't retain state,
	// return and let generic code clean it up
	// test configured_flag ?

	/* resume HC and each port */
// restore pci FLADJ value
	// khubd and drivers will set HC running, if needed;
	hcd->state = USB_STATE_READY;
	// FIXME Philips/Intel/... etc don't really have a "READY"
	// state ... turn on CMD_RUN too
	for (i = 0; i < ports; i++) {
		int	temp = readl (&ehci->regs->port_status [i]);

		if ((temp & PORT_PE) == 0
				|| (temp & PORT_SUSPEND) != 0)
			continue;
dbg ("%s: resume port %d", hcd->self.bus_name, i);
		temp |= PORT_RESUME;
		writel (temp, &ehci->regs->port_status [i]);
		readl (&ehci->regs->command);	/* unblock posted writes */

		wait_ms (20);
		temp &= ~PORT_RESUME;
		writel (temp, &ehci->regs->port_status [i]);
	}
	readl (&ehci->regs->command);	/* unblock posted writes */
	return 0;
}

#endif

/*-------------------------------------------------------------------------*/

/*
 * tasklet scheduled by some interrupts and other events
 * calls driver completion functions ... but not in_irq()
 */
static void ehci_tasklet (unsigned long param)
{
	struct ehci_hcd		*ehci = (struct ehci_hcd *) param;

	if (ehci->reclaim_ready)
		end_unlink_async (ehci);
	scan_async (ehci);
	if (ehci->next_uframe != -1)
		scan_periodic (ehci);
}

/*-------------------------------------------------------------------------*/

static void ehci_irq (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	u32			status = readl (&ehci->regs->status);
	int			bh;

	status &= INTR_MASK;
	if (!status)			/* irq sharing? */
		return;

	/* clear (just) interrupts */
	writel (status, &ehci->regs->status);
	readl (&ehci->regs->command);	/* unblock posted write */
	bh = 0;

#ifdef	EHCI_VERBOSE_DEBUG
	/* unrequested/ignored: Port Change Detect, Frame List Rollover */
	dbg_status (ehci, "irq", status);
#endif

	/* INT, ERR, and IAA interrupt rates can be throttled */

	/* normal [4.15.1.2] or error [4.15.1.1] completion */
	if (likely ((status & (STS_INT|STS_ERR)) != 0))
		bh = 1;

	/* complete the unlinking of some qh [4.15.2.3] */
	if (status & STS_IAA) {
		ehci->reclaim_ready = 1;
		bh = 1;
	}

	/* PCI errors [4.15.2.4] */
	if (unlikely ((status & STS_FATAL) != 0)) {
		err ("%s: fatal error, state %x", hcd->self.bus_name, hcd->state);
		ehci_reset (ehci);
		// generic layer kills/unlinks all urbs
		// then tasklet cleans up the rest
		bh = 1;
	}

	/* most work doesn't need to be in_irq() */
	if (likely (bh == 1))
		tasklet_schedule (&ehci->tasklet);
}

/*-------------------------------------------------------------------------*/

/*
 * non-error returns are a promise to giveback() the urb later
 * we drop ownership so next owner (or urb unlink) can get it
 *
 * urb + dev is in hcd_dev.urb_list
 * we're queueing TDs onto software and hardware lists
 *
 * hcd-specific init for hcpriv hasn't been done yet
 *
 * NOTE:  EHCI queues control and bulk requests transparently, like OHCI.
 */
static int ehci_urb_enqueue (
	struct usb_hcd	*hcd,
	struct urb	*urb,
	int		mem_flags
) {
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	struct list_head	qtd_list;

	urb->transfer_flags &= ~EHCI_STATE_UNLINK;
	INIT_LIST_HEAD (&qtd_list);
	switch (usb_pipetype (urb->pipe)) {

	case PIPE_CONTROL:
	case PIPE_BULK:
		if (!qh_urb_transaction (ehci, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		return submit_async (ehci, urb, &qtd_list, mem_flags);

	case PIPE_INTERRUPT:
		if (!qh_urb_transaction (ehci, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		return intr_submit (ehci, urb, &qtd_list, mem_flags);

	case PIPE_ISOCHRONOUS:
		if (urb->dev->speed == USB_SPEED_HIGH)
			return itd_submit (ehci, urb, mem_flags);
#ifdef have_split_iso
		else
			return sitd_submit (ehci, urb, mem_flags);
#else
		dbg ("no split iso support yet");
		return -ENOSYS;
#endif /* have_split_iso */

	default:	/* can't happen */
		return -ENOSYS;
	}
}

/* remove from hardware lists
 * completions normally happen asynchronously
 */

static int ehci_urb_dequeue (struct usb_hcd *hcd, struct urb *urb)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	struct ehci_qh		*qh = (struct ehci_qh *) urb->hcpriv;
	unsigned long		flags;

	dbg ("%s urb_dequeue %p qh state %d",
		hcd->self.bus_name, urb, qh->qh_state);

	switch (usb_pipetype (urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		spin_lock_irqsave (&ehci->lock, flags);
		if (ehci->reclaim) {
dbg ("dq: reclaim busy, %s", RUN_CONTEXT);
			if (in_interrupt ()) {
				spin_unlock_irqrestore (&ehci->lock, flags);
				return -EAGAIN;
			}
			while (qh->qh_state == QH_STATE_LINKED
					&& ehci->reclaim
					&& ehci->hcd.state != USB_STATE_HALT
					) {
				spin_unlock_irqrestore (&ehci->lock, flags);
// yeech ... this could spin for up to two frames!
dbg ("wait for dequeue: state %d, reclaim %p, hcd state %d",
    qh->qh_state, ehci->reclaim, ehci->hcd.state
);
				udelay (100);
				spin_lock_irqsave (&ehci->lock, flags);
			}
		}
		if (qh->qh_state == QH_STATE_LINKED)
			start_unlink_async (ehci, qh);
		spin_unlock_irqrestore (&ehci->lock, flags);
		return 0;

	case PIPE_INTERRUPT:
		intr_deschedule (ehci, urb->start_frame, qh,
			(urb->dev->speed == USB_SPEED_HIGH)
			    ? urb->interval
			    : (urb->interval << 3));
		if (ehci->hcd.state == USB_STATE_HALT)
			urb->status = -ESHUTDOWN;
		qh_completions (ehci, qh, 1);
		return 0;

	case PIPE_ISOCHRONOUS:
		// itd or sitd ...

		// wait till next completion, do it then.
		// completion irqs can wait up to 1024 msec,
		urb->transfer_flags |= EHCI_STATE_UNLINK;
		return 0;
	}
	return -EINVAL;
}

/*-------------------------------------------------------------------------*/

// bulk qh holds the data toggle

static void ehci_free_config (struct usb_hcd *hcd, struct usb_device *udev)
{
	struct hcd_dev		*dev = (struct hcd_dev *)udev->hcpriv;
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	int			i;
	unsigned long		flags;

	/* ASSERT:  nobody can be submitting urbs for this any more */

	dbg ("%s: free_config devnum %d", hcd->self.bus_name, udev->devnum);

	spin_lock_irqsave (&ehci->lock, flags);
	for (i = 0; i < 32; i++) {
		if (dev->ep [i]) {
			struct ehci_qh		*qh;

			/* dev->ep never has ITDs or SITDs */
			qh = (struct ehci_qh *) dev->ep [i];
			vdbg ("free_config, ep 0x%02x qh %p", i, qh);
			if (!list_empty (&qh->qtd_list)) {
				dbg ("ep 0x%02x qh %p not empty!", i, qh);
				BUG ();
			}
			dev->ep [i] = 0;

			/* wait_ms() won't spin here -- we're a thread */
			while (qh->qh_state == QH_STATE_LINKED
					&& ehci->reclaim
					&& ehci->hcd.state != USB_STATE_HALT
					) {
				spin_unlock_irqrestore (&ehci->lock, flags);
				wait_ms (1);
				spin_lock_irqsave (&ehci->lock, flags);
			}
			if (qh->qh_state == QH_STATE_LINKED) {
				start_unlink_async (ehci, qh);
				while (qh->qh_state != QH_STATE_IDLE) {
					spin_unlock_irqrestore (&ehci->lock,
						flags);
					wait_ms (1);
					spin_lock_irqsave (&ehci->lock, flags);
				}
			}
			qh_put (ehci, qh);
		}
	}

	spin_unlock_irqrestore (&ehci->lock, flags);
}

/*-------------------------------------------------------------------------*/

static const char	hcd_name [] = "ehci-hcd";

static const struct hc_driver ehci_driver = {
	.description =		hcd_name,

	/*
	 * generic hardware linkage
	 */
	.irq =			ehci_irq,
	.flags =		HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.start =		ehci_start,
#ifdef	CONFIG_PM
	.suspend =		ehci_suspend,
	.resume =		ehci_resume,
#endif
	.stop =			ehci_stop,

	/*
	 * memory lifecycle (except per-request)
	 */
	.hcd_alloc =		ehci_hcd_alloc,
	.hcd_free =		ehci_hcd_free,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ehci_urb_enqueue,
	.urb_dequeue =		ehci_urb_dequeue,
	.free_config =		ehci_free_config,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ehci_hub_status_data,
	.hub_control =		ehci_hub_control,
};

/*-------------------------------------------------------------------------*/

/* EHCI spec says PCI is required. */

/* PCI driver selection metadata; PCI hotplugging uses this */
static const struct pci_device_id __devinitdata pci_ids [] = { {

	/* handle any USB 2.0 EHCI controller */

	.class = 		((PCI_CLASS_SERIAL_USB << 8) | 0x20),
	.class_mask = 	~0,
	.driver_data =	(unsigned long) &ehci_driver,

	/* no matter who makes it */
	.vendor =	PCI_ANY_ID,
	.device =	PCI_ANY_ID,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,

}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver ehci_pci_driver = {
	.name =		(char *) hcd_name,
	.id_table =	pci_ids,

	.probe =	usb_hcd_pci_probe,
	.remove =	usb_hcd_pci_remove,

#ifdef	CONFIG_PM
	.suspend =	usb_hcd_pci_suspend,
	.resume =	usb_hcd_pci_resume,
#endif
};

#define DRIVER_INFO DRIVER_VERSION " " DRIVER_DESC

MODULE_DESCRIPTION (DRIVER_INFO);
MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_LICENSE ("GPL");

static int __init init (void) 
{
	dbg (DRIVER_INFO);
	dbg ("block sizes: qh %Zd qtd %Zd itd %Zd sitd %Zd",
		sizeof (struct ehci_qh), sizeof (struct ehci_qtd),
		sizeof (struct ehci_itd), sizeof (struct ehci_sitd));

	return pci_module_init (&ehci_pci_driver);
}
module_init (init);

static void __exit cleanup (void) 
{	
	pci_unregister_driver (&ehci_pci_driver);
}
module_exit (cleanup);
