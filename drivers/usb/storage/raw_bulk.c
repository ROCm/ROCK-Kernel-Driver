/*
 * Common routines for a handful of drivers.
 * Unrelated to CF/SM - just USB stuff.
 *
 * This is mostly a thin layer on top of transport.c.
 * It converts routines that return values like -EPIPE
 * into routines that return USB_STOR_TRANSPORT_ABORTED etc.
 *
 * There is also some debug printing here.
 */

#include "debug.h"
#include "transport.h"
#include "raw_bulk.h"

#ifdef CONFIG_USB_STORAGE_DEBUG
#define DEBUG_PRCT 12
#else
#define DEBUG_PRCT 0
#endif

/*
 * Send a control message and wait for the response.
 *
 * us - the pointer to the us_data structure for the device to use
 *
 * request - the URB Setup Packet's first 6 bytes. The first byte always
 *  corresponds to the request type, and the second byte always corresponds
 *  to the request.  The other 4 bytes do not correspond to value and index,
 *  since they are used in a custom way by the SCM protocol.
 *
 * xfer_data - a buffer from which to get, or to which to store, any data
 *  that gets send or received, respectively, with the URB. Even though
 *  it looks like we allocate a buffer in this code for the data, xfer_data
 *  must contain enough allocated space.
 *
 * xfer_len - the number of bytes to send or receive with the URB.
 *
 */

