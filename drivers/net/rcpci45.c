/* 
**
**  RCpci45.c  
**
**
**
**  ---------------------------------------------------------------------
**  ---     Copyright (c) 1998, 1999, RedCreek Communications Inc.    ---
**  ---                   All rights reserved.                        ---
**  ---------------------------------------------------------------------
**
** Written by Pete Popov and Brian Moyle.
**
** Known Problems
** 
** None known at this time.
**
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.

**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.

**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**   
**  Rasmus Andersen, December 2000: Converted to new PCI API and general
**  cleanup.
**
**  Pete Popov, January 11,99: Fixed a couple of 2.1.x problems 
**  (virt_to_bus() not called), tested it under 2.2pre5 (as a module), and 
**  added a #define(s) to enable the use of the same file for both, the 2.0.x 
**  kernels as well as the 2.1.x.
**
**  Ported to 2.1.x by Alan Cox 1998/12/9. 
**
**  Sometime in mid 1998, written by Pete Popov and Brian Moyle.
**
***************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <asm/irq.h>		/* For NR_IRQS only. */
#include <asm/bitops.h>
#include <asm/uaccess.h>

static char version[] __initdata =
    "RedCreek Communications PCI linux driver version 2.03\n";

#define RC_LINUX_MODULE
#include "rclanmtl.h"
#include "rcif.h"

#define RUN_AT(x) (jiffies + (x))

#define NEW_MULTICAST

/* PCI/45 Configuration space values */
#define RC_PCI45_VENDOR_ID  0x4916
#define RC_PCI45_DEVICE_ID  0x1960

#define MAX_ETHER_SIZE        1520
#define MAX_NMBR_RCV_BUFFERS    96
#define RC_POSTED_BUFFERS_LOW_MARK MAX_NMBR_RCV_BUFFERS-16
#define BD_SIZE 3		/* Bucket Descriptor size */
#define BD_LEN_OFFSET 2		/* Bucket Descriptor offset to length field */

/* RedCreek LAN device Target ID */
#define RC_LAN_TARGET_ID  0x10
/* RedCreek's OSM default LAN receive Initiator */
#define DEFAULT_RECV_INIT_CONTEXT  0xA17

static U32 DriverControlWord;

static void rc_timer (unsigned long);

static int RCinit (struct net_device *);

static int RCopen (struct net_device *);
static int RC_xmit_packet (struct sk_buff *, struct net_device *);
static void RCinterrupt (int, void *, struct pt_regs *);
static int RCclose (struct net_device *dev);
static struct net_device_stats *RCget_stats (struct net_device *);
static int RCioctl (struct net_device *, struct ifreq *, int);
static int RCconfig (struct net_device *, struct ifmap *);
static void RCxmit_callback (U32, U16, PU32, struct net_device *);
static void RCrecv_callback (U32, U8, U32, PU32, struct net_device *);
static void RCreset_callback (U32, U32, U32, struct net_device *);
static void RCreboot_callback (U32, U32, U32, struct net_device *);
static int RC_allocate_and_post_buffers (struct net_device *, int);

static struct pci_device_id rcpci45_pci_table[] __devinitdata = {
	{RC_PCI45_VENDOR_ID, RC_PCI45_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{}
};
MODULE_DEVICE_TABLE (pci, rcpci45_pci_table);
MODULE_LICENSE("GPL");

static void __exit
rcpci45_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	PDPA pDpa = dev->priv;

	if (!dev) {
		printk (KERN_ERR
			"(rcpci45 driver:) remove non-existent device\n");
		return;
	}

	dprintk ("remove_one: IOP reset: 0x%x\n", RCResetIOP (dev));

	/* RAA Inspired by starfire.c and yellowfin.c we keep these
	 * here. */
	unregister_netdev (dev);
	free_irq (dev->irq, dev);
	iounmap ((void *) dev->base_addr);
	pci_release_regions (pdev);
	kfree (pDpa->PLanApiPA);
	kfree (pDpa->pPab);
	kfree (pDpa);
	kfree (dev);
	pci_set_drvdata (pdev, NULL);
}

static int
RCinit (struct net_device *dev)
{
	dev->open = &RCopen;
	dev->hard_start_xmit = &RC_xmit_packet;
	dev->stop = &RCclose;
	dev->get_stats = &RCget_stats;
	dev->do_ioctl = &RCioctl;
	dev->set_config = &RCconfig;
	return 0;
}

