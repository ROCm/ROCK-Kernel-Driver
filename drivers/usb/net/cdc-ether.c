// Portions of this file taken from
// Petko Manolov - Petkan (petkan@dce.bg)
// from his driver pegasus.c

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
 */

#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <linux/module.h>
#include "cdc-ether.h"

static const char *version = __FILE__ ": v0.98.5 22 Sep 2001 Brad Hards and another";

/* Take any CDC device, and sort it out in probe() */
static struct usb_device_id CDCEther_ids[] = {
	{ USB_DEVICE_INFO(USB_CLASS_COMM, 0, 0) },
	{ } /* Terminating null entry */
};

/* 
 * module parameter that provides an alternate upper limit on the 
 * number of multicast filters we use, with a default to use all
 * the filters available to us. Note that the actual number used
 * is the lesser of this parameter and the number returned in the
 * descriptor for the particular device. See Table 41 of the CDC
 * spec for more info on the descriptor limit.
 */
static int multicast_filter_limit = 32767;


//////////////////////////////////////////////////////////////////////////////
// Callback routines from USB device /////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static void read_bulk_callback( struct urb *urb, struct pt_regs *regs )
{
	ether_dev_t *ether_dev = urb->context;
	struct net_device *net;
	int count = urb->actual_length, res;
	struct sk_buff	*skb;

	// Sanity check 
	if ( !ether_dev || !(ether_dev->flags & CDC_ETHER_RUNNING) ) {
		dbg("BULK IN callback but driver is not active!");
		return;
	}

	net = ether_dev->net;
	if ( !netif_device_present(net) ) {
		// Somebody killed our network interface...
		return;
	}

	if ( ether_dev->flags & CDC_ETHER_RX_BUSY ) {
		// Are we already trying to receive a frame???
		ether_dev->stats.rx_errors++;
		dbg("ether_dev Rx busy");
		return;
	}

	// We are busy, leave us alone!
	ether_dev->flags |= CDC_ETHER_RX_BUSY;

	switch ( urb->status ) {
		case 0:
			break;
		case -ETIMEDOUT:
			dbg( "no repsonse in BULK IN" );
			ether_dev->flags &= ~CDC_ETHER_RX_BUSY;
			break;
		default:
			dbg( "%s: RX status %d", net->name, urb->status );
			goto goon;
	}

	// Check to make sure we got some data...
	if ( !count ) {
		// We got no data!!!
		goto goon;
	}

	// Tell the kernel we want some memory
	if ( !(skb = dev_alloc_skb(count)) ) {
		// We got no receive buffer.
		goto goon;
	}

	// Here's where it came from
	skb->dev = net;
	
	// Now we copy it over
	eth_copy_and_sum(skb, ether_dev->rx_buff, count, 0);
	
	// Not sure
	skb_put(skb, count);
	// Not sure here either
	skb->protocol = eth_type_trans(skb, net);
	
	// Ship it off to the kernel
	netif_rx(skb);
	
	// update out statistics
	ether_dev->stats.rx_packets++;
	ether_dev->stats.rx_bytes += count;

goon:
	// Prep the USB to wait for another frame
	usb_fill_bulk_urb( ether_dev->rx_urb, ether_dev->usb,
			usb_rcvbulkpipe(ether_dev->usb, ether_dev->data_ep_in),
			ether_dev->rx_buff, ether_dev->wMaxSegmentSize, 
			read_bulk_callback, ether_dev );

	// Give this to the USB subsystem so it can tell us
	// when more data arrives.
	if ( (res = usb_submit_urb(ether_dev->rx_urb, GFP_ATOMIC)) ) {
		warn("%s failed submint rx_urb %d", __FUNCTION__, res);
	}

	// We are no longer busy, show us the frames!!!
	ether_dev->flags &= ~CDC_ETHER_RX_BUSY;
}

static void write_bulk_callback( struct urb *urb, struct pt_regs *regs )
{
	ether_dev_t *ether_dev = urb->context;

	// Sanity check
	if ( !ether_dev || !(ether_dev->flags & CDC_ETHER_RUNNING) ) {
		// We are insane!!!
		err( "write_bulk_callback: device not running" );
		return;
	}

	// Do we still have a valid kernel network device?
	if ( !netif_device_present(ether_dev->net) ) {
		// Someone killed our network interface.
		err( "write_bulk_callback: net device not present" );
		return;
	}

	// Hmm...  What on Earth could have happened???
	if ( urb->status ) {
		info("%s: TX status %d", ether_dev->net->name, urb->status);
	}

	// Update the network interface and tell it we are
	// ready for another frame
	ether_dev->net->trans_start = jiffies;
	netif_wake_queue( ether_dev->net );
}

//static void intr_callback( struct urb *urb )
//{
//	ether_dev_t *ether_dev = urb->context;
//	struct net_device *net;
//	__u8	*d;
//
//	if ( !ether_dev )
//		return;
//		
//	switch ( urb->status ) {
//		case 0:
//			break;
//		case -ENOENT:
//			return;
//		default:
//			info("intr status %d", urb->status);
//	}
//
//	d = urb->transfer_buffer;
//	net = ether_dev->net;
//	if ( d[0] & 0xfc ) {
//		ether_dev->stats.tx_errors++;
//		if ( d[0] & TX_UNDERRUN )
//			ether_dev->stats.tx_fifo_errors++;
//		if ( d[0] & (EXCESSIVE_COL | JABBER_TIMEOUT) )
//			ether_dev->stats.tx_aborted_errors++;
//		if ( d[0] & LATE_COL )
//			ether_dev->stats.tx_window_errors++;
//		if ( d[0] & (NO_CARRIER | LOSS_CARRIER) )
//			ether_dev->stats.tx_carrier_errors++;
//	}
//}

//////////////////////////////////////////////////////////////////////////////
// Routines for turning net traffic on and off on the USB side ///////////////
//////////////////////////////////////////////////////////////////////////////

