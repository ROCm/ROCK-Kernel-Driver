#ifndef __LINUX_HUB_H
#define __LINUX_HUB_H

#include <linux/list.h>

/*
 * Hub request types
 */

#define USB_RT_HUB	(USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define USB_RT_PORT	(USB_TYPE_CLASS | USB_RECIP_OTHER)

/*
 * Hub Class feature numbers
 */
#define C_HUB_LOCAL_POWER	0
#define C_HUB_OVER_CURRENT	1

/*
 * Port feature numbers
 */
#define USB_PORT_FEAT_CONNECTION	0
#define USB_PORT_FEAT_ENABLE		1
#define USB_PORT_FEAT_SUSPEND		2
#define USB_PORT_FEAT_OVER_CURRENT	3
#define USB_PORT_FEAT_RESET		4
#define USB_PORT_FEAT_POWER		8
#define USB_PORT_FEAT_LOWSPEED		9
#define USB_PORT_FEAT_C_CONNECTION	16
#define USB_PORT_FEAT_C_ENABLE		17
#define USB_PORT_FEAT_C_SUSPEND		18
#define USB_PORT_FEAT_C_OVER_CURRENT	19
#define USB_PORT_FEAT_C_RESET		20

struct usb_port_status {
	__u16 wPortStatus;
	__u16 wPortChange;	
} __attribute__ ((packed));

/* wPortStatus bits */
#define USB_PORT_STAT_CONNECTION	0x0001
#define USB_PORT_STAT_ENABLE		0x0002
#define USB_PORT_STAT_SUSPEND		0x0004
#define USB_PORT_STAT_OVERCURRENT	0x0008
#define USB_PORT_STAT_RESET		0x0010
#define USB_PORT_STAT_POWER		0x0100
#define USB_PORT_STAT_LOW_SPEED		0x0200

/* wPortChange bits */
#define USB_PORT_STAT_C_CONNECTION	0x0001
#define USB_PORT_STAT_C_ENABLE		0x0002
#define USB_PORT_STAT_C_SUSPEND		0x0004
#define USB_PORT_STAT_C_OVERCURRENT	0x0008
#define USB_PORT_STAT_C_RESET		0x0010

/* wHubCharacteristics (masks) */
#define HUB_CHAR_LPSM		0x0003
#define HUB_CHAR_COMPOUND	0x0004
#define HUB_CHAR_OCPM		0x0018

struct usb_hub_status {
	__u16 wHubStatus;
	__u16 wHubChange;
} __attribute__ ((packed));

/*
 *Hub Status & Hub Change bit masks
 */
#define HUB_STATUS_LOCAL_POWER	0x0001
#define HUB_STATUS_OVERCURRENT	0x0002

#define HUB_CHANGE_LOCAL_POWER	0x0001
#define HUB_CHANGE_OVERCURRENT	0x0002

#define HUB_DESCRIPTOR_MAX_SIZE	39	/* enough for 127 ports on a hub */

/* Hub descriptor */
struct usb_hub_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bNbrPorts;
	__u16 wHubCharacteristics;
	__u8  bPwrOn2PwrGood;
	__u8  bHubContrCurrent;

	/* DeviceRemovable and PortPwrCtrlMask want to be variable-length 
	   bitmaps that hold max 256 entries, but for now they're ignored */
	__u8  bitmap[0];
} __attribute__ ((packed));

struct usb_device;

struct usb_hub {
	struct usb_device *dev;

	struct urb *urb;		/* Interrupt polling pipe */

	char buffer[(USB_MAXCHILDREN + 1 + 7) / 8]; /* add 1 bit for hub status change */
					/* and add 7 bits to round up to byte boundary */
	int error;
	int nerrors;

	struct list_head hub_list;

	struct list_head event_list;

	/* Number of ports on the hub */
	int nports;

	struct usb_hub_descriptor *descriptor;
};

#endif
