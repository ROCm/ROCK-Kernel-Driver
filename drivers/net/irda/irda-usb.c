/*****************************************************************************
 *
 * Filename:      irda-usb.c
 * Version:       0.9
 * Description:   IrDA-USB Driver
 * Status:        Experimental 
 * Author:        Dag Brattli <dag@brattli.net>
 *
 *      Copyright (C) 2001, Dag Brattli <dag@brattli.net>
 *      Copyright (C) 2001, Jean Tourrilhes <jt@hpl.hp.com>
 *	Copyright (C) 2000, Roman Weissgaerber <weissg@vienna.at>
 *          
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/usb.h>

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/irda_device.h>
#include <net/irda/wrapper.h>

#include <net/irda/irda-usb.h>

static int qos_mtt_bits = 0;

static void irda_usb_dump_class_desc(struct irda_class_desc *desc);
static struct irda_class_desc *irda_usb_find_class_desc(struct usb_device *dev, unsigned int ifnum);
static void irda_usb_disconnect(struct usb_device *dev, void *ptr);
static void irda_usb_change_speed_xbofs(struct irda_usb_cb *self);
static int irda_usb_hard_xmit(struct sk_buff *skb, struct net_device *dev);
static int irda_usb_open(struct irda_usb_cb *self);
static int irda_usb_close(struct irda_usb_cb *self);
static void irda_usb_write_bulk(struct irda_usb_cb *self, purb_t purb);
static void write_bulk_callback(purb_t purb);
static void irda_usb_receive(purb_t purb);
static int irda_usb_net_init(struct net_device *dev);
static int irda_usb_net_open(struct net_device *dev);
static int irda_usb_net_close(struct net_device *dev);
static int irda_usb_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void irda_usb_net_timeout(struct net_device *dev);
static struct net_device_stats *irda_usb_net_get_stats(struct net_device *dev);

/* Master instance for each hardware found */
#define NIRUSB 4		/* Max number of USB-IrDA dongles */
static struct irda_usb_cb irda_instance[NIRUSB];

/* These are the currently known IrDA USB dongles. Add new dongles here */
static struct usb_device_id dongles[] = {
	/* ACTiSYS Corp,  ACT-IR2000U FIR-USB Adapter */
	{ USB_DEVICE(0x9c4, 0x011), driver_info: IUC_SPEED_BUG | IUC_NO_WINDOW },
	/* KC Technology Inc.,  KC-180 USB IrDA Device */
	{ USB_DEVICE(0x50f, 0x180), driver_info: IUC_SPEED_BUG | IUC_NO_WINDOW },
	/* Extended Systems, Inc.,  XTNDAccess IrDA USB (ESI-9685) */
	{ USB_DEVICE(0x8e9, 0x100), driver_info: IUC_SPEED_BUG | IUC_NO_WINDOW },
	{ match_flags: USB_DEVICE_ID_MATCH_INT_CLASS |
	               USB_DEVICE_ID_MATCH_INT_SUBCLASS,
	  bInterfaceClass: USB_CLASS_APP_SPEC,
	  bInterfaceSubClass: USB_CLASS_IRDA,
	  driver_info: IUC_DEFAULT, },
	{ }, /* The end */
};

MODULE_DEVICE_TABLE(usb, dongles);

/*
 * This routine is called by the USB subsystem for each new device
 * in the system. We need to check if the device is ours, and in
 * this case start handling it.
 * Note : it might be worth protecting this function by a global
 * spinlock...
 */