static inline int enable_net_traffic( ether_dev_t *ether_dev )
{
	struct usb_device *usb = ether_dev->usb;

	// Here would be the time to set the data interface to the configuration where
	// it has two endpoints that use a protocol we can understand.

	if (usb_set_interface( usb, 
	                        ether_dev->data_bInterfaceNumber, 
	                        ether_dev->data_bAlternateSetting_with_traffic ) )  {
		err("usb_set_interface() failed" );
		err("Attempted to set interface %d", ether_dev->data_bInterfaceNumber);
		err("To alternate setting       %d", ether_dev->data_bAlternateSetting_with_traffic);
		return -1;
	}
	return 0;
}

static inline void disable_net_traffic( ether_dev_t *ether_dev )
{
	// The thing to do is to set the data interface to the alternate setting that has
	// no endpoints.  This is what the spec suggests.

	if (ether_dev->data_interface_altset_num_without_traffic >= 0 ) {
		if (usb_set_interface( ether_dev->usb, 
		                        ether_dev->data_bInterfaceNumber, 
		                        ether_dev->data_bAlternateSetting_without_traffic ) ) 	{
			err("usb_set_interface() failed");
		}
	} else {
		// Some devices just may not support this...
		warn("No way to disable net traffic");
	}
}

//////////////////////////////////////////////////////////////////////////////
// Callback routines for kernel Ethernet Device //////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static void CDCEther_tx_timeout( struct net_device *net )
{
	ether_dev_t *ether_dev = net->priv;

	// Sanity check
	if ( !ether_dev ) {
		// Seems to be a case of insanity here
		return;
	}

	// Tell syslog we are hosed.
	warn("%s: Tx timed out.", net->name);
	
	// Tear the waiting frame off the list
	ether_dev->tx_urb->transfer_flags |= URB_ASYNC_UNLINK;
	usb_unlink_urb( ether_dev->tx_urb );
	
	// Update statistics
	ether_dev->stats.tx_errors++;
}

static int CDCEther_start_xmit( struct sk_buff *skb, struct net_device *net )
{
	ether_dev_t	*ether_dev = net->priv;
	int 	res;

	// Tell the kernel, "No more frames 'til we are done
	// with this one.'
	netif_stop_queue( net );

	// Copy it from kernel memory to OUR memory
	memcpy(ether_dev->tx_buff, skb->data, skb->len);

	// Fill in the URB for shipping it out.
	usb_fill_bulk_urb( ether_dev->tx_urb, ether_dev->usb,
			usb_sndbulkpipe(ether_dev->usb, ether_dev->data_ep_out),
			ether_dev->tx_buff, ether_dev->wMaxSegmentSize, 
			write_bulk_callback, ether_dev );

	// Tell the URB how much it will be transporting today
	ether_dev->tx_urb->transfer_buffer_length = skb->len;

	/* Deal with the zero length problem, I hope */
	ether_dev->tx_urb->transfer_flags |= URB_ZERO_PACKET;
	
	// Send the URB on its merry way.
	if ((res = usb_submit_urb(ether_dev->tx_urb, GFP_ATOMIC)))  {
		// Hmm...  It didn't go. Tell someone...
		warn("failed tx_urb %d", res);
		// update some stats...
		ether_dev->stats.tx_errors++;
		// and tell the kernel to give us another.
		// Maybe we'll get it right next time.
		netif_start_queue( net );
	} else {
		// Okay, it went out.
		// Update statistics
		ether_dev->stats.tx_packets++;
		ether_dev->stats.tx_bytes += skb->len;
		// And tell the kernel when the last transmit occurred.
		net->trans_start = jiffies;
	}

	// We are done with the kernel's memory
	dev_kfree_skb(skb);

	// We are done here.
	return 0;
}

static struct net_device_stats *CDCEther_netdev_stats( struct net_device *net )
{
	// Easy enough!
	return &((ether_dev_t *)net->priv)->stats;
}

static int CDCEther_open(struct net_device *net)
{
	ether_dev_t *ether_dev = (ether_dev_t *)net->priv;
	int	res;

	// Turn on the USB and let the packets flow!!!
	if ( (res = enable_net_traffic( ether_dev )) ) {
		err("%s can't enable_net_traffic() - %d", __FUNCTION__, res );
		return -EIO;
	}

	// Prep a receive URB
	usb_fill_bulk_urb( ether_dev->rx_urb, ether_dev->usb,
			usb_rcvbulkpipe(ether_dev->usb, ether_dev->data_ep_in),
			ether_dev->rx_buff, ether_dev->wMaxSegmentSize, 
			read_bulk_callback, ether_dev );

	// Put it out there so the device can send us stuff
	if ( (res = usb_submit_urb(ether_dev->rx_urb, GFP_KERNEL)) )
	{
		// Hmm...  Okay...
		warn("%s failed rx_urb %d", __FUNCTION__, res );
	}

	// Tell the kernel we are ready to start receiving from it
	netif_start_queue( net );
	
	// We are up and running.
	ether_dev->flags |= CDC_ETHER_RUNNING;

	// Let's get ready to move frames!!!
	return 0;
}

static int CDCEther_close( struct net_device *net )
{
	ether_dev_t	*ether_dev = net->priv;

	// We are no longer running.
	ether_dev->flags &= ~CDC_ETHER_RUNNING;
	
	// Tell the kernel to stop sending us stuff
	netif_stop_queue( net );
	
	// If we are not already unplugged, turn off USB
	// traffic
	if ( !(ether_dev->flags & CDC_ETHER_UNPLUG) ) {
		disable_net_traffic( ether_dev );
	}

	// We don't need the URBs anymore.
	usb_unlink_urb( ether_dev->rx_urb );
	usb_unlink_urb( ether_dev->tx_urb );
	usb_unlink_urb( ether_dev->intr_urb );
	
	// That's it.  I'm done.
	return 0;
}

