/*  
    UHCI HCD (Host Controller Driver) for USB, main part for HCD frame
   
    (c) 1999-2002 
    Georg Acher      +    Deti Fliegl    +    Thomas Sailer
    georg@acher.org      deti@fliegl.de   sailer@ife.ee.ethz.ch
   
    with the help of
    David Brownell, david-b@pacbell.net 
    Adam Richter, adam@yggdrasil.com
    Roman Weissgaerber, weissg@vienna.at    
    
    HW-initalization based on material of
    Randy Dunlap + Johannes Erdfelt + Gregory P. Smith + Linus Torvalds 

    $Id: usb-uhci-hcd.c,v 1.3 2002/05/25 16:42:41 acher Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>  /* for in_interrupt () */
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <linux/usb.h>

#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include "../core/hcd.h"
#include "usb-uhci-hcd.h"

#define DRIVER_VERSION "$Revision: 1.3 $"
#define DRIVER_AUTHOR "Georg Acher, Deti Fliegl, Thomas Sailer"
#define DRIVER_DESC "USB 1.1 Universal Host Controller Interface driver (HCD)"

/*--------------------------------------------------------------------------*/
/* Values you may tweak with module parameters
 *  
 * high_bw: 1=on (default), 0=off
 * Turns on Full Speed Bandwidth Reclamation: 
 * Feature that puts a loop on the descriptor chain when
 * there's some transfer going on. With FSBR, USB performance
 * is optimal, but PCI can be slowed down up-to 5 times, slowing down
 * system performance (eg. framebuffer devices).
 *
 * bulk_depth/ctrl_depth: 0=off (default), 1:on
 * Puts descriptors for bulk/control transfers in depth-first mode. 
 * This has somehow similar effect to FSBR (higher speed), but does not
 * slow PCI down. OTOH USB performace is slightly slower than
 * in FSBR case and single device could hog whole USB, starving
 * other devices. Some devices (e.g. STV680-based cameras) NEED this depth 
 * first search to work properly.
 *
 * Turning off both high_bw and bulk_depth/ctrl_depth
 * will lead to <64KB/sec performance over USB for bulk transfers targeting
 * one device's endpoint. You probably do not want to do that.
 */

// Other constants, there's usually no need to change them.
// stop bandwidth reclamation after (roughly) 50ms
#define IDLE_TIMEOUT  (HZ/20)

// Suppress HC interrupt error messages for 5s
#define ERROR_SUPPRESSION_TIME (HZ*5)

// HC watchdog
#define WATCHDOG_TIMEOUT (4*HZ)
#define MAX_REANIMATIONS 5

#define DEBUG_SYMBOLS
#ifdef DEBUG_SYMBOLS
        #ifndef EXPORT_SYMTAB
                #define EXPORT_SYMTAB
        #endif
#endif

#define queue_dbg dbg 
#define async_dbg dbg
#define init_dbg dbg

/*--------------------------------------------------------------------------*/
//                   NO serviceable parts below!
/*--------------------------------------------------------------------------*/

/* Can be set by module parameters */
static int high_bw = 1;
static int ctrl_depth = 0;  /* 0: Breadth first, 1: Depth first */
static int bulk_depth = 0;  /* 0: Breadth first, 1: Depth first */

// How much URBs with ->next are walked
#define MAX_NEXT_COUNT 2048

static struct uhci *devs = NULL;

/* used by userspace UHCI data structure dumper */
struct uhci **uhci_devices = &devs;

/* A few prototypes */
static int uhci_urb_dequeue (struct usb_hcd *hcd, struct urb *urb);
static int hc_reset (struct uhci_hcd *uhci);
static void uhci_stop (struct usb_hcd *hcd);
static int process_transfer (struct uhci_hcd *uhci, struct urb *urb, int mode);
static int process_iso (struct uhci_hcd *uhci, struct urb *urb, int mode);
static int process_interrupt (struct uhci_hcd *uhci, struct urb *urb, int mode);
static int process_urb (struct uhci_hcd *uhci, struct list_head *p);
static int uhci_urb_enqueue (struct usb_hcd *hcd, struct urb *urb, int mem_flags);
static int hc_defibrillate(struct uhci_hcd * uhci);
static int hc_irq_run(struct uhci_hcd *uhci);

#include "usb-uhci-dbg.c"
#include "usb-uhci-mem.c"
#include "usb-uhci-hub.c"
#include "usb-uhci-q.c"

#define PIPESTRING(x) (x==PIPE_BULK?"Bulk":(x==PIPE_INTERRUPT?"Interrupt":(x==PIPE_CONTROL?"Control":"Iso")))

