/*****************************************************************************/

/*
 *      plusb.c  --  prolific pl-2301/pl-2302 driver.
 *
 *      Copyright (C) 2000  Deti Fliegl (deti@fliegl.de)
 *      Copyright (C) 2000  Pavel Machek (pavel@suse.cz)
 *      Copyright (C) 2000  Eric Z. Ayers (eric@compgen.com)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  This driver creates a network interface (plusb0, plusb1, ...) that will
 *  send messages over a USB host-host cable based on the Prolific ASIC.
 *  It works a lot like plip or PP over an RS-232C null modem cable.
 *
 *  Expect speeds of around 330Kbytes/second over a UHCI host controller.
 *  OHCI should be faster.  Increase the MTU for faster transfers of large
 *  files (up-to 800Kbytes/second).  (16384 is a good size)
 *
 *  $Id: plusb.c,v 1.18 2000/02/14 10:38:58 fliegl Exp $
 *
 *  Changelog:
 *
 *    v0.1                deti
 *        Original Version of driver.
 *    v0.2     15 Sep 2000  pavel
 *        Patches to decrease latency by rescheduling the bottom half of
 *          interrupt code.
 *    v0.3     10 Oct 2000  eric
 *        Patches to work in v2.2 backport (v2.4 changes the way net_dev.name
 *          is allocated)
 *    v0.4     19 Oct 2000  eric
 *        Some more performance fixes.  Lock re-submitting urbs.
 *          Lower the number of sk_buff's to queue.
 *    v0.5     25 Oct 2000 eric
 *        Removed use of usb_bulk_msg() all together.  This caused
 *          the driver to block in an interrupt context.
 *        Consolidate read urb submission into read_urb_submit().
 *        Performance is the same as v0.4.
 *    v0.5.1   27 Oct 2000 eric
 *        Extra debugging messages to help diagnose problem with uchi.o stack.
 *    v0.5.2   27 Oct 2000 eric
 *        Set the 'start' flag for the network device in plusb_net_start()
 *         and plusb_net_stop() (doesn't help)
 *    v0.5.3   27 Oct 2000 pavel
 *        Commented out handlers when -EPIPE is received,
 *         (remove calls to usb_clear_halt()) Since the callback is in
 *         an interrupt context, it doesn't help, it just panics
 *         the kernel. (what do we do?)
 *        Under high load, dev_alloc_skb() fails, the read URB must
 *         be re-submitted.
 *        Added plusb_change_mtu() and increased the size of _BULK_DATA_LEN
 *    v0.5.4   31 Oct 2000 eric
 *        Fix race between plusb_net_xmit() and plusb_bulk_write_complete()
 *    v0.5.5    1 Nov 2000 eric
 *        Remove dev->start field, otherwise, it won't compile in 2.4
 *        Use dev_kfree_skb_any(). (important in 2.4 kernel)
 *    v0.5.6   2 Nov 2000 pavel,eric
 *        Add calls to netif_stop_queue() and netif_start_queue()
 *        Drop packets that come in while the free list is empty.
 *        (This version is being submitted after the release of 2.4-test10)
 *    v0.5.7   6 Nov 2000
 *        Fix to not re-submit the urb on error to help when cables
 *          are yanked (not tested)
 *
 *
 * KNOWN PROBLEMS: (Any suggestions greatfully accepted!)
 *
 *     2 Nov 2000
 *      - The shutdown for this may not be entirely clean.  Sometimes, the
 *         kernel will Oops when the cable is unplugged, or
 *         if the plusb module is removed.
 *      - If you ifdown a device and then ifup it again, the link will not
 *         always work.  You have to 'rmmod plusb ; modprobe plusb' on
 *         both machines to get it to work again.  Something must be wrong with
 *         plusb_net_open() and plusb_net_start() ?  Maybe
 *         the 'suspend' and 'resume' entry points need to be
 *         implemented?
 *      - Needs to handle -EPIPE correctly in bulk complete handlers.
 *         (replace usb_clear_halt() function with async urbs?)
 *      - I think this code relies too much on one spinlock and does
 *         too much in the interrupt handler.  The net1080 code is
 *         much more elegant, and should work for this chip.  Its
 *         only drawback is that it is going to be tough to backport
 *         it to v2.2.
 *      - Occasionally the device will hang under the 'uhci.o'
 *         driver.   The workaround is to ifdown the device and
 *         remove the modules, then re-insert them.  You may have
 *         better luck with the 'usb-uhci.o' driver.
 *      - After using ifconfig down ; ifconfig up, sometimes packets
 *         continue to be received, but there is a framing problem.
 *
 * FUTURE DIRECTIONS:
 *
 *     - Fix the known problems.
 *     - There isn't much functional difference between the net1080
 *        driver and this one.  It would be neat if the same driver
 *        could handle both types of chips.  Or if both drivers
 *        could handle both types of chips - this one is easier to
 *        backport to the 2.2 kernel.
 *     - Get rid of plusb_add_buf_tail and the single spinlock.
 *        Use a separate spinlock for the 2 lists, and use atomic
 *        operators for writeurb_submitted and readurb_submitted members.
 *
 *
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/socket.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
//#define DEBUG 1
#include <linux/usb.h>

#if (LINUX_VERSION_CODE < 0x020300)
#define dev_kfree_skb_any dev_kfree_skb
#endif

/* Definitions formerly in plusb.h relocated. No need to export them -EZA */