static int
rcpci45_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned long *vaddr;
	PDPA pDpa;
	int error;
	static int card_idx = -1;
	struct net_device *dev;
	unsigned long pci_start, pci_len;

	card_idx++;

	/* 
	 * Allocate and fill new device structure. 
	 * We need enough for struct net_device plus DPA plus the LAN API private
	 * area, which requires a minimum of 16KB.  The top of the allocated
	 * area will be assigned to struct net_device; the next chunk will be
	 * assigned to DPA; and finally, the rest will be assigned to the
	 * the LAN API layer.
	 */

	dev = init_etherdev (NULL, sizeof (*pDpa));
	if (!dev) {
		printk (KERN_ERR
			"(rcpci45 driver:) unable to allocate in init_etherdev\n");
		error = -ENOMEM;
		goto err_out;
	}

	error = pci_enable_device (pdev);
	if (error) {
		printk (KERN_ERR
			"(rcpci45 driver:) %d: unable to enable pci device, aborting\n",
			card_idx);
		goto err_out;
	}
	error = -ENOMEM;
	pci_start = pci_resource_start (pdev, 0);
	pci_len = pci_resource_len (pdev, 0);

	pci_set_drvdata (pdev, dev);

	pDpa = dev->priv;
	pDpa->id = card_idx;
	pDpa->pci_addr = pci_start;

	if (!pci_start || !(pci_resource_flags (pdev, 0) & IORESOURCE_MEM)) {
		printk (KERN_ERR
			"(rcpci45 driver:) No PCI memory resources! Aborting.\n");
		error = -EBUSY;
		goto err_out_free_dev;
	}

	/*
	 * Save the starting address of the LAN API private area.  We'll
	 * pass that to RCInitI2OMsgLayer().
	 */
	/* RAA FIXME: This size should be a #define somewhere after I
	 * clear up some questions: What flags are neeeded in the alloc below
	 * and what needs to be done before the memarea is long word aligned?
	 * (Look in old code for an approach.) (Also note that the 16K below
	 * is substantially less than the 32K allocated before (even though
	 * some of the spacce was used for data structures.) */
	pDpa->msgbuf = kmalloc (16384, GFP_KERNEL);
	if (!pDpa->msgbuf) {
		printk (KERN_ERR "(rcpci45 driver:) Could not allocate %d byte memory for the private msgbuf!\n", 16384);	/* RAA FIXME not hardcoded! */
		goto err_out_free_dev;
	}
	pDpa->PLanApiPA = (void *) (((long) pDpa->msgbuf + 0xff) & ~0xff);

	dprintk ("pDpa->PLanApiPA = 0x%x\n", (uint) pDpa->PLanApiPA);

	/* The adapter is accessible through memory-access read/write, not
	 * I/O read/write.  Thus, we need to map it to some virtual address
	 * area in order to access the registers as normal memory.
	 */
	error = pci_request_regions (pdev, dev->name);
	if (error)
		goto err_out_free_msgbuf;

	vaddr = (ulong *) ioremap (pci_start, pci_len);
	if (!vaddr) {
		printk (KERN_ERR
			"(rcpci45 driver:) Unable to remap address range from %lu to %lu\n",
			pci_start, pci_start + pci_len);
		goto err_out_free_region;
	}

	dprintk ("rcpci45_init_one: 0x%x, priv = 0x%x, vaddr = 0x%x\n",
		 (uint) dev, (uint) dev->priv, (uint) vaddr);
	dev->base_addr = (unsigned long) vaddr;
	dev->irq = pdev->irq;

	dev->init = &RCinit;

	return 0;		/* success */

err_out_free_region:
	pci_release_regions (pdev);
err_out_free_msgbuf:
	kfree (pDpa->msgbuf);
err_out_free_dev:
	unregister_netdev (dev);
	kfree (dev);
err_out:
	card_idx--;
	return error;
}

static struct pci_driver rcpci45_driver = {
	name:		"rcpci45",
	id_table:	rcpci45_pci_table,
	probe:		rcpci45_init_one,
	remove:		rcpci45_remove_one,
};

static int __init
rcpci_init_module (void)
{
	int rc = pci_module_init (&rcpci45_driver);

	if (!rc)
		printk (KERN_INFO "%s", version);
	return rc;
}

static int
RCopen (struct net_device *dev)
{
	int post_buffers = MAX_NMBR_RCV_BUFFERS;
	PDPA pDpa = dev->priv;
	int count = 0;
	int requested = 0;
	int error;

	dprintk ("(rcpci45 driver:) RCopen\n");

	/* Request a shared interrupt line. */
	error = request_irq (dev->irq, RCinterrupt, SA_SHIRQ, dev->name, dev);
	if (error) {
		printk (KERN_ERR "(rcpci45 driver:) %s: unable to get IRQ %d\n",
			dev->name, dev->irq);
		goto err_out;
	}

	error = RCInitI2OMsgLayer (dev, (PFNTXCALLBACK) RCxmit_callback,
				   (PFNRXCALLBACK) RCrecv_callback,
				   (PFNCALLBACK) RCreboot_callback);
	if (error) {
		printk (KERN_ERR
			"(rcpci45 driver:) Unable to initialize msg layer\n");
		goto err_out_free_irq;
	}
	if ((error = RCGetMAC (dev, NULL))) {
		printk (KERN_ERR
			"(rcpci45 driver:) Unable to get adapter MAC\n");
		goto err_out_free_irq;
	}

	DriverControlWord |= WARM_REBOOT_CAPABLE;
	RCReportDriverCapability (dev, DriverControlWord);

	printk (KERN_INFO "%s: RedCreek Communications IPSEC VPN adapter\n",
		dev->name);

	/* RAA: Old RCopen starts here */
	RCEnableI2OInterrupts (dev);

	/* RAA Hmm, how does the comment below jibe with the newly imported
	 * code above? A FIXME!!*/
	if (pDpa->nexus) {
		/* This is not the first time RCopen is called.  Thus,
		 * the interface was previously opened and later closed
		 * by RCclose().  RCclose() does a Shutdown; to wake up
		 * the adapter, a reset is mandatory before we can post
		 * receive buffers.  However, if the adapter initiated 
		 * a reboot while the interface was closed -- and interrupts
		 * were turned off -- we need will need to reinitialize
		 * the adapter, rather than simply waking it up.  
		 */
		dprintk (KERN_INFO "Waking up adapter...\n");
		RCResetLANCard (dev, 0, 0, 0);
	} else
		pDpa->nexus = 1;

	while (post_buffers) {
		if (post_buffers > MAX_NMBR_POST_BUFFERS_PER_MSG)
			requested = MAX_NMBR_POST_BUFFERS_PER_MSG;
		else
			requested = post_buffers;
		count = RC_allocate_and_post_buffers (dev, requested);

		if (count < requested) {
			/*
			 * Check to see if we were able to post any buffers at all.
			 */
			if (post_buffers == MAX_NMBR_RCV_BUFFERS) {
				printk (KERN_ERR
					"(rcpci45 driver:) Error RCopen: not able to allocate any buffers\r\n");
				return (-ENOMEM);
			}
			printk (KERN_WARNING
				"(rcpci45 driver:) Warning RCopen: not able to allocate all requested buffers\r\n");
			break;	/* we'll try to post more buffers later */
		} else
			post_buffers -= count;
	}
	pDpa->numOutRcvBuffers = MAX_NMBR_RCV_BUFFERS - post_buffers;
	pDpa->shutdown = 0;	/* just in case */
	dprintk ("RCopen: posted %d buffers\n", (uint) pDpa->numOutRcvBuffers);
	MOD_INC_USE_COUNT;
	netif_start_queue (dev);
	return 0;

err_out_free_irq:
	free_irq (dev->irq, dev);
err_out:
	return error;
}