/*--------------------------------------------------------------------------*/
static int uhci_urb_enqueue (struct usb_hcd *hcd, struct urb *urb, int mem_flags)
{
	struct uhci_hcd	*uhci = hcd_to_uhci (hcd);
	urb_priv_t *urb_priv;
	int ret = 0, type;
	unsigned long flags;
	struct urb *queued_urb=NULL;
	int bustime;
	type = usb_pipetype (urb->pipe);

//	err("submit_urb: scheduling %p (%s), tb %p, len %i", urb, 
//	PIPESTRING(type),urb->transfer_buffer,urb->transfer_buffer_length);

	if (uhci->need_init) {
		if (in_interrupt())
			return -ESHUTDOWN;

		spin_lock_irqsave (&uhci->urb_list_lock, flags);
		ret = hc_defibrillate(uhci);
		spin_unlock_irqrestore (&uhci->urb_list_lock, flags);

		if (ret)
			return ret;		
	}

	if (!uhci->running)
		return -ESHUTDOWN;

	spin_lock_irqsave (&uhci->urb_list_lock, flags);

	queued_urb = search_dev_ep (uhci, urb); // returns already queued urb for that pipe

	if (queued_urb) {
		queue_dbg("found bulk urb %p\n", queued_urb);

		if (( type != PIPE_BULK) ||
		    ((type == PIPE_BULK) &&
		     (!(urb->transfer_flags & USB_QUEUE_BULK) || !(queued_urb->transfer_flags & USB_QUEUE_BULK)))) {
			spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
			err("ENXIO (%s)  %08x, flags %x, urb %p, burb %p, probably device driver bug...", 
			    PIPESTRING(type),
			    urb->pipe,urb->transfer_flags,urb,queued_urb);
			return -ENXIO;	// urb already queued
		}
	}

	urb_priv = uhci_alloc_priv(mem_flags);

	if (!urb_priv) {
		spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
		return -ENOMEM;
	}

	urb->hcpriv = urb_priv;
	urb_priv->urb=urb;
	INIT_LIST_HEAD (&urb_priv->desc_list);
	
	if (type == PIPE_CONTROL)
		urb_priv->setup_packet_dma = pci_map_single(uhci->uhci_pci, urb->setup_packet,
							    sizeof(struct usb_ctrlrequest), PCI_DMA_TODEVICE);

	if (urb->transfer_buffer_length)
		urb_priv->transfer_buffer_dma = pci_map_single(uhci->uhci_pci,
							       urb->transfer_buffer,
							       urb->transfer_buffer_length,
							       usb_pipein(urb->pipe) ?
							       PCI_DMA_FROMDEVICE :
							       PCI_DMA_TODEVICE);

	// for bulk queuing it is essential that interrupts are disabled until submission
	// all other types enable interrupts again
	switch (type) {
	case PIPE_BULK:
		if (queued_urb) {
			while (((urb_priv_t*)queued_urb->hcpriv)->next_queued_urb)  // find last queued bulk
				queued_urb=((urb_priv_t*)queued_urb->hcpriv)->next_queued_urb;
				
			((urb_priv_t*)queued_urb->hcpriv)->next_queued_urb=urb;
		}
		atomic_inc (&uhci->avoid_bulk);
		ret = uhci_submit_bulk_urb (uhci, urb, queued_urb);
		atomic_dec (&uhci->avoid_bulk);
		spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
		break;

	case PIPE_ISOCHRONOUS:	
		spin_unlock_irqrestore (&uhci->urb_list_lock, flags);		

		if (urb->bandwidth == 0) {      /* not yet checked/allocated */
			bustime = usb_check_bandwidth (urb->dev, urb);
			if (bustime < 0) 
				ret = bustime;
			else {
				ret = uhci_submit_iso_urb(uhci, urb, mem_flags);
				if (ret == 0)
					usb_claim_bandwidth (urb->dev, urb, bustime, 1);
			}
		} else {        /* bandwidth is already set */
			ret = uhci_submit_iso_urb(uhci, urb, mem_flags);
		}
		break;

	case PIPE_INTERRUPT:
		spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
		if (urb->bandwidth == 0) {      /* not yet checked/allocated */
			bustime = usb_check_bandwidth (urb->dev, urb);
			if (bustime < 0)
				ret = bustime;
			else {
				ret = uhci_submit_int_urb(uhci, urb);
				if (ret == 0)
					usb_claim_bandwidth (urb->dev, urb, bustime, 0);
			}
		} else {        /* bandwidth is already set */
			ret = uhci_submit_int_urb(uhci, urb);
		}
		break;

	case PIPE_CONTROL:
		spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
		ret = uhci_submit_control_urb (uhci, urb);
		break;

	default:	
		spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
		ret = -EINVAL;
	}

//	err("submit_urb: scheduled with ret: %d", ret);
	
	if (ret != 0)
 		uhci_free_priv(uhci, urb, urb_priv);

	return ret;
}
/*--------------------------------------------------------------------------*/
static int uhci_urb_dequeue (struct usb_hcd *hcd, struct urb *urb)
{
	unsigned long flags=0;
	struct uhci_hcd	*uhci = hcd_to_uhci (hcd);
	int ret;

	dbg("uhci_urb_dequeue called for %p",urb);
	
	spin_lock_irqsave (&uhci->urb_list_lock, flags);
	ret = uhci_unlink_urb_async(uhci, urb, UNLINK_ASYNC_STORE_URB);
	spin_unlock_irqrestore (&uhci->urb_list_lock, flags);	
	return ret;
}
/*--------------------------------------------------------------------------*/
static int uhci_get_frame (struct usb_hcd *hcd)
{
	struct uhci_hcd	*uhci = hcd_to_uhci (hcd);
	return inw ((int)uhci->hcd.regs + USBFRNUM);
}