static void *irda_usb_probe(struct usb_device *dev, unsigned int ifnum,
			   const struct usb_device_id *id)
{
	struct irda_usb_cb *self = NULL;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct irda_class_desc *irda_desc;
	int capability = id->driver_info;
	int ret;
	int ep;
	int i;

	IRDA_DEBUG(0, "Vendor: %x, Product: %x\n", dev->descriptor.idVendor, dev->descriptor.idProduct);
	
	MESSAGE("IRDA-USB found at address %d\n", dev->devnum);

	/* Try to cleanup all instance that have a pending disconnect
	 * Instance will be in this state is the disconnect() occurs
	 * before the net_close().
	 * Jean II */
	for (i = 0; i < NIRUSB; i++) {
		struct irda_usb_cb *irda = &irda_instance[i];
		if ((irda->usbdev != NULL) &&
		    (irda->present == 0) &&
		    (irda->netopen == 0)) {
			IRDA_DEBUG(0, __FUNCTION__ "(), found a zombie instance !!!\n");
			irda_usb_disconnect(irda->usbdev, (void *) irda);
		}
	}

	/* Find an free instance to handle this new device... */
	self = NULL;
	for (i = 0; i < NIRUSB; i++) {
		if(irda_instance[i].usbdev == NULL) {
			self = &irda_instance[i];
			break;
		}
	}
	if (self == NULL) {
		IRDA_DEBUG(0, "Too many USB IrDA devices !!! (max = %d)\n",
			   NIRUSB);
		return NULL;
	}

	/* Reset the instance */
	self->present = 0;
	self->netopen = 0;

       /* Is this really necessary? */
	if (usb_set_configuration (dev, dev->config[0].bConfigurationValue) < 0) {
		err("set_configuration failed");
		return NULL;
	}
	
        /* Is this really necessary? */
	ret = usb_set_interface(dev, ifnum, 0);
	IRDA_DEBUG(0, "usb-irda: set interface result %d\n", ret);
	switch (ret) {
		case USB_ST_NOERROR:		/* 0 */
			break;
		case USB_ST_STALL:		/* -EPIPE = -32 */
			usb_clear_halt(dev, usb_sndctrlpipe(dev, 0));
			IRDA_DEBUG(0, __FUNCTION__ "(), Clearing stall on control interface\n" );
			break;
		default:
			IRDA_DEBUG(0, __FUNCTION__ "(), Unknown error %d\n", ret);
			return NULL;
			break;
	}
	
	/* Find our endpoints */
	interface = &dev->actconfig->interface[ifnum].altsetting[0];
	endpoint = interface->endpoint;
	ep = endpoint[0].bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	if ((endpoint[0].bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
		self->bulk_in_ep = ep;
	else {
		self->bulk_out_ep = ep;
		self->bulk_out_mtu = endpoint[0].wMaxPacketSize;
	}

	ep = endpoint[1].bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	if ((endpoint[1].bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
		self->bulk_in_ep = ep;
	else {
		self->bulk_out_ep = ep;
		self->bulk_out_mtu = endpoint[1].wMaxPacketSize;
	}

	if (self->bulk_out_ep == 0 || self->bulk_in_ep == 0 ||
	    endpoint [0].bmAttributes != USB_ENDPOINT_XFER_BULK ||
	    endpoint [1].bmAttributes != USB_ENDPOINT_XFER_BULK) 
	{
		ERROR(__FUNCTION__ "(), Bogus endpoints");
		return NULL;
	}

	/* Find IrDA class descriptor */
	irda_desc = irda_usb_find_class_desc(dev, ifnum);
	if (irda_desc == NULL)
		return NULL;
	
	self->irda_desc =  irda_desc;	
	self->present = 1;
	self->netopen = 0;
	self->capability = capability;
	self->usbdev = dev;
	ret = irda_usb_open(self);
	if (ret)
		return NULL;

	return self;
}

/*
 * Function irda_usb_find_class_desc(dev, ifnum)
 *
 *    Returns instance of IrDA class descriptor, or NULL if not found
 *
 */
static struct irda_class_desc *irda_usb_find_class_desc(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct irda_class_desc *desc, *ptr;
	int ret;
		
	desc = kmalloc(sizeof (struct irda_class_desc), GFP_KERNEL);
	if (desc == NULL) 
		return NULL;
	memset(desc, 0, sizeof(struct irda_class_desc));
	
	ret = usb_get_class_descriptor(dev, ifnum, USB_DT_IRDA, 0, (void *) desc, sizeof(struct irda_class_desc));
	IRDA_DEBUG(0, __FUNCTION__ "(), ret=%d\n", ret);
	if (ret) {
		WARNING("usb-irda: usb_get_class_descriptor failed (0x%x)\n", ret);  
	}

	/* Check if we found it? */
	if (desc->bDescriptorType == USB_DT_IRDA)
		return desc;

	IRDA_DEBUG(0, __FUNCTION__ "(), parsing extra descriptors ...\n");
	
	/* Check if the class descriptor is interleaved with standard descriptors */
	interface = &dev->actconfig->interface[ifnum].altsetting[0];
	ret = usb_get_extra_descriptor(interface, USB_DT_IRDA, &ptr);
	if (ret) {
		kfree(desc);
		return NULL;
	}
	*desc = *ptr;
#if 0
	irda_usb_dump_class_desc(desc);
#endif	
	return desc;
}

/*
 * Function usb_irda_dump_class_desc(desc)
 *
 *    Prints out the contents of the IrDA class descriptor
 *
 */
static void irda_usb_dump_class_desc(struct irda_class_desc *desc)
{
	printk("bLength=%x\n", desc->bLength);
	printk("bDescriptorType=%x\n", desc->bDescriptorType);
	printk("bcdSpecRevision=%x\n", desc->bcdSpecRevision); 
	printk("bmDataSize=%x\n", desc->bmDataSize);
	printk("bmWindowSize=%x\n", desc->bmWindowSize);
	printk("bmMinTurnaroundTime=%d\n", desc->bmMinTurnaroundTime);
	printk("wBaudRate=%x\n", desc->wBaudRate);
	printk("bmAdditionalBOFs=%x\n", desc->bmAdditionalBOFs);
	printk("bIrdaRateSniff=%x\n", desc->bIrdaRateSniff);
	printk("bMaxUnicastList=%x\n", desc->bMaxUnicastList);
}												
	
static void irda_usb_disconnect(struct usb_device *dev, void *ptr)
{
	struct irda_usb_cb *self = (struct irda_usb_cb *) ptr;
	int i;

	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	/* Oups ! We are not there any more */
	self->present = 0;

	/* Hum... Check if networking is still active */
	if (self->netopen) {
		/* Accept no more transmissions */
		/*netif_device_detach(self->netdev);*/
		netif_stop_queue(self->netdev);
		/* Stop all the receive URBs */
		for (i = 0; i < IU_MAX_RX_URBS; i++)
			usb_unlink_urb(&(self->rx_urb[i]));
		/* Cancel Tx and speed URB */
		usb_unlink_urb(&(self->tx_urb));
		usb_unlink_urb(&(self->speed_urb));

		IRDA_DEBUG(0, __FUNCTION__ "(), postponing disconnect, network is still active...\n");
		/* better not do anything just yet, usb_irda_cleanup()
		 * will do whats needed */
		return;
	}

	/* Cleanup the device stuff */
	irda_usb_close(self);
	/* No longer attached to USB bus */
	self->usbdev = NULL;
	IRDA_DEBUG(0, __FUNCTION__ "(), USB IrDA Disconnected\n");
}

static struct usb_driver irda_driver = {
	name:		"irda-usb",
	probe:		irda_usb_probe,
	disconnect:	irda_usb_disconnect,
	id_table:	dongles,
};

static void irda_usb_init_qos(struct irda_usb_cb *self)
{
	struct irda_class_desc *desc;

	desc = self->irda_desc;
	
	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);

	self->qos.baud_rate.bits       = desc->wBaudRate;
	self->qos.min_turn_time.bits   = desc->bmMinTurnaroundTime;
	self->qos.additional_bofs.bits = desc->bmAdditionalBOFs;
	self->qos.window_size.bits     = desc->bmWindowSize;
	self->qos.data_size.bits       = desc->bmDataSize;

	IRDA_DEBUG(0, __FUNCTION__ "(), dongle says speed=0x%X, size=0x%X, window=0x%X, bofs=0x%X, turn=0x%X\n", self->qos.baud_rate.bits, self->qos.data_size.bits, self->qos.window_size.bits, self->qos.additional_bofs.bits, self->qos.min_turn_time.bits);

	/* Don't always trust what the dongle tell us */
	if (self->capability & IUC_SIR_ONLY)
		self->qos.baud_rate.bits	&= 0xff;
	if (self->capability & IUC_SMALL_PKT)
		self->qos.data_size.bits	 = 0x07;
	if (self->capability & IUC_NO_WINDOW)
		self->qos.window_size.bits	 = 0x01;
	if (self->capability & IUC_MAX_WINDOW)
		self->qos.window_size.bits	 = 0x7f;
	if (self->capability & IUC_MAX_XBOFS)
		self->qos.additional_bofs.bits	 = 0x01;

#if 1
	/* Module parameter can override the rx window size */
	if (qos_mtt_bits)
		self->qos.min_turn_time.bits = qos_mtt_bits;
#endif	    
	/* 
	 * Note : most of those values apply only for the receive path,
	 * the transmit path will be set differently - Jean II 
	 */
	irda_qos_bits_to_value(&self->qos);

	self->flags |= IFF_SIR;
	if (self->qos.baud_rate.value > 115200)
		self->flags |= IFF_MIR;
	if (self->qos.baud_rate.value > 1152000)
		self->flags |= IFF_FIR;
	if (self->qos.baud_rate.value > 4000000)
		self->flags |= IFF_VFIR;
}

static int irda_usb_open(struct irda_usb_cb *self)
{
	struct net_device *netdev;
	int err;

	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	spin_lock_init(&self->lock);

	irda_usb_init_qos(self);
	
	self->tx_list = hashbin_new(HB_GLOBAL);

	/* Create a network device for us */
	if (!(netdev = dev_alloc("irda%d", &err))) {
		ERROR(__FUNCTION__ "(), dev_alloc() failed!\n");
		return -1;
	}
	self->netdev = netdev;
 	netdev->priv = (void *) self;

	/* Override the network functions we need to use */
	netdev->init            = irda_usb_net_init;
	netdev->hard_start_xmit = irda_usb_hard_xmit;
	netdev->tx_timeout	= irda_usb_net_timeout;
	netdev->watchdog_timeo  = 110*HZ/1000;  /* 110 ms > USB timeout */
	netdev->open            = irda_usb_net_open;
	netdev->stop            = irda_usb_net_close;
	netdev->get_stats	= irda_usb_net_get_stats;
	netdev->do_ioctl        = irda_usb_net_ioctl;

	rtnl_lock();
	err = register_netdevice(netdev);
	rtnl_unlock();
	if (err) {
		ERROR(__FUNCTION__ "(), register_netdev() failed!\n");
		return -1;
	}
	MESSAGE("IrDA: Registered device %s\n", netdev->name);

	return 0;
}

static int irda_usb_close(struct irda_usb_cb *self)
{
	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return -1;);

	/* Remove netdevice */
	if (self->netdev) {
		rtnl_lock();
		unregister_netdevice(self->netdev);
		self->netdev = NULL;
		rtnl_unlock();
	}
	hashbin_delete(self->tx_list, (FREE_FUNC) &dev_kfree_skb_any);
	
	return 0;
}

/*
 * Function irda_usb_build_header(self, skb, header)
 *
 *   Builds USB-IrDA outbound header
 *
 * Important note : the USB-IrDA spec 1.0 say very clearly in chapter 5.4.2.2
 * that the setting of the link speed and xbof number in this outbound header
 * should be applied *AFTER* the frame has been sent.
 * Unfortunately, some devices are not compliant with that... It seems that
 * reading the spec is far too difficult...
 * Jean II
 */
static void irda_usb_build_header(struct irda_usb_cb *self, u8 *header,
				  int force)
{
	/* Set the negotiated link speed */
	if (self->new_speed != -1) {
		/* Hum... Ugly hack :-(
		 * Some device are not compliant with the spec and change
		 * parameters *before* sending the frame. - Jean II
		 */
		if ((self->capability & IUC_SPEED_BUG) &&
		    (!force) && (self->speed != -1)) 
		{
			/* No speed and xbofs change here
			 * (we'll do it later in the write callback) */
			IRDA_DEBUG(2, __FUNCTION__ "(), not changing speed yet\n");
			*header = 0;
			return;
		}

		IRDA_DEBUG(2, __FUNCTION__ "(), changing speed to %d\n", self->new_speed);
		self->speed = self->new_speed;
		self->new_speed = -1;

		switch (self->speed) {
		case 2400:
		        *header = SPEED_2400;
			break;
		default:
		case 9600:
			*header = SPEED_9600;
			break;
		case 19200:
			*header = SPEED_19200;
			break;
		case 38400:
			*header = SPEED_38400;
			break;
		case 57600:
		        *header = SPEED_57600;
			break;
		case 115200:
		        *header = SPEED_115200;
			break;
		case 576000:
		        *header = SPEED_576000;
			break;
		case 1152000:
		        *header = SPEED_1152000;
			break;
		case 4000000:
		        *header = SPEED_4000000;
			self->new_xbofs = 0;
			break;
		}
	} else
		/* No change */
		*header = 0;
	
	/* Set the negotiated additional XBOFS */
	if (self->new_xbofs != -1) {
		IRDA_DEBUG(0, __FUNCTION__ "(), changing xbofs to %d\n", self->new_xbofs);
		self->xbofs = self->new_xbofs;
		self->new_xbofs = -1;

		switch (self->xbofs) {
		case 48:
			*header |= 0x10;
			break;
		case 28:
		case 24: /* USB spec 1.0 says 24 */
			*header |= 0x20;
			break;
		default:
		case 12:
			*header |= 0x30;
			break;
		case 5: /* Bug in IrLAP spec? (should be 6) */
		case 6:
			*header |= 0x40;
			break;
		case 3:
			*header |= 0x50;
			break;
		case 2:
			*header |= 0x60;
			break;
		case 1:
			*header |= 0x70;
			break;
		case 0:
			*header |= 0x80;
			break;
		}
	}
}

static void irda_usb_change_speed_xbofs(struct irda_usb_cb *self)
{
	struct sk_buff *skb;
	unsigned long flags;
	purb_t purb;
	int ret;

	IRDA_DEBUG(2, __FUNCTION__ "(), speed=%d, xbofs=%d\n",
		   self->new_speed, self->new_xbofs);

	purb = &self->speed_urb;
	if (purb->status != USB_ST_NOERROR) {
		WARNING(__FUNCTION__ "(), URB still in use!\n");
		return;
	}
	spin_lock_irqsave(&self->lock, flags);

	/* Allocate the fake frame */
	skb = dev_alloc_skb(IRDA_USB_SPEED_MTU);
	if (!skb)
		return;
	((struct irda_skb_cb *)skb->cb)->context = self;

	/* Set the new speed and xbofs in this fake frame */
	irda_usb_build_header(self, skb_put(skb, USB_IRDA_HEADER), 1);

	/* Submit the 0 length IrDA frame to trigger new speed settings */
        FILL_BULK_URB(purb, self->usbdev,
		      usb_sndbulkpipe(self->usbdev, self->bulk_out_ep),
                      skb->data, IRDA_USB_MAX_MTU,
                      write_bulk_callback, skb);
	purb->transfer_buffer_length = skb->len;
	purb->transfer_flags |= USB_QUEUE_BULK;
	purb->timeout = MSECS_TO_JIFFIES(100);

	if ((ret = usb_submit_urb(purb))) {
		IRDA_DEBUG(0, __FUNCTION__ "(), failed Speed URB\n");
	}
	spin_unlock_irqrestore(&self->lock, flags);
}

static int irda_usb_hard_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct irda_usb_cb *self = netdev->priv;
	purb_t purb = &self->tx_urb;
	unsigned long flags;
	s32 speed;
	s16 xbofs;
	int mtt;

	/* Check if the device is still there */
	if ((!self) || (!self->present)) {
		IRDA_DEBUG(0, __FUNCTION__ "(), Device is gone...\n");
		return 1;	/* Failed */
	}

	netif_stop_queue(netdev);

	/* Check if we need to change the number of xbofs */
        xbofs = irda_get_next_xbofs(skb);
        if ((xbofs != self->xbofs) && (xbofs != -1)) {
		self->new_xbofs = xbofs;
	}

        /* Check if we need to change the speed */
	speed = irda_get_next_speed(skb);
	if ((speed != self->speed) && (speed != -1)) {
		/* Set the desired speed */
		self->new_speed = speed;

		/* Check for empty frame */
		if (!skb->len) {
			/* IrLAP send us an empty frame to make us change the
			 * speed. Changing speed with the USB adapter is in
			 * fact sending an empty frame to the adapter, so we
			 * could just let the present function do its job.
			 * However, we would wait for min turn time,
			 * do an extra memcpy and increment packet counters...
			 * Jean II */
			irda_usb_change_speed_xbofs(self);
			netdev->trans_start = jiffies;
			dev_kfree_skb(skb);
			/* Will netif_wake_queue() in callback */
			return 0;
		}
	}

	if (purb->status != USB_ST_NOERROR) {
		WARNING(__FUNCTION__ "(), URB still in use!\n");
		dev_kfree_skb(skb);
		return 0;
	}

	/* Make room for IrDA-USB header */
	if (skb_cow(skb, USB_IRDA_HEADER)) {
		dev_kfree_skb(skb);
		return 0;
	}

	spin_lock_irqsave(&self->lock, flags);
	
	/* Change setting for next frame */
	irda_usb_build_header(self, skb_push(skb, USB_IRDA_HEADER), 0);

	/* FIXME: Make macro out of this one */
	((struct irda_skb_cb *)skb->cb)->context = self;

        FILL_BULK_URB(purb, self->usbdev, 
		      usb_sndbulkpipe(self->usbdev, self->bulk_out_ep),
                      skb->data, IRDA_USB_MAX_MTU,
                      write_bulk_callback, skb);
	purb->transfer_buffer_length = skb->len;
	purb->transfer_flags |= USB_QUEUE_BULK;
	purb->timeout = MSECS_TO_JIFFIES(100);
	
	/* Generate min turn time */
	mtt = irda_get_mtt(skb);
	if (mtt) {
		int diff;
		get_fast_time(&self->now);
		diff = self->now.tv_usec - self->stamp.tv_usec;
		if (diff < 0)
			diff += 1000000;

                /* Check if the mtt is larger than the time we have
		 * already used by all the protocol processing
		 */
		if (mtt > diff) {
			mtt -= diff;
			if (mtt > 1000) {
				/* 
				* FIXME: can we do better than this? Maybe call
				* a function which sends a frame to a non 
				* existing device, or change the speed to the 
				* current one a number of times just to burn 
				* time a better way.
				*/															
				mdelay(mtt/1000);
				irda_usb_write_bulk(self, purb);
				goto out;	
			} else
				udelay(mtt);
		}
	}	
	irda_usb_write_bulk(self, purb);
out:
	spin_unlock_irqrestore(&self->lock, flags);
	
	return 0;
}

static void irda_usb_write_bulk(struct irda_usb_cb *self, purb_t purb)
{
	int len = purb->transfer_buffer_length; 
	int res;
	
	if ((res = usb_submit_urb(purb))) {
		IRDA_DEBUG(0, __FUNCTION__ "(), failed Tx URB\n");
		self->stats.tx_errors++;
		netif_start_queue(self->netdev);

		return;
	}
	self->stats.tx_packets++;
        self->stats.tx_bytes += len;
	self->netdev->trans_start = jiffies;
	
	/* Send empty frame if size if a multiple of the USB max packet size */
	ASSERT(self->bulk_out_mtu == 64, return;);
	if ((len % self->bulk_out_mtu) == 0) {
		/* Borrow speed urb */
		purb = &self->speed_urb;
		FILL_BULK_URB(purb, self->usbdev,
			      usb_sndbulkpipe(self->usbdev, self->bulk_out_ep),
			      self, /* Anything not on the stack will do */
			      IRDA_USB_MAX_MTU, NULL, NULL);
		purb->transfer_buffer_length = 0;
		purb->transfer_flags |= USB_QUEUE_BULK;
		res = usb_submit_urb(purb);
	}
}

/*
 * Note : this function will be called with both tx_urb and speed_urb...
 */
static void write_bulk_callback(purb_t purb)
{
	struct sk_buff *skb = purb->context;
	struct irda_usb_cb *self = ((struct irda_skb_cb *) skb->cb)->context;
	
	/* We should always have a context */
	if (self == NULL) {
		IRDA_DEBUG(0, __FUNCTION__ "(), Bug : self == NULL\n");
		return;
	}

	/* urb is now available */
	purb->status = USB_ST_NOERROR;

	dev_kfree_skb_any(skb);
	purb->context = NULL;

        /* Check for timeout and other USB nasties */
        if (purb->status != USB_ST_NOERROR) {
	        /* I get a lot of -ECONNABORTED = -103 here - Jean II */
	        WARNING(__FUNCTION__ "(), URB complete status %d, transfer_flags 0x%04X\n", 
			purb->status, purb->transfer_flags);
	        /* Don't do anything here, that might confuse the USB layer,
	         * and we could go in recursion and blow the kernel stack...
	         * Instead, we will wait for irda_usb_net_timeout(), the
	         * network layer watchdog, to fix the situation.
	         * Jean II */
	        /* A reset of the dongle might be welcomed here - Jean II */
	        return;
	}
													

	/* URB is now available */
	purb->status = USB_ST_NOERROR;

	/* If the network is closed, stop everything */
	if ((!self->netopen) || (!self->present)) {
		IRDA_DEBUG(0, __FUNCTION__ "(), Network is gone...\n");
		return;
	}

	/* If we need to change the speed or xbofs, do it now */
	if ((self->new_speed != -1) || (self->new_xbofs != -1)) {
		IRDA_DEBUG(0, __FUNCTION__ "(), Changing speed now...\n");
		irda_usb_change_speed_xbofs(self);
	} else {
		/* Otherwise, allow the stack to send more packets */
		netif_wake_queue(self->netdev);
	}
}

static void irda_usb_submit(struct irda_usb_cb *self, struct sk_buff *skb, purb_t purb)
{
	struct irda_skb_cb *cb;
	int ret;

	IRDA_DEBUG(2, __FUNCTION__ "()\n");

	/* Check that we have an urb */
	if (!purb) {
		IRDA_DEBUG(0, __FUNCTION__ "(), Bug : purb == NULL\n");
		return;
	}

	/* Allocate new skb if it has not been recycled */
	if (!skb) {
		skb = dev_alloc_skb(IRDA_USB_MAX_MTU + 1);
		if (!skb) {
			/* If this ever happen, we are in deep s***.
			 * Basically, the Rx path will stop... */
			IRDA_DEBUG(0, __FUNCTION__ "(), Failed to allocate Rx skb\n");
			return;
		}
	} else  {
		/* Reset recycled skb */
		skb->data = skb->tail = skb->head;
		skb->len = 0;
	}
	/* Make sure IP header get aligned (IrDA header is 5 bytes ) */
	skb_reserve(skb, 1);

	/* Save ourselves */
	cb = (struct irda_skb_cb *) skb->cb;
	cb->context = self;

	/* Reinitialize URB */
	FILL_BULK_URB(purb, self->usbdev, 
		      usb_rcvbulkpipe(self->usbdev, self->bulk_in_ep), 
		      skb->data, skb->truesize,
                      irda_usb_receive, skb);
	purb->transfer_flags |= USB_QUEUE_BULK;
	purb->status = USB_ST_NOERROR;
	purb->next = NULL; /* Make sure we don't auto resubmit */ 
	
	ret = usb_submit_urb(purb);
	if (ret) {
		/* If this ever happen, we are in deep s***.
		 * Basically, the Rx path will stop... */
		IRDA_DEBUG(0, __FUNCTION__ "(), Failed to submit Rx URB %d\n", ret);
	}
}

/*
 * Function irda_usb_receive(purb)
 *
 *     Called by the USB subsystem when a frame has been received
 *
 */
static void irda_usb_receive(purb_t purb) 
{
	struct sk_buff *skb = (struct sk_buff *) purb->context;
	struct irda_usb_cb *self; 
	struct irda_skb_cb *cb;
	struct sk_buff *new;
	
	/* Find ourselves */
	cb = (struct irda_skb_cb *) skb->cb;
	ASSERT(cb != NULL, return;);
	self = (struct irda_usb_cb *) cb->context;
	ASSERT(self != NULL, return;);

	/* If the network is closed or the device gone, stop everything */
	if ((!self->netopen) || (!self->present)) {
		IRDA_DEBUG(0, __FUNCTION__ "(), Network is gone!\n");
		/* Don't re-submit the URB : will stall the Rx path */
		return;
	}
	
	/* Check the status */
	if (purb->status != USB_ST_NOERROR) {
		switch (purb->status) {
		case USB_ST_CRC:		/* -EILSEQ */
			self->stats.rx_errors++;
			self->stats.rx_crc_errors++;	
			break;
		case -ECONNRESET:
		        WARNING(__FUNCTION__ "(), Connection Reset !!!\n");
			/* uhci_cleanup_unlink() is going to kill the Rx
			 * URB just after we return. No problem, at this
			 * point the URB will be idle ;-) - Jean II */
			break;
		default:
			WARNING(__FUNCTION__ "(), RX status %d\n", purb->status);
			break;
		}
		goto done;
	}

	/* Check for empty frames */
	if (purb->actual_length <= USB_IRDA_HEADER) {
		WARNING(__FUNCTION__ "(), empty frame!\n");
		goto done;
	}

	/*  
	 * Remember the time we received this frame, so we can
	 * reduce the min turn time a bit since we will know
	 * how much time we have used for protocol processing
	 */
        get_fast_time(&self->stamp);

	/* Fix skb, and remove USB-IrDA header */
	skb_put(skb, purb->actual_length);
	skb_pull(skb, USB_IRDA_HEADER);

	/* Don't waste a lot of memory on small IrDA frames */
	if (skb->len < RX_COPY_THRESHOLD) {
		new = dev_alloc_skb(skb->len+1);
		if (!new) {
			self->stats.rx_dropped++;
			goto done;  
		}

		/* Make sure IP header get aligned (IrDA header is 5 bytes) */
		skb_reserve(new, 1);
		
		/* Copy packet, so we can recycle the original */
		memcpy(skb_put(new, skb->len), skb->data, skb->len);
		/* We will cleanup the skb in irda_usb_submit */
	} else {
		/* Deliver the original skb */
		new = skb;
		skb = NULL;
	}
	
	self->stats.rx_bytes += new->len;
	self->stats.rx_packets++;

	/* Ask the networking layer to queue the packet for the IrDA stack */
        new->dev = self->netdev;
        new->mac.raw  = new->data;
        new->protocol = htons(ETH_P_IRDA);
        netif_rx(new);
done:
	/* Recycle Rx URB (and possible the skb as well) */
	irda_usb_submit(self, skb, self->rx_idle_urb);

        /* Recycle Rx URB : Now, the idle URB is the present one */
	self->rx_idle_urb = purb;
	purb->context = NULL;
	/* Prevent the USB layer playing games with our URBs */
	purb->status = -ENOENT;
}

static int irda_usb_net_init(struct net_device *dev)
{
	IRDA_DEBUG(0, __FUNCTION__ "()\n");
	
	/* Set up to be a normal IrDA network device driver */
	irda_device_setup(dev);

	/* Insert overrides below this line! */

	return 0;
}

/*
 * Function irda_usb_net_open (dev)
 *
 *    Network device is taken up. Usually this is done by "ifconfig irda0 up" 
 *   
 * Note : don't mess with self->netopen - Jean II
 */
static int irda_usb_net_open(struct net_device *netdev)
{
	struct irda_usb_cb *self;
	int i;
	
	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	ASSERT(netdev != NULL, return -1;);
	self = (struct irda_usb_cb *) netdev->priv;
	ASSERT(self != NULL, return -1;);

	/* Can only open the device if it's there */
	if(!self->present) {
		WARNING(__FUNCTION__ "(), device not present!\n");
		return -1;
	}

	/* Initialise default speed and xbofs value
	 * (IrLAP will change that soon) */
	self->speed = -1;
	self->xbofs = -1;
	self->new_speed = -1;
	self->new_xbofs = -1;

	/* To do *before* submitting Rx urbs and starting net Tx queue
	 * Jean II */
	self->netopen = 1;

	/* 
	 * Now that everything should be initialized properly,
	 * Open new IrLAP layer instance to take care of us...
	 * Note : will send immediately a speed change...
	 */
	self->irlap = irlap_open(netdev, &self->qos);
	ASSERT(self->irlap != NULL, return -1;);

	/* Allow IrLAP to send data to us */
	netif_start_queue(netdev);

	/* Now that we can pass data to IrLAP, allow the USB layer
	 * to send us some data... */
	for (i = 0; i < IU_MAX_ACTIVE_RX_URBS; i++)
		irda_usb_submit(self, NULL, &(self->rx_urb[i]));
	/* Note : we submit all the Rx URB except for one - Jean II */
	self->rx_idle_urb = &(self->rx_urb[IU_MAX_ACTIVE_RX_URBS]);
	self->rx_idle_urb->context = NULL;

	/* Ready to play !!! */
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * Function irda_usb_net_close (self)
 *
 *    Network device is taken down. Usually this is done by 
 *    "ifconfig irda0 down" 
 */
static int irda_usb_net_close(struct net_device *netdev)
{
	struct irda_usb_cb *self;
	int i;

	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	ASSERT(netdev != NULL, return -1;);
	self = (struct irda_usb_cb *) netdev->priv;
	ASSERT(self != NULL, return -1;);

	/* Clear this flag *before* unlinking the urbs and *before*
	 * stopping the network Tx queue - Jean II */
	self->netopen = 0;

	/* Stop network Tx queue */
	netif_stop_queue(netdev);

	/* Deallocate all the Rx path buffers (URBs and skb) */
	for (i = 0; i < IU_MAX_RX_URBS; i++) {
		purb_t purb = &(self->rx_urb[i]);
		struct sk_buff *skb = (struct sk_buff *) purb->context;
		/* Cancel the receive command */
		usb_unlink_urb(purb);
		/* The skb is ours, free it */
		if (skb) {
			dev_kfree_skb(skb);
			purb->context = NULL;
		}
	}
	/* Cancel Tx and speed URB */
	usb_unlink_urb(&self->tx_urb);
	usb_unlink_urb(&self->speed_urb);
	
	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;

	MOD_DEC_USE_COUNT;

	return 0;
}

static void irda_usb_net_timeout(struct net_device *netdev)
{
	struct irda_usb_cb *self = netdev->priv;
	int done = 0;	/* If we have made any progress */
	purb_t purb;

	IRDA_DEBUG(0, __FUNCTION__ "(), Network layer thinks we timed out!\n");

	if (!self || !self->present) {
		WARNING(__FUNCTION__ "(), device not present!\n");
		netif_stop_queue(netdev);
		return;
	}
	WARNING("%s: Tx timed out\n", netdev->name);

	/* Check Tx URB */
	purb = &self->tx_urb;
	switch (purb->status) {
		case -ECONNABORTED:	/* Can't find proper USB_ST_* code */
			purb->status = USB_ST_NOERROR;
			netif_wake_queue(self->netdev);
			done = 1;
			break;
		case USB_ST_URB_PENDING:	/* -EINPROGRESS == -115 */
			usb_unlink_urb(purb);
			done = 1;
			break;
		default:
			break;
	}
	/* Check speed URB */
	purb = &self->speed_urb;
	switch (purb->status) {
		case -ECONNABORTED:	/* Can't find proper USB_ST_* code */
			purb->status = USB_ST_NOERROR;
			netif_wake_queue(self->netdev);
			done = 1;
			break;
		case USB_ST_URB_PENDING:	/* -EINPROGRESS */
			usb_unlink_urb(purb);
			done = 1;
			break;
		default:
			break;
	}
	/* Maybe we need a reset */
	/* if(done == 0) */
}

static int irda_usb_is_receiving(struct irda_usb_cb *self)
{
	return 0; /* For now */
}

static int irda_usb_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct irda_usb_cb *self;
	int ret = 0;

	ASSERT(dev != NULL, return -1;);
	self = dev->priv;
	ASSERT(self != NULL, return -1;);

	IRDA_DEBUG(2, __FUNCTION__ "(), %s, (cmd=0x%X)\n", dev->name, cmd);

	/* Check if the device is still there */
	if (!self->present)
		return -EFAULT;

	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		/* Set the desired speed */
		self->new_speed = irq->ifr_baudrate;
		irda_usb_change_speed_xbofs(self);
		/* Note : will spinlock in above function */
		break;
	case SIOCSMEDIABUSY: /* Set media busy */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		irda_device_set_media_busy(self->netdev, TRUE);
		break;
	case SIOCGRECEIVING: /* Check if we are receiving right now */
		irq->ifr_receiving = irda_usb_is_receiving(self);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	return ret;
}

static struct net_device_stats *irda_usb_net_get_stats(struct net_device *dev)
{
	struct irda_usb_cb *self = dev->priv;
	return &self->stats;
}

int __init usb_irda_init(void)
{
	if (usb_register(&irda_driver) < 0)
		return -1;

	MESSAGE("USB IrDA support registered\n");
	return 0;
}
module_init(usb_irda_init);

void __exit usb_irda_cleanup(void)
{
	struct irda_usb_cb *irda = NULL;
	int i;

	/* Find zombie instances and kill them... */
	for (i = 0; i < NIRUSB; i++) {
		irda = &irda_instance[i];
		/* If the Device is zombie */
		if((irda->usbdev != NULL) && (irda->present == 0)) {
			IRDA_DEBUG(0, __FUNCTION__ "(), disconnect zombie now !\n");
			irda_usb_disconnect(irda->usbdev, (void *) irda);
		}
	}
	/* Deregister the driver and remove all pending instances */
	usb_deregister(&irda_driver);
}
module_exit(usb_irda_cleanup);

MODULE_PARM(qos_mtt_bits, "i");
MODULE_PARM_DESC(qos_mtt_bits, "Minimum Turn Time");
MODULE_AUTHOR("Roman Weissgaerber <weissg@vienna.at>, Dag Brattli <dag@brattli.net> and Jean Tourrilhes <jt@hpl.hp.com>");
MODULE_DESCRIPTION("IrDA-USB Dongle Driver"); 



