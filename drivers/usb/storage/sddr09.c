/* Driver for SanDisk SDDR-09 SmartMedia reader
 *
 * $Id: sddr09.c,v 1.14 2000/11/21 02:58:26 mdharm Exp $
 *
 * SDDR09 driver v0.1:
 *
 * First release
 *
 * Current development and maintenance by:
 *   (c) 2000 Robert Baruch (autophile@dol.net)
 *
 * The SanDisk SDDR-09 SmartMedia reader uses the Shuttle EUSB-01 chip.
 * This chip is a programmable USB controller. In the SDDR-09, it has
 * been programmed to obey a certain limited set of SCSI commands. This
 * driver translates the "real" SCSI commands to the SDDR-09 SCSI
 * commands.
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

#include "transport.h"
#include "protocol.h"
#include "usb.h"
#include "debug.h"
#include "sddr09.h"

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/malloc.h>

#define short_pack(lsb,msb) ( ((u16)(lsb)) | ( ((u16)(msb))<<8 ) )
#define LSB_of(s) ((s)&0xFF)
#define MSB_of(s) ((s)>>8)

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

static int sddr09_send_control(struct us_data *us,
		int pipe,
		unsigned char request,
		unsigned char requesttype,
		unsigned short value,
		unsigned short index,
		unsigned char *xfer_data,
		unsigned int xfer_len) {

	int result;

	// If data is going to be sent or received with the URB,
	// then allocate a buffer for it. If data is to be sent,
	// copy the data into the buffer.
/*
	if (xfer_len > 0) {
		buffer = kmalloc(xfer_len, GFP_KERNEL);
		if (!(command[0] & USB_DIR_IN))
			memcpy(buffer, xfer_data, xfer_len);
	}
*/
	// Send the URB to the device and wait for a response.

	/* Why are request and request type reversed in this call? */

	result = usb_stor_control_msg(us, pipe,
			request, requesttype, value, index,
			xfer_data, xfer_len);


	// If data was sent or received with the URB, free the buffer we
	// allocated earlier, but not before reading the data out of the
	// buffer if we wanted to receive data.
/*
	if (xfer_len > 0) {
		if (command[0] & USB_DIR_IN)
			memcpy(xfer_data, buffer, xfer_len);
		kfree(buffer);
	}
*/
	// Check the return code for the command.

	if (result < 0) {
		/* if the command was aborted, indicate that */
		if (result == -ENOENT)
			return USB_STOR_TRANSPORT_ABORTED;

		/* a stall is a fatal condition from the device */
		if (result == -EPIPE) {
			US_DEBUGP("-- Stall on control pipe. Clearing\n");
			result = usb_clear_halt(us->pusb_dev, pipe);
			US_DEBUGP("-- usb_clear_halt() returns %d\n", result);
			return USB_STOR_TRANSPORT_FAILED;
		}

		/* Uh oh... serious problem here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

static int sddr09_raw_bulk(struct us_data *us, 
		int direction,
		unsigned char *data,
		unsigned int len) {

	int result;
	int act_len;
	int pipe;

	if (direction == SCSI_DATA_READ)
		pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
	else
		pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);

	result = usb_stor_bulk_msg(us, data, pipe, len, &act_len);

        /* if we stall, we need to clear it before we go on */
        if (result == -EPIPE) {
       	        US_DEBUGP("EPIPE: clearing endpoint halt for"
			" pipe 0x%x, stalled at %d bytes\n",
			pipe, act_len);
               	usb_clear_halt(us->pusb_dev, pipe);
        }

	if (result) {

                /* NAK - that means we've retried a few times already */
       	        if (result == -ETIMEDOUT) {
                        US_DEBUGP("usbat_raw_bulk():"
				" device NAKed\n");
                        return US_BULK_TRANSFER_FAILED;
                }

                /* -ENOENT -- we canceled this transfer */
                if (result == -ENOENT) {
                        US_DEBUGP("usbat_raw_bulk():"
				" transfer aborted\n");
                        return US_BULK_TRANSFER_ABORTED;
                }

		if (result == -EPIPE) {
			US_DEBUGP("usbat_raw_bulk():"
				" output pipe stalled\n");
			return USB_STOR_TRANSPORT_FAILED;
		}

                /* the catch-all case */
                US_DEBUGP("us_transfer_partial(): unknown error\n");
                return US_BULK_TRANSFER_FAILED;
        }

	if (act_len != len) {
		US_DEBUGP("Warning: Transferred only %d bytes\n",
			act_len);
		return US_BULK_TRANSFER_SHORT;
	}

	US_DEBUGP("Transfered %d of %d bytes\n", act_len, len);

	return US_BULK_TRANSFER_GOOD;
}