#define _PLUSB_INTPIPE		0x1
#define _PLUSB_BULKOUTPIPE	0x2
#define _PLUSB_BULKINPIPE	0x3

#define _SKB_NUM		32

/* increase size of BULK_DATA_LEN so we can use bigger MTU's*/
#define _BULK_DATA_LEN		32768


typedef struct
{
	int connected;   /* indicates if this structure is active */
	struct usb_device *usbdev;
					 /* keep track of USB structure */
	int status;      /* Prolific status byte returned from interrupt */
	int in_bh;       /* flag to indicate that we are in the bulk handler */
	int opened;      /* flag to indicate that network dev is open    */

	spinlock_t lock; /* Lock for the buffer list. re-used for
						locking around submitting the readurb member.
					 */
	urb_t *inturb;   /* Read buffer for the interrupt callback */
	unsigned char *		interrupt_in_buffer;
			 /* holds data for the inturb*/
	urb_t *readurb;  /* Read buffer for the bulk data callback */
	unsigned char *		bulk_in_buffer;
					 /* kmalloc'ed data for the readurb */
	int readurb_submitted;
					 /* Flag to indicate that readurb already sent */
	urb_t *writeurb; /* Write buffer for the bulk data callback */
	int writeurb_submitted;
					 /* Flag to indicate that writeurb already sent */
	
	struct list_head tx_skb_list;
		 			 /* sk_buff's read from net device */
	struct list_head free_skb_list;
					/* free sk_buff list */
	struct net_device net_dev;
					/* handle to linux network device */
	struct net_device_stats net_stats;
					/* stats to return for ifconfig output */
} plusb_t,*pplusb_t;

/*
 * skb_list - queue of packets from the network driver to be delivered to USB
 */
typedef struct
{
	struct list_head skb_list;
	struct sk_buff *skb;
	int state;
	plusb_t *s;
} skb_list_t,*pskb_list_t;


/* --------------------------------------------------------------------- */

#define NRPLUSB 4

/*
 * Interrupt endpoint status byte, from Prolific PL-2301 docs
 * Check the 'download' link at www.prolifictech.com
 */
#define _PL_INT_RES1    0x80 /* reserved              */
#define _PL_INT_RES2    0x40 /* reserved              */
#define _PL_INT_RXD	_PL_INT_RES2  /* Read data ready - Not documented by Prolific, but seems to work! */
#define _PL_INT_TX_RDY	0x20 /* OK to transmit data   */
#define _PL_INT_RESET_O	0x10 /* reset output pipe     */
#define _PL_INT_RESET_I 0x08 /* reset input pipe      */
#define _PL_INT_TX_C    0x04 /* transmission complete */
#define _PL_INT_TX_REQ  0x02 /* transmission received */
#define _PL_INT_PEER_E  0x01 /* peer exists           */

/*-------------------------------------------------------------------*/

static plusb_t plusb[NRPLUSB];

static void plusb_write_bulk_complete(urb_t *purb);
static void plusb_read_bulk_complete(urb_t *purb);
static void plusb_int_complete(urb_t *purb);