static int CDCEther_ioctl( struct net_device *net, struct ifreq *rq, int cmd )
{
	//__u16 *data = (__u16 *)&rq->ifr_data;
	//ether_dev_t	*ether_dev = net->priv;

	// No support here yet.
	// Do we need support???
	switch(cmd) {
		case SIOCDEVPRIVATE:
			return -EOPNOTSUPP;
		case SIOCDEVPRIVATE+1:
			return -EOPNOTSUPP;
		case SIOCDEVPRIVATE+2:
			//return 0;
			return -EOPNOTSUPP;
		default:
			return -EOPNOTSUPP;
	}
}

#if 0
static void CDC_SetEthernetPacketFilter (ether_dev_t *ether_dev)
{
	usb_control_msg(ether_dev->usb,
			usb_sndctrlpipe(ether_dev->usb, 0),
			SET_ETHERNET_PACKET_FILTER, /* request */
			USB_TYPE_CLASS | USB_DIR_OUT | USB_RECIP_INTERFACE, /* request type */
			cpu_to_le16(ether_dev->mode_flags), /* value */
			cpu_to_le16((u16)ether_dev->comm_interface), /* index */
			NULL,
			0, /* size */
			HZ); /* timeout */
}
#endif


static void CDCEther_set_multicast( struct net_device *net )
{
	ether_dev_t *ether_dev = net->priv;
	int i;
	__u8 *buff;


	// Tell the kernel to stop sending us frames while we get this
	// all set up.
//	netif_stop_queue(net);

// FIXME: We hold xmit_lock. If you want to do the queue stuff you need
//	  to enable it from a completion handler

      /* Note: do not reorder, GCC is clever about common statements. */
        if (net->flags & IFF_PROMISC) {
                /* Unconditionally log net taps. */
                info( "%s: Promiscuous mode enabled", net->name);
		ether_dev->mode_flags = MODE_FLAG_PROMISCUOUS |
			MODE_FLAG_ALL_MULTICAST |
			MODE_FLAG_DIRECTED |
			MODE_FLAG_BROADCAST |
			MODE_FLAG_MULTICAST;
        } else if (net->mc_count > ether_dev->wNumberMCFilters) {
                /* Too many to filter perfectly -- accept all multicasts. */
		info("%s: set too many MC filters, using allmulti", net->name);
		ether_dev->mode_flags = MODE_FLAG_ALL_MULTICAST |
			MODE_FLAG_DIRECTED |
			MODE_FLAG_BROADCAST |
			MODE_FLAG_MULTICAST;
	} else if (net->flags & IFF_ALLMULTI) {
                /* Filter in software */
		info("%s: using allmulti", net->name);
		ether_dev->mode_flags = MODE_FLAG_ALL_MULTICAST |
			MODE_FLAG_DIRECTED |
			MODE_FLAG_BROADCAST |
			MODE_FLAG_MULTICAST;
        } else {
		/* do multicast filtering in hardware */
                struct dev_mc_list *mclist;
		info("%s: set multicast filters", net->name);
		ether_dev->mode_flags = MODE_FLAG_ALL_MULTICAST |
			MODE_FLAG_DIRECTED |
			MODE_FLAG_BROADCAST |
			MODE_FLAG_MULTICAST;
		buff = kmalloc(6 * net->mc_count, GFP_ATOMIC);
                for (i = 0, mclist = net->mc_list;
		     mclist && i < net->mc_count;
                     i++, mclist = mclist->next) {
			memcpy(&mclist->dmi_addr, &buff[i * 6], 6);
		}
#if 0
		usb_control_msg(ether_dev->usb,
// FIXME: We hold a spinlock. You must not use a synchronous API
				usb_sndctrlpipe(ether_dev->usb, 0),
				SET_ETHERNET_MULTICAST_FILTER, /* request */
				USB_TYPE_CLASS | USB_DIR_OUT | USB_RECIP_INTERFACE, /* request type */
				cpu_to_le16(net->mc_count), /* value */
				cpu_to_le16((u16)ether_dev->comm_interface), /* index */
				buff,
				(6* net->mc_count), /* size */
				HZ); /* timeout */
#endif
		kfree(buff);
	}

#if 0 
	CDC_SetEthernetPacketFilter(ether_dev);
#endif	
        // Tell the kernel to start giving frames to us again.
//	netif_wake_queue(net);
}

//////////////////////////////////////////////////////////////////////////////
// Routines used to parse out the Functional Descriptors /////////////////////
//////////////////////////////////////////////////////////////////////////////

static int parse_header_functional_descriptor( int *bFunctionLength, 
                                               int bDescriptorType, 
                                               int bDescriptorSubtype,
                                               unsigned char *data,
                                               ether_dev_t *ether_dev,
                                               int *requirements )
{
	// Check to make sure we haven't seen one of these already.
	if ( (~*requirements) & REQ_HDR_FUNC_DESCR ) {
		err( "Multiple Header Functional Descriptors found." );
		return -1;
	}
	
	// Is it the right size???
	if (*bFunctionLength != 5) {
		info( "Invalid length in Header Functional Descriptor" );
		// This is a hack to get around a particular device (NO NAMES)
		// It has this function length set to the length of the
		// whole class-specific descriptor
		*bFunctionLength = 5;
	}
	
	// Nothing extremely useful here.
	// We'll keep it for posterity
	ether_dev->bcdCDC = data[0] + (data[1] << 8);
	dbg( "Found Header descriptor, CDC version %x", ether_dev->bcdCDC);

	// We've seen one of these
	*requirements &= ~REQ_HDR_FUNC_DESCR;
	
	// It's all good.
	return 0;
}