static int
RC_xmit_packet (struct sk_buff *skb, struct net_device *dev)
{

	PDPA pDpa = dev->priv;
	singleTCB tcb;
	psingleTCB ptcb = &tcb;
	RC_RETURN status = 0;

	netif_stop_queue (dev);

	if (pDpa->shutdown || pDpa->reboot) {
		dprintk ("RC_xmit_packet: tbusy!\n");
		return 1;
	}

	/*
	 * The user is free to reuse the TCB after RCI2OSendPacket() returns, since
	 * the function copies the necessary info into its own private space.  Thus,
	 * our TCB can be a local structure.  The skb, on the other hand, will be
	 * freed up in our interrupt handler.
	 */

	ptcb->bcount = 1;

	/* 
	 * we'll get the context when the adapter interrupts us to tell us that
	 * the transmission is done. At that time, we can free skb.
	 */
	ptcb->b.context = (U32) skb;
	ptcb->b.scount = 1;
	ptcb->b.size = skb->len;
	ptcb->b.addr = virt_to_bus ((void *) skb->data);

	dprintk ("RC xmit: skb = 0x%x, pDpa = 0x%x, id = %d, ptcb = 0x%x\n",
		 (uint) skb, (uint) pDpa, (uint) pDpa->id, (uint) ptcb);
	if ((status = RCI2OSendPacket (dev, (U32) NULL, (PRCTCB) ptcb))
	    != RC_RTN_NO_ERROR) {
		dprintk ("RC send error 0x%x\n", (uint) status);
		return 1;
	} else {
		dev->trans_start = jiffies;
		netif_wake_queue (dev);
	}
	/*
	 * That's it!
	 */
	return 0;
}

/*
 * RCxmit_callback()
 *
 * The transmit callback routine. It's called by RCProcI2OMsgQ()
 * because the adapter is done with one or more transmit buffers and
 * it's returning them to us, or we asked the adapter to return the
 * outstanding transmit buffers by calling RCResetLANCard() with 
 * RC_RESOURCE_RETURN_PEND_TX_BUFFERS flag. 
 * All we need to do is free the buffers.
 */
static void
RCxmit_callback (U32 Status,
		 U16 PcktCount, PU32 BufferContext, struct net_device *dev)
{
	struct sk_buff *skb;
	PDPA pDpa = dev->priv;

	if (!pDpa) {
		printk (KERN_ERR
			"(rcpci45 driver:) Fatal error: xmit callback, !pDpa\n");
		return;
	}

/*      dprintk("xmit_callback: Status = 0x%x\n", (uint)Status); */
	if (Status != I2O_REPLY_STATUS_SUCCESS)
		dprintk ("xmit_callback: Status = 0x%x\n", (uint) Status);
	if (pDpa->shutdown || pDpa->reboot)
		dprintk ("xmit callback: shutdown||reboot\n");

	dprintk ("xmit_callback: PcktCount = %d, BC = 0x%x\n",
		 (uint) PcktCount, (uint) BufferContext);

	while (PcktCount--) {
		skb = (struct sk_buff *) (BufferContext[0]);
		dprintk ("skb = 0x%x\n", (uint) skb);
		BufferContext++;
		dev_kfree_skb_irq (skb);
	}
	netif_wake_queue (dev);
}

static void
RCreset_callback (U32 Status, U32 p1, U32 p2, struct net_device *dev)
{
	PDPA pDpa = dev->priv;

	dprintk ("RCreset_callback Status 0x%x\n", (uint) Status);
	/*
	 * Check to see why we were called.
	 */
	if (pDpa->shutdown) {
		printk (KERN_INFO
			"(rcpci45 driver:) Shutting down interface\n");
		pDpa->shutdown = 0;
		pDpa->reboot = 0;
		MOD_DEC_USE_COUNT;
	} else if (pDpa->reboot) {
		printk (KERN_INFO
			"(rcpci45 driver:) reboot, shutdown adapter\n");
		/*
		 * We don't set any of the flags in RCShutdownLANCard()
		 * and we don't pass a callback routine to it.
		 * The adapter will have already initiated the reboot by
		 * the time the function returns.
		 */
		RCDisableI2OInterrupts (dev);
		RCShutdownLANCard (dev, 0, 0, 0);
		printk (KERN_INFO "(rcpci45 driver:) scheduling timer...\n");
		init_timer (&pDpa->timer);
		pDpa->timer.expires = RUN_AT ((40 * HZ) / 10);	/* 4 sec. */
		pDpa->timer.data = (unsigned long) dev;
		pDpa->timer.function = &rc_timer;	/* timer handler */
		add_timer (&pDpa->timer);
	}
}