/* --------------------------------------------------------------------- */

/*
 * plusb_add_buf_tail - Take the head of the src list and append it to
 *                      the tail of the dest list
 */
static int plusb_add_buf_tail (plusb_t *s, struct list_head *dst, struct list_head *src)
{
	unsigned long flags = 0;
	struct list_head *tmp;
	int ret = 0;

	spin_lock_irqsave (&s->lock, flags);

	if (list_empty (src)) {
		// no elements in source buffer
		ret = -1;
		goto err;
	}
	tmp = src->next;
	list_del (tmp);
	list_add_tail (tmp, dst);

  err:	spin_unlock_irqrestore (&s->lock, flags);
	return ret;
}
/*-------------------------------------------------------------------*/

/*
 * dequeue_next_skb - submit the first thing on the tx_skb_list to the
 * USB stack.  This function should be called each time we get a new
 * message to send to the other host, or each time a message is sucessfully
 * sent.
 */
static void dequeue_next_skb(char * func, plusb_t * s)
{
	skb_list_t * skb_list;
	unsigned long flags = 0;

	if (!s->connected)
		return;
	
	spin_lock_irqsave (&s->lock, flags);
	
	if (!list_empty (&s->tx_skb_list) && !s->writeurb_submitted) {
		int submit_ret;
		skb_list = list_entry (s->tx_skb_list.next, skb_list_t, skb_list);

		if (skb_list->skb) {
			s->writeurb_submitted = 1;
		
			/* Use the buffer inside the sk_buff directly. why copy? */
			FILL_BULK_URB_TO(s->writeurb, s->usbdev,
							 usb_sndbulkpipe(s->usbdev, _PLUSB_BULKOUTPIPE),
							 skb_list->skb->data, skb_list->skb->len,
							 plusb_write_bulk_complete, skb_list, 500);
			
			dbg ("%s: %s: submitting urb. skb_list %p", s->net_dev.name, func, skb_list);
		
			submit_ret = usb_submit_urb(s->writeurb);
			if (submit_ret) {
				s->writeurb_submitted = 0;
				printk (KERN_CRIT "%s: %s: can't submit writeurb: %d\n",
						s->net_dev.name, func, submit_ret);
			}
		} /* end if the skb value has been filled in */
	}
	
	spin_unlock_irqrestore (&s->lock, flags);	
}

/*
 * submit_read_urb - re-submit the read URB to the stack
 */
void submit_read_urb(char * func, plusb_t * s)
{
	unsigned long flags=0;

	if (!s->connected)
		return;
	
	spin_lock_irqsave (&s->lock, flags);
	
	if (!s->readurb_submitted) {
		int ret;
		s->readurb_submitted=1;
		s->readurb->dev=s->usbdev;
		ret = usb_submit_urb(s->readurb);
		if (ret) {
			printk (KERN_CRIT "%s: %s: error %d submitting read URB\n",
				   s->net_dev.name, func, ret);
			s->readurb_submitted=0;
		}
	}

	spin_unlock_irqrestore (&s->lock, flags);
	
}
/* --------------------------------------------------------------------- */

/*
 * plusb_net_xmit - callback from the network device driver for outgoing data
 *
 * Data has arrived to the network device from the local machine and needs
 * to be sent over the USB cable.  This is in an interrupt, so we don't
 * want to spend too much time in this function.
 *
 */
