/* lirc_igorplugusb - USB remote support for LIRC
 *
 * Supports the standard homebrew IgorPlugUSB receiver with Igor's firmware.
 * See http://www.cesko.host.sk/IgorPlugUSB/IgorPlug-USB%20(AVR)_eng.htm 
 * 
 * The device can only record bursts of up to 36 pulses/spaces.
 * Works fine with RC5. Longer commands lead to device buffer overrun.
 * (Maybe a better firmware or a microcontroller with more ram can help?)
 *
 * Version 0.1  [beta status]
 *
 * Copyright (C) 2004 Jan M. Hochstein <hochstein@algo.informatik.tu-darmstadt.de>
 *
 * This driver was derived from:
 *   Paul Miller <pmiller9@users.sourceforge.net>
 *      "lirc_atiusb" module
 *   Vladimir Dergachev <volodya@minspring.com>'s 2002
 *      "USB ATI Remote support" (input device)
 *   Adrian Dewhurst <sailor-lk@sailorfrag.net>'s 2002
 *      "USB StreamZap remote driver" (LIRC)
 *   Artur Lipowski <alipowski@kki.net.pl>'s 2002
 *      "lirc_dev" and "lirc_gpio" LIRC modules
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 0)
#error "*******************************************************"
#error "Sorry, this driver needs kernel version 2.4.0 or higher"
#error "*******************************************************"
#endif

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/usb.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/time.h>
#include "kcompat.h"
#include "lirc.h"
#include "lirc_dev.h"

#if !defined(KERNEL_2_5)
#        define USB_CTRL_GET_TIMEOUT    5
#endif

/* lock irctl structure */
#define IRLOCK			down_interruptible(&ir->lock)
#define IRUNLOCK		up(&ir->lock)

/* module identification */
#define DRIVER_VERSION		"0.1"
#define DRIVER_AUTHOR		\
        "Jan M. Hochstein <hochstein@algo.informatik.tu-darmstadt.de>"
#define DRIVER_DESC		"USB remote driver for LIRC"
#define DRIVER_NAME		"lirc_igorplugusb"

/* debugging support */
#ifdef CONFIG_USB_DEBUG
        static int debug = 1;
#else
        static int debug = 0;
#endif

