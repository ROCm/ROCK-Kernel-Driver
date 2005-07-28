/*      $Id: lirc_streamzap.c,v 1.10 2005/03/06 14:39:36 lirc Exp $      */

/*
 * Streamzap Remote Control driver
 *
 * Copyright (c) 2005 Christoph Bartelmus <lirc@bartelmus.de>
 * 
 * This driver was based on the work of Greg Wickham and Adrian
 * Dewhurst. It was substantially rewritten to support correct signal
 * gaps and now maintains a delay buffer, which is used to present
 * consistent timing behaviour to user space applications. Without the
 * delay buffer an ugly hack would be required in lircd, which can
 * cause sluggish signal decoding in certain situations.
 *
 * This driver is based on the USB skeleton driver packaged with the
 * kernel; copyright (C) 2001-2003 Greg Kroah-Hartman (greg@kroah.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include	<linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 0)
#error "*******************************************************"
#error "Sorry, this driver needs kernel version 2.4.0 or higher"
#error "*******************************************************"
#endif

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/usb.h>

#include "lirc.h"
#include "kcompat.h"
#include "lirc_dev.h"

#define DRIVER_VERSION	"$Revision: 1.10 $"
#define DRIVER_NAME	"lirc_streamzap"
#define DRIVER_DESC     "Streamzap Remote Control driver"

/* ------------------------------------------------------------------ */

static int debug = 0;

#define USB_STREAMZAP_VENDOR_ID		0x0e9c
#define USB_STREAMZAP_PRODUCT_ID	0x0000