static int plusb_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	plusb_t *s=dev->priv;
	skb_list_t *skb_list;
	unsigned int flags;

	dbg("plusb_net_xmit: len:%d i:%d",skb->len,in_interrupt());

	if(!s->connected || !s->opened) {
		/*
		  NOTE: If we get to this point, you'll return the error
		  kernel: virtual device plusb0 asks to queue packet

		  Other things we could do:
		  1) just drop this packet
		  2) drop other packets in the queue
		*/
		return 1;
	}

	spin_lock_irqsave (&s->lock, flags);

	if  (list_empty(&s->free_skb_list)
	|| plusb_add_buf_tail (s, &s->tx_skb_list, &s->free_skb_list)) {
		/* The buffers on this side are full. DROP the packet
		   I think that this shouldn't happen with the correct
		   use of the netif_XXX functions -EZA
		 */
		dbg ("plusb: Free list is empty.");
		kfree_skb(skb);
		s->net_stats.tx_dropped++;
		spin_unlock_irqrestore (&s->lock, flags);
		return 0;
	}
	
	skb_list = list_entry (s->tx_skb_list.prev, skb_list_t, skb_list);
	skb_list->skb=skb;
	skb_list->state=1;
	skb_list->s=s;

	if (list_empty(&s->free_skb_list)) {
		/* apply "backpressure". Tell the net layer to stop sending
		   the driver packets.
		*/
		netif_stop_queue(dev);
	}
	
	spin_unlock_irqrestore (&s->lock, flags);
	
	/* If there is no write urb outstanding, pull the first thing
	   off of the list and submit it to the USB stack
	*/
	dequeue_next_skb("plusb_net_xmit", s);
	
	return 0;
}

/* --------------------------------------------------------------------- */

/*
 * plusb_write_bulk_complete () - callback after the data has been
 *   sent to the USB device, or a timeout occured.
 */
static void plusb_write_bulk_complete(urb_t *purb)
{
	skb_list_t * skb_list=purb->context;
	plusb_t *s=skb_list->s;

	dbg ("%s: plusb_write_bulk_complete: status:%d skb_list:%p\n",
		 s->net_dev.name, purb->status, skb_list);

	skb_list->state=0;

	if( purb->status == -EPIPE )
		printk(KERN_CRIT "%s: plusb_write_bulk_complete: got -EPIPE and don't know what to do!\n",
		     s->net_dev.name);
		
	if(!purb->status) {
		s->net_stats.tx_packets++;
		s->net_stats.tx_bytes+=skb_list->skb->len;
	}
	else {
		err ("%s: plusb_write_bulk_complete: returned ERROR status:%d\n",
			 s->net_dev.name, purb->status);

		s->net_stats.tx_errors++;
		s->net_stats.tx_aborted_errors++;
	}
	
	dbg("plusb_bh: dev_kfree_skb");

	/* NOTE: In 2.4 it's a problem to call dev_kfree_skb() in a hard IRQ:
	   Oct 28 23:42:14 bug kernel: Warning: kfree_skb on hard IRQ c023329a
	*/
	dev_kfree_skb_any(skb_list->skb);
	
	skb_list->skb = NULL;
	if (plusb_add_buf_tail (s, &s->free_skb_list, &s->tx_skb_list)) {
		err ("plusb: tx list empty. This shouldn't happen.");
	}

	purb->status = 0;
	s->writeurb_submitted = 0;
	
    netif_wake_queue((&s->net_dev));
	
	dequeue_next_skb("plusb_write_bulk_complete", s);

	
}

/*
 * plusb_read_bulk_complete - Callback for data arriving from the USB device
 *
 * This gets called back when a full 'urb' is received from the remote system.
 * This urb was allocated by this driver and is kept in the member: s->readurb
 *
 */
static void plusb_read_bulk_complete(urb_t *purb)
{
	
	plusb_t *s=purb->context;

	dbg("plusb_read_bulk_complete: status:%d length:%d", purb->status,purb->actual_length);
	
	if(!s->connected)
		return;

	if( purb->status == -EPIPE )
		printk(KERN_CRIT "%s: plusb_read_bulk_complete: got -EPIPE and I don't know what to do!\n",
		     s->net_dev.name);
	else if (!purb->status) {
		struct sk_buff *skb;
		unsigned char *dst;
		int len=purb->transfer_buffer_length;
		struct net_device_stats *stats=&s->net_stats;

		skb=dev_alloc_skb(len);

		if(!skb) {
			printk (KERN_CRIT "%s: plusb_read_bulk_complete: dev_alloc_skb(%d)=NULL, dropping frame\n", s->net_dev.name, len);
			stats->rx_dropped++;
		} else {
			dst=(char *)skb_put(skb, len);
			memcpy( dst, purb->transfer_buffer, len);
			
			skb->dev=&s->net_dev;
			skb->protocol=eth_type_trans(skb, skb->dev);
			stats->rx_packets++;
			stats->rx_bytes+=len;
			netif_rx(skb);
		}
		
	}
	
	s->readurb_submitted = 0;
	
	if (purb->status) {
		/* Give the system a chance to "catch its breath". Shortcut
		   re-submitting the read URB>  It will be re-submitted if
		   another interrupt comes back.  The problem scenario is that
		   the plub is pulled and the read returns an error.
		   You don't want to resumbit in this case.
		*/
		err ("%s: plusb_read_bulk_complete: returned status %d\n",
			 s->net_dev.name, purb->status);
		return;
	}


	purb->status=0;

	/* Keep it coming! resubmit the URB for reading.. Make sure
	   we aren't in contention with the interrupt callback.
	*/
	submit_read_urb("plusb_read_bulk_complete", s);
}