static void
RCreboot_callback (U32 Status, U32 p1, U32 p2, struct net_device *dev)
{
	PDPA pDpa = dev->priv;

	dprintk ("RCreboot: rcv buffers outstanding = %d\n",
		 (uint) pDpa->numOutRcvBuffers);

	if (pDpa->shutdown) {
		printk (KERN_INFO
			"(rcpci45 driver:) skipping reboot sequence -- shutdown already initiated\n");
		return;
	}
	pDpa->reboot = 1;
	/*
	 * OK, we reset the adapter and ask it to return all
	 * outstanding transmit buffers as well as the posted
	 * receive buffers.  When the adapter is done returning
	 * those buffers, it will call our RCreset_callback() 
	 * routine.  In that routine, we'll call RCShutdownLANCard()
	 * to tell the adapter that it's OK to start the reboot and
	 * schedule a timer callback routine to execute 3 seconds 
	 * later; this routine will reinitialize the adapter at that time.
	 */
	RCResetLANCard (dev, RC_RESOURCE_RETURN_POSTED_RX_BUCKETS |
			RC_RESOURCE_RETURN_PEND_TX_BUFFERS, 0,
			(PFNCALLBACK) RCreset_callback);
}

int
broadcast_packet (unsigned char *address)
{
	int i;
	for (i = 0; i < 6; i++)
		if (address[i] != 0xff)
			return 0;

	return 1;
}

/*
 * RCrecv_callback()
 * 
 * The receive packet callback routine.  This is called by
 * RCProcI2OMsgQ() after the adapter posts buffers which have been
 * filled (one ethernet packet per buffer).
 */
static void
RCrecv_callback (U32 Status,
		 U8 PktCount,
		 U32 BucketsRemain,
		 PU32 PacketDescBlock, struct net_device *dev)
{

	U32 len, count;
	PDPA pDpa = dev->priv;
	struct sk_buff *skb;
	singleTCB tcb;
	psingleTCB ptcb = &tcb;

	ptcb->bcount = 1;

	dprintk ("RCrecv_callback: 0x%x, 0x%x, 0x%x\n",
		 (uint) PktCount, (uint) BucketsRemain, (uint) PacketDescBlock);

	if ((pDpa->shutdown || pDpa->reboot) && !Status)
		dprintk ("shutdown||reboot && !Status: PktCount = %d\n",
			 PktCount);

	if ((Status != I2O_REPLY_STATUS_SUCCESS) || pDpa->shutdown) {
		/*
		 * Free whatever buffers the adapter returned, but don't
		 * pass them to the kernel.
		 */

		if (!pDpa->shutdown && !pDpa->reboot)
			printk (KERN_INFO
				"(rcpci45 driver:) RCrecv error: status = 0x%x\n",
				(uint) Status);
		else
			dprintk ("Returning %d buffers, status = 0x%x\n",
				 PktCount, (uint) Status);
		/*
		 * TO DO: check the nature of the failure and put the adapter in
		 * failed mode if it's a hard failure.  Send a reset to the adapter
		 * and free all outstanding memory.
		 */
		if (Status == I2O_REPLY_STATUS_ABORT_NO_DATA_TRANSFER)
			dprintk ("RCrecv status ABORT NO DATA TRANSFER\n");

		/* check for reset status: I2O_REPLY_STATUS_ABORT_NO_DATA_TRANSFER */
		if (PacketDescBlock) {
			while (PktCount--) {
				skb = (struct sk_buff *) PacketDescBlock[0];
				dprintk ("free skb 0x%p\n", skb);
				dev_kfree_skb (skb);
				pDpa->numOutRcvBuffers--;
				PacketDescBlock += BD_SIZE;	/* point to next context field */
			}
		}
		return;
	} else {
		while (PktCount--) {
			skb = (struct sk_buff *) PacketDescBlock[0];
			if (pDpa->shutdown)
				dprintk ("shutdown: skb=0x%x\n", (uint) skb);

			dprintk ("skb = 0x%x: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
				 (uint) skb, (uint) skb->data[0],
				 (uint) skb->data[1], (uint) skb->data[2],
				 (uint) skb->data[3], (uint) skb->data[4],
				 (uint) skb->data[5]);

#ifdef PROMISCUOUS_BY_DEFAULT	/* early 2.x firmware */
			if ((memcmp (dev->dev_addr, skb->data, 6)) &&
			    (!broadcast_packet (skb->data))) {
				/*
				 * Re-post the buffer to the adapter.  Since the adapter usually
				 * return 1 to 2 receive buffers at a time, it's not too inefficient
				 * post one buffer at a time but ... may be that should be 
				 * optimized at some point.
				 */
				ptcb->b.context = (U32) skb;
				ptcb->b.scount = 1;
				ptcb->b.size = MAX_ETHER_SIZE;
				ptcb->b.addr = virt_to_bus ((void *) skb->data);

				if (RCPostRecvBuffers (dev, (PRCTCB) ptcb) !=
				    RC_RTN_NO_ERROR) {
					printk (KERN_WARNING
						"(rcpci45 driver:) RCrecv_callback: post buffer failed!\n");
					dev_kfree_skb (skb);
				} else
					pDpa->numOutRcvBuffers++;
			} else
#endif				/* PROMISCUOUS_BY_DEFAULT */
			{
				len = PacketDescBlock[2];
				skb->dev = dev;
				skb_put (skb, len);	/* adjust length and tail */
				skb->protocol = eth_type_trans (skb, dev);
				netif_rx (skb);	/* send the packet to the kernel */
				dev->last_rx = jiffies;
			}
			pDpa->numOutRcvBuffers--;
			PacketDescBlock += BD_SIZE;	/* point to next context field */
		}
	}

	/*
	 * Replenish the posted receive buffers. 
	 * DO NOT replenish buffers if the driver has already
	 * initiated a reboot or shutdown!
	 */

	if (!pDpa->shutdown && !pDpa->reboot) {
		count = RC_allocate_and_post_buffers (dev,
						      MAX_NMBR_RCV_BUFFERS -
						      pDpa->numOutRcvBuffers);
		pDpa->numOutRcvBuffers += count;
	}

}

