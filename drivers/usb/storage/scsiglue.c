/* Driver for USB Mass Storage compliant devices
 * SCSI layer glue code
 *
 * $Id: scsiglue.c,v 1.26 2002/04/22 03:39:43 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *   (c) 2000 Stephen J. Gowdy (SGowdy@lbl.gov)
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
#include "scsiglue.h"
#include "usb.h"
#include "debug.h"
#include "transport.h"

#include <linux/slab.h>
#include <linux/module.h>


/***********************************************************************
 * Host functions 
 ***********************************************************************/

static const char* host_info(struct Scsi_Host *host)
{
	return "SCSI emulation for USB Mass Storage devices";
}

static int slave_configure (struct scsi_device *sdev)
{
	/* set device to use 10-byte commands where possible */
	sdev->use_10_for_ms = 1;
	sdev->use_10_for_rw = 1;

	/* this is to satisify the compiler, tho I don't think the 
	 * return code is ever checked anywhere. */
	return 0;
}

/* queue a command */
/* This is always called with scsi_lock(srb->host) held */
static int queuecommand( Scsi_Cmnd *srb , void (*done)(Scsi_Cmnd *))
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];

	US_DEBUGP("%s called\n", __FUNCTION__);
	srb->host_scribble = (unsigned char *)us;

	/* enqueue the command */
	if (us->sm_state != US_STATE_IDLE || us->srb != NULL) {
		printk(KERN_ERR USB_STORAGE "Error in %s: " 
			"state = %d, us->srb = %p\n",
			__FUNCTION__, us->sm_state, us->srb);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	srb->scsi_done = done;
	us->srb = srb;

	/* wake up the process task */
	up(&(us->sema));

	return 0;
}

/***********************************************************************
 * Error handling functions
 ***********************************************************************/

/* Command abort */
/* This is always called with scsi_lock(srb->host) held */
static int command_abort( Scsi_Cmnd *srb )
{
	struct Scsi_Host *host = srb->device->host;
	struct us_data *us = (struct us_data *) host->hostdata[0];

	US_DEBUGP("%s called\n", __FUNCTION__);

	/* Is this command still active? */
	if (us->srb != srb) {
		US_DEBUGP ("-- nothing to abort\n");
		return FAILED;
	}

	/* Normally the current state is RUNNING.  If the control thread
	 * hasn't even started processing this command, the state will be
	 * IDLE.  Anything else is a bug. */
	if (us->sm_state != US_STATE_RUNNING
				&& us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* Set state to ABORTING, set the ABORTING bit, and release the lock */
	us->sm_state = US_STATE_ABORTING;
	set_bit(US_FLIDX_ABORTING, &us->flags);
	scsi_unlock(host);

	/* Stop an ongoing USB transfer */
	usb_stor_stop_transport(us);

	/* Wait for the aborted command to finish */
	wait_for_completion(&us->notify);

	/* Reacquire the lock and allow USB transfers to resume */
	scsi_lock(host);
	clear_bit(US_FLIDX_ABORTING, &us->flags);
	return SUCCESS;
}

/* This invokes the transport reset mechanism to reset the state of the
 * device */
/* This is always called with scsi_lock(srb->host) held */
static int device_reset( Scsi_Cmnd *srb )
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];
	int result;

	US_DEBUGP("%s called\n", __FUNCTION__);
	if (us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* set the state and release the lock */
	us->sm_state = US_STATE_RESETTING;
	scsi_unlock(srb->device->host);

	/* lock the device pointers and do the reset */
	down(&(us->dev_semaphore));
	if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
		result = FAILED;
		US_DEBUGP("No reset during disconnect\n");
	} else
		result = us->transport_reset(us);
	up(&(us->dev_semaphore));

	/* lock access to the state and clear it */
	scsi_lock(srb->device->host);
	us->sm_state = US_STATE_IDLE;
	return result;
}

/* This resets the device's USB port. */
/* It refuses to work if there's more than one interface in
 * the device, so that other users are not affected. */