/* --------------------------------------------------------------------- */
/*
 * plusb_int_complete - USB driver callback for interrupt msg from the device
 *
 * Interrupts are scheduled to go off on a periodic basis (see FILL_INT_URB)
 * For the prolific device, this is basically just returning a register
 * filled with bits.  See the macro definitions for _PL_INT_XXX above.
 * Most of these bits are for implementing a machine-machine protocol
 * and can be set with a special message (described as the "Quicklink"
 * feature in the prolific documentation.)
 *
 * I don't think we need any of that to work as a network device. If a
 * message is lost, big deal - that's what UNIX networking expects from
 * the physical layer.
 *
 */
static void plusb_int_complete(urb_t *purb)
{
	plusb_t *s=purb->context;
	s->status=((unsigned char*)purb->transfer_buffer)[0]&255;
	
#if 0
	/* This isn't right because 0x20 is TX_RDY and
	   sometimes will not be set
	*/
	if((s->status&0x3f)!=0x20) {
		warn("invalid device status %02X", s->status);
		return;
	}
#endif	
	if(!s->connected)
		return;

	/* Don't turn this on unless you want to see the log flooded. */
#if 0
	printk("plusb_int_complete: PEER_E:%d TX_REQ:%d TX_C:%d RESET_IN:%d RESET_O: %d TX_RDY:%d RES1:%d RES2:%d\n",
	    s->status & _PL_INT_PEER_E  ? 1 : 0,
	    s->status & _PL_INT_TX_REQ  ? 1 : 0,
	    s->status & _PL_INT_TX_C    ? 1 : 0,
	    s->status & _PL_INT_RESET_I ? 1 : 0,
	    s->status & _PL_INT_RESET_O ? 1 : 0,
	    s->status & _PL_INT_TX_RDY  ? 1 : 0,
	    s->status & _PL_INT_RES1    ? 1 : 0,
	    s->status & _PL_INT_RES2    ? 1 : 0);
#endif

#if 1
	/* At first glance, this logic appears to not really be needed, but
	   it can help recover from intermittent problems where the
	   usb_submit_urb() fails in the read callback. -EZA
	*/
	
	/* Try to submit the read URB again. Make sure
	   we aren't in contention with the bulk read callback
	*/
	submit_read_urb ("plusb_int_complete", s);
	
	/* While we are at it, why not check to see if the
	   write urb should be re-submitted?
	*/
	dequeue_next_skb("plusb_int_complete", s);	
	
#endif

}

/* --------------------------------------------------------------------- */
/*
 * plusb_free_all - deallocate all memory kept for an instance of the device.
 */