static int parse_union_functional_descriptor( int *bFunctionLength, 
                                              int bDescriptorType, 
                                              int bDescriptorSubtype,
                                              unsigned char *data,
                                              ether_dev_t *ether_dev,
                                              int *requirements )
{
	// Check to make sure we haven't seen one of these already.
	if ( (~*requirements) & REQ_UNION_FUNC_DESCR ) {
		err( "Multiple Union Functional Descriptors found." );
		return -1;
	}

	// Is it the right size?
	if (*bFunctionLength != 5) {
		// It is NOT the size we expected.
		err( "Unsupported length in Union Functional Descriptor" );
		return -1;
	}
	
	// Sanity check of sorts
	if (ether_dev->comm_interface != data[0]) {
		// This tells us that we are chasing the wrong comm
		// interface or we are crazy or something else weird.
		if (ether_dev->comm_interface == data[1]) {
			info( "Probably broken Union descriptor, fudging data interface" );
			// We'll need this in a few microseconds, 
			// so guess here, and hope for the best
			ether_dev->data_interface = data[0];
		} else {
			err( "Union Functional Descriptor is broken beyond repair" );
			return -1;
		}
	} else{ // Descriptor is OK
       		// We'll need this in a few microseconds!
		ether_dev->data_interface = data[1];
	}

	// We've seen one of these now.
	*requirements &= ~REQ_UNION_FUNC_DESCR;
	
	// Done
	return 0;
}

static int parse_ethernet_functional_descriptor( int *bFunctionLength, 
                                                 int bDescriptorType, 
                                                 int bDescriptorSubtype,
                                                 unsigned char *data,
                                                 ether_dev_t *ether_dev,
                                                 int *requirements )
{
	// Check to make sure we haven't seen one of these already.
	if ( (~*requirements) & REQ_ETH_FUNC_DESCR ) {
		err( "Multiple Ethernet Functional Descriptors found." );
		return -1;
	}
	
	// Is it the right size?
	if (*bFunctionLength != 13) {
		err( "Invalid length in Ethernet Networking Functional Descriptor" );
		return -1;
	}
	
	// Lots of goodies from this one.  They are all important.
	ether_dev->iMACAddress = data[0];
	ether_dev->bmEthernetStatistics = data[1] + (data[2] << 8) + (data[3] << 16) + (data[4] << 24);
	ether_dev->wMaxSegmentSize = data[5] + (data[6] << 8);
	ether_dev->wNumberMCFilters = (data[7] + (data[8] << 8)) & 0x00007FFF;
	if (ether_dev->wNumberMCFilters > multicast_filter_limit) {
		ether_dev->wNumberMCFilters = multicast_filter_limit;
		}	
	ether_dev->bNumberPowerFilters = data[9];
	
	// We've seen one of these now.
	*requirements &= ~REQ_ETH_FUNC_DESCR;
	
	// That's all she wrote.
	return 0;
}

static int parse_protocol_unit_functional_descriptor( int *bFunctionLength, 
                                                      int bDescriptorType, 
                                                      int bDescriptorSubtype,
                                                      unsigned char *data,
                                                      ether_dev_t *ether_dev,
                                                      int *requirements )
{
	// There should only be one type if we are sane
	if (bDescriptorType != CS_INTERFACE) {
		info( "Invalid bDescriptorType found." );
		return -1;
	}

	// The Subtype tells the tale.
	switch (bDescriptorSubtype){
		case 0x00:	// Header Functional Descriptor
			return parse_header_functional_descriptor( bFunctionLength,
			                                           bDescriptorType,
			                                           bDescriptorSubtype,
			                                           data,
			                                           ether_dev,
			                                           requirements );
			break;
		case 0x06:	// Union Functional Descriptor
			return parse_union_functional_descriptor( bFunctionLength,
			                                          bDescriptorType,
			                                          bDescriptorSubtype,
			                                          data,
			                                          ether_dev,
			                                          requirements );
			break;
		case 0x0F:	// Ethernet Networking Functional Descriptor
			return parse_ethernet_functional_descriptor( bFunctionLength,
			                                             bDescriptorType,
			                                             bDescriptorSubtype,
			                                             data,
			                                             ether_dev,
			                                             requirements );
			break;
		default:	// We don't support this at this time...
			// However that doesn't necessarily indicate an error.
			dbg( "Unexpected header type %x:", bDescriptorSubtype );
			return 0;
	}
	// How did we get here???
	return -1;
}