/* This is always called with scsi_lock(srb->host) held */
static int bus_reset( Scsi_Cmnd *srb )
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];
	int result;

	US_DEBUGP("%s called\n", __FUNCTION__);
	if (us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* set the state and release the lock */
	us->sm_state = US_STATE_RESETTING;
	scsi_unlock(srb->device->host);

	/* The USB subsystem doesn't handle synchronisation between
	 * a device's several drivers. Therefore we reset only devices
	 * with just one interface, which we of course own. */

	down(&(us->dev_semaphore));
	if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
		result = -EIO;
		US_DEBUGP("No reset during disconnect\n");
	} else if (us->pusb_dev->actconfig->desc.bNumInterfaces != 1) {
		result = -EBUSY;
		US_DEBUGP("Refusing to reset a multi-interface device\n");
	} else {
		result = usb_reset_device(us->pusb_dev);
		US_DEBUGP("usb_reset_device returns %d\n", result);
	}
	up(&(us->dev_semaphore));

	/* lock access to the state and clear it */
	scsi_lock(srb->device->host);
	us->sm_state = US_STATE_IDLE;
	return result < 0 ? FAILED : SUCCESS;
}

/***********************************************************************
 * /proc/scsi/ functions
 ***********************************************************************/

/* we use this macro to help us write into the buffer */
#undef SPRINTF
#define SPRINTF(args...) \
	do { if (pos < buffer+length) pos += sprintf(pos, ## args); } while (0)

static int proc_info (struct Scsi_Host *hostptr, char *buffer, char **start, off_t offset,
		int length, int inout)
{
	struct us_data *us;
	char *pos = buffer;
	unsigned long f;

	/* if someone is sending us data, just throw it away */
	if (inout)
		return length;

	us = (struct us_data*)hostptr->hostdata[0];

	/* print the controller name */
	SPRINTF("   Host scsi%d: usb-storage\n", hostptr->host_no);

	/* print product, vendor, and serial number strings */
	SPRINTF("       Vendor: %s\n", us->vendor);
	SPRINTF("      Product: %s\n", us->product);
	SPRINTF("Serial Number: %s\n", us->serial);

	/* show the protocol and transport */
	SPRINTF("     Protocol: %s\n", us->protocol_name);
	SPRINTF("    Transport: %s\n", us->transport_name);

	/* show the device flags */
	if (pos < buffer + length) {
		pos += sprintf(pos, "       Quirks:");
		f = us->flags;

#define DO_FLAG(a)  	if (f & US_FL_##a)  pos += sprintf(pos, " " #a)
		DO_FLAG(SINGLE_LUN);
		DO_FLAG(MODE_XLATE);
		DO_FLAG(SCM_MULT_TARG);
		DO_FLAG(FIX_INQUIRY);
		DO_FLAG(FIX_CAPACITY);
#undef DO_FLAG

		*(pos++) = '\n';
		}

	/*
	 * Calculate start of next buffer, and return value.
	 */
	*start = buffer + offset;

	if ((pos - buffer) < offset)
		return (0);
	else if ((pos - buffer - offset) < length)
		return (pos - buffer - offset);
	else
		return (length);
}

/*
 * this defines our host template, with which we'll allocate hosts
 */

struct scsi_host_template usb_stor_host_template = {
	/* basic userland interface stuff */
	.name =				"usb-storage",
	.proc_name =			"usb-storage",
	.proc_info =			proc_info,
	.info =				host_info,

	/* command interface -- queued only */
	.queuecommand =			queuecommand,

	/* error and abort handlers */
	.eh_abort_handler =		command_abort,
	.eh_device_reset_handler =	device_reset,
	.eh_bus_reset_handler =		bus_reset,

	/* queue commands only, only one command per LUN */
	.can_queue =			1,
	.cmd_per_lun =			1,

	/* unknown initiator id */
	.this_id =			-1,

	.slave_configure =		slave_configure,

	/* lots of sg segments can be handled */
	.sg_tablesize =			SG_ALL,

	/* merge commands... this seems to help performance, but
	 * periodically someone should test to see which setting is more
	 * optimal.
	 */
	.use_clustering =		TRUE,

	/* emulated HBA */
	.emulated =			TRUE,

	/* module management */
	.module =			THIS_MODULE
};

/* For a device that is "Not Ready" */
unsigned char usb_stor_sense_notready[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= 0x02,			    /* not ready */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x04,			    /* not ready */
	[13]	= 0x03			    /* manual intervention */
};

/* To Report "Illegal Request: Invalid Field in CDB */
unsigned char usb_stor_sense_invalidCDB[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= ILLEGAL_REQUEST,	    /* Illegal Request = 0x05 */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x24			    /* Invalid Field in CDB */
};

#define USB_STOR_SCSI_SENSE_HDRSZ 4
#define USB_STOR_SCSI_SENSE_10_HDRSZ 8

struct usb_stor_scsi_sense_hdr
{
  __u8* dataLength;
  __u8* mediumType;
  __u8* devSpecParms;
  __u8* blkDescLength;
};

typedef struct usb_stor_scsi_sense_hdr Usb_Stor_Scsi_Sense_Hdr;

union usb_stor_scsi_sense_hdr_u
{
  Usb_Stor_Scsi_Sense_Hdr hdr;
  __u8* array[USB_STOR_SCSI_SENSE_HDRSZ];
};

typedef union usb_stor_scsi_sense_hdr_u Usb_Stor_Scsi_Sense_Hdr_u;

struct usb_stor_scsi_sense_hdr_10
{
  __u8* dataLengthMSB;
  __u8* dataLengthLSB;
  __u8* mediumType;
  __u8* devSpecParms;
  __u8* reserved1;
  __u8* reserved2;
  __u8* blkDescLengthMSB;
  __u8* blkDescLengthLSB;
};

typedef struct usb_stor_scsi_sense_hdr_10 Usb_Stor_Scsi_Sense_Hdr_10;

union usb_stor_scsi_sense_hdr_10_u
{
  Usb_Stor_Scsi_Sense_Hdr_10 hdr;
  __u8* array[USB_STOR_SCSI_SENSE_10_HDRSZ];
};

typedef union usb_stor_scsi_sense_hdr_10_u Usb_Stor_Scsi_Sense_Hdr_10_u;

void usb_stor_scsiSenseParseBuffer( Scsi_Cmnd* , Usb_Stor_Scsi_Sense_Hdr_u*,
				    Usb_Stor_Scsi_Sense_Hdr_10_u*, int* );

int usb_stor_scsiSense10to6( Scsi_Cmnd* the10 )
{
  __u8 *buffer=0;
  int outputBufferSize = 0;
  int length=0;
  struct scatterlist *sg = 0;
  int i=0, j=0, element=0;
  Usb_Stor_Scsi_Sense_Hdr_u the6Locations;
  Usb_Stor_Scsi_Sense_Hdr_10_u the10Locations;
  int sb=0,si=0,db=0,di=0;
  int sgLength=0;

  US_DEBUGP("-- converting 10 byte sense data to 6 byte\n");
  the10->cmnd[0] = the10->cmnd[0] & 0xBF;

  /* Determine buffer locations */
  usb_stor_scsiSenseParseBuffer( the10, &the6Locations, &the10Locations,
				 &length );

  /* Work out minimum buffer to output */
  outputBufferSize = *the10Locations.hdr.dataLengthLSB;
  outputBufferSize += USB_STOR_SCSI_SENSE_HDRSZ;

  /* Check to see if we need to trucate the output */
  if ( outputBufferSize > length )
    {
      printk( KERN_WARNING USB_STORAGE 
	      "Had to truncate MODE_SENSE_10 buffer into MODE_SENSE.\n" );
      printk( KERN_WARNING USB_STORAGE
	      "outputBufferSize is %d and length is %d.\n",
	      outputBufferSize, length );
    }
  outputBufferSize = length;

  /* Data length */
  if ( *the10Locations.hdr.dataLengthMSB != 0 ) /* MSB must be zero */
    {
      printk( KERN_WARNING USB_STORAGE 
	      "Command will be truncated to fit in SENSE6 buffer.\n" );
      *the6Locations.hdr.dataLength = 0xff;
    }
  else
    {
      *the6Locations.hdr.dataLength = *the10Locations.hdr.dataLengthLSB;
    }

  /* Medium type and DevSpecific parms */
  *the6Locations.hdr.mediumType = *the10Locations.hdr.mediumType;
  *the6Locations.hdr.devSpecParms = *the10Locations.hdr.devSpecParms;

  /* Block descriptor length */
  if ( *the10Locations.hdr.blkDescLengthMSB != 0 ) /* MSB must be zero */
    {
      printk( KERN_WARNING USB_STORAGE 
	      "Command will be truncated to fit in SENSE6 buffer.\n" );
      *the6Locations.hdr.blkDescLength = 0xff;
    }
  else
    {
      *the6Locations.hdr.blkDescLength = *the10Locations.hdr.blkDescLengthLSB;
    }

  if ( the10->use_sg == 0 )
    {
      buffer = the10->request_buffer;
      /* Copy the rest of the data */
      memmove( &(buffer[USB_STOR_SCSI_SENSE_HDRSZ]),
	       &(buffer[USB_STOR_SCSI_SENSE_10_HDRSZ]),
	       outputBufferSize - USB_STOR_SCSI_SENSE_HDRSZ );
      /* initialise last bytes left in buffer due to smaller header */
      memset( &(buffer[outputBufferSize
	    -(USB_STOR_SCSI_SENSE_10_HDRSZ-USB_STOR_SCSI_SENSE_HDRSZ)]),
	      0,
	      USB_STOR_SCSI_SENSE_10_HDRSZ-USB_STOR_SCSI_SENSE_HDRSZ );
    }
  else
    {
      sg = (struct scatterlist *) the10->request_buffer;
      /* scan through this scatterlist and figure out starting positions */
      for ( i=0; i < the10->use_sg; i++)
	{
	  sgLength = sg[i].length;
	  for ( j=0; j<sgLength; j++ )
	    {
	      /* get to end of header */
	      if ( element == USB_STOR_SCSI_SENSE_HDRSZ )
		{
		  db=i;
		  di=j;
		}
	      if ( element == USB_STOR_SCSI_SENSE_10_HDRSZ )
		{
		  sb=i;
		  si=j;
		  /* we've found both sets now, exit loops */
		  j=sgLength;
		  i=the10->use_sg;
		}
	      element++;
	    }
	}

      /* Now we know where to start the copy from */
      element = USB_STOR_SCSI_SENSE_HDRSZ;
      while ( element < outputBufferSize
	      -(USB_STOR_SCSI_SENSE_10_HDRSZ-USB_STOR_SCSI_SENSE_HDRSZ) )
	{
	  /* check limits */
	  if ( sb >= the10->use_sg ||
	       si >= sg[sb].length ||
	       db >= the10->use_sg ||
	       di >= sg[db].length )
	    {
	      printk( KERN_ERR USB_STORAGE
		      "Buffer overrun averted, this shouldn't happen!\n" );
	      break;
	    }

	  /* copy one byte */
	  {
		char *src = sg_address(sg[sb]) + si;
		char *dst = sg_address(sg[db]) + di;

		 *dst = *src;
	  }

	  /* get next destination */
	  if ( sg[db].length-1 == di )
	    {
	      db++;
	      di=0;
	    }
	  else
	    {
	      di++;
	    }

	  /* get next source */
	  if ( sg[sb].length-1 == si )
	    {
	      sb++;
	      si=0;
	    }
	  else
	    {
	      si++;
	    }

	  element++;
	}
      /* zero the remaining bytes */
      while ( element < outputBufferSize )
	{
	  /* check limits */
	  if ( db >= the10->use_sg ||
	       di >= sg[db].length )
	    {
	      printk( KERN_ERR USB_STORAGE
		      "Buffer overrun averted, this shouldn't happen!\n" );
	      break;
	    }

	  *(char*)(sg_address(sg[db])) = 0;

	  /* get next destination */
	  if ( sg[db].length-1 == di )
	    {
	      db++;
	      di=0;
	    }
	  else
	    {
	      di++;
	    }
	  element++;
	}
    }

  /* All done any everything was fine */
  return 0;
}

int usb_stor_scsiSense6to10( Scsi_Cmnd* the6 )
{
  /* will be used to store part of buffer */  
  __u8 tempBuffer[USB_STOR_SCSI_SENSE_10_HDRSZ-USB_STOR_SCSI_SENSE_HDRSZ],
    *buffer=0;
  int outputBufferSize = 0;
  int length=0;
  struct scatterlist *sg = 0;
  int i=0, j=0, element=0;
  Usb_Stor_Scsi_Sense_Hdr_u the6Locations;
  Usb_Stor_Scsi_Sense_Hdr_10_u the10Locations;
  int sb=0,si=0,db=0,di=0;
  int lsb=0,lsi=0,ldb=0,ldi=0;

  US_DEBUGP("-- converting 6 byte sense data to 10 byte\n");
  the6->cmnd[0] = the6->cmnd[0] | 0x40;

  /* Determine buffer locations */
  usb_stor_scsiSenseParseBuffer( the6, &the6Locations, &the10Locations,
				 &length );

  /* Work out minimum buffer to output */
  outputBufferSize = *the6Locations.hdr.dataLength;
  outputBufferSize += USB_STOR_SCSI_SENSE_10_HDRSZ;

  /* Check to see if we need to trucate the output */
  if ( outputBufferSize > length )
    {
      printk( KERN_WARNING USB_STORAGE 
	      "Had to truncate MODE_SENSE into MODE_SENSE_10 buffer.\n" );
      printk( KERN_WARNING USB_STORAGE
	      "outputBufferSize is %d and length is %d.\n",
	      outputBufferSize, length );
    }
  outputBufferSize = length;

  /* Block descriptor length - save these before overwriting */
  tempBuffer[2] = *the10Locations.hdr.blkDescLengthMSB;
  tempBuffer[3] = *the10Locations.hdr.blkDescLengthLSB;
  *the10Locations.hdr.blkDescLengthLSB = *the6Locations.hdr.blkDescLength;
  *the10Locations.hdr.blkDescLengthMSB = 0;

  /* reserved - save these before overwriting */
  tempBuffer[0] = *the10Locations.hdr.reserved1;
  tempBuffer[1] = *the10Locations.hdr.reserved2;
  *the10Locations.hdr.reserved1 = *the10Locations.hdr.reserved2 = 0;

  /* Medium type and DevSpecific parms */
  *the10Locations.hdr.devSpecParms = *the6Locations.hdr.devSpecParms;
  *the10Locations.hdr.mediumType = *the6Locations.hdr.mediumType;

  /* Data length */
  *the10Locations.hdr.dataLengthLSB = *the6Locations.hdr.dataLength;
  *the10Locations.hdr.dataLengthMSB = 0;

  if ( !the6->use_sg )
    {
      buffer = the6->request_buffer;
      /* Copy the rest of the data */
      memmove( &(buffer[USB_STOR_SCSI_SENSE_10_HDRSZ]),
	      &(buffer[USB_STOR_SCSI_SENSE_HDRSZ]),
	      outputBufferSize-USB_STOR_SCSI_SENSE_10_HDRSZ );
      /* Put the first four bytes (after header) in place */
      memcpy( &(buffer[USB_STOR_SCSI_SENSE_10_HDRSZ]),
	      tempBuffer,
	      USB_STOR_SCSI_SENSE_10_HDRSZ-USB_STOR_SCSI_SENSE_HDRSZ );
    }
  else
    {
      sg = (struct scatterlist *) the6->request_buffer;
      /* scan through this scatterlist and figure out ending positions */
      for ( i=0; i < the6->use_sg; i++)
	{
	  for ( j=0; j<sg[i].length; j++ )
	    {
	      /* get to end of header */
	      if ( element == USB_STOR_SCSI_SENSE_HDRSZ )
		{
		  ldb=i;
		  ldi=j;
		}
	      if ( element == USB_STOR_SCSI_SENSE_10_HDRSZ )
		{
		  lsb=i;
		  lsi=j;
		  /* we've found both sets now, exit loops */
		  j=sg[i].length;
		  i=the6->use_sg;
		  break;
		}
	      element++;
	    }
	}
      /* scan through this scatterlist and figure out starting positions */
      element = length-1;
      /* destination is the last element */
      db=the6->use_sg-1;
      di=sg[db].length-1;
      for ( i=the6->use_sg-1; i >= 0; i--)
	{
	  for ( j=sg[i].length-1; j>=0; j-- )
	    {
	      /* get to end of header and find source for copy */
	      if ( element == length - 1
		   - (USB_STOR_SCSI_SENSE_10_HDRSZ-USB_STOR_SCSI_SENSE_HDRSZ) )
		{
		  sb=i;
		  si=j;
		  /* we've found both sets now, exit loops */
		  j=-1;
		  i=-1;
		}
	      element--;
	    }
	}
      /* Now we know where to start the copy from */
      element = length-1
	- (USB_STOR_SCSI_SENSE_10_HDRSZ-USB_STOR_SCSI_SENSE_HDRSZ);
      while ( element >= USB_STOR_SCSI_SENSE_10_HDRSZ )
	{
	  /* check limits */
	  if ( ( sb <= lsb && si < lsi ) ||
	       ( db <= ldb && di < ldi ) )
	    {
	      printk( KERN_ERR USB_STORAGE
		      "Buffer overrun averted, this shouldn't happen!\n" );
	      break;
	    }

	  /* copy one byte */
	  {
		char *src = sg_address(sg[sb]) + si;
		char *dst = sg_address(sg[db]) + di;

		 *dst = *src;
	  }

	  /* get next destination */
	  if ( di == 0 )
	    {
	      db--;
	      di=sg[db].length-1;
	    }
	  else
	    {
	      di--;
	    }

	  /* get next source */
	  if ( si == 0 )
	    {
	      sb--;
	      si=sg[sb].length-1;
	    }
	  else
	    {
	      si--;
	    }

	  element--;
	}
      /* copy the remaining four bytes */
      while ( element >= USB_STOR_SCSI_SENSE_HDRSZ )
	{
	  /* check limits */
	  if ( db <= ldb && di < ldi )
	    {
	      printk( KERN_ERR USB_STORAGE
		      "Buffer overrun averted, this shouldn't happen!\n" );
	      break;
	    }

	 {
		 char *dst = sg_address(sg[db]) + di;

		 *dst = tempBuffer[element-USB_STOR_SCSI_SENSE_HDRSZ];
	 }


	  /* get next destination */
	  if ( di == 0 )
	    {
	      db--;
	      di=sg[db].length-1;
	    }
	  else
	    {
	      di--;
	    }
	  element--;
	}
    }

  /* All done and everything was fine */
  return 0;
}

void usb_stor_scsiSenseParseBuffer( Scsi_Cmnd* srb, Usb_Stor_Scsi_Sense_Hdr_u* the6,
			       Usb_Stor_Scsi_Sense_Hdr_10_u* the10,
			       int* length_p )

{
  int i = 0, j=0, element=0;
  struct scatterlist *sg = 0;
  int length = 0;
  __u8* buffer=0;

  /* are we scatter-gathering? */
  if ( srb->use_sg != 0 )
    {
      /* loop over all the scatter gather structures and 
       * get pointer to the data members in the headers
       * (also work out the length while we're here)
       */
      sg = (struct scatterlist *) srb->request_buffer;
      for (i = 0; i < srb->use_sg; i++)
	{
	  length += sg[i].length;
	  /* We only do the inner loop for the headers */
	  if ( element < USB_STOR_SCSI_SENSE_10_HDRSZ )
	    {
	      /* scan through this scatterlist */
	      for ( j=0; j<sg[i].length; j++ )
		{
		  if ( element < USB_STOR_SCSI_SENSE_HDRSZ )
		    {
		      /* fill in the pointers for both header types */
		      the6->array[element] = sg_address(sg[i]) + j;
		      the10->array[element] = sg_address(sg[i]) + j;

		    }
		  else if ( element < USB_STOR_SCSI_SENSE_10_HDRSZ )
		    {
		      /* only the longer headers still cares now */
		      the10->array[element] = sg_address(sg[i]) + j;
		       
		    }
		  /* increase element counter */
		  element++;
		}
	    }
	}
    }
  else
    {
      length = srb->request_bufflen;
      buffer = srb->request_buffer;
      if ( length < USB_STOR_SCSI_SENSE_10_HDRSZ )
	printk( KERN_ERR USB_STORAGE
		"Buffer length smaller than header!!" );
      for( i=0; i<USB_STOR_SCSI_SENSE_10_HDRSZ; i++ )
	{
	  if ( i < USB_STOR_SCSI_SENSE_HDRSZ )
	    {
	      the6->array[i] = &(buffer[i]);
	      the10->array[i] = &(buffer[i]);
	    }
	  else
	    {
	      the10->array[i] = &(buffer[i]);
	    }
	}
    }

  /* Set value of length passed in */
  *length_p = length;
}