static void plusb_free_all(plusb_t *s)
{
	struct list_head *skb;
	skb_list_t *skb_list;
	
	dbg("plusb_free_all");
	
	/* set a flag to tell all callbacks to cease and desist */
	s->connected = 0;

	/* If the interrupt handler is about to fire, let it finish up */
	run_task_queue(&tq_immediate);	

	if(s->inturb) {
		dbg("unlink inturb");
		usb_unlink_urb(s->inturb);
		dbg("free_urb inturb");
		usb_free_urb(s->inturb);
		s->inturb=NULL;
	}
	
	if(s->interrupt_in_buffer) {
		dbg("kfree s->interrupt_in_buffer");
		kfree(s->interrupt_in_buffer);
		s->interrupt_in_buffer=NULL;
	}

	if(s->readurb) {
		dbg("unlink readurb");
		usb_unlink_urb(s->readurb);
		dbg("free_urb readurb:");
		usb_free_urb(s->readurb);
		s->readurb=NULL;
	}

	if(s->bulk_in_buffer) {
		dbg("kfree s->bulk_in_buffer");
		kfree(s->bulk_in_buffer);
		s->bulk_in_buffer=NULL;
	}
	
	s->readurb_submitted = 0;
	
	if(s->writeurb) {
		dbg("unlink writeurb");
		usb_unlink_urb(s->writeurb);
		dbg("free_urb writeurb:");
		usb_free_urb(s->writeurb);
		s->writeurb=NULL;
	}

	s->writeurb_submitted = 0;
	
	while(!list_empty(&s->free_skb_list)) {
		skb=s->free_skb_list.next;
		list_del(skb);
		skb_list = list_entry (skb, skb_list_t, skb_list);
		kfree(skb_list);
	}

	while(!list_empty(&s->tx_skb_list)) {
		skb=s->tx_skb_list.next;
		list_del(skb);
		skb_list = list_entry (skb, skb_list_t, skb_list);
		if (skb_list->skb) {
			dbg ("Freeing SKB in queue");
			dev_kfree_skb_any(skb_list->skb);
			skb_list->skb = NULL;
		}
		kfree(skb_list);
	}
	
	s->in_bh=0;
	
	dbg("plusb_free_all: finished");	
}

/*-------------------------------------------------------------------*/
/*
 * plusb_alloc - allocate memory associated with one instance of the device
 */
static int plusb_alloc(plusb_t *s)
{
	int i;
	skb_list_t *skb;

	dbg("plusb_alloc");

	for(i=0 ; i < _SKB_NUM ; i++) {
		skb=kmalloc(sizeof(skb_list_t), GFP_KERNEL);
		if(!skb) {
			err("kmalloc for skb_list failed");
			goto reject;
		}
		memset(skb, 0, sizeof(skb_list_t));
		list_add(&skb->skb_list, &s->free_skb_list);
	}

	dbg("inturb allocation:");
	s->inturb=usb_alloc_urb(0);
	if(!s->inturb) {
		err("alloc_urb failed");
		goto reject;
	}

	dbg("bulk read urb allocation:");
	s->readurb=usb_alloc_urb(0);
	if(!s->readurb) {
		err("alloc_urb failed");
		goto reject;
	}
	
	dbg("bulk write urb allocation:");
	s->writeurb=usb_alloc_urb(0);
	if(!s->writeurb) {
		err("alloc_urb for writeurb failed");
		goto reject;
	}
	
	dbg("readurb/inturb init:");
	s->interrupt_in_buffer=kmalloc(64, GFP_KERNEL);
	if(!s->interrupt_in_buffer) {
		err("kmalloc failed");
		goto reject;
	}

	/* The original value of '10' makes this interrupt fire off a LOT.
	   It was set so low because the callback determined when to
	   sumbit the buld read URB. I've lowered it to 100 - the driver
	   doesn't depend on that logic anymore. -EZA
	*/
	FILL_INT_URB(s->inturb, s->usbdev,
		     usb_rcvintpipe (s->usbdev, _PLUSB_INTPIPE),
		     s->interrupt_in_buffer, 1,
		     plusb_int_complete, s, HZ);

	dbg("inturb submission:");
	if(usb_submit_urb(s->inturb)<0) {
		err("usb_submit_urb failed");
		goto reject;
	}
	
	dbg("readurb init:");
	s->bulk_in_buffer = kmalloc(_BULK_DATA_LEN, GFP_KERNEL);
	if (!s->bulk_in_buffer) {
		err("kmalloc %d bytes for bulk in buffer failed", _BULK_DATA_LEN);
	}

	FILL_BULK_URB(s->readurb, s->usbdev,
		      usb_rcvbulkpipe(s->usbdev, _PLUSB_BULKINPIPE),
		      s->bulk_in_buffer, _BULK_DATA_LEN,
		      plusb_read_bulk_complete, s);

	/* The write urb will be initialized inside the network
	   interrupt.
	*/

	/* get the bulk read going */
	submit_read_urb("plusb_alloc", s);

	dbg ("plusb_alloc: finished. readurb=%p writeurb=%p inturb=%p",
		s->readurb, s->writeurb, s->inturb);
	
	return 0;

  reject:
  	dbg("plusb_alloc: failed");
	
	plusb_free_all(s);
	return -ENOMEM;
}