int
usb_storage_send_control(struct us_data *us,
			 int pipe,
			 unsigned char request,
			 unsigned char requesttype,
			 unsigned int value,
			 unsigned int index,
			 unsigned char *xfer_data,
			 unsigned int xfer_len) {

	int result;

	// Send the URB to the device and wait for a response.

	/* Why are request and request type reversed in this call? */

	result = usb_stor_control_msg(us, pipe,
			request, requesttype, value, index,
			xfer_data, xfer_len);

	/* did we abort this command? */
	if (atomic_read(&us->sm_state) == US_STATE_ABORTING) {
		US_DEBUGP("usb_stor_send_control(): transfer aborted\n");
		return USB_STOR_TRANSPORT_ABORTED;
	}

	// Check the return code for the command.
	if (result < 0) {

		/* a stall indicates a protocol error */
		if (result == -EPIPE) {
			US_DEBUGP("-- Stall on control pipe\n");
			return USB_STOR_TRANSPORT_ERROR;
		}

		/* Uh oh... serious problem here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

int
usb_storage_raw_bulk(struct us_data *us, int direction, unsigned char *data,
		     unsigned int len, unsigned int *act_len) {

	int result;
	int pipe;

	if (direction == SCSI_DATA_READ)
		pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
	else
		pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);

	result = usb_stor_bulk_msg(us, data, pipe, len, act_len);

	/* if we stall, we need to clear it before we go on */
	if (result == -EPIPE) {
		US_DEBUGP("EPIPE: clearing endpoint halt for"
			  " pipe 0x%x, stalled at %d bytes\n",
			  pipe, *act_len);
		if (usb_stor_clear_halt(us, pipe) < 0)
			return USB_STOR_XFER_ERROR;
		return USB_STOR_XFER_STALLED;
	}

	/* did we abort this command? */
	if (atomic_read(&us->sm_state) == US_STATE_ABORTING) {
		US_DEBUGP("usb_storage_raw_bulk(): transfer aborted\n");
		return USB_STOR_XFER_ABORTED;
	}

	if (result) {
		/* NAK - that means we've retried a few times already */
       		if (result == -ETIMEDOUT)
			US_DEBUGP("raw_bulk(): device NAKed\n");
		else if (result == -EOVERFLOW)
			US_DEBUGP("raw_bulk(): babble/overflow\n");
		else if (result == -ECONNRESET)
			US_DEBUGP("raw_bulk(): asynchronous reset\n");
		else if (result != -EPIPE)
			US_DEBUGP("raw_bulk(): unknown error %d\n",
				  result);

		return USB_STOR_XFER_ERROR;
	}

	if (*act_len != len) {
		US_DEBUGP("Warning: Transferred only %d of %d bytes\n",
			  *act_len, len);
		return USB_STOR_XFER_SHORT;
	}

#if 0
	US_DEBUGP("raw_bulk(): Transferred %s %d of %d bytes\n",
		  (direction == SCSI_DATA_READ) ? "in" : "out",
		  *act_len, len);
#endif

	return USB_STOR_XFER_GOOD;
}

int
usb_storage_bulk_transport(struct us_data *us, int direction,
			   unsigned char *data, unsigned int len,
			   int use_sg) {

	int result = USB_STOR_XFER_ERROR;
	int transferred = 0;
	int i;
	struct scatterlist *sg;
	unsigned int act_len;

	if (len == 0)
		return USB_STOR_XFER_GOOD;

#if DEBUG_PRCT

	if (direction == SCSI_DATA_WRITE && !use_sg) {
		char string[64];

		/* Debug-print the first N bytes of the write transfer */

		strcpy(string, "wr: ");
		for (i=0; i<len && i<DEBUG_PRCT; i++) {
			sprintf(string+strlen(string), "%02X ", data[i]);
			if ((i%16) == 15) {
				US_DEBUGP("%s\n", string);
				strcpy(string, "wr: ");
			}
		}
		if ((i%16)!=0)
			US_DEBUGP("%s\n", string);
	}

	US_DEBUGP("SCM data %s transfer %d sg buffers %d\n",
		  (direction == SCSI_DATA_READ) ? "in" : "out",
		  len, use_sg);

#endif /* DEBUG_PRCT */

	if (!use_sg)
		result = usb_storage_raw_bulk(us, direction,
					      data, len, &act_len);
	else {
		sg = (struct scatterlist *)data;

		for (i=0; i<use_sg && transferred<len; i++) {
			unsigned char *buf;
			unsigned int length;

			buf = sg_address(sg[i]);
			length = len-transferred;
			if (length > sg[i].length)
				length = sg[i].length;

			result = usb_storage_raw_bulk(us, direction,
						      buf, length, &act_len);
			if (result != USB_STOR_XFER_GOOD)
				break;
			transferred += length;
		}
	}

#if DEBUG_PRCT

	if (direction == SCSI_DATA_READ && !use_sg) {
		char string[64];

		/* Debug-print the first N bytes of the read transfer */

		strcpy(string, "rd: ");
		for (i=0; i<len && i<act_len && i<DEBUG_PRCT; i++) {
			sprintf(string+strlen(string), "%02X ", data[i]);
			if ((i%16) == 15) {
				US_DEBUGP("%s\n", string);
				strcpy(string, "rd: ");
			}
		}
		if ((i%16)!=0)
			US_DEBUGP("%s\n", string);
	}

#endif /* DEBUG_PRCT */

	return result;
}

/*
 * The routines below convert scatter-gather to single buffer.
 * Some drivers claim this is necessary.
 * Nothing is done when use_sg is zero.
 */

/*
 * Copy from scatter-gather buffer into a newly allocated single buffer,
 * starting at a given index and offset.
 * When done, update index and offset.
 * Return a pointer to the single buffer.
 */
unsigned char *
us_copy_from_sgbuf(unsigned char *content, int len,
		   int *index, int *offset, int use_sg) {
	struct scatterlist *sg;
	unsigned char *buffer;
	int transferred, i;

	if (!use_sg)
		return content;

	sg = (struct scatterlist *)content;
	buffer = kmalloc(len, GFP_NOIO);
	if (buffer == NULL)
		return NULL;

	transferred = 0;
	i = *index;
	while (i < use_sg && transferred < len) {
		unsigned char *ptr;
		unsigned int length, room;

		ptr = sg_address(sg[i]) + *offset;

		room = sg[i].length - *offset;
		length = len - transferred;
		if (length > room)
			length = room;

		memcpy(buffer+transferred, ptr, length);
		transferred += length;
		*offset += length;
		if (length == room) {
			i++;
			*offset = 0;
		}
	}
	*index = i;

	return buffer;
}

unsigned char *
us_copy_from_sgbuf_all(unsigned char *content, int len, int use_sg) {
	int index, offset;

	index = offset = 0;
	return us_copy_from_sgbuf(content, len, &index, &offset, use_sg);
}

/*
 * Copy from a single buffer into a scatter-gather buffer,
 * starting at a given index and offset.
 * When done, update index and offset.
 */
void
us_copy_to_sgbuf(unsigned char *buffer, int buflen,
		 void *content, int *index, int *offset, int use_sg) {
	struct scatterlist *sg;
	int i, transferred;

	if (!use_sg)
		return;

	transferred = 0;
	sg = content;
	i = *index;
	while (i < use_sg && transferred < buflen) {
		unsigned char *ptr;
		unsigned int length, room;

		ptr = sg_address(sg[i]) + *offset;

		room = sg[i].length - *offset;
		length = buflen - transferred;
		if (length > room)
			length = room;
		
		memcpy(ptr, buffer+transferred, length);
		transferred += sg[i].length;
		*offset += length;
		if (length == room) {
			i++;
			*offset = 0;
		}
	}
	*index = i;
}

void
us_copy_to_sgbuf_all(unsigned char *buffer, int buflen,
		     void *content, int use_sg) {
	int index, offset;

	index = offset = 0;
	us_copy_to_sgbuf(buffer, buflen, content, &index, &offset, use_sg);
}
