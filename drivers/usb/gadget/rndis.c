/* 
 * RNDIS MSG parser
 * 
 * Version:     $Id: rndis.c,v 1.19 2004/03/25 21:33:46 robert Exp $
 * 
 * Authors:	Benedikt Spranger, Pengutronix
 * 		Robert Schwebel, Pengutronix
 * 
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              version 2, as published by the Free Software Foundation. 
 * 
 *		This software was originally developed in conformance with
 *		Microsoft's Remote NDIS Specification License Agreement.
 *              
 * 03/12/2004 Kai-Uwe Bloem <linux-development@auerswald.de>
 *		Fixed message length bug in init_response
 * 
 * 03/25/2004 Kai-Uwe Bloem <linux-development@auerswald.de>
 * 		Fixed rndis_rm_hdr length bug.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>

#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/system.h>

#include "rndis.h"


/* The driver for your USB chip needs to support ep0 OUT to work with
 * RNDIS, plus the same three descriptors as CDC Ethernet.
 *
 * Windows hosts need an INF file like Documentation/usb/linux.inf
 */

#ifndef	__LITTLE_ENDIAN
#warning this code is missing all cpu_to_leXX() calls ...
#endif

#if 0
#define DEBUG if (rndis_debug) printk 
static int rndis_debug = 0;

module_param (rndis_debug, bool, 0);
MODULE_PARM_DESC (rndis_debug, "enable debugging");

#else
#define DEBUG(str,args...) do{}while(0)
#endif

#define RNDIS_MAX_CONFIGS	1

static struct proc_dir_entry *rndis_connect_dir;
static struct proc_dir_entry *rndis_connect_state [RNDIS_MAX_CONFIGS];

static rndis_params rndis_per_dev_params [RNDIS_MAX_CONFIGS];

/* Driver Version */
static const u32 rndis_driver_version = __constant_cpu_to_le32 (1);

/* Function Prototypes */
static int rndis_init_response (int configNr, rndis_init_msg_type *buf);
static int rndis_query_response (int configNr, rndis_query_msg_type *buf);
static int rndis_set_response (int configNr, rndis_set_msg_type *buf);
static int rndis_reset_response (int configNr, rndis_reset_msg_type *buf);
static int rndis_keepalive_response (int configNr, 
				     rndis_keepalive_msg_type *buf);

static rndis_resp_t *rndis_add_response (int configNr, u32 length);

/* FIXME OMITTED OIDs, that RNDIS-on-USB "must" support, include
 *  - power management (OID_PNP_CAPABILITIES, ...)
 *  - network wakeup (OID_PNP_ENABLE_WAKE_UP, ...)
 */