/*-------------------------------------------------------------------*/

static int plusb_net_open(struct net_device *dev)
{
	plusb_t *s=dev->priv;
	
	dbg("plusb_net_open");
	
	if(plusb_alloc(s))
		return -ENOMEM;

	s->opened=1;
	
	MOD_INC_USE_COUNT;

	netif_start_queue(dev);

	dbg("plusb_net_open: success");
	
	return 0;
	
}

/* --------------------------------------------------------------------- */

static int plusb_net_stop(struct net_device *dev)
{
	plusb_t *s=dev->priv;

	netif_stop_queue(dev);
	
	dbg("plusb_net_stop");	
	
	s->opened=0;
	plusb_free_all(s);

	MOD_DEC_USE_COUNT;
	dbg("plusb_net_stop:finished");
	return 0;
}

/* --------------------------------------------------------------------- */

static struct net_device_stats *plusb_net_get_stats(struct net_device *dev)
{
	plusb_t *s=dev->priv;
	
	dbg("net_device_stats");
	
	return &s->net_stats;
}

/* --------------------------------------------------------------------- */

static plusb_t *plusb_find_struct (void)
{
	int u;

	for (u = 0; u < NRPLUSB; u++) {
		plusb_t *s = &plusb[u];
		if (!s->connected)
			return s;
	}
	return NULL;
}

/* --------------------------------------------------------------------- */

static void plusb_disconnect (struct usb_device *usbdev, void *ptr)
{
	plusb_t *s = ptr;

	dbg("plusb_disconnect");
	
	plusb_free_all(s);

	if(!s->opened && s->net_dev.name) {
		dbg("unregistering netdev: %s",s->net_dev.name);
		unregister_netdev(&s->net_dev);
		s->net_dev.name[0] = '\0';
#if (LINUX_VERSION_CODE < 0x020300)
		dbg("plusb_disconnect: About to free name");
 		kfree (s->net_dev.name);
 		s->net_dev.name = NULL;
#endif	
	}
	
	dbg("plusb_disconnect: finished");
	MOD_DEC_USE_COUNT;
}

/* --------------------------------------------------------------------- */

static int plusb_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > _BULK_DATA_LEN))
		return -EINVAL;

	printk("plusb: changing mtu to %d\n", new_mtu);
	dev->mtu = new_mtu;
	
	/* NOTE: Could we change the size of the READ URB here dynamically
	   to save kernel memory?
	*/
	return 0;
}

/* --------------------------------------------------------------------- */

int plusb_net_init(struct net_device *dev)
{
	dbg("plusb_net_init");
	
	dev->open=plusb_net_open;
	dev->stop=plusb_net_stop;
	dev->hard_start_xmit=plusb_net_xmit;
	dev->get_stats	= plusb_net_get_stats;
	ether_setup(dev);
	dev->change_mtu = plusb_change_mtu;
	/* Setting the default MTU to 16K gives good performance for
	   me, and keeps the ping latency low too.  Setting it up
	   to 32K made performance go down. -EZA
	   Pavel says it would be best not to do this...
	*/
	/*dev->mtu=16384; */
	dev->tx_queue_len = 0;	
	dev->flags = IFF_POINTOPOINT|IFF_NOARP;

	
	dbg("plusb_net_init: finished");
	return 0;
}

/* --------------------------------------------------------------------- */