/*
 * Note: direction must be set if command_len == 0.
 */

static int sddr09_bulk_transport(struct us_data *us,
			  int direction,
			  unsigned char *data,
			  unsigned int len,
			  int use_sg) {

	int result = USB_STOR_TRANSPORT_GOOD;
	int transferred = 0;
	int i;
	struct scatterlist *sg;
	char string[64];

	if (len==0)
		return USB_STOR_TRANSPORT_GOOD;


	/* transfer the data */

	if (direction == SCSI_DATA_WRITE) {

		/* Debug-print the first 48 bytes of the write transfer */

		if (!use_sg) {
			strcpy(string, "wr: ");
			for (i=0; i<len && i<48; i++) {
				sprintf(string+strlen(string), "%02X ",
				  data[i]);
				if ((i%16)==15) {
					US_DEBUGP("%s\n", string);
					strcpy(string, "wr: ");
				}
			}
			if ((i%16)!=0)
				US_DEBUGP("%s\n", string);
		}
	}


	US_DEBUGP("SCM data %s transfer %d sg buffers %d\n",
		  ( direction==SCSI_DATA_READ ? "in" : "out"),
		  len, use_sg);

	if (!use_sg)
		result = sddr09_raw_bulk(us, direction, data, len);
	else {
		sg = (struct scatterlist *)data;
		for (i=0; i<use_sg && transferred<len; i++) {
			result = sddr09_raw_bulk(us, direction,
				sg[i].address, 
				len-transferred > sg[i].length ?
					sg[i].length : len-transferred);
			if (result!=US_BULK_TRANSFER_GOOD)
				break;
			transferred += sg[i].length;
		}
	}

	if (direction == SCSI_DATA_READ) {

		/* Debug-print the first 48 bytes of the read transfer */

		if (!use_sg) {
			strcpy(string, "rd: ");
			for (i=0; i<len && i<48; i++) {
				sprintf(string+strlen(string), "%02X ",
				  data[i]);
				if ((i%16)==15) {
					US_DEBUGP("%s\n", string);
					strcpy(string, "rd: ");
				}
			}
			if ((i%16)!=0)
				US_DEBUGP("%s\n", string);
		}
	}

	return result;
}