#define dprintk(fmt, args...)                                 \
	do{                                                   \
		if(debug) printk(KERN_DEBUG fmt, ## args);    \
	}while(0)

/* general constants */
#define SUCCESS                 0

/* One mode2 pulse/space has 4 bytes. */
#define CODE_LENGTH             sizeof(lirc_t)

/* Igor's firmware cannot record bursts longer than 36. */
#define DEVICE_BUFLEN           36

/** Header at the beginning of the device's buffer:
        unsigned char data_length
        unsigned char data_start    (!=0 means ring-buffer overrun)
        unsigned char counter       (incremented by each burst)
**/
#define DEVICE_HEADERLEN        3

/* This is for the gap */
#define ADDITIONAL_LIRC_BYTES   2

/* times to poll per second */
#define SAMPLE_RATE             10


/**** Igor's USB Request Codes */

#define SET_INFRABUFFER_EMPTY   1
/** 
 * Params: none
 * Answer: empty
 *
**/

#define GET_INFRACODE           2
/** 
 * Params: 
 *   wValue: offset to begin reading infra buffer
 *
 * Answer: infra data
 *
**/

#define SET_DATAPORT_DIRECTION  3
/** 
 * Params: 
 *   wValue: (byte) 1 bit for each data port pin (0=in, 1=out)
 *
 * Answer: empty
 *
**/

#define GET_DATAPORT_DIRECTION  4
/** 
 * Params: none
 *
 * Answer: (byte) 1 bit for each data port pin (0=in, 1=out)
 *
**/

#define SET_OUT_DATAPORT        5
/** 
 * Params: 
 *   wValue: byte to write to output data port
 *
 * Answer: empty
 *
**/

#define GET_OUT_DATAPORT        6
/** 
 * Params: none
 *
 * Answer: least significant 3 bits read from output data port
 *
**/

#define GET_IN_DATAPORT         7
/** 
 * Params: none
 *
 * Answer: least significant 3 bits read from input data port
 *
**/

#define READ_EEPROM             8
/** 
 * Params: 
 *   wValue: offset to begin reading EEPROM
 *
 * Answer: EEPROM bytes
 *
**/

#define WRITE_EEPROM            9
/** 
 * Params: 
 *   wValue: offset to EEPROM byte
 *   wIndex: byte to write
 *
 * Answer: empty
 *
**/

#define SEND_RS232              10
/** 
 * Params: 
 *   wValue: byte to send
 *
 * Answer: empty
 *
**/

#define RECV_RS232              11
/** 
 * Params: none
 *
 * Answer: byte received
 *
**/

#define SET_RS232_BAUD          12
/** 
 * Params: 
 *   wValue: byte to write to UART bit rate register (UBRR)
 *
 * Answer: empty
 *
**/

#define GET_RS232_BAUD          13
/** 
 * Params: none
 *
 * Answer: byte read from UART bit rate register (UBRR)
 *
**/


/* data structure for each usb remote */
struct irctl {

	/* usb */
	struct usb_device *usbdev;
	struct urb *urb_in;
	int devnum;

	unsigned char *buf_in;
	unsigned int len_in;
        int in_space;
	struct timeval last_time;

#if defined(KERNEL_2_5)
	dma_addr_t dma_in;
#endif

	/* lirc */
	struct lirc_plugin *p;
	int connected;

	/* handle sending (init strings) */
	int send_flags;
	wait_queue_head_t wait_out;

	struct semaphore lock;
};

static int unregister_from_lirc(struct irctl *ir)
{
	struct lirc_plugin *p = ir->p;
	int devnum;
	int rtn;

	if(!ir->p)
        	return -EINVAL;
        
	devnum = ir->devnum;
	dprintk(DRIVER_NAME "[%d]: unregister from lirc called\n", devnum);

	if ((rtn = lirc_unregister_plugin(p->minor)) > 0) {
		printk(DRIVER_NAME "[%d]: error in lirc_unregister minor: %d\n"
			"Trying again...\n", devnum, p->minor);
		if (rtn == -EBUSY) {
			printk(DRIVER_NAME
				"[%d]: device is opened, will unregister"
				" on close\n", devnum);
			return -EAGAIN;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);

		if ((rtn = lirc_unregister_plugin(p->minor)) > 0) {
			printk(DRIVER_NAME "[%d]: lirc_unregister failed\n",
			devnum);
		}
	}

	if (rtn != SUCCESS) {
		dprintk(DRIVER_NAME "[%d]: didn't free resources\n", devnum);
		return -EAGAIN;
	}

	printk(DRIVER_NAME "[%d]: usb remote disconnected\n", devnum);

	lirc_buffer_free(p->rbuf);
	kfree(p->rbuf);
	kfree(p);
	kfree(ir);
        ir->p = NULL;
	return SUCCESS;
}

static int set_use_inc(void *data)
{
	struct irctl *ir = data;

	if (!ir) {
		printk(DRIVER_NAME "[?]: set_use_inc called with no context\n");
		return -EIO;
	}
	dprintk(DRIVER_NAME "[%d]: set use inc\n", ir->devnum);

	MOD_INC_USE_COUNT;

	if (!ir->connected) {
		if (!ir->usbdev)
			return -ENODEV;

		ir->connected = 1;
	}

	return SUCCESS;
}

static void set_use_dec(void *data)
{
	struct irctl *ir = data;

	if (!ir) {
		printk(DRIVER_NAME "[?]: set_use_dec called with no context\n");
		return;
	}
	dprintk(DRIVER_NAME "[%d]: set use dec\n", ir->devnum);

	if (ir->connected) {
		IRLOCK;
		ir->connected = 0;
                unregister_from_lirc(ir);
		IRUNLOCK;
	}
	MOD_DEC_USE_COUNT;
}


/** 
 * Called in user context.
 * return 0 if data was added to the buffer and
 * -ENODATA if none was available. This should add some number of bits
 * evenly divisible by code_length to the buffer
**/
static int usb_remote_poll(void* data, struct lirc_buffer* buf)
{
	int ret;
	struct irctl *ir = (struct irctl *)data;

	if(!ir->usbdev)  /* Has the device been removed? */
		return -ENODEV;

	memset(ir->buf_in, 0, ir->len_in);
  
	if((ret = usb_control_msg(
        	ir->usbdev, usb_rcvctrlpipe(ir->usbdev, 0), 
		GET_INFRACODE, USB_TYPE_VENDOR|USB_DIR_IN,
		0/* offset */, /*unused*/0, 
		ir->buf_in, ir->len_in, 
		/*timeout*/HZ * USB_CTRL_GET_TIMEOUT)) > 0)
	{
		int i = DEVICE_HEADERLEN;
		lirc_t code,timediff;
                struct timeval now;

		if(ret <= 1)  /* ACK packet has 1 byte --> ignore */
			return -ENODATA;

		dprintk(DRIVER_NAME ": Got %d bytes. Header: %02x %02x %02x\n", 
                	ret, ir->buf_in[0], ir->buf_in[1], ir->buf_in[2]);
      
		if(ir->buf_in[2] != 0) {
			printk(DRIVER_NAME "[%d]: Device buffer overrun.\n", 
                        	ir->devnum);
			i = DEVICE_HEADERLEN + ir->buf_in[2];  /* start at earliest byte */
			/* where are we now? space, gap or pulse? */
		}
      
		do_gettimeofday(&now);
		timediff = now.tv_sec - ir->last_time.tv_sec;
		if(timediff+1 > PULSE_MASK/1000000)
			timediff = PULSE_MASK;
		else {
			timediff *= 1000000;
			timediff += now.tv_usec - ir->last_time.tv_usec;
		}
		ir->last_time.tv_sec = now.tv_sec;
		ir->last_time.tv_usec = now.tv_usec;

		/* create leading gap  */
		code = timediff;
  		lirc_buffer_write_n(buf, (unsigned char*)&code, 1);
		ir->in_space = 1;   /* next comes a pulse */

		/* MODE2: pulse/space (PULSE_BIT) in 1us units */

		while(i < ret) {
			/* 1 Igor-tick = 85.333333 us */
			code = (unsigned int)ir->buf_in[i] * 85 
				+ (unsigned int)ir->buf_in[i]/3;
			if(ir->in_space)
				code |= PULSE_BIT;
			lirc_buffer_write_n(buf, (unsigned char*)&code, 1);  
			/* 1 chunk = CODE_LENGTH bytes */
			ir->in_space ^= 1;
			++ i;
		}

		if((ret = usb_control_msg(
                	ir->usbdev, usb_rcvctrlpipe(ir->usbdev, 0), 
			SET_INFRABUFFER_EMPTY, USB_TYPE_VENDOR|USB_DIR_IN,
			/*unused*/0, /*unused*/0, 
			/*dummy*/ir->buf_in, /*dummy*/ir->len_in, 
                        /*timeout*/HZ * USB_CTRL_GET_TIMEOUT)) < 0)
		{
			printk(DRIVER_NAME "[%d]: SET_INFRABUFFER_EMPTY: error %d\n", 
				ir->devnum, ret);
		}
		return SUCCESS;
	}
	else {
		printk(DRIVER_NAME "[%d]: GET_INFRACODE: error %d\n", 
                	ir->devnum, ret);
	}

	return -ENODATA;
}



#if defined(KERNEL_2_5)
static int usb_remote_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *dev = NULL;
	struct usb_host_interface *idesc = NULL;
	struct usb_host_endpoint *ep_ctl2;
#else
static void *usb_remote_probe(struct usb_device *dev, unsigned int ifnum,
				const struct usb_device_id *id)
{
	struct usb_interface *intf;
	struct usb_interface_descriptor *idesc;
	struct usb_endpoint_descriptor *ep_ctl2;
#endif
	struct irctl *ir = NULL;
	struct lirc_plugin *plugin = NULL;
	struct lirc_buffer *rbuf = NULL;
	int devnum, pipe, maxp, bytes_in_key;
	int minor = 0;
	char buf[63], name[128]="";
	int mem_failure = 0;
	int ret;

	dprintk(DRIVER_NAME ": usb probe called.\n");

#if defined(KERNEL_2_5)
	dev = interface_to_usbdev(intf);

#  if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 5)
	idesc = &intf->altsetting[intf->act_altsetting];  /* in 2.6.4 */
#  else
	idesc = intf->cur_altsetting;  /* in 2.6.6 */
#  endif

	if (idesc->desc.bNumEndpoints != 1)
		return -ENODEV;
	ep_ctl2 = idesc->endpoint;
	if (((ep_ctl2->desc.bEndpointAddress & USB_ENDPOINT_DIR_MASK) != USB_DIR_IN)
		|| (ep_ctl2->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		!= USB_ENDPOINT_XFER_CONTROL)
		return -ENODEV;
	pipe = usb_rcvctrlpipe(dev, ep_ctl2->desc.bEndpointAddress);
#else
	intf = &dev->actconfig->interface[ifnum];
	idesc = &intf->altsetting[intf->act_altsetting];
	if (idesc->bNumEndpoints != 1)
		return NULL;
	ep_ctl2 = idesc->endpoint;
	if (((ep_ctl2->bEndpointAddress & USB_ENDPOINT_DIR_MASK) != USB_DIR_IN)
		|| (ep_ctl2->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		!= USB_ENDPOINT_XFER_CONTROL)
		return NULL;
	pipe = usb_rcvctrlpipe(dev, ep_ctl2->bEndpointAddress);
#endif
	devnum = dev->devnum;
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	bytes_in_key = CODE_LENGTH;

	dprintk(DRIVER_NAME "[%d]: bytes_in_key=%d maxp=%d\n",
		devnum, bytes_in_key, maxp);


	/* allocate kernel memory */
	mem_failure = 0;
	if (!(ir = kmalloc(sizeof(struct irctl), GFP_KERNEL))) {
		mem_failure = 1;
	} else {
		memset(ir, 0, sizeof(struct irctl));

		if (!(plugin = kmalloc(sizeof(struct lirc_plugin), GFP_KERNEL))) {
			mem_failure = 2;
		} else if (!(rbuf = kmalloc(sizeof(struct lirc_buffer), GFP_KERNEL))) {
			mem_failure = 3;
		} else if (lirc_buffer_init(rbuf, bytes_in_key, 
                		DEVICE_BUFLEN+ADDITIONAL_LIRC_BYTES)) {
			mem_failure = 4;
#if defined(KERNEL_2_5)
		} else if (!(ir->buf_in = usb_buffer_alloc(dev, 
				DEVICE_BUFLEN+DEVICE_HEADERLEN, 
                                SLAB_ATOMIC, &ir->dma_in))) {
			mem_failure = 5;
#else
		} else if (!(ir->buf_in = kmalloc(
				DEVICE_BUFLEN+DEVICE_HEADERLEN, GFP_KERNEL))) {
			mem_failure = 5;
#endif
		} else {

			memset(plugin, 0, sizeof(struct lirc_plugin));

			strcpy(plugin->name, DRIVER_NAME " ");
			plugin->minor = -1;
			plugin->code_length = bytes_in_key*8; /* in bits */
			plugin->features = LIRC_CAN_REC_MODE2;
			plugin->data = ir;
			plugin->rbuf = rbuf;
			plugin->set_use_inc = &set_use_inc;
			plugin->set_use_dec = &set_use_dec;
			plugin->sample_rate = SAMPLE_RATE;    /* per second */
			plugin->add_to_buf = &usb_remote_poll;

			init_MUTEX(&ir->lock);
			init_waitqueue_head(&ir->wait_out);

			if ((minor = lirc_register_plugin(plugin)) < 0) {
				mem_failure = 9;
			}
		}
	}

	/* free allocated memory in case of failure */
	switch (mem_failure) {
	case 9:
#if defined(KERNEL_2_5)
		usb_buffer_free(dev, DEVICE_BUFLEN+DEVICE_HEADERLEN, 
                	ir->buf_in, ir->dma_in);
#else
		kfree(ir->buf_in);
#endif
	case 5:
		lirc_buffer_free(rbuf);
	case 4:
		kfree(rbuf);
	case 3:
		kfree(plugin);
	case 2:
		kfree(ir);
	case 1:
		printk(DRIVER_NAME "[%d]: out of memory (code=%d)\n",
			devnum, mem_failure);
#if defined(KERNEL_2_5)
		return -ENOMEM;
#else
		return NULL;
#endif
	}

	plugin->minor = minor;
	ir->p = plugin;
	ir->devnum = devnum;
	ir->usbdev = dev;
	ir->len_in = DEVICE_BUFLEN+DEVICE_HEADERLEN;
	ir->connected = 0;
	ir->in_space = 1; /* First mode2 event is a space. */
	do_gettimeofday(&ir->last_time);

	if (dev->descriptor.iManufacturer
		&& usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0)
		strncpy(name, buf, 128);
	if (dev->descriptor.iProduct
		&& usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0)
		snprintf(name, 128, "%s %s", name, buf);
	printk(DRIVER_NAME "[%d]: %s on usb%d:%d\n", devnum, name,
	       dev->bus->busnum, devnum);

	/* clear device buffer */
	if ((ret = usb_control_msg(ir->usbdev, usb_rcvctrlpipe(ir->usbdev, 0), 
		SET_INFRABUFFER_EMPTY, USB_TYPE_VENDOR|USB_DIR_IN,
		/*unused*/0, /*unused*/0, 
		/*dummy*/ir->buf_in, /*dummy*/ir->len_in, 
		/*timeout*/HZ * USB_CTRL_GET_TIMEOUT)) < 0)
	{
		printk(DRIVER_NAME "[%d]: SET_INFRABUFFER_EMPTY: error %d\n", 
			devnum, ret);
	}

#if defined(KERNEL_2_5)
	usb_set_intfdata(intf, ir);
	return SUCCESS;
#else
	return ir;
#endif
}


#if defined(KERNEL_2_5)
static void usb_remote_disconnect(struct usb_interface *intf)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct irctl *ir = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
#else
static void usb_remote_disconnect(struct usb_device *dev, void *ptr)
{
	struct irctl *ir = ptr;
#endif

	if (!ir || !ir->p)
		return;

	ir->usbdev = NULL;
	wake_up_all(&ir->wait_out);

	IRLOCK;
#if defined(KERNEL_2_5)
	usb_buffer_free(dev, ir->len_in, ir->buf_in, ir->dma_in);
#else
	kfree(ir->buf_in);
#endif
	IRUNLOCK;

	unregister_from_lirc(ir);
}

static struct usb_device_id usb_remote_id_table [] = {
	{ USB_DEVICE(0x03eb, 0x0002) },	/* Igor Plug USB (Atmel's Manufact. ID) */
	{ }				/* Terminating entry */
};

static struct usb_driver usb_remote_driver = {
	.owner =	THIS_MODULE,
	.name =		DRIVER_NAME,
	.probe =	usb_remote_probe,
	.disconnect =	usb_remote_disconnect,
	.id_table =	usb_remote_id_table
};

static int __init usb_remote_init(void)
{
	int i;

	printk("\n" DRIVER_NAME ": " DRIVER_DESC " v" DRIVER_VERSION "\n");
	printk(DRIVER_NAME ": " DRIVER_AUTHOR "\n");
	dprintk(DRIVER_NAME ": debug mode enabled\n");

	request_module("lirc_dev");

	if ((i = usb_register(&usb_remote_driver)) < 0) {
		printk(DRIVER_NAME ": usb register failed, result = %d\n", i);
		return -ENODEV;
	}

	return SUCCESS;
}

static void __exit usb_remote_exit(void)
{
	usb_deregister(&usb_remote_driver);
}

module_init(usb_remote_init);
module_exit(usb_remote_exit);

#if defined(KERNEL_2_5)
#include <linux/vermagic.h>
MODULE_INFO(vermagic, VERMAGIC_STRING);
#endif

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, usb_remote_id_table);

EXPORT_NO_SYMBOLS;

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
