 /*
 * Driver for Microtek Scanmaker X6 USB scanner and possibly others.
 * 
 * (C) Copyright 2000 John Fremlin <vii@penguinpowered.com>
 * (C) Copyright 2000 Oliver Neukum <Oliver.Neukum@lrz.uni-muenchen.de>
 *
 * See microtek.c for history
 *
 */

typedef void (*mts_scsi_cmnd_callback)(Scsi_Cmnd *);
typedef void (*mts_usb_urb_callback) (struct urb *);


struct mts_transfer_context
{
	struct mts_desc* instance;
	mts_scsi_cmnd_callback final_callback;
	Scsi_Cmnd *srb;
	
	void* data;
	unsigned data_length;
	int data_pipe;

	enum {
		mts_con_none,
		mts_con_command,
		mts_con_data,
		mts_con_status,
		mts_con_error,
		mts_con_done
	}
	state;

	atomic_t do_abort; /* when != 0 URB completion routines will
			      return straightaway */
	
	u8 status; /* status returned from ep_response after command completion */
};


struct mts_desc {
	struct mts_desc *next;
	struct mts_desc *prev;

	struct usb_device *usb_dev;
	
	int interface;

	/* Endpoint addresses */
	u8 ep_out;
	u8 ep_response;
	u8 ep_image;
	
	struct Scsi_Host * host;
	Scsi_Host_Template ctempl;
	int host_number;
	
	struct semaphore lock;

	struct urb urb;
	struct mts_transfer_context context;
};


#define MTS_EP_OUT	0x1
#define MTS_EP_RESPONSE	0x2
#define MTS_EP_IMAGE	0x3
#define MTS_EP_TOTAL	0x3

#define MTS_SCSI_ERR_MASK ~0x3fu 