/* Use our own dbg macro */
#define dprintk(fmt, args...)                                   \
	do{                                                     \
		if(debug)                                       \
	                printk(KERN_DEBUG DRIVER_NAME "[%d]: "  \
                               fmt "\n", ## args);              \
	}while(0)

/*
 * table of devices that work with this driver
 */
static struct usb_device_id streamzap_table [] = {
	{ USB_DEVICE(USB_STREAMZAP_VENDOR_ID, USB_STREAMZAP_PRODUCT_ID) },
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, streamzap_table);

#define STREAMZAP_PULSE_MASK 0xf0
#define STREAMZAP_SPACE_MASK 0x0f
#define STREAMZAP_RESOLUTION 256

/* number of samples buffered */
#define STREAMZAP_BUFFER_SIZE 64

enum StreamzapDecoderState
{
	PulseSpace,
	FullPulse,
	FullSpace,
	IgnorePulse
};

/* Structure to hold all of our device specific stuff */
/* some remarks regarding locking:
   theoretically this struct can be accessed from three threads:
   
   - from lirc_dev through set_use_inc/set_use_dec
   
   - from the USB layer throuh probe/disconnect/irq
   
     Careful placement of lirc_register_plugin/lirc_unregister_plugin
     calls will prevent conflicts. lirc_dev makes sure that
     set_use_inc/set_use_dec are not being executed and will not be
     called after lirc_unregister_plugin returns.

   - by the timer callback
   
     The timer is only running when the device is connected and the
     LIRC device is open. Making sure the timer is deleted by
     set_use_dec will make conflicts impossible.
*/
struct usb_streamzap {

	/* usb */
	/* save off the usb device pointer */
	struct usb_device *	udev;
	/* the interface for this device */
	struct usb_interface *	interface;

	/* buffer & dma */
	unsigned char *		buf_in;
	dma_addr_t		dma_in;
	unsigned int		buf_in_len;

	struct usb_endpoint_descriptor *endpoint;

	/* IRQ */
	struct urb		*urb_in;

	/* lirc */
	struct lirc_plugin	plugin;	
	struct lirc_buffer      delay_buf;
	struct lirc_buffer      lirc_buf;
	
	/* timer used to support delay buffering */
	struct timer_list	delay_timer;
	int                     timer_running;
	spinlock_t              timer_lock;
	
	/* tracks whether we are currently receiving some signal */
	int                     idle;
	/* sum of signal lengths received since signal start */
	unsigned long           sum;
	/* start time of signal; necessary for gap tracking */
	struct timeval          signal_last;
	struct timeval          signal_start;
	enum StreamzapDecoderState decoder_state;
	unsigned long           flush_jiffies;
};


/* local function prototypes */
#ifdef KERNEL_2_5
static int streamzap_probe(struct usb_interface *interface,
			   const struct usb_device_id *id);
static void streamzap_disconnect(struct usb_interface *interface);
static void usb_streamzap_irq(struct urb *urb, struct pt_regs *regs);
#else
static void *streamzap_probe(struct usb_device *udev, unsigned int ifnum,
			     const struct usb_device_id *id);
static void streamzap_disconnect(struct usb_device *dev, void *ptr);
static void usb_streamzap_irq(struct urb *urb);
#endif
static int streamzap_use_inc( void *data );
static void streamzap_use_dec( void *data );
static int streamzap_ioctl(struct inode *node, struct file *filep,
			   unsigned int cmd, unsigned long arg);

/* usb specific object needed to register this driver with the usb subsystem */

static struct usb_driver streamzap_driver = {
	.owner =	THIS_MODULE,
	.name =		DRIVER_NAME,
	.probe =	streamzap_probe,
	.disconnect =	streamzap_disconnect,
	.id_table =	streamzap_table,
};

static void stop_timer(struct usb_streamzap *sz)
{
	unsigned long flags;
	
	spin_lock_irqsave(&sz->timer_lock, flags);
	if(sz->timer_running)
	{
		sz->timer_running = 0;
		del_timer_sync(&sz->delay_timer);
	}
	spin_unlock_irqrestore(&sz->timer_lock, flags);
}

static void delay_timeout(unsigned long arg)
{
	struct usb_streamzap *sz = (struct usb_streamzap *) arg;
	lirc_t sum=10000;
	lirc_t data;
	
	spin_lock(&sz->timer_lock);
	while(!lirc_buffer_empty(&sz->delay_buf))
	{
		lirc_buffer_read_1( &sz->delay_buf, (unsigned char *) &data);
		lirc_buffer_write_1(&sz->lirc_buf, (unsigned char *) &data);
		sum += (data&PULSE_MASK)+STREAMZAP_RESOLUTION/2;
		if(sum > 1000000/HZ)
		{
			break;
		}
	}
	if(!lirc_buffer_empty(&sz->delay_buf))
	{
		while(lirc_buffer_available(&sz->delay_buf) < 
		      STREAMZAP_BUFFER_SIZE/2)
		{
			lirc_buffer_read_1( &sz->delay_buf,
					    (unsigned char *) &data);
			lirc_buffer_write_1(&sz->lirc_buf,
					    (unsigned char *) &data);
		}
		if(sz->timer_running)
		{
			sz->delay_timer.expires++;
			add_timer(&sz->delay_timer);
		}
	}
	else
	{
		sz->timer_running = 0;
	}
	if(!lirc_buffer_empty(&sz->lirc_buf))
	{
		wake_up(&sz->lirc_buf.wait_poll);
	}
	spin_unlock(&sz->timer_lock);
}

static inline void flush_delay_buffer(struct usb_streamzap *sz)
{
	lirc_t data;
	int empty = 1;
	
	while(!lirc_buffer_empty(&sz->delay_buf))
	{
		empty = 0;
		lirc_buffer_read_1( &sz->delay_buf, (unsigned char *) &data);
		lirc_buffer_write_1(&sz->lirc_buf, (unsigned char *) &data);
	}
	if(!empty) wake_up( &sz->lirc_buf.wait_poll );
}

static inline void push(struct usb_streamzap *sz, unsigned char *data)
{
	unsigned long flags;
	
	spin_lock_irqsave(&sz->timer_lock, flags);
	if(lirc_buffer_full(&sz->delay_buf))
	{
		lirc_t data;
		
		lirc_buffer_read_1( &sz->delay_buf, (unsigned char *) &data);
		lirc_buffer_write_1(&sz->lirc_buf, (unsigned char *) &data);
		
		dprintk("buffer overflow", sz->plugin.minor);
	}
	
	lirc_buffer_write_1(&sz->delay_buf, data);
	
	if(!sz->timer_running)
	{
		sz->delay_timer.expires = jiffies + HZ/10;
		add_timer(&sz->delay_timer);
		sz->timer_running = 1;
	}

	spin_unlock_irqrestore(&sz->timer_lock, flags);
}

static inline void push_full_pulse(struct usb_streamzap *sz,
				   unsigned char value)
{
	lirc_t pulse;
	
	if(sz->idle)
	{
		long deltv;
		lirc_t tmp;
			
		sz->signal_last = sz->signal_start;
		do_gettimeofday(&sz->signal_start);
		
		deltv=sz->signal_start.tv_sec-sz->signal_last.tv_sec;
		if(deltv>15) 
		{
			tmp=PULSE_MASK; /* really long time */
		}
		else
		{
			tmp=(lirc_t) (deltv*1000000+
				      sz->signal_start.tv_usec-
				      sz->signal_last.tv_usec);
			tmp-=sz->sum;
		}
		dprintk("ls %u", sz->plugin.minor, tmp);
		push(sz, (char *)&tmp);
		
		sz->idle = 0;
		sz->sum = 0;
	}
	
	pulse = ((lirc_t) value)*STREAMZAP_RESOLUTION;
	pulse += STREAMZAP_RESOLUTION/2;
	sz->sum += pulse;
	pulse |= PULSE_BIT;
	
	dprintk("p %u", sz->plugin.minor, pulse&PULSE_MASK);
	push(sz, (char *)&pulse);
}

static inline void push_half_pulse(struct usb_streamzap *sz,
				   unsigned char value)
{
	push_full_pulse(sz, (value & STREAMZAP_PULSE_MASK)>>4);
}

static inline void push_full_space(struct usb_streamzap *sz,
				   unsigned char value)
{
	lirc_t space;
	
	space = ((lirc_t) value)*STREAMZAP_RESOLUTION;
	space += STREAMZAP_RESOLUTION/2;
	sz->sum += space;
	dprintk("s %u", sz->plugin.minor, space);
	push(sz, (char *)&space);
}

static inline void push_half_space(struct usb_streamzap *sz,
				   unsigned char value)
{
	push_full_space(sz, value & STREAMZAP_SPACE_MASK);
}

/*
 * usb_streamzap_irq - IRQ handler
 *
 * This procedure is invoked on reception of data from
 * the usb remote.
 */
#ifdef KERNEL_2_5
static void usb_streamzap_irq(struct urb *urb, struct pt_regs *regs) 
#else
static void usb_streamzap_irq(struct urb *urb) 
#endif
{
	struct usb_streamzap *sz;
	int		len;
	unsigned int	i = 0;

	if ( ! urb )
		return;

	sz = urb->context;
	len = urb->actual_length;

	switch (urb->status)
	{
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		/* sz might already be invalid at this point */
		dprintk("urb status: %d", -1, urb->status);
		return;
	default:
		break;
	}

	dprintk("received %d", sz->plugin.minor, urb->actual_length);
	if(sz->flush_jiffies < jiffies) for (i=0; i < urb->actual_length; i++)
	{
		dprintk("%d: %x", sz->plugin.minor,
			i, (unsigned char) sz->buf_in[i]);
		switch(sz->decoder_state)
		{
		case PulseSpace:
			if( (sz->buf_in[i]&STREAMZAP_PULSE_MASK) ==
			    STREAMZAP_PULSE_MASK)
			{
				sz->decoder_state = FullPulse;
				continue;
			}
			else if( (sz->buf_in[i]&STREAMZAP_SPACE_MASK) ==
				 STREAMZAP_SPACE_MASK)
			{
				push_half_pulse(sz, sz->buf_in[i]);
				sz->decoder_state = FullSpace;
				continue;
			}
			else
			{
				push_half_pulse(sz, sz->buf_in[i]);
				push_half_space(sz, sz->buf_in[i]);
			}
			break;
		
		case FullPulse:
			push_full_pulse(sz, sz->buf_in[i]);
			sz->decoder_state = IgnorePulse;
			break;
		
		case FullSpace:
			if(sz->buf_in[i] == 0xff)
			{
				sz->idle=1;
				stop_timer(sz);
				flush_delay_buffer(sz);
			}
			else
			{
				push_full_space(sz, sz->buf_in[i]);
			}
			sz->decoder_state = PulseSpace;
			break;
		
		case IgnorePulse:
			if( (sz->buf_in[i]&STREAMZAP_SPACE_MASK) == 
			    STREAMZAP_SPACE_MASK)
			{
				sz->decoder_state = FullSpace;
				continue;
			}
			push_half_space(sz, sz->buf_in[i]);
			break;
		}
	}

#ifdef KERNEL_2_5
	/* resubmit only for 2.6 */
	usb_submit_urb( urb, SLAB_ATOMIC );
#endif

	return;
}

/**
 *	streamzap_probe
 *
 *	Called by usb-core to associated with a candidate device
 *	On any failure the return value is the ERROR
 *	On success return 0
 */
#ifdef KERNEL_2_5
static int streamzap_probe( struct usb_interface *interface, const struct usb_device_id *id )
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *iface_host;
#else
static void *streamzap_probe(struct usb_device *udev, unsigned int ifnum,
			     const struct usb_device_id *id)
{
	struct usb_interface *interface = &udev->actconfig->interface[ifnum];
	struct usb_interface_descriptor *iface_host;
#endif
	int retval = -ENOMEM;
	struct usb_streamzap *sz = NULL;
	char buf[63], name[128] = "";

	/***************************************************
	 * Allocate space for device driver specific data
	 */
	if (( sz = kmalloc (sizeof(struct usb_streamzap), GFP_KERNEL)) == NULL )
		goto error;

	memset(sz, 0, sizeof(*sz));
        sz->udev = udev;
        sz->interface = interface;
	
	/***************************************************
	 * Check to ensure endpoint information matches requirements
	 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,4)
	iface_host = interface->cur_altsetting;
#else
	iface_host = &interface->altsetting[interface->act_altsetting];
#endif

#ifdef KERNEL_2_5
        if (iface_host->desc.bNumEndpoints != 1) {
#else
	if(iface_host->bNumEndpoints != 1) {
#endif
#ifdef KERNEL_2_5
                err("%s: Unexpected desc.bNumEndpoints (%d)", __FUNCTION__,
		    iface_host->desc.bNumEndpoints);
#else
                err("%s: Unexpected desc.bNumEndpoints (%d)", __FUNCTION__,
		    iface_host->bNumEndpoints);
#endif
		retval = -ENODEV;
                goto error;
        }

#ifdef KERNEL_2_5
	sz->endpoint = &(iface_host->endpoint[0].desc);
#else
	sz->endpoint = &(iface_host->endpoint[0]);
#endif
        if (( sz->endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
	    != USB_DIR_IN) {
                err("%s: endpoint doesn't match input device 02%02x",
		    __FUNCTION__, sz->endpoint->bEndpointAddress );
                retval = -ENODEV;
                goto error;
        }

        if (( sz->endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	    != USB_ENDPOINT_XFER_INT) {
                err("%s: endpoint attributes don't match xfer 02%02x",
		    __FUNCTION__, sz->endpoint->bmAttributes );
                retval = -ENODEV;
                goto error;
        }

        if ( sz->endpoint->wMaxPacketSize == 0 ) {
                err("%s: endpoint message size==0? ", __FUNCTION__);
                retval = -ENODEV;
                goto error;
        }

	/***************************************************
	 * Allocate the USB buffer and IRQ URB 
	 */

	sz->buf_in_len = sz->endpoint->wMaxPacketSize;
#ifdef KERNEL_2_5
        if((sz->buf_in = usb_buffer_alloc(sz->udev, sz->buf_in_len,
					  SLAB_ATOMIC, &sz->dma_in)) == NULL )
	{
                goto error;
	}
	if (!( sz->urb_in = usb_alloc_urb(0, GFP_KERNEL)))
		goto error;
#else
	if( (sz->buf_in = kmalloc(sz->buf_in_len, GFP_KERNEL))
	    == NULL)
	{
		goto error;
	}
	if( (sz->urb_in = usb_alloc_urb(0)) == NULL)
	{
		goto error;
	}
#endif
	/***************************************************
	 * Connect this device to the LIRC sub-system
	 */

	if(lirc_buffer_init(&sz->lirc_buf, sizeof(lirc_t),
			    STREAMZAP_BUFFER_SIZE))
	{
		goto error;
	}
	if(lirc_buffer_init(&sz->delay_buf, sizeof(lirc_t),
			    STREAMZAP_BUFFER_SIZE))
	{
		lirc_buffer_free(&sz->lirc_buf);
		goto error;
	}

	/***************************************************
	 * As required memory is allocated now populate the plugin structure
	 */

	memset(&sz->plugin, 0, sizeof(sz->plugin));

	strcpy(sz->plugin.name, DRIVER_NAME);
	sz->plugin.minor = -1;
	sz->plugin.sample_rate = 0;
	sz->plugin.code_length = sizeof(lirc_t) * 8;
	sz->plugin.features = LIRC_CAN_REC_MODE2;
	sz->plugin.data = sz;
	sz->plugin.rbuf = &sz->lirc_buf;
	sz->plugin.set_use_inc = &streamzap_use_inc;
	sz->plugin.set_use_dec = &streamzap_use_dec;
	sz->plugin.ioctl = streamzap_ioctl;
	sz->plugin.owner = THIS_MODULE;

	sz->idle = 1;
	sz->decoder_state = PulseSpace;
	init_timer(&sz->delay_timer);
	sz->delay_timer.function = delay_timeout;
	sz->delay_timer.data = (unsigned long) sz;
	sz->timer_running = 0;
	spin_lock_init(&sz->timer_lock);

	/***************************************************
	 * Complete final initialisations
	 */

	usb_fill_int_urb(sz->urb_in, udev,
		usb_rcvintpipe( udev, sz->endpoint->bEndpointAddress ),
		sz->buf_in, sz->buf_in_len, usb_streamzap_irq, sz,
		sz->endpoint->bInterval);

        if ( udev->descriptor.iManufacturer
                && usb_string( udev,  udev->descriptor.iManufacturer, buf, 63) > 0)
                strncpy(name, buf, 128);

        if ( udev->descriptor.iProduct
                && usb_string( udev,  udev->descriptor.iProduct, buf, 63) > 0)
                snprintf(name, 128, "%s %s", name, buf);

        printk(KERN_INFO DRIVER_NAME "[%d]: %s on usb%d:%d attached\n",
	       sz->plugin.minor, name,
	       udev->bus->busnum, sz->udev->devnum);

#ifdef KERNEL_2_5
	usb_set_intfdata( interface , sz );
#endif

	if(lirc_register_plugin(&sz->plugin) < 0)
	{
		lirc_buffer_free(&sz->delay_buf);
		lirc_buffer_free(&sz->lirc_buf);
		goto error;
	}

#ifdef KERNEL_2_5
	return 0;
#else
	return sz;
#endif

error:

	/***************************************************
	 * Premise is that a 'goto error' can be invoked from inside the
	 * probe function and all necessary cleanup actions will be taken
	 * including freeing any necessary memory blocks
	 */

	if ( retval == -ENOMEM )
		err ("Out of memory");

	if ( sz ) {

		if ( sz->urb_in )
			usb_free_urb( sz->urb_in );

		if ( sz->buf_in )
		{
#ifdef KERNEL_2_5
			usb_buffer_free(udev, sz->buf_in_len,
					sz->buf_in, sz->dma_in);
#else
			kfree(sz->buf_in);
#endif
		}
		kfree( sz );
	}

#ifdef KERNEL_2_5
	return retval;
#else
	return NULL;
#endif
}

static int streamzap_use_inc(void *data)
{
	struct usb_streamzap *sz = data;

	if(!sz)
	{
		dprintk("%s called with no context", -1, __FUNCTION__);
		return -EINVAL;
	}
	dprintk("set use inc", sz->plugin.minor);

	MOD_INC_USE_COUNT;
	
	while(!lirc_buffer_empty(&sz->lirc_buf))
		lirc_buffer_remove_1(&sz->lirc_buf);
	while(!lirc_buffer_empty(&sz->delay_buf))
		lirc_buffer_remove_1(&sz->delay_buf);
		
	sz->flush_jiffies = jiffies + HZ;
	sz->urb_in->dev = sz->udev;
#ifdef KERNEL_2_5
	if (usb_submit_urb(sz->urb_in, SLAB_ATOMIC))
#else
	if (usb_submit_urb(sz->urb_in))
#endif
	{
		dprintk("open result = -EIO error submitting urb",
			sz->plugin.minor);
		MOD_DEC_USE_COUNT;
		return -EIO;
	}
	
	return 0;
}

static void streamzap_use_dec(void *data)
{
        struct usb_streamzap *sz = data;

        if (!sz) {
                dprintk("%s called with no context", -1, __FUNCTION__);
                return;
        }
        dprintk("set use dec", sz->plugin.minor);
	
	stop_timer(sz);
	
	usb_unlink_urb(sz->urb_in);
	
        MOD_DEC_USE_COUNT;
}

static int streamzap_ioctl(struct inode *node, struct file *filep,
			   unsigned int cmd, unsigned long arg)
{
        int result;
	
	switch(cmd)
	{
	case LIRC_GET_REC_RESOLUTION:
		result=put_user(STREAMZAP_RESOLUTION, (unsigned long *) arg);
		if(result) return(result); 
		break;
	default:
		return(-ENOIOCTLCMD);
	}
	return(0);
}

/**
 *	streamzap_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 *
 *	This routine guarantees that the driver will not submit any more urbs
 *	by clearing dev->udev.  It is also supposed to terminate any currently
 *	active urbs.  Unfortunately, usb_bulk_msg(), used in streamzap_read(), does
 *	not provide any way to do this.
 */
#ifdef KERNEL_2_5
static void streamzap_disconnect( struct usb_interface *interface )
#else
static void streamzap_disconnect(struct usb_device *dev, void *ptr)
#endif
{
	struct usb_streamzap *sz;
	int errnum;
	int minor;

#ifdef KERNEL_2_5
	sz = usb_get_intfdata( interface );
#else
	sz = ptr;
#endif

	/*
	 * unregister from the LIRC sub-system
	 */

        if (( errnum = lirc_unregister_plugin( sz->plugin.minor )) != 0) {

                dprintk("error in lirc_unregister: (returned %d)",
			sz->plugin.minor, errnum );
        }

	lirc_buffer_free(&sz->delay_buf);
	lirc_buffer_free(&sz->lirc_buf);

	/*
	 * unregister from the USB sub-system
	 */

	usb_free_urb( sz->urb_in );

#ifdef KERNEL_2_5
        usb_buffer_free( sz->udev , sz->buf_in_len, sz->buf_in, sz->dma_in );
#else
	kfree(sz->buf_in);
#endif

	minor = sz->plugin.minor;
	kfree( sz );

        printk(KERN_INFO DRIVER_NAME "[%d]: disconnected\n", minor);
}

#ifdef MODULE

/**
 *	usb_streamzap_init
 */
static int __init usb_streamzap_init(void)
{
	int result;

	/* register this driver with the USB subsystem */

	result = usb_register( &streamzap_driver );

	if (result) {
		err("usb_register failed. Error number %d",
		    result);
		return result;
	}

	printk(KERN_INFO DRIVER_NAME " " DRIVER_VERSION " registered\n");
	return 0;
}

/**
 *	usb_streamzap_exit
 */
static void __exit usb_streamzap_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&streamzap_driver);
}


module_init (usb_streamzap_init);
module_exit (usb_streamzap_exit);

MODULE_AUTHOR("Christoph Bartelmus, Greg Wickham, Adrian Dewhurst");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable debugging messages");

EXPORT_NO_SYMBOLS;

#endif /* MODULE */