/*
 * RCinterrupt()
 * 
 * Interrupt handler. 
 * This routine sets up a couple of pointers and calls
 * RCProcI2OMsgQ(), which in turn process the message and
 * calls one of our callback functions.
 */
static void
RCinterrupt (int irq, void *dev_id, struct pt_regs *regs)
{

	PDPA pDpa;
	struct net_device *dev = dev_id;

	pDpa = dev->priv;

	if (pDpa->shutdown)
		dprintk ("shutdown: service irq\n");

	dprintk ("RC irq: pDpa = 0x%x, dev = 0x%x, id = %d\n",
		 (uint) pDpa, (uint) dev, (uint) pDpa->id);
	dprintk ("dev = 0x%x\n", (uint) dev);

	RCProcI2OMsgQ (dev);
}

#define REBOOT_REINIT_RETRY_LIMIT 4
static void
rc_timer (unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	PDPA pDpa = dev->priv;
	int init_status;
	static int retry;
	int post_buffers = MAX_NMBR_RCV_BUFFERS;
	int count = 0;
	int requested = 0;

	if (pDpa->reboot) {
		init_status =
		    RCInitI2OMsgLayer (dev, (PFNTXCALLBACK) RCxmit_callback,
				       (PFNRXCALLBACK) RCrecv_callback,
				       (PFNCALLBACK) RCreboot_callback);

		switch (init_status) {
		case RC_RTN_NO_ERROR:

			pDpa->reboot = 0;
			pDpa->shutdown = 0;	/* just in case */
			RCReportDriverCapability (dev, DriverControlWord);
			RCEnableI2OInterrupts (dev);

			if (dev->flags & IFF_UP) {
				while (post_buffers) {
					if (post_buffers >
					    MAX_NMBR_POST_BUFFERS_PER_MSG)
						requested =
						    MAX_NMBR_POST_BUFFERS_PER_MSG;
					else
						requested = post_buffers;
					count =
					    RC_allocate_and_post_buffers (dev,
									  requested);
					post_buffers -= count;
					if (count < requested)
						break;
				}
				pDpa->numOutRcvBuffers =
				    MAX_NMBR_RCV_BUFFERS - post_buffers;
				dprintk ("rc: posted %d buffers \r\n",
					 (uint) pDpa->numOutRcvBuffers);
			}
			dprintk ("Initialization done.\n");
			netif_wake_queue (dev);
			retry = 0;
			return;
		case RC_RTN_FREE_Q_EMPTY:
			retry++;
			printk (KERN_WARNING
				"(rcpci45 driver:) inbound free q empty\n");
			break;
		default:
			retry++;
			printk (KERN_WARNING
				"(rcpci45 driver:) bad status after reboot: %d\n",
				init_status);
			break;
		}

		if (retry > REBOOT_REINIT_RETRY_LIMIT) {
			printk (KERN_WARNING
				"(rcpci45 driver:) unable to reinitialize adapter after reboot\n");
			printk (KERN_WARNING
				"(rcpci45 driver:) decrementing driver and closing interface\n");
			RCDisableI2OInterrupts (dev);
			dev->flags &= ~IFF_UP;
			MOD_DEC_USE_COUNT;
		} else {
			printk (KERN_INFO
				"(rcpci45 driver:) rescheduling timer...\n");
			init_timer (&pDpa->timer);
			pDpa->timer.expires = RUN_AT ((40 * HZ) / 10);	/* 3 sec. */
			pDpa->timer.data = (unsigned long) dev;
			pDpa->timer.function = &rc_timer;	/* timer handler */
			add_timer (&pDpa->timer);
		}
	} else
		printk (KERN_WARNING "(rcpci45 driver:) timer??\n");
}

static int
RCclose (struct net_device *dev)
{
	PDPA pDpa = dev->priv;

	netif_stop_queue (dev);

	dprintk ("RCclose\r\n");

	if (pDpa->reboot) {
		printk (KERN_INFO
			"(rcpci45 driver:) skipping reset -- adapter already in reboot mode\n");
		dev->flags &= ~IFF_UP;
		pDpa->shutdown = 1;
		return 0;
	}
	dprintk ("receive buffers outstanding: %d\n",
		 (uint) pDpa->numOutRcvBuffers);

	pDpa->shutdown = 1;

	/*
	 * We can't allow the driver to be unloaded until the adapter returns
	 * all posted receive buffers.  It doesn't hurt to tell the adapter
	 * to return all posted receive buffers and outstanding xmit buffers,
	 * even if there are none.
	 */

	RCShutdownLANCard (dev, RC_RESOURCE_RETURN_POSTED_RX_BUCKETS |
			   RC_RESOURCE_RETURN_PEND_TX_BUFFERS, 0,
			   (PFNCALLBACK) RCreset_callback);

	dev->flags &= ~IFF_UP;
	return 0;
}