static void *plusb_probe (struct usb_device *usbdev, unsigned int ifnum)
{
	plusb_t *s;

	dbg("plusb: probe: vendor id 0x%x, device id 0x%x ifnum:%d",
	  usbdev->descriptor.idVendor, usbdev->descriptor.idProduct, ifnum);

	if (usbdev->descriptor.idVendor != 0x067b || usbdev->descriptor.idProduct > 0x1)
		return NULL;

	/* We don't handle multiple configurations */
	if (usbdev->descriptor.bNumConfigurations != 1)
		return NULL;

	s = plusb_find_struct ();
	if (!s)
		return NULL;

	s->usbdev = usbdev;

	if (usb_set_configuration (s->usbdev, usbdev->config[0].bConfigurationValue) < 0) {
		err("set_configuration failed");
		return NULL;
	}

	if (usb_set_interface (s->usbdev, 0, 0) < 0) {
		err("set_interface failed");
		return NULL;
	}

#if (LINUX_VERSION_CODE < 0x020300)
 	{
 		int i;
		
 		/* For Kernel version 2.2, the driver is responsible for
 		   allocating this memory. For version 2.4, the rules
 		   have apparently changed, but there is a nifty function
 		   'init_netdev' that might make this easier...  It's in 
 		   ../net/net_init.c - but can we get there from here?  (no)
		   -EZA
 		*/
 		
 		/* Find the device number... we seem to have lost it... -EZA */
 		for (i=0; i<NRPLUSB; i++) {
 			if (&plusb[i] == s)
 				break;
 		}
 	
 		if(!s->net_dev.name) {
 			s->net_dev.name = kmalloc(strlen("plusbXXXX"), GFP_KERNEL);
 			sprintf (s->net_dev.name, "plusb%d", i);
 			s->net_dev.init=plusb_net_init;
 			s->net_dev.priv=s;
 			
 			printk ("plusb_probe: Registering Device\n");	
 			if(!register_netdev(&s->net_dev))
 				info("registered: %s", s->net_dev.name);
 			else {
 				err("register_netdev failed");
 				s->net_dev.name[0] = '\0';
 			}
 			dbg ("plusb_probe: Connected!");
 		}
 	}
#else
 	/* Kernel version 2.3+ works a little bit differently than 2.2 */
	if(!s->net_dev.name[0]) {
		strcpy(s->net_dev.name, "plusb%d");
		s->net_dev.init=plusb_net_init;
		s->net_dev.priv=s;
		if(!register_netdev(&s->net_dev))
			info("registered: %s", s->net_dev.name);
		else {
			err("register_netdev failed");
			s->net_dev.name[0] = '\0';
		}
	}
#endif
	
	s->connected = 1;

	if(s->opened) {
		dbg("net device already allocated, restarting USB transfers");
		plusb_alloc(s);
	}

	info("bound to interface: %d dev: %p", ifnum, usbdev);
	MOD_INC_USE_COUNT;
	return s;
}
/* --------------------------------------------------------------------- */

static struct usb_driver plusb_driver =
{
	name: "plusb",
	probe: plusb_probe,
	disconnect: plusb_disconnect,
};

/* --------------------------------------------------------------------- */

static int __init plusb_init (void)
{
	unsigned u;
	dbg("plusb_init");
	
	/* initialize struct */
	for (u = 0; u < NRPLUSB; u++) {
		plusb_t *s = &plusb[u];
		memset (s, 0, sizeof (plusb_t));
		INIT_LIST_HEAD (&s->tx_skb_list);
		INIT_LIST_HEAD (&s->free_skb_list);
		spin_lock_init (&s->lock);
	}

	/* register misc device */
	usb_register (&plusb_driver);

	dbg("plusb_init: driver registered");

	return 0;
}

/* --------------------------------------------------------------------- */

static void __exit plusb_cleanup (void)
{
	unsigned u;

	dbg("plusb_cleanup");
	for (u = 0; u < NRPLUSB; u++) {
		plusb_t *s = &plusb[u];
#if (LINUX_VERSION_CODE < 0x020300)
		if(s->net_dev.name) {
			dbg("unregistering netdev: %s",s->net_dev.name);
			unregister_netdev(&s->net_dev);
			s->net_dev.name[0] = '\0';
			kfree (s->net_dev.name);
			s->net_dev.name = NULL;		
		}		
#else
		if(s->net_dev.name[0]) {
			dbg("unregistering netdev: %s",s->net_dev.name);
			unregister_netdev(&s->net_dev);
			s->net_dev.name[0] = '\0';
		}
#endif
	}
	usb_deregister (&plusb_driver);
	dbg("plusb_cleanup: finished");
}

/* --------------------------------------------------------------------- */

MODULE_AUTHOR ("Deti Fliegl, deti@fliegl.de");
MODULE_DESCRIPTION ("PL-2302 USB Interface Driver for Linux (c)2000");


module_init (plusb_init);
module_exit (plusb_cleanup);

/* --------------------------------------------------------------------- */