/*--------------------------------------------------------------------------*/
//             Init and shutdown functions for HW 
/*--------------------------------------------------------------------------*/
static int hc_reset (struct uhci_hcd *uhci)
{
	unsigned long io_addr = (unsigned long)uhci->hcd.regs;

	uhci->apm_state = 0;
	uhci->running = 0;
	outw (USBCMD_GRESET, io_addr + USBCMD);
	uhci_wait_ms (50);   /* Global reset for 50ms */
	outw (0, io_addr + USBCMD);
	uhci_wait_ms (10);	
	return 0;
}
/*--------------------------------------------------------------------------*/
static int hc_irq_run(struct uhci_hcd *uhci)
{
	unsigned long io_addr = (unsigned long)uhci->hcd.regs;
	/* Turn on all interrupts */
	outw (USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC | USBINTR_SP, io_addr + USBINTR);

	/* Start at frame 0 */
	outw (0, io_addr + USBFRNUM);
	outl (uhci->framelist_dma, io_addr + USBFLBASEADD);

	/* Run and mark it configured with a 64-byte max packet */
	outw (USBCMD_RS | USBCMD_CF | USBCMD_MAXP, io_addr + USBCMD);
	uhci->apm_state = 1;
	uhci->running = 1;
	uhci->last_hcd_irq = jiffies+4*HZ;
	return 0;
}
/*--------------------------------------------------------------------------*/
static int hc_start (struct uhci_hcd *uhci)
{
	unsigned long io_addr = (unsigned long)uhci->hcd.regs;
	int timeout = 10;
	struct usb_device	*udev;
	init_dbg("hc_start uhci %p",uhci);
	/*
	 * Reset the HC - this will force us to get a
	 * new notification of any already connected
	 * ports due to the virtual disconnect that it
	 * implies.
	 */
	outw (USBCMD_HCRESET, io_addr + USBCMD);

	while (inw (io_addr + USBCMD) & USBCMD_HCRESET) {
		if (!--timeout) {
			err("USBCMD_HCRESET timed out!");
			break;
		}
		udelay(1);
	}
	
	hc_irq_run(uhci);

	/* connect the virtual root hub */
	uhci->hcd.self.root_hub = udev = usb_alloc_dev (NULL, &uhci->hcd.self);
	uhci->hcd.state = USB_STATE_READY;
	if (!udev) {
	    uhci->running = 0;
	    return -ENOMEM;
	}

	usb_connect (udev);
	udev->speed = USB_SPEED_FULL;
	if (usb_register_root_hub (udev, &uhci->hcd.pdev->dev) != 0) {
		usb_free_dev (udev); 
		uhci->running = 0;
		return -ENODEV;
	}
	
	return 0;
}

/*--------------------------------------------------------------------------*/
// Start up UHCI, find ports, init DMA lists