/* NDIS Functions */
static int gen_ndis_query_resp (int configNr, u32 OID, rndis_resp_t *r)
{
	int 			retval = -ENOTSUPP;
	u32 			length = 0;
	rndis_query_cmplt_type	*resp;

	if (!r) return -ENOMEM;
	resp = (rndis_query_cmplt_type *) r->buf;

	if (!resp) return -ENOMEM;
	
	switch (OID) {
	/* mandatory */
	case OID_GEN_SUPPORTED_LIST:
		DEBUG ("%s: OID_GEN_SUPPORTED_LIST\n", __FUNCTION__);
		length = sizeof (oid_supported_list);
		memcpy ((u8 *) resp + 24, oid_supported_list, length); 
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_HARDWARE_STATUS:
		DEBUG("%s: OID_GEN_HARDWARE_STATUS\n", __FUNCTION__);
		length = 4;
		/* Bogus question! 
		 * Hardware must be ready to recieve high level protocols.
		 * BTW: 
		 * reddite ergo quae sunt Caesaris Caesari
		 * et quae sunt Dei Deo!
		 */
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_MEDIA_SUPPORTED:
		DEBUG("%s: OID_GEN_MEDIA_SUPPORTED\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = rndis_per_dev_params [configNr].medium;
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_MEDIA_IN_USE:
		DEBUG("%s: OID_GEN_MEDIA_IN_USE\n", __FUNCTION__);
		length = 4;
		/* one medium, one transport... (maybe you do it better) */
		*((u32 *) resp + 6) = rndis_per_dev_params [configNr].medium;
		retval = 0;
		break;
		
	case OID_GEN_MAXIMUM_LOOKAHEAD:
		DEBUG("%s: OID_GEN_MAXIMUM_LOOKAHEAD\n", __FUNCTION__);
		break;
		
	/* mandatory */
	case OID_GEN_MAXIMUM_FRAME_SIZE:
		DEBUG("%s: OID_GEN_MAXIMUM_FRAME_SIZE\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			length = 4;
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr]
						.dev->mtu;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_LINK_SPEED:
		DEBUG("%s: OID_GEN_LINK_SPEED\n", __FUNCTION__);
		length = 4;
		if (rndis_per_dev_params [configNr].media_state
			== NDIS_MEDIA_STATE_DISCONNECTED)
		    *((u32 *) resp + 6) = 0;
		else
		    *((u32 *) resp + 6) = rndis_per_dev_params [configNr].speed;
		retval = 0;
		break;
		
	case OID_GEN_TRANSMIT_BUFFER_SPACE:
		DEBUG("%s: OID_GEN_TRANSMIT_BUFFER_SPACE\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	case OID_GEN_RECEIVE_BUFFER_SPACE:
		DEBUG("%s: OID_GEN_RECEIVE_BUFFER_SPACE\n", __FUNCTION__);
		break;
		
	/* mandatory */
	case OID_GEN_TRANSMIT_BLOCK_SIZE:
		DEBUG("%s: OID_GEN_TRANSMIT_BLOCK_SIZE\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			length = 4;
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr]
						.dev->mtu;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_RECEIVE_BLOCK_SIZE:
		DEBUG("%s: OID_GEN_RECEIVE_BLOCK_SIZE\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			length = 4;
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr]
						.dev->mtu;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_VENDOR_ID:
		DEBUG("%s: OID_GEN_VENDOR_ID\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = rndis_per_dev_params [configNr].vendorID;
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_VENDOR_DESCRIPTION:
		DEBUG("%s: OID_GEN_VENDOR_DESCRIPTION\n", __FUNCTION__);
		length = strlen (rndis_per_dev_params [configNr].vendorDescr);
		memcpy ((u8 *) resp + 24, 
			rndis_per_dev_params [configNr].vendorDescr, length);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_CURRENT_PACKET_FILTER:
		DEBUG("%s: OID_GEN_CURRENT_PACKET_FILTER\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = rndis_per_dev_params[configNr].filter;
		retval = 0;
		break;
		
	case OID_GEN_CURRENT_LOOKAHEAD:
		DEBUG("%s: OID_GEN_CURRENT_LOOKAHEAD\n", __FUNCTION__);
		break;
		
	case OID_GEN_DRIVER_VERSION:
		DEBUG("%s: OID_GEN_DRIVER_VERSION\n", __FUNCTION__);
		break;
		
	/* mandatory */
	case OID_GEN_MAXIMUM_TOTAL_SIZE:
		DEBUG("%s: OID_GEN_MAXIMUM_TOTAL_SIZE\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = RNDIS_MAX_TOTAL_SIZE;
		retval = 0;
		break;
		
	case OID_GEN_PROTOCOL_OPTIONS:
		DEBUG("%s: OID_GEN_PROTOCOL_OPTIONS\n", __FUNCTION__);
		break;
		
	case OID_GEN_MAC_OPTIONS:
		DEBUG("%s: OID_GEN_MAC_OPTIONS\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = NDIS_MAC_OPTION_RECEIVE_SERIALIZED | 
		    NDIS_MAC_OPTION_FULL_DUPLEX;
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_MEDIA_CONNECT_STATUS:
		DEBUG("%s: OID_GEN_MEDIA_CONNECT_STATUS\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = rndis_per_dev_params [configNr]
						.media_state;
		retval = 0;
		break;
		
	case OID_GEN_MAXIMUM_SEND_PACKETS:
		DEBUG("%s: OID_GEN_MAXIMUM_SEND_PACKETS\n", __FUNCTION__);
		break;
		
	/* mandatory */
	case OID_GEN_VENDOR_DRIVER_VERSION:
		DEBUG("%s: OID_GEN_VENDOR_DRIVER_VERSION\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = rndis_driver_version;
		retval = 0;
		break;
		
	case OID_GEN_SUPPORTED_GUIDS:
		DEBUG("%s: OID_GEN_SUPPORTED_GUIDS\n", __FUNCTION__);
		break;
		
	case OID_GEN_NETWORK_LAYER_ADDRESSES:
		DEBUG("%s: OID_GEN_NETWORK_LAYER_ADDRESSES\n", __FUNCTION__);
		break;
		
	case OID_GEN_TRANSPORT_HEADER_OFFSET:
		DEBUG("%s: OID_GEN_TRANSPORT_HEADER_OFFSET\n", __FUNCTION__);
		break;
		
	case OID_GEN_MACHINE_NAME:
		DEBUG("%s: OID_GEN_MACHINE_NAME\n", __FUNCTION__);
		break;
		
	case OID_GEN_RNDIS_CONFIG_PARAMETER:
		DEBUG("%s: OID_GEN_RNDIS_CONFIG_PARAMETER\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	case OID_GEN_VLAN_ID:
		DEBUG("%s: OID_GEN_VLAN_ID\n", __FUNCTION__);
		break;
		
	case OID_GEN_MEDIA_CAPABILITIES:
		DEBUG("%s: OID_GEN_MEDIA_CAPABILITIES\n", __FUNCTION__);
		break;
		
	case OID_GEN_PHYSICAL_MEDIUM:
		DEBUG("%s: OID_GEN_PHYSICAL_MEDIUM\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_XMIT_OK:
		DEBUG("%s: OID_GEN_XMIT_OK\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			length = 4;
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr]
					.stats->tx_packets - 
			    rndis_per_dev_params [configNr].stats->tx_errors -
			    rndis_per_dev_params [configNr].stats->tx_dropped;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_RCV_OK:
		DEBUG("%s: OID_GEN_RCV_OK\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			length = 4;
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->rx_packets - 
			    rndis_per_dev_params [configNr].stats->rx_errors -
			    rndis_per_dev_params [configNr].stats->rx_dropped;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_XMIT_ERROR:
		DEBUG("%s: OID_GEN_XMIT_ERROR\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			length = 4;
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->tx_errors;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_RCV_ERROR:
		DEBUG("%s: OID_GEN_RCV_ERROR\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->rx_errors;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_RCV_NO_BUFFER:
		DEBUG("%s: OID_GEN_RCV_NO_BUFFER\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->rx_dropped;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_DIRECTED_BYTES_XMIT:
		DEBUG("%s: OID_GEN_DIRECTED_BYTES_XMIT\n", __FUNCTION__);
		/* 
		 * Aunt Tilly's size of shoes
		 * minus antarctica count of penguins
		 * divided by weight of Alpha Centauri
		 */
		if (rndis_per_dev_params [configNr].stats) {
			length = 4;
			*((u32 *) resp + 6) = (rndis_per_dev_params [configNr].
					stats->tx_packets - 
			    rndis_per_dev_params [configNr].stats->tx_errors -
			    rndis_per_dev_params [configNr].stats->tx_dropped)
					*123;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_DIRECTED_FRAMES_XMIT:
		DEBUG("%s: OID_GEN_DIRECTED_FRAMES_XMIT\n", __FUNCTION__);
		/* dito */
		if (rndis_per_dev_params [configNr].stats) {
			length = 4;
			*((u32 *) resp + 6) = (rndis_per_dev_params [configNr].
					stats->tx_packets - 
			    rndis_per_dev_params [configNr].stats->tx_errors -
			    rndis_per_dev_params [configNr].stats->tx_dropped)
			    		/123;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_MULTICAST_BYTES_XMIT:
		DEBUG("%s: OID_GEN_MULTICAST_BYTES_XMIT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->multicast*1234;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_MULTICAST_FRAMES_XMIT:
		DEBUG("%s: OID_GEN_MULTICAST_FRAMES_XMIT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->multicast;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_BROADCAST_BYTES_XMIT:
		DEBUG("%s: OID_GEN_BROADCAST_BYTES_XMIT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->tx_packets/42*255;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_BROADCAST_FRAMES_XMIT:
		DEBUG("%s: OID_GEN_BROADCAST_FRAMES_XMIT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->tx_packets/42;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_DIRECTED_BYTES_RCV:
		DEBUG("%s: OID_GEN_DIRECTED_BYTES_RCV\n", __FUNCTION__);
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	case OID_GEN_DIRECTED_FRAMES_RCV:
		DEBUG("%s: OID_GEN_DIRECTED_FRAMES_RCV\n", __FUNCTION__);
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	case OID_GEN_MULTICAST_BYTES_RCV:
		DEBUG("%s: OID_GEN_MULTICAST_BYTES_RCV\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->multicast*1111;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_MULTICAST_FRAMES_RCV:
		DEBUG("%s: OID_GEN_MULTICAST_FRAMES_RCV\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->multicast;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_BROADCAST_BYTES_RCV:
		DEBUG("%s: OID_GEN_BROADCAST_BYTES_RCV\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->rx_packets/42*255;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_BROADCAST_FRAMES_RCV:
		DEBUG("%s: OID_GEN_BROADCAST_FRAMES_RCV\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->rx_packets/42;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_RCV_CRC_ERROR:
		DEBUG("%s: OID_GEN_RCV_CRC_ERROR\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr].
					stats->rx_crc_errors;
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	case OID_GEN_TRANSMIT_QUEUE_LENGTH:
		DEBUG("%s: OID_GEN_TRANSMIT_QUEUE_LENGTH\n", __FUNCTION__);
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	case OID_GEN_GET_TIME_CAPS:
		DEBUG("%s: OID_GEN_GET_TIME_CAPS\n", __FUNCTION__);
		break;
		
	case OID_GEN_GET_NETCARD_TIME:
		DEBUG("%s: OID_GEN_GET_NETCARD_TIME\n", __FUNCTION__);
		break;
		
	case OID_GEN_NETCARD_LOAD:
		DEBUG("%s: OID_GEN_NETCARD_LOAD\n", __FUNCTION__);
		break;
		
	case OID_GEN_DEVICE_PROFILE:
		DEBUG("%s: OID_GEN_DEVICE_PROFILE\n", __FUNCTION__);
		break;
		
	case OID_GEN_INIT_TIME_MS:
		DEBUG("%s: OID_GEN_INIT_TIME_MS\n", __FUNCTION__);
		break;
		
	case OID_GEN_RESET_COUNTS:
		DEBUG("%s: OID_GEN_RESET_COUNTS\n", __FUNCTION__);
		break;
		
	case OID_GEN_MEDIA_SENSE_COUNTS:
		DEBUG("%s: OID_GEN_MEDIA_SENSE_COUNTS\n", __FUNCTION__);
		break;
		
	case OID_GEN_FRIENDLY_NAME:
		DEBUG("%s: OID_GEN_FRIENDLY_NAME\n", __FUNCTION__);
		break;
		
	case OID_GEN_MINIPORT_INFO:
		DEBUG("%s: OID_GEN_MINIPORT_INFO\n", __FUNCTION__);
		break;
		
	case OID_GEN_RESET_VERIFY_PARAMETERS:
		DEBUG("%s: OID_GEN_RESET_VERIFY_PARAMETERS\n", __FUNCTION__);
		break;
		
	/* mandatory */
	case OID_802_3_PERMANENT_ADDRESS:
		DEBUG("%s: OID_802_3_PERMANENT_ADDRESS\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			length = ETH_ALEN;
			memcpy ((u8 *) resp + 24,
				rndis_per_dev_params [configNr].host_mac,
				length);
			retval = 0;
		} else {
			*((u32 *) resp + 6) = 0;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_802_3_CURRENT_ADDRESS:
		DEBUG("%s: OID_802_3_CURRENT_ADDRESS\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			length = ETH_ALEN;
			memcpy ((u8 *) resp + 24,
				rndis_per_dev_params [configNr].host_mac,
				length);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_802_3_MULTICAST_LIST:
		DEBUG("%s: OID_802_3_MULTICAST_LIST\n", __FUNCTION__);
		length = 4;
		/* Multicast base address only */
		*((u32 *) resp + 6) = 0xE0000000;
		retval = 0;
		break;
		
	/* mandatory */
	case OID_802_3_MAXIMUM_LIST_SIZE:
		DEBUG("%s: OID_802_3_MAXIMUM_LIST_SIZE\n", __FUNCTION__);
		 length = 4;
		/* Multicast base address only */
		*((u32 *) resp + 6) = 1;
		retval = 0;
		break;
		
	case OID_802_3_MAC_OPTIONS:
		DEBUG("%s: OID_802_3_MAC_OPTIONS\n", __FUNCTION__);
		break;
		
	/* mandatory */
	case OID_802_3_RCV_ERROR_ALIGNMENT:
		DEBUG("%s: OID_802_3_RCV_ERROR_ALIGNMENT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats)
		{
			length = 4;
			*((u32 *) resp + 6) = rndis_per_dev_params [configNr]
					.stats->rx_frame_errors;
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_802_3_XMIT_ONE_COLLISION:
		DEBUG("%s: OID_802_3_XMIT_ONE_COLLISION\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	/* mandatory */
	case OID_802_3_XMIT_MORE_COLLISIONS:
		DEBUG("%s: OID_802_3_XMIT_MORE_COLLISIONS\n", __FUNCTION__);
		length = 4;
		*((u32 *) resp + 6) = 0;
		retval = 0;
		break;
		
	case OID_802_3_XMIT_DEFERRED:
		DEBUG("%s: OID_802_3_XMIT_DEFERRED\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_MAX_COLLISIONS:
		DEBUG("%s: OID_802_3_XMIT_MAX_COLLISIONS\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_RCV_OVERRUN:
		DEBUG("%s: OID_802_3_RCV_OVERRUN\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_UNDERRUN:
		DEBUG("%s: OID_802_3_XMIT_UNDERRUN\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_HEARTBEAT_FAILURE:
		DEBUG("%s: OID_802_3_XMIT_HEARTBEAT_FAILURE\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_TIMES_CRS_LOST:
		DEBUG("%s: OID_802_3_XMIT_TIMES_CRS_LOST\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_LATE_COLLISIONS:
		DEBUG("%s: OID_802_3_XMIT_LATE_COLLISIONS\n", __FUNCTION__);
		/* TODO */
		break;		
		
	default: printk (KERN_ERR "%s: unknown OID 0x%08X\n", 
			 __FUNCTION__, OID);
	}
	
	resp->InformationBufferOffset = 16;
	resp->InformationBufferLength = length;
	resp->MessageLength = 24 + length;
	r->length = 24 + length;
	return retval;
}

static int gen_ndis_set_resp (u8 configNr, u32 OID, u8 *buf, u32 buf_len, 
			      rndis_resp_t *r)
{
	rndis_set_cmplt_type		*resp;
	int 				i, retval = -ENOTSUPP;
	struct rndis_config_parameter	*param;
	struct rndis_params		*params;
	u8 *cp;

	if (!r)
		return -ENOMEM;
	resp = (rndis_set_cmplt_type *) r->buf;
	if (!resp)
		return -ENOMEM;

	cp = (u8 *)resp;

	switch (OID) {
	case OID_GEN_CURRENT_PACKET_FILTER:
		params = &rndis_per_dev_params [configNr];
		retval = 0;

		/* FIXME use this NDIS_PACKET_TYPE_* bitflags to
		 * filter packets in hard_start_xmit()
		 * NDIS_PACKET_TYPE_x == CDC_PACKET_TYPE_x for x in:
		 *	PROMISCUOUS, DIRECTED,
		 *	MULTICAST, ALL_MULTICAST, BROADCAST
		 */
		params->filter = *(u32 *)buf;
		DEBUG("%s: OID_GEN_CURRENT_PACKET_FILTER %08x\n",
			__FUNCTION__, params->filter);

		/* this call has a significant side effect:  it's
		 * what makes the packet flow start and stop, like
		 * activating the CDC Ethernet altsetting.
		 */
		if (params->filter) {
			params->state = RNDIS_DATA_INITIALIZED;
			netif_carrier_on(params->dev);
			if (netif_running(params->dev))
				netif_wake_queue (params->dev);
		} else {
			params->state = RNDIS_INITIALIZED;
			netif_carrier_off (params->dev);
			netif_stop_queue (params->dev);
		}
		break;
		
	case OID_802_3_MULTICAST_LIST:
		/* I think we can ignore this */		
		DEBUG("%s: OID_802_3_MULTICAST_LIST\n", __FUNCTION__);
		retval = 0;
		break;
		
	case OID_GEN_RNDIS_CONFIG_PARAMETER:
		DEBUG("%s: OID_GEN_RNDIS_CONFIG_PARAMETER\n", __FUNCTION__);
		param = (struct rndis_config_parameter *) buf;
		if (param) {
			for (i = 0; i < param->ParameterNameLength; i++) {
				DEBUG ("%c", 
				       *(buf + param->ParameterNameOffset + i));
			}
			DEBUG ("\n");
		}
		
		retval = 0;
		break;
		
	default: printk (KERN_ERR "%s: unknown OID 0x%08X\n", 
			 __FUNCTION__, OID);
	}
	
	return retval;
}

/* 
 * Response Functions 
 */

static int rndis_init_response (int configNr, rndis_init_msg_type *buf)
{
	rndis_init_cmplt_type	*resp; 
	rndis_resp_t            *r;
	
	if (!rndis_per_dev_params [configNr].dev) return -ENOTSUPP;
	
	r = rndis_add_response (configNr, sizeof (rndis_init_cmplt_type));
	
	if (!r) return -ENOMEM;
	
	resp = (rndis_init_cmplt_type *) r->buf;
	
	if (!resp) return -ENOMEM;
	
	resp->MessageType = REMOTE_NDIS_INITIALIZE_CMPLT;
	resp->MessageLength = 52;
	resp->RequestID = buf->RequestID;
	resp->Status = RNDIS_STATUS_SUCCESS;
	resp->MajorVersion = RNDIS_MAJOR_VERSION;
	resp->MinorVersion = RNDIS_MINOR_VERSION;
	resp->DeviceFlags = RNDIS_DF_CONNECTIONLESS;
	resp->Medium = RNDIS_MEDIUM_802_3;
	resp->MaxPacketsPerTransfer = 1;
	resp->MaxTransferSize = rndis_per_dev_params [configNr].dev->mtu
		+ sizeof (struct ethhdr)
		+ sizeof (struct rndis_packet_msg_type)
		+ 22;
	resp->PacketAlignmentFactor = 0;
	resp->AFListOffset = 0;
	resp->AFListSize = 0;
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	
	return 0;
}

static int rndis_query_response (int configNr, rndis_query_msg_type *buf)
{
	rndis_query_cmplt_type *resp;
	rndis_resp_t            *r;
	
	DEBUG("%s: OID = %08X\n", __FUNCTION__, buf->OID);
	if (!rndis_per_dev_params [configNr].dev) return -ENOTSUPP;
	
	/* 
	 * we need more memory: 
	 * oid_supported_list is the largest answer 
	 */
	r = rndis_add_response (configNr, sizeof (oid_supported_list));
	
	if (!r) return -ENOMEM;
	resp = (rndis_query_cmplt_type *) r->buf;
	
	if (!resp) return -ENOMEM;
	
	resp->MessageType = REMOTE_NDIS_QUERY_CMPLT;
	resp->MessageLength = 24;
	resp->RequestID = buf->RequestID;
	
	if (gen_ndis_query_resp (configNr, buf->OID, r)) {
		/* OID not supported */
		resp->Status = RNDIS_STATUS_NOT_SUPPORTED;
		resp->InformationBufferLength = 0;
		resp->InformationBufferOffset = 0;
	} else
		resp->Status = RNDIS_STATUS_SUCCESS;
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	return 0;
}

static int rndis_set_response (int configNr, rndis_set_msg_type *buf)
{
	rndis_set_cmplt_type	*resp;
	rndis_resp_t		*r;
	int			i;
	
	r = rndis_add_response (configNr, sizeof (rndis_set_cmplt_type));
	
	if (!r) return -ENOMEM;
	resp = (rndis_set_cmplt_type *) r->buf;
	if (!resp) return -ENOMEM;
	
	DEBUG("%s: Length: %d\n", __FUNCTION__, buf->InformationBufferLength);
	DEBUG("%s: Offset: %d\n", __FUNCTION__, buf->InformationBufferOffset);
	DEBUG("%s: InfoBuffer: ", __FUNCTION__);
	
	for (i = 0; i < buf->InformationBufferLength; i++) {
		DEBUG ("%02x ", *(((u8 *) buf) + i + 12 +
		       buf->InformationBufferOffset));
	}
	
	DEBUG ("\n");
	
	resp->MessageType = REMOTE_NDIS_SET_CMPLT;
	resp->MessageLength = 16;
	resp->RequestID = buf->RequestID;
	if (gen_ndis_set_resp (configNr, buf->OID, 
			       ((u8 *) buf) + 28, 
			       buf->InformationBufferLength, r))
	    resp->Status = RNDIS_STATUS_NOT_SUPPORTED;
	else resp->Status = RNDIS_STATUS_SUCCESS;
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (rndis_per_dev_params [configNr].dev);
	
	return 0;
}

static int rndis_reset_response (int configNr, rndis_reset_msg_type *buf)
{
	rndis_reset_cmplt_type	*resp;
	rndis_resp_t		*r;
	
	r = rndis_add_response (configNr, sizeof (rndis_reset_cmplt_type));
	
	if (!r) return -ENOMEM;
	resp = (rndis_reset_cmplt_type *) r->buf;
	if (!resp) return -ENOMEM;
	
	resp->MessageType = REMOTE_NDIS_RESET_CMPLT;
	resp->MessageLength = 16;
	resp->Status = RNDIS_STATUS_SUCCESS;
	resp->AddressingReset = 1; /* resent information */
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);

	return 0;
}

static int rndis_keepalive_response (int configNr,
				     rndis_keepalive_msg_type *buf)
{
	rndis_keepalive_cmplt_type	*resp;
	rndis_resp_t			*r;

	/* host "should" check only in RNDIS_DATA_INITIALIZED state */

	r = rndis_add_response (configNr, sizeof (rndis_keepalive_cmplt_type));
	resp = (rndis_keepalive_cmplt_type *) r->buf;
	if (!resp) return -ENOMEM;
		
	resp->MessageType = REMOTE_NDIS_KEEPALIVE_CMPLT;
	resp->MessageLength = 16;
	resp->RequestID = buf->RequestID;
	resp->Status = RNDIS_STATUS_SUCCESS;
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	
	return 0;
}


/* 
 * Device to Host Comunication 
 */
static int rndis_indicate_status_msg (int configNr, u32 status)
{
	rndis_indicate_status_msg_type	*resp;	
	rndis_resp_t			*r;
	
	if (rndis_per_dev_params [configNr].state == RNDIS_UNINITIALIZED)
	    return -ENOTSUPP;
	
	r = rndis_add_response (configNr, 
				sizeof (rndis_indicate_status_msg_type));
	if (!r) return -ENOMEM;
	
	resp = (rndis_indicate_status_msg_type *) r->buf;
	if (!resp) return -ENOMEM;
	
	resp->MessageType = REMOTE_NDIS_INDICATE_STATUS_MSG;
	resp->MessageLength = 20;
	resp->Status = status;
	resp->StatusBufferLength = 0;
	resp->StatusBufferOffset = 0;
	
	if (rndis_per_dev_params [configNr].ack) 
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	return 0;
}

int rndis_signal_connect (int configNr)
{
	rndis_per_dev_params [configNr].media_state
			= NDIS_MEDIA_STATE_CONNECTED;
	return rndis_indicate_status_msg (configNr, 
					  RNDIS_STATUS_MEDIA_CONNECT);
}

int rndis_signal_disconnect (int configNr)
{
	rndis_per_dev_params [configNr].media_state
			= NDIS_MEDIA_STATE_DISCONNECTED;
	return rndis_indicate_status_msg (configNr,
					  RNDIS_STATUS_MEDIA_DISCONNECT);
}

void rndis_set_host_mac (int configNr, const u8 *addr)
{
	rndis_per_dev_params [configNr].host_mac = addr;
}

/* 
 * Message Parser 
 */
int rndis_msg_parser (u8 configNr, u8 *buf)
{
	u32 MsgType, MsgLength, *tmp;
	struct rndis_params		*params;
	
	if (!buf)
		return -ENOMEM;
	
	tmp = (u32 *) buf; 
	MsgType = *tmp;
	MsgLength = *(tmp + 1);
	
	if (configNr >= RNDIS_MAX_CONFIGS)
		return -ENOTSUPP;
	params = &rndis_per_dev_params [configNr];
	
	/* For USB: responses may take up to 10 seconds */
	switch (MsgType)
	{
	case REMOTE_NDIS_INITIALIZE_MSG:
		DEBUG(KERN_INFO "%s: REMOTE_NDIS_INITIALIZE_MSG\n", 
			__FUNCTION__ );
		params->state = RNDIS_INITIALIZED;
		return  rndis_init_response (configNr,
					     (rndis_init_msg_type *) buf);
		
	case REMOTE_NDIS_HALT_MSG:
		DEBUG(KERN_INFO "%s: REMOTE_NDIS_HALT_MSG\n",
			__FUNCTION__ );
		params->state = RNDIS_UNINITIALIZED;
		if (params->dev) {
			netif_carrier_off (params->dev);
			netif_stop_queue (params->dev);
		}
		return 0;
		
	case REMOTE_NDIS_QUERY_MSG:
		DEBUG(KERN_INFO "%s: REMOTE_NDIS_QUERY_MSG\n", 
			__FUNCTION__ );
		return rndis_query_response (configNr, 
					     (rndis_query_msg_type *) buf);
		
	case REMOTE_NDIS_SET_MSG:
		DEBUG(KERN_INFO "%s: REMOTE_NDIS_SET_MSG\n", 
			__FUNCTION__ );
		return rndis_set_response (configNr, 
					   (rndis_set_msg_type *) buf);
		
	case REMOTE_NDIS_RESET_MSG:
		DEBUG(KERN_INFO "%s: REMOTE_NDIS_RESET_MSG\n", 
			__FUNCTION__ );
		return rndis_reset_response (configNr,
					     (rndis_reset_msg_type *) buf);

	case REMOTE_NDIS_KEEPALIVE_MSG:
		/* For USB: host does this every 5 seconds */
		DEBUG(KERN_INFO "%s: REMOTE_NDIS_KEEPALIVE_MSG\n", 
			__FUNCTION__ );
		return rndis_keepalive_response (configNr,
						 (rndis_keepalive_msg_type *) 
						 buf);
		
	default: 
		printk (KERN_ERR "%s: unknown RNDIS Message Type 0x%08X\n", 
			__FUNCTION__ , MsgType);
		break;
	}
	
	return -ENOTSUPP;
}

int rndis_register (int (* rndis_control_ack) (struct net_device *))
{
	u8 i;
	DEBUG("%s: ", __FUNCTION__ );
	
	for (i = 0; i < RNDIS_MAX_CONFIGS; i++) {
		if (!rndis_per_dev_params [i].used) {
			rndis_per_dev_params [i].used = 1;
			rndis_per_dev_params [i].ack = rndis_control_ack;
			DEBUG("configNr = %d\n", i);
			return i;
		}
	}
	DEBUG("failed\n");
	
	return -1;
}

void rndis_deregister (int configNr)
{
	DEBUG("%s: \n", __FUNCTION__ );
	
	if (configNr >= RNDIS_MAX_CONFIGS) return;
	rndis_per_dev_params [configNr].used = 0;
	
	return;
}

int rndis_set_param_dev (u8 configNr, struct net_device *dev, 
			 struct net_device_stats *stats)
{
	DEBUG("%s:\n", __FUNCTION__ );
	if (!dev || !stats) return -1;
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;
	
	rndis_per_dev_params [configNr].dev = dev;
	rndis_per_dev_params [configNr].stats = stats;
	
	return 0;
}

int rndis_set_param_vendor (u8 configNr, u32 vendorID, const char *vendorDescr)
{
	DEBUG("%s:\n", __FUNCTION__ );
	if (!vendorDescr) return -1;
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;
	
	rndis_per_dev_params [configNr].vendorID = vendorID;
	rndis_per_dev_params [configNr].vendorDescr = vendorDescr;
	
	return 0;
}

int rndis_set_param_medium (u8 configNr, u32 medium, u32 speed)
{
	DEBUG("%s:\n", __FUNCTION__ );
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;
	
	rndis_per_dev_params [configNr].medium = medium;
	rndis_per_dev_params [configNr].speed = speed;
	
	return 0;
}

void rndis_add_hdr (struct sk_buff *skb)
{
	if (!skb) return;
	skb_push (skb, sizeof (struct rndis_packet_msg_type));
	memset (skb->data, 0, sizeof (struct rndis_packet_msg_type));
	*((u32 *) skb->data) = 1;
	*((u32 *) skb->data + 1) = skb->len;
	*((u32 *) skb->data + 2) = 36;
	*((u32 *) skb->data + 3) = skb->len - 44;
	
	return;
}

void rndis_free_response (int configNr, u8 *buf)
{
	rndis_resp_t		*r;
	struct list_head	*act, *tmp;
	
	list_for_each_safe (act, tmp, 
			    &(rndis_per_dev_params [configNr].resp_queue))
	{
		r = list_entry (act, rndis_resp_t, list);
		if (r && r->buf == buf) {
			list_del (&r->list);
			kfree (r);
		}
	}
}

u8 *rndis_get_next_response (int configNr, u32 *length)
{
	rndis_resp_t		*r;
	struct list_head 	*act, *tmp;
	
	if (!length) return NULL;
	
	list_for_each_safe (act, tmp, 
			    &(rndis_per_dev_params [configNr].resp_queue))
	{
		r = list_entry (act, rndis_resp_t, list);
		if (!r->send) {
			r->send = 1;
			*length = r->length;
			return r->buf;
		}
	}
	
	return NULL;
}

static rndis_resp_t *rndis_add_response (int configNr, u32 length)
{
	rndis_resp_t	*r;
	
	r = kmalloc (sizeof (rndis_resp_t) + length, GFP_ATOMIC);
	if (!r) return NULL;
	
	r->buf = (u8 *) (r + 1);
	r->length = length;
	r->send = 0;
	
	list_add_tail (&r->list, 
		       &(rndis_per_dev_params [configNr].resp_queue));
	return r;
}

int rndis_rm_hdr (u8 *buf, u32 *length)
{
	u32 i, messageLen, dataOffset;
	
	if (!buf || !length) return -1;
	if (*((u32 *) buf) != 1) return -1;
	
	messageLen = *((u32 *) buf + 1);
	
	dataOffset = *((u32 *) buf + 2) + 8;
	if (messageLen < dataOffset || messageLen > *length) return -1;
	
	for (i = dataOffset; i < messageLen; i++)
		buf [i - dataOffset] = buf [i];
		
	*length = messageLen - dataOffset;
	
	return 0;
}

int rndis_proc_read (char *page, char **start, off_t off, int count, int *eof, 
		     void *data)
{
	char *out = page;
	int len;
	rndis_params *param = (rndis_params *) data;
	
	out += snprintf (out, count, 
			 "Config Nr. %d\n"
			 "used      : %s\n"
			 "state     : %s\n"
			 "medium    : 0x%08X\n"
			 "speed     : %d\n"
			 "cable     : %s\n"
			 "vendor ID : 0x%08X\n"
			 "vendor    : %s\n", 
			 param->confignr, (param->used) ? "y" : "n", 
			 ({ char *s = "?";
			 switch (param->state) {
			 case RNDIS_UNINITIALIZED:
				s = "RNDIS_UNINITIALIZED"; break;
			 case RNDIS_INITIALIZED:
				s = "RNDIS_INITIALIZED"; break;
			 case RNDIS_DATA_INITIALIZED:
				s = "RNDIS_DATA_INITIALIZED"; break;
			}; s; }),
			 param->medium, 
			 (param->media_state) ? 0 : param->speed*100, 
			 (param->media_state) ? "disconnected" : "connected",
			 param->vendorID, param->vendorDescr);      
	
	len = out - page;
	len -= off;
	
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;
	
	*start = page + off;
	return len;
}

int rndis_proc_write (struct file *file, const char *buffer, 
		      unsigned long count, void *data)
{
	u32 speed = 0;
	int i, fl_speed = 0;
	
	for (i = 0; i < count; i++) {
		switch (*buffer) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			fl_speed = 1;
			speed = speed*10 + *buffer - '0';
			break;
		case 'C':
		case 'c':
			rndis_signal_connect (((rndis_params *) data)
				->confignr);
			break;
		case 'D':
		case 'd':
			rndis_signal_disconnect (((rndis_params *) data)
				->confignr);
			break;
		default: 
			if (fl_speed) ((rndis_params *) data)->speed = speed;
			else DEBUG ("%c is not valid\n", *buffer);
			break;
		}
		
		buffer++;
	}
	
	return count;
}

int __init rndis_init (void)
{
	u8 i;
	char name [4];

	/* FIXME this should probably be /proc/driver/rndis,
	 * and only if debugging is enabled
	 */
	
	if (!(rndis_connect_dir =  proc_mkdir ("rndis", NULL))) {
		printk (KERN_ERR "%s: couldn't create /proc/rndis entry", 
			__FUNCTION__);
		return -EIO;
	}
	
	for (i = 0; i < RNDIS_MAX_CONFIGS; i++) {
		sprintf (name, "%03d", i);
		if (!(rndis_connect_state [i]
				= create_proc_entry (name, 0660,
						rndis_connect_dir))) 
		{
			DEBUG ("%s :remove entries", __FUNCTION__);
			for (i--; i > 0; i--) {
				sprintf (name, "%03d", i);
				remove_proc_entry (name, rndis_connect_dir);
			}
			DEBUG ("\n");
			
			remove_proc_entry ("000", rndis_connect_dir);
			remove_proc_entry ("rndis", NULL);
			return -EIO;
		}
		rndis_connect_state [i]->nlink = 1;
		rndis_connect_state [i]->write_proc = rndis_proc_write;
		rndis_connect_state [i]->read_proc = rndis_proc_read;
		rndis_connect_state [i]->data = (void *)
				(rndis_per_dev_params + i);
		rndis_per_dev_params [i].confignr = i;
		rndis_per_dev_params [i].used = 0;
		rndis_per_dev_params [i].state = RNDIS_UNINITIALIZED;
		rndis_per_dev_params [i].media_state
				= NDIS_MEDIA_STATE_DISCONNECTED;
		INIT_LIST_HEAD (&(rndis_per_dev_params [i].resp_queue));
	}
	
	return 0;
}

void rndis_exit (void)
{
	u8 i;
	char name [4];
	
	for (i = 0; i < RNDIS_MAX_CONFIGS; i++) {
		sprintf (name, "%03d", i);
		remove_proc_entry (name, rndis_connect_dir);
	}
	remove_proc_entry ("rndis", NULL);
	return;
}