int sddr09_read_data(struct us_data *us,
		unsigned long address,
		unsigned short sectors,
		unsigned char *content,
		int use_sg) {

	int result;
	unsigned char command[12] = {
		0xe8, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	struct sddr09_card_info *info = (struct sddr09_card_info *)us->extra;
	unsigned int lba;
	unsigned int pba;
	unsigned short page;
	unsigned short pages;
	unsigned char *buffer = NULL;
	unsigned char *ptr;
	struct scatterlist *sg = NULL;
	int i;
	int len;
	int transferred;

	// If we're using scatter-gather, we have to create a new
	// buffer to read all of the data in first, since a
	// scatter-gather buffer could in theory start in the middle
	// of a page, which would be bad. A developer who wants a
	// challenge might want to write a limited-buffer
	// version of this code.

	len = sectors*info->pagesize;

	if (use_sg) {
		sg = (struct scatterlist *)content;
		buffer = kmalloc(len, GFP_KERNEL);
		if (buffer == NULL)
			return USB_STOR_TRANSPORT_ERROR;
		ptr = buffer;
	} else
		ptr = content;

	// Figure out the initial LBA and page

	pba = address >> (info->pageshift + info->blockshift);
	lba = info->pba_to_lba[pba];
	page = (address >> info->pageshift) & info->blockmask;

	// This could be made much more efficient by checking for
	// contiguous LBA's. Another exercise left to the student.

	while (sectors>0) {

		pba = info->lba_to_pba[lba];

		// Read as many sectors as possible in this block

		pages = info->blocksize - page;
		if (pages > sectors)
			pages = sectors;

		US_DEBUGP("Read %02X pages, from PBA %04X"
			" (LBA %04X) page %02X\n",
			pages, pba, lba, page);

		address = ( (pba << info->blockshift) + page ) << 
			info->pageshift;

		// Unlike in the documentation, the address is in
		// words of 2 bytes.

		command[2] = MSB_of(address>>17);
		command[3] = LSB_of(address>>17); 
		command[4] = MSB_of((address>>1)&0xFFFF);
		command[5] = LSB_of((address>>1)&0xFFFF); 

		command[10] = MSB_of(pages);
		command[11] = LSB_of(pages);

		result = sddr09_send_control(us,
			usb_sndctrlpipe(us->pusb_dev,0),
			0,
			0x41,
			0,
			0,
			command,
			12);

		US_DEBUGP("Result for send_control in read_data %d\n",
			result);

		if (result != USB_STOR_TRANSPORT_GOOD) {
			if (use_sg)
				kfree(buffer);
			return result;
		}

		result = sddr09_bulk_transport(us,
			SCSI_DATA_READ, ptr,
			pages<<info->pageshift, 0);

		if (result != USB_STOR_TRANSPORT_GOOD) {
			if (use_sg)
				kfree(buffer);
			return result;
		}

		page = 0;
		lba++;
		sectors -= pages;
		ptr += (pages << info->pageshift);
	}

	if (use_sg) {
		transferred = 0;
		for (i=0; i<use_sg && transferred<len; i++) {
			memcpy(sg[i].address, buffer+transferred,
				len-transferred > sg[i].length ?
					sg[i].length : len-transferred);
			transferred += sg[i].length;
		}
		kfree(buffer);
	}

	return USB_STOR_TRANSPORT_GOOD;
}

int sddr09_read_control(struct us_data *us,
		unsigned long address,
		unsigned short blocks,
		unsigned char *content,
		int use_sg) {

	// Unlike in the documentation, the last two bytes are the
	// number of blocks, not sectors.

	int result;
	unsigned char command[12] = {
		0xe8, 0x21, MSB_of(address>>16),
		LSB_of(address>>16), MSB_of(address&0xFFFF),
		LSB_of(address&0xFFFF), 0, 0, 0, 0,
		MSB_of(blocks), LSB_of(blocks)
	};

	US_DEBUGP("Read control address %08lX blocks %04X\n",
		address, blocks);

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);

	US_DEBUGP("Result for send_control in read_control %d\n",
		result);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = sddr09_bulk_transport(us,
		SCSI_DATA_READ, content,
		blocks<<6, use_sg); // 0x40 bytes per block

	US_DEBUGP("Result for bulk read in read_control %d\n",
		result);

	return result;
}

int sddr09_read_deviceID(struct us_data *us,
		unsigned char *manufacturerID,
		unsigned char *deviceID) {

	int result;
	unsigned char command[12] = {
		0xed, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	unsigned char content[64];

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);

	US_DEBUGP("Result of send_control for device ID is %d\n",
		result);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = sddr09_bulk_transport(us,
		SCSI_DATA_READ, content,
		64, 0);

	*manufacturerID = content[0];
	*deviceID = content[1];

	return result;
}