static struct net_device_stats *
RCget_stats (struct net_device *dev)
{
	RCLINKSTATS RCstats;

	PDPA pDpa = dev->priv;

	if (!pDpa) {
		dprintk ("RCget_stats: !pDpa\n");
		return 0;
	} else if (!(dev->flags & IFF_UP)) {
		dprintk ("RCget_stats: device down\n");
		return 0;
	}

	memset (&RCstats, 0, sizeof (RCLINKSTATS));
	if ((RCGetLinkStatistics (dev, &RCstats, (void *) 0)) ==
	    RC_RTN_NO_ERROR) {
		dprintk ("TX_good 0x%x\n", (uint) RCstats.TX_good);
		dprintk ("TX_maxcol 0x%x\n", (uint) RCstats.TX_maxcol);
		dprintk ("TX_latecol 0x%x\n", (uint) RCstats.TX_latecol);
		dprintk ("TX_urun 0x%x\n", (uint) RCstats.TX_urun);
		dprintk ("TX_crs 0x%x\n", (uint) RCstats.TX_crs);
		dprintk ("TX_def 0x%x\n", (uint) RCstats.TX_def);
		dprintk ("TX_singlecol 0x%x\n", (uint) RCstats.TX_singlecol);
		dprintk ("TX_multcol 0x%x\n", (uint) RCstats.TX_multcol);
		dprintk ("TX_totcol 0x%x\n", (uint) RCstats.TX_totcol);

		dprintk ("Rcv_good 0x%x\n", (uint) RCstats.Rcv_good);
		dprintk ("Rcv_CRCerr 0x%x\n", (uint) RCstats.Rcv_CRCerr);
		dprintk ("Rcv_alignerr 0x%x\n", (uint) RCstats.Rcv_alignerr);
		dprintk ("Rcv_reserr 0x%x\n", (uint) RCstats.Rcv_reserr);
		dprintk ("Rcv_orun 0x%x\n", (uint) RCstats.Rcv_orun);
		dprintk ("Rcv_cdt 0x%x\n", (uint) RCstats.Rcv_cdt);
		dprintk ("Rcv_runt 0x%x\n", (uint) RCstats.Rcv_runt);

		pDpa->stats.rx_packets = RCstats.Rcv_good;	/* total packets received    */
		pDpa->stats.tx_packets = RCstats.TX_good;	/* total packets transmitted    */

		pDpa->stats.rx_errors = RCstats.Rcv_CRCerr + RCstats.Rcv_alignerr + RCstats.Rcv_reserr + RCstats.Rcv_orun + RCstats.Rcv_cdt + RCstats.Rcv_runt;	/* bad packets received        */

		pDpa->stats.tx_errors = RCstats.TX_urun + RCstats.TX_crs + RCstats.TX_def + RCstats.TX_totcol;	/* packet transmit problems    */

		/*
		 * This needs improvement.
		 */
		pDpa->stats.rx_dropped = 0;	/* no space in linux buffers    */
		pDpa->stats.tx_dropped = 0;	/* no space available in linux    */
		pDpa->stats.multicast = 0;	/* multicast packets received    */
		pDpa->stats.collisions = RCstats.TX_totcol;

		/* detailed rx_errors: */
		pDpa->stats.rx_length_errors = 0;
		pDpa->stats.rx_over_errors = RCstats.Rcv_orun;	/* receiver ring buff overflow    */
		pDpa->stats.rx_crc_errors = RCstats.Rcv_CRCerr;	/* recved pkt with crc error    */
		pDpa->stats.rx_frame_errors = 0;	/* recv'd frame alignment error */
		pDpa->stats.rx_fifo_errors = 0;	/* recv'r fifo overrun        */
		pDpa->stats.rx_missed_errors = 0;	/* receiver missed packet    */

		/* detailed tx_errors */
		pDpa->stats.tx_aborted_errors = 0;
		pDpa->stats.tx_carrier_errors = 0;
		pDpa->stats.tx_fifo_errors = 0;
		pDpa->stats.tx_heartbeat_errors = 0;
		pDpa->stats.tx_window_errors = 0;

		return ((struct net_device_stats *) &(pDpa->stats));
	}
	return 0;
}

