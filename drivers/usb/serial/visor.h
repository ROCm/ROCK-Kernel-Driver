/*
 * USB HandSpring Visor driver
 *
 *	Copyright (C) 1999, 2000
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 */

#ifndef __LINUX_USB_SERIAL_VISOR_H
#define __LINUX_USB_SERIAL_VISOR_H


#define HANDSPRING_VENDOR_ID		0x082d
#define HANDSPRING_VISOR_ID		0x0100

/****************************************************************************
 * Handspring Visor Vendor specific request codes (bRequest values)
 * A big thank you to Handspring for providing the following information.
 * If anyone wants the original file where these values and structures came
 * from, send email to <greg@kroah.com>.
 ****************************************************************************/

/****************************************************************************
 * VISOR_REQUEST_BYTES_AVAILABLE asks the visor for the number of bytes that
 * are available to be transfered to the host for the specified endpoint.
 * Currently this is not used, and always returns 0x0001
 ****************************************************************************/
#define VISOR_REQUEST_BYTES_AVAILABLE		0x01

/****************************************************************************
 * VISOR_CLOSE_NOTIFICATION is set to the device to notify it that the host
 * is now closing the pipe. An empty packet is sent in response.
 ****************************************************************************/
#define VISOR_CLOSE_NOTIFICATION		0x02

/****************************************************************************
 * VISOR_GET_CONNECTION_INFORMATION is sent by the host during enumeration to
 * get the endpoints used by the connection.
 ****************************************************************************/
#define VISOR_GET_CONNECTION_INFORMATION	0x03


/****************************************************************************
 * VISOR_GET_CONNECTION_INFORMATION returns data in the following format
 ****************************************************************************/
struct visor_connection_info {
	__u16	num_ports;
	struct {
		__u8	port_function_id;
		__u8	port;
	} connections[2];
};


/* struct visor_connection_info.connection[x].port defines: */
#define VISOR_ENDPOINT_1		0x01
#define VISOR_ENDPOINT_2		0x02

/* struct visor_connection_info.connection[x].port_function_id defines: */
#define VISOR_FUNCTION_GENERIC		0x00
#define VISOR_FUNCTION_DEBUGGER		0x01
#define VISOR_FUNCTION_HOTSYNC		0x02
#define VISOR_FUNCTION_CONSOLE		0x03
#define VISOR_FUNCTION_REMOTE_FILE_SYS	0x04

#endif