static int parse_ethernet_class_information( unsigned char *data, int length, ether_dev_t *ether_dev )
{
	int loc = 0;
	int rc;
	int bFunctionLength;
	int bDescriptorType;
	int bDescriptorSubtype;
	int requirements = REQUIREMENTS_TOTAL;

	// As long as there is something here, we will try to parse it
	while (loc < length) {
		// Length
		bFunctionLength = data[loc];
		loc++;
		
		// Type
		bDescriptorType = data[loc];
		loc++;
		
		// Subtype
		bDescriptorSubtype = data[loc];
		loc++;
		
		// ship this off to be processed elsewhere.
		rc = parse_protocol_unit_functional_descriptor( &bFunctionLength, 
		                                                bDescriptorType, 
		                                                bDescriptorSubtype, 
		                                                &data[loc],
		                                                ether_dev,
		                                                &requirements );
		// Did it process okay?
		if (rc)	{
			// Something was hosed somewhere.
			// No need to continue;
			err("Bad descriptor parsing: %x", rc );
			return -1;
		}
		// We have already taken three bytes.
		loc += (bFunctionLength - 3);
	}
	// Check to see if we got everything we need.
	if (requirements) {
		// We missed some of the requirements...
		err( "Not all required functional descriptors present 0x%08X", requirements );
		return -1;
	}
	// We got everything.
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Routine to check for the existence of the Functional Descriptors //////////
//////////////////////////////////////////////////////////////////////////////

static int find_and_parse_ethernet_class_information( struct usb_device *device, ether_dev_t *ether_dev )
{
	struct usb_host_config *conf = NULL;
	struct usb_interface *comm_intf_group = NULL;
	struct usb_host_interface *comm_intf = NULL;
	int rc = -1;
	// The assumption here is that find_ethernet_comm_interface
	// and find_valid_configuration 
	// have already filled in the information about where to find
	// the a valid commication interface.

	conf = &( device->config[ether_dev->configuration_num] );
	comm_intf_group = &( conf->interface[ether_dev->comm_interface] );
	comm_intf = &( comm_intf_group->altsetting[ether_dev->comm_interface_altset_num] );
	// Let's check and see if it has the extra information we need...

	if (comm_intf->extralen > 0) {
		// This is where the information is SUPPOSED to be.
		rc = parse_ethernet_class_information( comm_intf->extra, comm_intf->extralen, ether_dev );
	} else if (conf->extralen > 0) {
		// This is a hack.  The spec says it should be at the interface 
		// location checked above.  However I have seen it here also.
		// This is the same device that requires the functional descriptor hack above
		warn( "Ethernet information found at device configuration.  This is broken." );
		rc = parse_ethernet_class_information( conf->extra, conf->extralen, ether_dev );
	} else 	{
		// I don't know where else to look.
		warn( "No ethernet information found." );
		rc = -1;
	}
	return rc;
}

//////////////////////////////////////////////////////////////////////////////
// Routines to verify the data interface /////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static int get_data_interface_endpoints( struct usb_device *device, ether_dev_t *ether_dev )
{
	struct usb_host_config *conf = NULL;
	struct usb_interface *data_intf_group = NULL;
	struct usb_host_interface *data_intf = NULL;
	
	// Walk through and get to the data interface we are checking.
	conf = &( device->config[ether_dev->configuration_num] );
	data_intf_group = &( conf->interface[ether_dev->data_interface] );
	data_intf = &( data_intf_group->altsetting[ether_dev->data_interface_altset_num_with_traffic] );

	// Start out assuming we won't find anything we can use
	ether_dev->data_ep_in = 0;
	ether_dev->data_ep_out = 0;
	
	// If these are not BULK endpoints, we don't want them
	if ( data_intf->endpoint[0].desc.bmAttributes != 0x02 ) {
		return -1;
	} if ( data_intf->endpoint[1].desc.bmAttributes != 0x02 ) {
		return -1;
	}

	// Check the first endpoint to see if it is IN or OUT
	if ( data_intf->endpoint[0].desc.bEndpointAddress & 0x80 ) {
		// This endpoint is IN
		ether_dev->data_ep_in = data_intf->endpoint[0].desc.bEndpointAddress & 0x7F;
	} else {
		// This endpoint is OUT
		ether_dev->data_ep_out = data_intf->endpoint[0].desc.bEndpointAddress & 0x7F;
		ether_dev->data_ep_out_size = data_intf->endpoint[0].desc.wMaxPacketSize;
	}

	// Check the second endpoint to see if it is IN or OUT
	if ( data_intf->endpoint[1].desc.bEndpointAddress & 0x80 ) {
		// This endpoint is IN
		ether_dev->data_ep_in = data_intf->endpoint[1].desc.bEndpointAddress & 0x7F;
	} else	{
		// This endpoint is OUT
		ether_dev->data_ep_out = data_intf->endpoint[1].desc.bEndpointAddress & 0x7F;
		ether_dev->data_ep_out_size = data_intf->endpoint[1].desc.wMaxPacketSize;
	}
	
	// Now make sure we got both an IN and an OUT
	if (ether_dev->data_ep_in && ether_dev->data_ep_out) {
		// We did get both, we are in good shape...
		info( "detected BULK OUT packets of size %d", ether_dev->data_ep_out_size );
		return 0;
	}
	return -1;
}

static int verify_ethernet_data_interface( struct usb_device *device, ether_dev_t *ether_dev )
{
	struct usb_host_config *conf = NULL;
	struct usb_interface *data_intf_group = NULL;
	struct usb_interface_descriptor *data_intf = NULL;
	int rc = -1;
	int status;
	int altset_num;

	// The assumption here is that parse_ethernet_class_information()
	// and find_valid_configuration() 
	// have already filled in the information about where to find
	// a data interface
	conf = &( device->config[ether_dev->configuration_num] );
	data_intf_group = &( conf->interface[ether_dev->data_interface] );

	// start out assuming we won't find what we are looking for.
	ether_dev->data_interface_altset_num_with_traffic = -1;
	ether_dev->data_bAlternateSetting_with_traffic = -1;
	ether_dev->data_interface_altset_num_without_traffic = -1;
	ether_dev->data_bAlternateSetting_without_traffic = -1;

	// Walk through every possible setting for this interface until
	// we find what makes us happy.
	for ( altset_num = 0; altset_num < data_intf_group->num_altsetting; altset_num++ ) {
		data_intf = &( data_intf_group->altsetting[altset_num].desc );

		// Is this a data interface we like?
		if ( ( data_intf->bInterfaceClass == 0x0A )
		   && ( data_intf->bInterfaceSubClass == 0x00 )
		   && ( data_intf->bInterfaceProtocol == 0x00 ) ) {
			if ( data_intf->bNumEndpoints == 2 ) {
				// We are required to have one of these.
				// An interface with 2 endpoints to send Ethernet traffic back and forth
				// It actually may be possible that the device might only
				// communicate in a vendor specific manner.
				// That would not be very nice.
				// We can add that one later.
				ether_dev->data_bInterfaceNumber = data_intf->bInterfaceNumber;
				ether_dev->data_interface_altset_num_with_traffic = altset_num;
				ether_dev->data_bAlternateSetting_with_traffic = data_intf->bAlternateSetting;
				status = get_data_interface_endpoints( device, ether_dev );
				if (!status) {
					rc = 0;
				}
			}
			if ( data_intf->bNumEndpoints == 0 ) {
				// According to the spec we are SUPPOSED to have one of these
				// In fact the device is supposed to come up in this state.
				// However, I have seen a device that did not have such an interface.
				// So it must be just optional for our driver...
				ether_dev->data_bInterfaceNumber = data_intf->bInterfaceNumber;
				ether_dev->data_interface_altset_num_without_traffic = altset_num;
				ether_dev->data_bAlternateSetting_without_traffic = data_intf->bAlternateSetting;
			}
		}
	}
	return rc;
}

//////////////////////////////////////////////////////////////////////////////
// Routine to find a communication interface /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static int find_ethernet_comm_interface( struct usb_device *device, ether_dev_t *ether_dev )
{
	struct usb_host_config *conf = NULL;
	struct usb_interface *comm_intf_group = NULL;
	struct usb_interface_descriptor *comm_intf = NULL;
	int intf_num;
	int altset_num;
	int rc;

	conf = &( device->config[ether_dev->configuration_num] );

	// We need to check and see if any of these interfaces are something we want.
	// Walk through each interface one at a time
	for ( intf_num = 0; intf_num < conf->desc.bNumInterfaces; intf_num++ ) {
		comm_intf_group = &( conf->interface[intf_num] );
		// Now for each of those interfaces, check every possible
		// alternate setting.
		for ( altset_num = 0; altset_num < comm_intf_group->num_altsetting; altset_num++ ) {
			comm_intf = &( comm_intf_group->altsetting[altset_num].desc);

			// Is this a communication class of interface of the
			// ethernet subclass variety.
			if ( ( comm_intf->bInterfaceClass == 0x02 )
			   && ( comm_intf->bInterfaceSubClass == 0x06 )
			   && ( comm_intf->bInterfaceProtocol == 0x00 ) ) {
				if ( comm_intf->bNumEndpoints == 1 ) {
					// Good, we found one, we will try this one
					// Fill in the structure...
					ether_dev->comm_interface = intf_num;
					ether_dev->comm_bInterfaceNumber = comm_intf->bInterfaceNumber;
					ether_dev->comm_interface_altset_num = altset_num;
					ether_dev->comm_bAlternateSetting = comm_intf->bAlternateSetting;

					// Look for the Ethernet Functional Descriptors
					rc = find_and_parse_ethernet_class_information( device, ether_dev );
					if (rc) {
						// Nope this was no good after all.
						continue;
					}

					// Check that we really can talk to the data
					// interface 
					// This includes # of endpoints, protocols,
					// etc.
					rc = verify_ethernet_data_interface( device, ether_dev );
					if (rc)	{
						// We got something we didn't like
						continue;
					}
					// This communication interface seems to give us everything
					// we require.  We have all the ethernet info we need.
					// Let's get out of here and go home right now.
					return 0;
				} else {
                                        // bNumEndPoints != 1
					// We found an interface that had the wrong number of 
					// endpoints but would have otherwise been okay
				} // end bNumEndpoints check.
			} // end interface specifics check.
		} // end for altset_num
	} // end for intf_num
	return -1;
}

//////////////////////////////////////////////////////////////////////////////
// Routine to go through all configurations and find one that ////////////////
// is an Ethernet Networking Device //////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static int find_valid_configuration( struct usb_device *device, ether_dev_t *ether_dev )
{
	struct usb_host_config *conf = NULL;
	int conf_num;
	int rc;

	// We will try each and every possible configuration
	for ( conf_num = 0; conf_num < device->descriptor.bNumConfigurations; conf_num++ ) {
		conf = &( device->config[conf_num] );

		// Our first requirement : 2 interfaces
		if ( conf->desc.bNumInterfaces != 2 ) {
			// I currently don't know how to handle devices with any number of interfaces
			// other than 2.
			continue;
		}

		// This one passed our first check, fill in some 
		// useful data
		ether_dev->configuration_num = conf_num;
		ether_dev->bConfigurationValue = conf->desc.bConfigurationValue;

		// Now run it through the ringers and see what comes
		// out the other side.
		rc = find_ethernet_comm_interface( device, ether_dev );

		// Check if we found an ethernet Communcation Device
		if ( !rc ) {
			// We found one.
			return 0;
		}
	}
	// None of the configurations suited us.
	return -1;
}

//////////////////////////////////////////////////////////////////////////////
// Routine that checks a given configuration to see if any driver ////////////
// has claimed any of the devices interfaces /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static int check_for_claimed_interfaces( struct usb_host_config *config )
{
	struct usb_interface *comm_intf_group;
	int intf_num;

	// Go through all the interfaces and make sure none are 
	// claimed by anybody else.
	for ( intf_num = 0; intf_num < config->desc.bNumInterfaces; intf_num++ ) {
		comm_intf_group = &( config->interface[intf_num] );
		if ( usb_interface_claimed( comm_intf_group ) )	{
			// Somebody has beat us to this guy.
			// We can't change the configuration out from underneath of whoever
			// is using this device, so we will go ahead and give up.
			return -1;
		}
	}
	// We made it all the way through.
	// I guess no one has claimed any of these interfaces.
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Routines to ask for and set the kernel network interface's MAC address ////
// Used by driver's probe routine ////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static inline unsigned char hex2dec( unsigned char digit )
{
	// Is there a standard way to do this???
	// I have written this code TOO MANY times.
	if ( (digit >= '0') && (digit <= '9') )	{
		return (digit - '0');
	}
	if ( (digit >= 'a') && (digit <= 'f') )	{
		return (digit - 'a' + 10);
	}
	if ( (digit >= 'A') && (digit <= 'F') )	{
		return (digit - 'A' + 10);
	}
	return 0;
}

static void set_ethernet_addr( ether_dev_t *ether_dev )
{
	unsigned char	mac_addr[6];
	int		i;
	int 		len;
	unsigned char	buffer[13];

	// Let's assume we don't get anything...
	mac_addr[0] = 0x00;
	mac_addr[1] = 0x00;
	mac_addr[2] = 0x00;
	mac_addr[3] = 0x00;
	mac_addr[4] = 0x00;
	mac_addr[5] = 0x00;

	// Let's ask the device...
	len = usb_string(ether_dev->usb, ether_dev->iMACAddress, buffer, 13);

	// Sanity check!
	if (len != 12)	{
		// You gotta love failing sanity checks
		err("Attempting to get MAC address returned %d bytes", len);
		return;
	}

	// Fill in the mac_addr
	for (i = 0; i < 6; i++)	{
		mac_addr[i] = ( hex2dec( buffer[2 * i] ) << 4 ) + hex2dec( buffer[2 * i + 1] );
	}

	// Now copy it over to the kernel's network driver.
	memcpy( ether_dev->net->dev_addr, mac_addr, sizeof(mac_addr) );
}

//////////////////////////////////////////////////////////////////////////////
// Routine to print to syslog information about the driver ///////////////////
// Used by driver's probe routine ////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static void log_device_info(ether_dev_t *ether_dev)
{
	int len;
	int string_num;
	unsigned char *manu = NULL;
	unsigned char *prod = NULL;
	unsigned char *sern = NULL;
	unsigned char *mac_addr;

	manu = kmalloc(256, GFP_KERNEL);
	prod = kmalloc(256, GFP_KERNEL);
	sern = kmalloc(256, GFP_KERNEL);
	if (!manu || !prod || !sern) {
		dbg("no mem for log_device_info");
		goto fini;
	}

	// Default empty strings in case we don't find a real one
	manu[0] = 0x00;
	prod[0] = 0x00;
	sern[0] = 0x00;

	// Try to get the device Manufacturer
	string_num = ether_dev->usb->descriptor.iManufacturer;
	if (string_num)	{
		// Put it into its buffer
		len = usb_string(ether_dev->usb, string_num, manu, 255);
		// Just to be safe
		manu[len] = 0x00;
	}

	// Try to get the device Product Name
	string_num = ether_dev->usb->descriptor.iProduct;
	if (string_num)	{
		// Put it into its buffer
		len = usb_string(ether_dev->usb, string_num, prod, 255);
		// Just to be safe
		prod[len] = 0x00;
	}

	// Try to get the device Serial Number
	string_num = ether_dev->usb->descriptor.iSerialNumber;
	if (string_num)	{
		// Put it into its buffer
		len = usb_string(ether_dev->usb, string_num, sern, 255);
		// Just to be safe
		sern[len] = 0x00;
	}

	// This makes it easier for us to print
	mac_addr = ether_dev->net->dev_addr;

	// Now send everything we found to the syslog
	info( "%s: %s %s %s %02X:%02X:%02X:%02X:%02X:%02X", 
	      ether_dev->net->name, manu, prod, sern, mac_addr[0], 
	      mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], 
	      mac_addr[5] );
fini:
	kfree(manu);
	kfree(prod);
	kfree(sern);
}

/* Forward declaration */
static struct usb_driver CDCEther_driver ;

//////////////////////////////////////////////////////////////////////////////
// Module's probe routine ////////////////////////////////////////////////////
// claims interfaces if they are for an Ethernet CDC /////////////////////////
//////////////////////////////////////////////////////////////////////////////

static int CDCEther_probe( struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	struct usb_device	*usb = interface_to_usbdev(intf);
	struct net_device	*net;
	ether_dev_t		*ether_dev;
	int 			rc;

	// First we should check the active configuration to see if 
	// any other driver has claimed any of the interfaces.
	if ( check_for_claimed_interfaces( usb->actconfig ) ) {
		// Someone has already put there grubby paws on this device.
		// We don't want it now...
		return -ENODEV;
	}

	// We might be finding a device we can use.
	// We all go ahead and allocate our storage space.
	// We need to because we have to start filling in the data that
	// we are going to need later.
	if(!(ether_dev = kmalloc(sizeof(ether_dev_t), GFP_KERNEL))) {
		err("out of memory allocating device structure");
		return -ENOMEM;
	}

	// Zero everything out.
	memset(ether_dev, 0, sizeof(ether_dev_t));

	ether_dev->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ether_dev->rx_urb) {
		kfree(ether_dev);
		return -ENOMEM;
	}
	ether_dev->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ether_dev->tx_urb) {
		usb_free_urb(ether_dev->rx_urb);
		kfree(ether_dev);
		return -ENOMEM;
	}
	ether_dev->intr_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ether_dev->intr_urb) {
		usb_free_urb(ether_dev->tx_urb);
		usb_free_urb(ether_dev->rx_urb);
		kfree(ether_dev);
		return -ENOMEM;
	}

	// Let's see if we can find a configuration we can use.
	rc = find_valid_configuration( usb, ether_dev );
	if (rc)	{
		// Nope we couldn't find one we liked.
		// This device was not meant for us to control.
		goto error_all;
	}

	// Now that we FOUND a configuration. let's try to make the
	// device go into it.
	if ( usb_set_configuration( usb, ether_dev->bConfigurationValue ) ) {
		err("usb_set_configuration() failed");
		goto error_all;
	}

	// Now set the communication interface up as required.
	if (usb_set_interface(usb, ether_dev->comm_bInterfaceNumber, ether_dev->comm_bAlternateSetting)) {
		err("usb_set_interface() failed");
		goto error_all;
	}

	// Only turn traffic on right now if we must...
	if (ether_dev->data_interface_altset_num_without_traffic >= 0)	{
		// We found an alternate setting for the data
		// interface that allows us to turn off traffic.
		// We should use it.
		if (usb_set_interface( usb,
		                       ether_dev->data_bInterfaceNumber,
		                       ether_dev->data_bAlternateSetting_without_traffic)) {
			err("usb_set_interface() failed");
			goto error_all;
		}
	} else	{
		// We didn't find an alternate setting for the data
		// interface that would let us turn off traffic.
		// Oh well, let's go ahead and do what we must...
		if (usb_set_interface( usb,
		                       ether_dev->data_bInterfaceNumber,
		                       ether_dev->data_bAlternateSetting_with_traffic)) {
			err("usb_set_interface() failed");
			goto error_all;
		}
	}

	// Now we need to get a kernel Ethernet interface.
	net = init_etherdev( NULL, 0 );
	if ( !net ) {
		// Hmm...  The kernel is not sharing today...
		// Fine, we didn't want it anyway...
		err( "Unable to initialize ethernet device" );
		goto error_all;
	}

	// Now that we have an ethernet device, let's set it up
	// (And I don't mean "set [it] up the bomb".)
	net->priv = ether_dev;
	SET_MODULE_OWNER(net);
	net->open = CDCEther_open;
	net->stop = CDCEther_close;
	net->watchdog_timeo = CDC_ETHER_TX_TIMEOUT;
	net->tx_timeout = CDCEther_tx_timeout;   // TX timeout function
	net->do_ioctl = CDCEther_ioctl;
	net->hard_start_xmit = CDCEther_start_xmit;
	net->set_multicast_list = CDCEther_set_multicast;
	net->get_stats = CDCEther_netdev_stats;
	net->mtu = ether_dev->wMaxSegmentSize - 14;

	// We'll keep track of this information for later...
	ether_dev->usb = usb;
	ether_dev->net = net;

	// and don't forget the MAC address.
	set_ethernet_addr( ether_dev );

	// Send a message to syslog about what we are handling
	log_device_info( ether_dev );

	// I claim this interface to be a CDC Ethernet Networking device
	usb_driver_claim_interface( &CDCEther_driver,
	                            &(usb->config[ether_dev->configuration_num].interface[ether_dev->comm_interface]),
	                            ether_dev );
	// I claim this interface to be a CDC Ethernet Networking device
	usb_driver_claim_interface( &CDCEther_driver,
	                            &(usb->config[ether_dev->configuration_num].interface[ether_dev->data_interface]),
	                            ether_dev );

	// Does this REALLY do anything???
	usb_get_dev( usb );

	// TODO - last minute HACK
	ether_dev->comm_ep_in = 5;