static int
RCioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	RCuser_struct RCuser;
	PDPA pDpa = dev->priv;

	dprintk ("RCioctl: cmd = 0x%x\n", cmd);

	if (!capable (CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {

	case RCU_PROTOCOL_REV:
		/*
		 * Assign user protocol revision, to tell user-level
		 * controller program whether or not it's in sync.
		 */
		rq->ifr_ifru.ifru_data = (caddr_t) USER_PROTOCOL_REV;
		break;

	case RCU_COMMAND:
		{
			if (copy_from_user
			    (&RCuser, rq->ifr_data, sizeof (RCuser)))
				return -EFAULT;

			dprintk ("RCioctl: RCuser_cmd = 0x%x\n", RCuser.cmd);

			switch (RCuser.cmd) {
			case RCUC_GETFWVER:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETFWVER\n");
				RCUD_GETFWVER = &RCuser.RCUS_GETFWVER;
				RCGetFirmwareVer (dev,
						  (PU8) & RCUD_GETFWVER->
						  FirmString, NULL);
				break;
			case RCUC_GETINFO:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETINFO\n");
				RCUD_GETINFO = &RCuser.RCUS_GETINFO;
				RCUD_GETINFO->mem_start = dev->base_addr;
				RCUD_GETINFO->mem_end =
				    dev->base_addr + pDpa->pci_addr_len;
				RCUD_GETINFO->base_addr = pDpa->pci_addr;
				RCUD_GETINFO->irq = dev->irq;
				break;
			case RCUC_GETIPANDMASK:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETIPANDMASK\n");
				RCUD_GETIPANDMASK = &RCuser.RCUS_GETIPANDMASK;
				RCGetRavlinIPandMask (dev,
						      (PU32) &
						      RCUD_GETIPANDMASK->IpAddr,
						      (PU32) &
						      RCUD_GETIPANDMASK->
						      NetMask, NULL);
				break;
			case RCUC_GETLINKSTATISTICS:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETLINKSTATISTICS\n");
				RCUD_GETLINKSTATISTICS =
				    &RCuser.RCUS_GETLINKSTATISTICS;
				RCGetLinkStatistics (dev,
						     (P_RCLINKSTATS) &
						     RCUD_GETLINKSTATISTICS->
						     StatsReturn, NULL);
				break;
			case RCUC_GETLINKSTATUS:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETLINKSTATUS\n");
				RCUD_GETLINKSTATUS = &RCuser.RCUS_GETLINKSTATUS;
				RCGetLinkStatus (dev,
						 (PU32) & RCUD_GETLINKSTATUS->
						 ReturnStatus, NULL);
				break;
			case RCUC_GETMAC:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETMAC\n");
				RCUD_GETMAC = &RCuser.RCUS_GETMAC;
				RCGetMAC (dev, NULL);
				break;
			case RCUC_GETPROM:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETPROM\n");
				RCUD_GETPROM = &RCuser.RCUS_GETPROM;
				RCGetPromiscuousMode (dev,
						      (PU32) & RCUD_GETPROM->
						      PromMode, NULL);
				break;
			case RCUC_GETBROADCAST:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETBROADCAST\n");
				RCUD_GETBROADCAST = &RCuser.RCUS_GETBROADCAST;
				RCGetBroadcastMode (dev,
						    (PU32) & RCUD_GETBROADCAST->
						    BroadcastMode, NULL);
				break;
			case RCUC_GETSPEED:
				printk (KERN_INFO
					"(rcpci45 driver:) RC GETSPEED\n");
				if (!(dev->flags & IFF_UP)) {
					printk (KERN_ERR
						"(rcpci45 driver:) RCioctl, GETSPEED error: interface down\n");
					return -ENODATA;
				}
				RCUD_GETSPEED = &RCuser.RCUS_GETSPEED;
				RCGetLinkSpeed (dev,
						(PU32) & RCUD_GETSPEED->
						LinkSpeedCode, NULL);
				printk (KERN_INFO
					"(rcpci45 driver:) RC speed = 0x%u\n",
					RCUD_GETSPEED->LinkSpeedCode);
				break;
			case RCUC_SETIPANDMASK:
				printk (KERN_INFO
					"(rcpci45 driver:) RC SETIPANDMASK\n");
				RCUD_SETIPANDMASK = &RCuser.RCUS_SETIPANDMASK;
				printk (KERN_INFO
					"(rcpci45 driver:) RC New IP Addr = %d.%d.%d.%d, ",
					(U8) ((RCUD_SETIPANDMASK->
					       IpAddr) & 0xff),
					(U8) ((RCUD_SETIPANDMASK->
					       IpAddr >> 8) & 0xff),
					(U8) ((RCUD_SETIPANDMASK->
					       IpAddr >> 16) & 0xff),
					(U8) ((RCUD_SETIPANDMASK->
					       IpAddr >> 24) & 0xff));
				printk (KERN_INFO
					"(rcpci45 driver:) RC New Mask = %d.%d.%d.%d\n",
					(U8) ((RCUD_SETIPANDMASK->
					       NetMask) & 0xff),
					(U8) ((RCUD_SETIPANDMASK->
					       NetMask >> 8) & 0xff),
					(U8) ((RCUD_SETIPANDMASK->
					       NetMask >> 16) & 0xff),
					(U8) ((RCUD_SETIPANDMASK->
					       NetMask >> 24) & 0xff));
				RCSetRavlinIPandMask (dev,
						      (U32) RCUD_SETIPANDMASK->
						      IpAddr,
						      (U32) RCUD_SETIPANDMASK->
						      NetMask);
				break;
			case RCUC_SETMAC:
				printk (KERN_INFO
					"(rcpci45 driver:) RC SETMAC\n");
				RCUD_SETMAC = &RCuser.RCUS_SETMAC;
				printk (KERN_INFO
					"(rcpci45 driver:) RC New MAC addr = %02X:%02X:%02X:%02X:%02X:%02X\n",
					(U8) (RCUD_SETMAC->mac[0]),
					(U8) (RCUD_SETMAC->mac[1]),
					(U8) (RCUD_SETMAC->mac[2]),
					(U8) (RCUD_SETMAC->mac[3]),
					(U8) (RCUD_SETMAC->mac[4]),
					(U8) (RCUD_SETMAC->mac[5]));
				RCSetMAC (dev, (PU8) & RCUD_SETMAC->mac);
				break;
			case RCUC_SETSPEED:
				printk (KERN_INFO
					"(rcpci45 driver:) RC SETSPEED\n");
				RCUD_SETSPEED = &RCuser.RCUS_SETSPEED;
				RCSetLinkSpeed (dev,
						(U16) RCUD_SETSPEED->
						LinkSpeedCode);
				printk (KERN_INFO
					"(rcpci45 driver:) RC New speed = 0x%x\n",
					RCUD_SETSPEED->LinkSpeedCode);
				break;
			case RCUC_SETPROM:
				printk (KERN_INFO
					"(rcpci45 driver:) RC SETPROM\n");
				RCUD_SETPROM = &RCuser.RCUS_SETPROM;
				RCSetPromiscuousMode (dev,
						      (U16) RCUD_SETPROM->
						      PromMode);
				printk (KERN_INFO
					"(rcpci45 driver:) RC New prom mode = 0x%x\n",
					RCUD_SETPROM->PromMode);
				break;
			case RCUC_SETBROADCAST:
				printk (KERN_INFO
					"(rcpci45 driver:) RC SETBROADCAST\n");
				RCUD_SETBROADCAST = &RCuser.RCUS_SETBROADCAST;
				RCSetBroadcastMode (dev,
						    (U16) RCUD_SETBROADCAST->
						    BroadcastMode);
				printk (KERN_INFO
					"(rcpci45 driver:) RC New broadcast mode = 0x%x\n",
					RCUD_SETBROADCAST->BroadcastMode);
				break;
			default:
				printk (KERN_INFO
					"(rcpci45 driver:) RC command default\n");
				RCUD_DEFAULT = &RCuser.RCUS_DEFAULT;
				RCUD_DEFAULT->rc = 0x11223344;
				break;
			}
			if (copy_to_user
			    (rq->ifr_data, &RCuser, sizeof (RCuser)))
				return -EFAULT;
			break;
		}		/* RCU_COMMAND */

	default:
		rq->ifr_ifru.ifru_data = (caddr_t) 0x12345678;
		return -EINVAL;
	}
	return 0;
}

static int
RCconfig (struct net_device *dev, struct ifmap *map)
{
	/*
	 * To be completed ...
	 */
	dprintk ("RCconfig\n");
	return 0;
	if (dev->flags & IFF_UP)	/* can't act on a running interface */
		return -EBUSY;

	/* Don't allow changing the I/O address */
	if (map->base_addr != dev->base_addr) {
		printk (KERN_WARNING
			"(rcpci45 driver:)  Change I/O address not implemented\n");
		return -EOPNOTSUPP;
	}
	return 0;
}

static void __exit
rcpci_cleanup_module (void)
{
	pci_unregister_driver (&rcpci45_driver);
}

module_init (rcpci_init_module);
module_exit (rcpci_cleanup_module);

static int
RC_allocate_and_post_buffers (struct net_device *dev, int numBuffers)
{

	int i;
	PU32 p;
	psingleB pB;
	struct sk_buff *skb;
	RC_RETURN status;
	U32 res;

	if (!numBuffers)
		return 0;
	else if (numBuffers > MAX_NMBR_POST_BUFFERS_PER_MSG) {
		dprintk ("Too many buffers requested!\n");
		dprintk ("attempting to allocate only 32 buffers\n");
		numBuffers = 32;
	}

	p = (PU32) kmalloc (sizeof (U32) + numBuffers * sizeof (singleB),
			    GFP_KERNEL);

	dprintk ("TCB = 0x%x\n", (uint) p);

	if (!p) {
		printk (KERN_WARNING
			"(rcpci45 driver:) RCopen: unable to allocate TCB\n");
		return 0;
	}

	p[0] = 0;		/* Buffer Count */
	pB = (psingleB) ((U32) p + sizeof (U32));	/* point to the first buffer */

	dprintk ("p[0] = 0x%x, p = 0x%x, pB = 0x%x\n", (uint) p[0], (uint) p,
		 (uint) pB);
	dprintk ("pB = 0x%x\n", (uint) pB);

	for (i = 0; i < numBuffers; i++) {
		skb = dev_alloc_skb (MAX_ETHER_SIZE + 2);
		if (!skb) {
			dprintk
			    ("Doh! RCopen: unable to allocate enough skbs!\n");
			if (*p != 0) {	/* did we allocate any buffers at all? */
				dprintk ("will post only %d buffers \n",
					 (uint) (*p));
				break;
			} else {
				kfree (p);	/* Free the TCB */
				return 0;
			}
		}
		dprintk ("post 0x%x\n", (uint) skb);
		skb_reserve (skb, 2);	/* Align IP on 16 byte boundaries */
		pB->context = (U32) skb;
		pB->scount = 1;	/* segment count */
		pB->size = MAX_ETHER_SIZE;
		pB->addr = virt_to_bus ((void *) skb->data);
		p[0]++;
		pB++;
	}

	if ((status = RCPostRecvBuffers (dev, (PRCTCB) p)) != RC_RTN_NO_ERROR) {
		printk (KERN_WARNING
			"(rcpci45 driver:) Post buffer failed with error code 0x%x!\n",
			status);
		pB = (psingleB) ((U32) p + sizeof (U32));	/* point to the first buffer */
		while (p[0]) {
			skb = (struct sk_buff *) pB->context;
			dprintk ("freeing 0x%x\n", (uint) skb);
			dev_kfree_skb (skb);
			p[0]--;
			pB++;
		}
		dprintk ("freed all buffers, p[0] = %d\n", (uint) p[0]);
	}
	res = p[0];
	kfree (p);
	return (res);		/* return the number of posted buffers */
}