static int __devinit uhci_start (struct usb_hcd *hcd)
{
	struct uhci_hcd	*uhci = hcd_to_uhci (hcd);
	int ret;
	unsigned long io_addr=(unsigned long)hcd->regs, io_size=0x20;
	
	init_dbg("uhci_start hcd %p uhci %p, pdev %p",hcd,uhci,hcd->pdev);
	/* disable legacy emulation, Linux takes over... */
	pci_write_config_word (hcd->pdev, USBLEGSUP, 0);

	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/* they may have more but give no way to determine how many they */
	/* have, so default to 2 */
	/* According to the UHCI spec, Bit 7 is always set to 1. So we try */
	/* to use this to our advantage */

	for (uhci->maxports = 0; uhci->maxports < (io_size - 0x10) / 2; uhci->maxports++) {
		unsigned int portstatus;

		portstatus = inw (io_addr + 0x10 + (uhci->maxports * 2));
		dbg("port %i, adr %x status %x", uhci->maxports,
			io_addr + 0x10 + (uhci->maxports * 2), portstatus);
		if (!(portstatus & 0x0080))
			break;
	}
	warn("Detected %d ports", uhci->maxports);
	
	if (uhci->maxports < 2 || uhci->maxports > 8) {
		dbg("Port count misdetected, forcing to 2 ports");
		uhci->maxports = 2;
	}	       

	ret=init_skel(uhci);
	if (ret)
		return ret;

	hc_reset (uhci);

	if (hc_start (uhci) < 0) {
		err ("can't start %s", uhci->hcd.self.bus_name);
		uhci_stop (hcd);
		return -EBUSY;
	}

	// Enable PIRQ
	pci_write_config_word (hcd->pdev, USBLEGSUP, USBLEGSUP_DEFAULT);

	set_td_ioc(uhci->td128ms); // start watchdog interrupt
	uhci->last_hcd_irq=jiffies+5*HZ;

	return 0;	
}
/*--------------------------------------------------------------------------*/
static void uhci_free_config (struct usb_hcd *hcd, struct usb_device *udev)
{
	dbg("uhci_free_config for dev %p", udev);
	uhci_unlink_urbs (hcd_to_uhci (hcd), udev, 0);  // Forced unlink of remaining URBs
}
/*--------------------------------------------------------------------------*/
static void uhci_stop (struct usb_hcd *hcd)
{
	struct uhci_hcd	*uhci = hcd_to_uhci (hcd);
	
	init_dbg("%s: stop controller", hcd->bus_name);
	
	uhci->running=0;
	hc_reset (uhci);
	wait_ms (1);
	uhci_unlink_urbs (uhci, 0, CLEAN_FORCED);  // Forced unlink of remaining URBs
	uhci_cleanup_unlink (uhci, CLEAN_FORCED);  // force cleanup of async killed URBs
	cleanup_skel (uhci);
}
/*--------------------------------------------------------------------------*/
//                  UHCI INTERRUPT PROCESSING
/*--------------------------------------------------------------------------*/
static void uhci_irq (struct usb_hcd *hcd)
{	
	struct uhci_hcd	*uhci = hcd_to_uhci (hcd);
	unsigned long io_addr = (unsigned long)hcd->regs;
	unsigned short status;
	struct list_head *p, *p2;
	int restarts, work_done;

	/*
	 * Read the interrupt status, and write it back to clear the
	 * interrupt cause
	 */

	status = inw (io_addr + USBSTS);

	if (!status)		/* shared interrupt, not mine */
		return;

	dbg("interrupt");

	uhci->last_hcd_irq=jiffies;  // for watchdog

	if (status != 1) {
		// Avoid too much error messages at a time
		if (time_after(jiffies, uhci->last_error_time + ERROR_SUPPRESSION_TIME)) {
			warn("interrupt, status %x, frame# %i", status, 
			     UHCI_GET_CURRENT_FRAME(uhci));
			uhci->last_error_time = jiffies;
		}
		
		// remove host controller halted state
		if ((status&0x20) && (uhci->running)) {
			err("Host controller halted, waiting for timeout.");
//			outw (USBCMD_RS | inw(io_addr + USBCMD), io_addr + USBCMD);
		}
		//uhci_show_status (s);
	}
	/*
	 * traverse the list in *reverse* direction, because new entries
	 * may be added at the end.
	 * also, because process_urb may unlink the current urb,
	 * we need to advance the list before
	 * New: check for max. workload and restart count
	 */

	spin_lock (&uhci->urb_list_lock);

	restarts=0;
	work_done=0;

restart:
	uhci->unlink_urb_done=0;
	p = uhci->urb_list.prev;	

	while (p != &uhci->urb_list && (work_done < 1024)) {
		p2 = p;
		p = p->prev;

		process_urb (uhci, p2);
		
		work_done++;

		if (uhci->unlink_urb_done) {
			uhci->unlink_urb_done=0;
			restarts++;
			
			if (restarts<16)	// avoid endless restarts
				goto restart;
			else 
				break;
		}
	}
	if (time_after(jiffies, uhci->timeout_check + (HZ/30)))
		uhci_check_timeouts(uhci);

	clean_descs(uhci, CLEAN_NOT_FORCED);
	uhci_cleanup_unlink(uhci, CLEAN_NOT_FORCED);
	uhci_switch_timer_int(uhci);
							
	spin_unlock (&uhci->urb_list_lock);
	
	outw (status, io_addr + USBSTS);

	//dbg("uhci_interrupt: done");
}