/* FIXME!!! This driver needs to be fixed to work with the new USB interface logic
 * this is not the correct thing to be doing here, we need to set the interface
 * driver specific data field.
 */
	// Okay, we are finally done...
	return 0;

	// bailing out with our tail between our knees
error_all:
	usb_free_urb(ether_dev->tx_urb);
	usb_free_urb(ether_dev->rx_urb);
	usb_free_urb(ether_dev->intr_urb);
	kfree( ether_dev );
	return -EIO;
}


//////////////////////////////////////////////////////////////////////////////
// Module's disconnect routine ///////////////////////////////////////////////
// Called when the driver is unloaded or the device is unplugged /////////////
// (Whichever happens first assuming the driver suceeded at its probe) ///////
//////////////////////////////////////////////////////////////////////////////

static void CDCEther_disconnect( struct usb_interface *intf )
{
	ether_dev_t *ether_dev = usb_get_intfdata(intf);
	struct usb_device *usb;

	usb_set_intfdata(intf, NULL);

	// Sanity check!!!
	if ( !ether_dev || !ether_dev->usb ) {
		// We failed.  We are insane!!!
		warn("unregistering non-existant device");
		return;
	}

	// Make sure we fail the sanity check if we try this again.
	ether_dev->usb = NULL;
	
	usb = interface_to_usbdev(intf);

	// It is possible that this function is called before
	// the "close" function.
	// This tells the close function we are already disconnected
	ether_dev->flags |= CDC_ETHER_UNPLUG;
	
	// We don't need the network device any more
	unregister_netdev( ether_dev->net );
	
	// For sanity checks
	ether_dev->net = NULL;

	// I ask again, does this do anything???
	usb_put_dev( usb );

	// We are done with this interface
	usb_driver_release_interface( &CDCEther_driver, 
	                              &(usb->config[ether_dev->configuration_num].interface[ether_dev->comm_interface]) );

	// We are done with this interface too
	usb_driver_release_interface( &CDCEther_driver, 
	                              &(usb->config[ether_dev->configuration_num].interface[ether_dev->data_interface]) );

	// No more tied up kernel memory
	usb_free_urb(ether_dev->intr_urb);
	usb_free_urb(ether_dev->rx_urb);
	usb_free_urb(ether_dev->rx_urb);
	kfree( ether_dev );
	
	// This does no good, but it looks nice!
	ether_dev = NULL;
}