int sddr09_read_status(struct us_data *us,
		unsigned char *status) {

	int result;
	unsigned char command[12] = {
		0xec, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = sddr09_bulk_transport(us,
		SCSI_DATA_READ, status,
		1, 0);

	return result;
}

int sddr09_reset(struct us_data *us) {

	int result;
	unsigned char command[12] = {
		0xeb, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);

	return result;
}

unsigned long sddr09_get_capacity(struct us_data *us,
		unsigned int *pagesize, unsigned int *blocksize) {

	unsigned char manufacturerID;
	unsigned char deviceID;
	int result;

	US_DEBUGP("Reading capacity...\n");

	result = sddr09_read_deviceID(us,
		&manufacturerID,
		&deviceID);

	US_DEBUGP("Result of read_deviceID is %d\n",
		result);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return 0;

	US_DEBUGP("Device ID = %02X\n", deviceID);
	US_DEBUGP("Manuf  ID = %02X\n", manufacturerID);

	*pagesize = 512;
	*blocksize = 16;

	switch (deviceID) {

	case 0x6e: // 1MB
	case 0xe8:
	case 0xec:
		*pagesize = 256;
		return 0x00100000;

	case 0xea: // 2MB
	case 0x5d: // 5d is a ROM card with pagesize 512.
	case 0x64:
		if (deviceID!=0x5D)
			*pagesize = 256;
		return 0x00200000;

	case 0xe3: // 4MB
	case 0xe5:
	case 0x6b:
	case 0xd5:
		return 0x00400000;

	case 0xe6: // 8MB
	case 0xd6:
		return 0x00800000;

	case 0x73: // 16MB
		*blocksize = 32;
		return 0x01000000;

	case 0x75: // 32MB
		*blocksize = 32;
		return 0x02000000;

	case 0x76: // 64MB
		*blocksize = 32;
		return 0x04000000;

	default: // unknown
		return 0;

	}
}

int sddr09_read_map(struct us_data *us) {

	struct scatterlist *sg;
	struct sddr09_card_info *info = (struct sddr09_card_info *)(us->extra);
	int numblocks;
	int i;
	unsigned char *ptr;
	unsigned short lba;
	unsigned char parity;
	unsigned char fast_parity[16] = {
		0, 1, 1, 0, 1, 0, 0, 1,
		1, 0, 0, 1, 0, 1, 1, 0
	};
	int result;
	int alloc_len;
	int alloc_blocks;

	if (!info->capacity)
		return -1;

	// read 64 (1<<6) bytes for every block 
	// ( 1 << ( blockshift + pageshift ) bytes)
	//	 of capacity:
	// (1<<6)*capacity/(1<<(b+p)) =
	// ((1<<6)*capacity)>>(b+p) =
	// capacity>>(b+p-6)

	alloc_len = info->capacity >> 
		(info->blockshift + info->pageshift - 6);

	// Allocate a number of scatterlist structures according to
	// the number of 128k blocks in the alloc_len. Adding 128k-1
	// and then dividing by 128k gives the correct number of blocks.
	// 128k = 1<<17

	alloc_blocks = (alloc_len + (1<<17) - 1) >> 17;
	sg = kmalloc(alloc_blocks*sizeof(struct scatterlist),
		GFP_KERNEL);
	if (sg == NULL)
		return 0;

	for (i=0; i<alloc_blocks; i++) {
		if (i<alloc_blocks-1) {
			sg[i].address = kmalloc( (1<<17), GFP_KERNEL );
			sg[i].length = (1<<17);
		} else {
			sg[i].address = kmalloc(alloc_len, GFP_KERNEL);
			sg[i].length = alloc_len;
		}
		alloc_len -= sg[i].length;
	}
	for (i=0; i<alloc_blocks; i++)
		if (sg[i].address == NULL) {
			for (i=0; i<alloc_blocks; i++)
				if (sg[i].address != NULL)
					kfree(sg[i].address);
			kfree(sg);
			return 0;
		}

	numblocks = info->capacity >> (info->blockshift + info->pageshift);

	if ( (result = sddr09_read_control(us, 0, numblocks,
			(unsigned char *)sg, alloc_blocks)) !=
			USB_STOR_TRANSPORT_GOOD) {
		for (i=0; i<alloc_blocks; i++)
			kfree(sg[i].address);
		kfree(sg);
		return -1;
	}



	if (info->lba_to_pba)
		kfree(info->lba_to_pba);
	if (info->pba_to_lba)
		kfree(info->pba_to_lba);
	info->lba_to_pba = kmalloc(numblocks*sizeof(int), GFP_KERNEL);
	info->pba_to_lba = kmalloc(numblocks*sizeof(int), GFP_KERNEL);

	if (info->lba_to_pba == NULL || info->pba_to_lba == NULL) {
		if (info->lba_to_pba != NULL)
			kfree(info->lba_to_pba);
		if (info->pba_to_lba != NULL)
			kfree(info->pba_to_lba);
		info->lba_to_pba = NULL;
		info->pba_to_lba = NULL;
		for (i=0; i<alloc_blocks; i++)
			kfree(sg[i].address);
		kfree(sg);
		return 0;
	}

	memset(info->lba_to_pba, 0, numblocks*sizeof(int));
	memset(info->pba_to_lba, 0, numblocks*sizeof(int));

	// Each block is 64 bytes of control data, so block i is located in
	// scatterlist block i*64/128k = i*(2^6)*(2^-17) = i*(2^-11)

	for (i=0; i<numblocks; i++) {
		ptr = sg[i>>11].address+(i<<6);
		if (ptr[0]!=0xFF || ptr[1]!=0xFF || ptr[2]!=0xFF ||
		    ptr[3]!=0xFF || ptr[4]!=0xFF || ptr[5]!=0xFF)
			continue;
		if ((ptr[6]>>4)!=0x01)
			continue;

		/* ensure even parity */

		lba = short_pack(ptr[7], ptr[6]);
		parity = 1; // the parity of 0x1000
		parity ^= fast_parity[lba & 0x000F];
		parity ^= fast_parity[(lba>>4) & 0x000F];
		parity ^= fast_parity[(lba>>8) & 0x000F];

		if (parity) { /* bad parity bit */
			US_DEBUGP("Bad parity in LBA for block %04X\n", i);
			continue;
		}

		lba = (lba&0x07FF)>>1;

			/* Every 1024 physical blocks, the LBA numbers
			 * go back to zero, but are within a higher
			 * block of LBA's. In other words, in blocks
			 * 1024-2047 you will find LBA 0-1023 which are
			 * really LBA 1024-2047.
			 */

		lba += (i&~0x3FF);

		if (lba>=numblocks) {
			US_DEBUGP("Bad LBA %04X for block %04X\n", lba, i);
			continue;
		}

		if (lba<0x10)
			US_DEBUGP("LBA %04X <-> PBA %04X\n", lba, i);

		info->pba_to_lba[i] = lba;
		info->lba_to_pba[lba] = i;
	}

	for (i=0; i<alloc_blocks; i++)
		kfree(sg[i].address);
	kfree(sg);
	return 0;
}

/*
static int init_sddr09(struct us_data *us) {

	int result;
	unsigned char data[14];
	unsigned char command[8] = {
		0xc1, 0x01, 0, 0, 0, 0, 0, 0
	};
	unsigned char command2[8] = {
		0x41, 0, 0, 0, 0, 0, 0, 0
	};
	unsigned char tur[12] = {
		0x03, 0x20, 0, 0, 0x0e, 0, 0, 0, 0, 0, 0, 0
	};

	// What the hey is all this for? Doesn't seem to
	// affect the device, so we won't do device inits.

	if ( (result = sddr09_send_control(us, command, data, 2)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("SDDR09: %02X %02X\n", data[0], data[1]);

	command[1] = 0x08;

	if ( (result = sddr09_send_control(us, command, data, 2)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("SDDR09: %02X %02X\n", data[0], data[1]);

	if ( (result = sddr09_send_control(us, command2, tur, 12)) !=
			USB_STOR_TRANSPORT_GOOD) {
		US_DEBUGP("SDDR09: request sense failed\n");
		return result;
	}

	if ( (result = sddr09_raw_bulk(
		us, SCSI_DATA_READ, data, 14)) !=
			USB_STOR_TRANSPORT_GOOD) {
		US_DEBUGP("SDDR09: request sense bulk in failed\n");
		return result;
	}

	US_DEBUGP("SDDR09: request sense worked\n");

	return result;
}
*/

void sddr09_card_info_destructor(void *extra) {
	struct sddr09_card_info *info = (struct sddr09_card_info *)extra;

	if (!extra)
		return;

	if (info->lba_to_pba)
		kfree(info->lba_to_pba);
	if (info->pba_to_lba)
		kfree(info->pba_to_lba);
}

/*
 * Transport for the Sandisk SDDR-09
 */
int sddr09_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;
	int i;
	char string[64];
	unsigned char inquiry_response[36] = {
		0x00, 0x80, 0x00, 0x02, 0x1F, 0x00, 0x00, 0x00
	};
	unsigned char mode_page_01[4] = { // write-protected for now
		0x03, 0x00, 0x80, 0x00
	};
	unsigned char *ptr;
	unsigned long capacity;
	unsigned int lba;
	unsigned int pba;
	unsigned int page;
	unsigned short pages;
	struct sddr09_card_info *info = (struct sddr09_card_info *)(us->extra);

	if (!us->extra) {
		us->extra = kmalloc(
			sizeof(struct sddr09_card_info), GFP_KERNEL);
		if (!us->extra)
			return USB_STOR_TRANSPORT_ERROR;
		memset(us->extra, 0, sizeof(struct sddr09_card_info));
		us->extra_destructor = sddr09_card_info_destructor;
	}

	ptr = (unsigned char *)srb->request_buffer;

	/* Dummy up a response for INQUIRY since SDDR09 doesn't
	   respond to INQUIRY commands */

	if (srb->cmnd[0] == INQUIRY) {
		memset(inquiry_response+8, 0, 28);
		fill_inquiry_response(us, inquiry_response, 36);
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == READ_CAPACITY) {

		capacity = sddr09_get_capacity(us, &info->pagesize,
			&info->blocksize);

		if (!capacity)
			return USB_STOR_TRANSPORT_FAILED;

		info->capacity = capacity;
		for (info->pageshift=1; 
			(info->pagesize>>info->pageshift);
			info->pageshift++);
		info->pageshift--;
		for (info->blockshift=1; 
			(info->blocksize>>info->blockshift);
			info->blockshift++);
		info->blockshift--;
		info->blockmask = (1<<info->blockshift)-1;

		// Last page in the card

		capacity /= info->pagesize;
		capacity--;

		ptr[0] = MSB_of(capacity>>16);
		ptr[1] = LSB_of(capacity>>16);
		ptr[2] = MSB_of(capacity&0xFFFF);
		ptr[3] = LSB_of(capacity&0xFFFF);

		// The page size

		ptr[4] = MSB_of(info->pagesize>>16);
		ptr[5] = LSB_of(info->pagesize>>16);
		ptr[6] = MSB_of(info->pagesize&0xFFFF);
		ptr[7] = LSB_of(info->pagesize&0xFFFF);

		sddr09_read_map(us);

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == MODE_SENSE) {

			// Read-write error recovery page: there needs to
			// be a check for write-protect here

		if ( (srb->cmnd[2] & 0x3F) == 0x01 ) {
			if (ptr==NULL || srb->request_bufflen<4)
				return USB_STOR_TRANSPORT_ERROR;
			memcpy(ptr, mode_page_01, sizeof(mode_page_01));
			return USB_STOR_TRANSPORT_GOOD;
		}

		// FIXME: sense buffer?

		return USB_STOR_TRANSPORT_ERROR;
	}

	if (srb->cmnd[0] == READ_10) {

		page = short_pack(srb->cmnd[3], srb->cmnd[2]);
		page <<= 16;
		page |= short_pack(srb->cmnd[5], srb->cmnd[4]);
		pages = short_pack(srb->cmnd[8], srb->cmnd[7]);

		// convert page to block and page-within-block

		lba = page >> info->blockshift;
		page = page & info->blockmask;

		// locate physical block corresponding to logical block

		if (lba >=
			(info->capacity >> 
				(info->pageshift + info->blockshift) ) ) {

			// FIXME: sense buffer?

			return USB_STOR_TRANSPORT_ERROR;
		}

		pba = info->lba_to_pba[lba];

		// if pba is 0, either it's really 0, in which case
		// the pba-to-lba map for pba 0 will be the lba,
		// or that lba doesn't exist.

		if (pba==0 && info->pba_to_lba[0] != lba) {

			// FIXME: sense buffer?

			return USB_STOR_TRANSPORT_ERROR;
		}

		US_DEBUGP("READ_10: read block %04X (LBA %04X) page %01X"
			" pages %d\n",
			pba, lba, page, pages);

		return sddr09_read_data(us,
			( (pba << info->blockshift) + page) << info->pageshift,
			pages, ptr, srb->use_sg);
	}

	// Pass TEST_UNIT_READY and REQUEST_SENSE through

	if (srb->cmnd[0] != TEST_UNIT_READY &&
	    srb->cmnd[0] != REQUEST_SENSE)
		return USB_STOR_TRANSPORT_ERROR; // FIXME: sense buffer?

	for (; srb->cmd_len<12; srb->cmd_len++)
		srb->cmnd[srb->cmd_len] = 0;

	srb->cmnd[1] = 0x20;

	string[0] = 0;
	for (i=0; i<12; i++)
	  sprintf(string+strlen(string), "%02X ", srb->cmnd[i]);

	US_DEBUGP("SDDR09: Send control for command %s\n",
		string);

	if ( (result = sddr09_send_control(us,
			usb_sndctrlpipe(us->pusb_dev,0),
			0,
			0x41,
			0,
			0,
			srb->cmnd,
			12)) != USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("SDDR09: Control for command OK\n");

	if (srb->request_bufflen == 0)
		return USB_STOR_TRANSPORT_GOOD;

	if (srb->sc_data_direction == SCSI_DATA_WRITE ||
	    srb->sc_data_direction == SCSI_DATA_READ) {

		US_DEBUGP("SDDR09: %s %d bytes\n",
			srb->sc_data_direction==SCSI_DATA_WRITE ?
			  "sending" : "receiving",
			srb->request_bufflen);

		result = sddr09_bulk_transport(us,
			srb->sc_data_direction,
			srb->request_buffer, 
			srb->request_bufflen, srb->use_sg);

		return result;

	} 

	return USB_STOR_TRANSPORT_GOOD;
}

