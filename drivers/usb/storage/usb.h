/* Driver for USB Mass Storage compliant devices
 * Main Header File
 *
 * $Id: usb.h,v 1.21 2002/04/21 02:57:59 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _USB_H_
#define _USB_H_

#include <linux/usb.h>
#include <linux/blk.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/version.h>
#include "scsi.h"
#include "hosts.h"

struct us_data;

/*
 * Unusual device list definitions 
 */

struct us_unusual_dev {
	const char* vendorName;
	const char* productName;
	__u8  useProtocol;
	__u8  useTransport;
	int (*initFunction)(struct us_data *);
	unsigned int flags;
};

/* Flag definitions: these entries are static */
#define US_FL_SINGLE_LUN      0x00000001 /* allow access to only LUN 0	    */
#define US_FL_MODE_XLATE      0x00000002 /* translate _6 to _10 commands for
						    Win/MacOS compatibility */
#define US_FL_START_STOP      0x00000004 /* ignore START_STOP commands	    */
#define US_FL_IGNORE_SER      0x00000010 /* Ignore the serial number given  */
#define US_FL_SCM_MULT_TARG   0x00000020 /* supports multiple targets	    */
#define US_FL_FIX_INQUIRY     0x00000040 /* INQUIRY response needs fixing   */
#define US_FL_FIX_CAPACITY    0x00000080 /* READ CAPACITY response too big  */

/* Dynamic flag definitions: used in set_bit() etc. */
#define US_FLIDX_URB_ACTIVE	18  /* 0x00040000  current_urb is in use  */
#define US_FLIDX_SG_ACTIVE	19  /* 0x00080000  current_sg is in use   */
#define US_FLIDX_ABORTING	20  /* 0x00100000  abort is in progress   */
#define US_FLIDX_DISCONNECTING	21  /* 0x00200000  disconnect in progress */
#define DONT_SUBMIT	((1UL << US_FLIDX_ABORTING) | \
			 (1UL << US_FLIDX_DISCONNECTING))


/* processing state machine states */
#define US_STATE_IDLE		1
#define US_STATE_RUNNING	2
#define US_STATE_RESETTING	3
#define US_STATE_ABORTING	4

#define USB_STOR_STRING_LEN 32

typedef int (*trans_cmnd)(Scsi_Cmnd*, struct us_data*);
typedef int (*trans_reset)(struct us_data*);
typedef void (*proto_cmnd)(Scsi_Cmnd*, struct us_data*);
typedef void (*extra_data_destructor)(void *);	 /* extra data destructor   */

/* we allocate one of these for every device that we remember */
struct us_data {
	/* The device we're working with
	 * It's important to note:
	 *    (o) you must hold dev_semaphore to change pusb_dev
	 */
	struct semaphore	dev_semaphore;	 /* protect pusb_dev */
	struct usb_device	*pusb_dev;	 /* this usb_device */
	unsigned long		flags;		 /* from filter initially */
	unsigned int		send_bulk_pipe;	 /* cached pipe values */
	unsigned int		recv_bulk_pipe;
	unsigned int		send_ctrl_pipe;
	unsigned int		recv_ctrl_pipe;
	unsigned int		recv_intr_pipe;

	/* information about the device -- always good */
	char			vendor[USB_STOR_STRING_LEN];
	char			product[USB_STOR_STRING_LEN];
	char			serial[USB_STOR_STRING_LEN];
	char			*transport_name;
	char			*protocol_name;
	u8			subclass;
	u8			protocol;
	u8			max_lun;

	/* information about the device -- only good if device is attached */
	u8			ifnum;		 /* interface number   */
	u8			ep_in;		 /* bulk in endpoint   */
	u8			ep_out;		 /* bulk out endpoint  */
	u8			ep_int;		 /* interrupt endpoint */ 
	u8			ep_bInterval;	 /* interrupt interval */ 

	/* function pointers for this device */
	trans_cmnd		transport;	 /* transport function	   */
	trans_reset		transport_reset; /* transport device reset */
	proto_cmnd		proto_handler;	 /* protocol handler	   */

	/* SCSI interfaces */
	struct Scsi_Host	*host;		 /* our dummy host data */
	Scsi_Cmnd		*srb;		 /* current srb		*/

	/* thread information */
	int			pid;		 /* control thread	 */
	atomic_t		sm_state;	 /* what we are doing	 */

	/* interrupt communications data */
	unsigned char		irqdata[2];	 /* data from USB IRQ	 */

	/* control and bulk communications data */
	struct urb		*current_urb;	 /* non-int USB requests */
	struct usb_ctrlrequest	*dr;		 /* control requests	 */
	struct usb_sg_request	*current_sg;	 /* scatter-gather USB   */

	/* the semaphore for sleeping the control thread */
	struct semaphore	sema;		 /* to sleep thread on   */

	/* mutual exclusion structures */
	struct completion	notify;		 /* thread begin/end	    */
	struct us_unusual_dev   *unusual_dev;	 /* If unusual device       */
	void			*extra;		 /* Any extra data          */
	extra_data_destructor	extra_destructor;/* extra data destructor   */
};

/* The structure which defines our driver */
extern struct usb_driver usb_storage_driver;

/* Function to fill an inquiry response. See usb.c for details */
extern void fill_inquiry_response(struct us_data *us,
	unsigned char *data, unsigned int data_len);

/* The scsi_lock() and scsi_unlock() macros protect the sm_state and the
 * single queue element srb for write access */
#define scsi_unlock(host)	spin_unlock_irq(host->host_lock)
#define scsi_lock(host)		spin_lock_irq(host->host_lock)
#define sg_address(psg)		(page_address((psg).page) + (psg).offset)

#endif