//////////////////////////////////////////////////////////////////////////////
// Driver info ///////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static struct usb_driver CDCEther_driver = {
	.name =		"CDCEther",
	.probe =	CDCEther_probe,
	.disconnect =	CDCEther_disconnect,
	.id_table =	CDCEther_ids,
};

//////////////////////////////////////////////////////////////////////////////
// init and exit routines called when driver is installed and uninstalled ////
//////////////////////////////////////////////////////////////////////////////

int __init CDCEther_init(void)
{
	info( "%s", version );
	return usb_register( &CDCEther_driver );
}

void __exit CDCEther_exit(void)
{
	usb_deregister( &CDCEther_driver );
}

//////////////////////////////////////////////////////////////////////////////
// Module info ///////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

module_init( CDCEther_init );
module_exit( CDCEther_exit );

MODULE_AUTHOR("Brad Hards and another");
MODULE_DESCRIPTION("USB CDC Ethernet driver");
MODULE_LICENSE("GPL");

MODULE_PARM (multicast_filter_limit, "i");
MODULE_PARM_DESC (multicast_filter_limit, "CDCEther maximum number of filtered multicast addresses");

MODULE_DEVICE_TABLE (usb, CDCEther_ids);

//////////////////////////////////////////////////////////////////////////////
// End of file ///////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