/*--------------------------------------------------------------------------*/
//             POWER MANAGEMENT

#ifdef	CONFIG_PM
static int uhci_suspend (struct usb_hcd *hcd, u32 state)
{
	struct uhci_hcd		*uhci = hcd_to_uhci (hcd);
	hc_reset(uhci);
	return 0;
}
/*--------------------------------------------------------------------------*/
static int uhci_resume (struct usb_hcd *hcd)
{
	struct uhci_hcd		*uhci = hcd_to_uhci (hcd);
	hc_start(uhci);
	return 0;
}
#endif
/*--------------------------------------------------------------------------*/
static const char	hcd_name [] = "usb-uhci-hcd";

static const struct hc_driver uhci_driver = {
	description:		hcd_name,

	// generic hardware linkage
	irq:			uhci_irq,
	flags:			HCD_USB11,

	// basic lifecycle operations
	start:			uhci_start,
#ifdef	CONFIG_PM
	suspend:		uhci_suspend,
	resume:			uhci_resume,
#endif
	stop:			uhci_stop,

	// memory lifecycle (except per-request)
	hcd_alloc:		uhci_hcd_alloc,
	hcd_free:		uhci_hcd_free,

	// managing i/o requests and associated device resources
	urb_enqueue:		uhci_urb_enqueue,
	urb_dequeue:		uhci_urb_dequeue,
	free_config:		uhci_free_config,

	// scheduling support
	get_frame_number:	uhci_get_frame,

	// root hub support
	hub_status_data:	uhci_hub_status_data,
	hub_control:		uhci_hub_control,
};

#define DRIVER_INFO DRIVER_VERSION " " DRIVER_DESC

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_INFO);
MODULE_LICENSE ("GPL");
MODULE_PARM (high_bw, "i");
MODULE_PARM_DESC (high_bw, "high_hw: Enable high bandwidth mode, 1=on (default), 0=off"); 
MODULE_PARM (bulk_depth, "i");
MODULE_PARM_DESC (bulk_depth, "bulk_depth: Depth first processing for bulk transfers, 0=off (default), 1=on");
MODULE_PARM (ctrl_depth, "i");
MODULE_PARM_DESC (ctrl_depth, "ctrl_depth: Depth first processing for control transfers, 0=off (default), 1=on");

static const struct pci_device_id __devinitdata pci_ids [] = { {

	/* handle any USB UHCI controller */
	class:		(PCI_CLASS_SERIAL_USB << 8) | 0x00,
	class_mask:	~0,
	driver_data:	(unsigned long) &uhci_driver,

	/* no matter who makes it */
	vendor:		PCI_ANY_ID,
	device:		PCI_ANY_ID,
	subvendor:	PCI_ANY_ID,
	subdevice:	PCI_ANY_ID,
	}, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver uhci_pci_driver = {
	name:		(char *) hcd_name,
	id_table:	pci_ids,

	probe:		usb_hcd_pci_probe,
	remove:		usb_hcd_pci_remove,

#ifdef	CONFIG_PM
	suspend:	usb_hcd_pci_suspend,
	resume:		usb_hcd_pci_resume,
#endif
};
/*-------------------------------------------------------------------------*/

static int __init uhci_hcd_init (void) 
{
	init_dbg (DRIVER_INFO);
	init_dbg ("block sizes: hq %d td %d",
		sizeof (struct qh), sizeof (struct td));
	info("High bandwidth mode %s.%s%s",
	     high_bw?"enabled":"disabled",
	     ctrl_depth?"CTRL depth first enabled":"",
	     bulk_depth?"BULK depth first enabled":"");
	return pci_module_init (&uhci_pci_driver);
}

static void __exit uhci_hcd_cleanup (void) 
{	
	pci_unregister_driver (&uhci_pci_driver);
}

module_init (uhci_hcd_init);
module_exit (uhci_hcd_cleanup);


