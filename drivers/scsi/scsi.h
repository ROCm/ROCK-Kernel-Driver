/*
 *  scsi.h Copyright (C) 1992 Drew Eckhardt 
 *         Copyright (C) 1993, 1994, 1995, 1998, 1999 Eric Youngdale
 *  generic SCSI package header file by
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *       Modified by Eric Youngdale eric@andante.org to
 *       add scatter-gather, multiple outstanding request, and other
 *       enhancements.
 */

#ifndef _SCSI_H
#define _SCSI_H

#include <linux/config.h>	    /* for CONFIG_SCSI_LOGGING */

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi.h>

/*
 * These are the values that the SCpnt->sc_data_direction and 
 * SRpnt->sr_data_direction can take.  These need to be set
 * The SCSI_DATA_UNKNOWN value is essentially the default.
 * In the event that the command creator didn't bother to
 * set a value, you will see SCSI_DATA_UNKNOWN.
 */
#define SCSI_DATA_UNKNOWN       0
#define SCSI_DATA_WRITE         1
#define SCSI_DATA_READ          2
#define SCSI_DATA_NONE          3

#ifdef CONFIG_PCI
#include <linux/pci.h>
#if ((SCSI_DATA_UNKNOWN == PCI_DMA_BIDIRECTIONAL) && (SCSI_DATA_WRITE == PCI_DMA_TODEVICE) && (SCSI_DATA_READ == PCI_DMA_FROMDEVICE) && (SCSI_DATA_NONE == PCI_DMA_NONE))
#define scsi_to_pci_dma_dir(scsi_dir)	((int)(scsi_dir))
#else
extern __inline__ int scsi_to_pci_dma_dir(unsigned char scsi_dir)
{
        if (scsi_dir == SCSI_DATA_UNKNOWN)
                return PCI_DMA_BIDIRECTIONAL;
        if (scsi_dir == SCSI_DATA_WRITE)
                return PCI_DMA_TODEVICE;
        if (scsi_dir == SCSI_DATA_READ)
                return PCI_DMA_FROMDEVICE;
        return PCI_DMA_NONE;
}
#endif
#endif

#if defined(CONFIG_SBUS) && !defined(CONFIG_SUN3) && !defined(CONFIG_SUN3X)
#include <asm/sbus.h>
#if ((SCSI_DATA_UNKNOWN == SBUS_DMA_BIDIRECTIONAL) && (SCSI_DATA_WRITE == SBUS_DMA_TODEVICE) && (SCSI_DATA_READ == SBUS_DMA_FROMDEVICE) && (SCSI_DATA_NONE == SBUS_DMA_NONE))
#define scsi_to_sbus_dma_dir(scsi_dir)	((int)(scsi_dir))
#else
extern __inline__ int scsi_to_sbus_dma_dir(unsigned char scsi_dir)
{
        if (scsi_dir == SCSI_DATA_UNKNOWN)
                return SBUS_DMA_BIDIRECTIONAL;
        if (scsi_dir == SCSI_DATA_WRITE)
                return SBUS_DMA_TODEVICE;
        if (scsi_dir == SCSI_DATA_READ)
                return SBUS_DMA_FROMDEVICE;
        return SBUS_DMA_NONE;
}
#endif
#endif

/*
 * Some defs, in case these are not defined elsewhere.
 */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MAX_SCSI_DEVICE_CODE 14
extern const char *const scsi_device_types[MAX_SCSI_DEVICE_CODE];

#ifdef DEBUG
#define SCSI_TIMEOUT (5*HZ)
#else
#define SCSI_TIMEOUT (2*HZ)
#endif

/*
 *  Use these to separate status msg and our bytes
 *
 *  These are set by:
 *
 *      status byte = set from target device
 *      msg_byte    = return status from host adapter itself.
 *      host_byte   = set by low-level driver to indicate status.
 *      driver_byte = set by mid-level.
 */
#define status_byte(result) (((result) >> 1) & 0x1f)
#define msg_byte(result)    (((result) >> 8) & 0xff)
#define host_byte(result)   (((result) >> 16) & 0xff)
#define driver_byte(result) (((result) >> 24) & 0xff)
#define suggestion(result)  (driver_byte(result) & SUGGEST_MASK)

#define sense_class(sense)  (((sense) >> 4) & 0x7)
#define sense_error(sense)  ((sense) & 0xf)
#define sense_valid(sense)  ((sense) & 0x80);

#define NEEDS_RETRY     0x2001
#define SUCCESS         0x2002
#define FAILED          0x2003
#define QUEUED          0x2004
#define SOFT_ERROR      0x2005
#define ADD_TO_MLQUEUE  0x2006

#define IDENTIFY_BASE       0x80
#define IDENTIFY(can_disconnect, lun)   (IDENTIFY_BASE |\
		     ((can_disconnect) ?  0x40 : 0) |\
		     ((lun) & 0x07))

/* host byte codes */
#define DID_OK          0x00	/* NO error                                */
#define DID_NO_CONNECT  0x01	/* Couldn't connect before timeout period  */
#define DID_BUS_BUSY    0x02	/* BUS stayed busy through time out period */
#define DID_TIME_OUT    0x03	/* TIMED OUT for other reason              */
#define DID_BAD_TARGET  0x04	/* BAD target.                             */
#define DID_ABORT       0x05	/* Told to abort for some other reason     */
#define DID_PARITY      0x06	/* Parity error                            */
#define DID_ERROR       0x07	/* Internal error                          */
#define DID_RESET       0x08	/* Reset by somebody.                      */
#define DID_BAD_INTR    0x09	/* Got an interrupt we weren't expecting.  */
#define DID_PASSTHROUGH 0x0a	/* Force command past mid-layer            */
#define DID_SOFT_ERROR  0x0b	/* The low level driver just wish a retry  */
#define DRIVER_OK       0x00	/* Driver status                           */

/*
 *  These indicate the error that occurred, and what is available.
 */

#define DRIVER_BUSY         0x01
#define DRIVER_SOFT         0x02
#define DRIVER_MEDIA        0x03
#define DRIVER_ERROR        0x04

#define DRIVER_INVALID      0x05
#define DRIVER_TIMEOUT      0x06
#define DRIVER_HARD         0x07
#define DRIVER_SENSE	    0x08

#define SUGGEST_RETRY       0x10
#define SUGGEST_ABORT       0x20
#define SUGGEST_REMAP       0x30
#define SUGGEST_DIE         0x40
#define SUGGEST_SENSE       0x80
#define SUGGEST_IS_OK       0xff

#define DRIVER_MASK         0x0f
#define SUGGEST_MASK        0xf0

/*
 *  SCSI command sets
 */

#define SCSI_UNKNOWN    0
#define SCSI_1          1
#define SCSI_1_CCS      2
#define SCSI_2          3
#define SCSI_3          4

/*
 *  Every SCSI command starts with a one byte OP-code.
 *  The next byte's high three bits are the LUN of the
 *  device.  Any multi-byte quantities are stored high byte
 *  first, and may have a 5 bit MSB in the same byte
 *  as the LUN.
 */

/*
 *  As the scsi do command functions are intelligent, and may need to
 *  redo a command, we need to keep track of the last command
 *  executed on each one.
 */

#define WAS_RESET       0x01
#define WAS_TIMEDOUT    0x02
#define WAS_SENSE       0x04
#define IS_RESETTING    0x08
#define IS_ABORTING     0x10
#define ASKED_FOR_SENSE 0x20
#define SYNC_RESET      0x40

struct Scsi_Host;
struct scsi_cmnd;
struct scsi_device;
struct scsi_target;
struct scatterlist;

/*
 * These are the error handling functions defined in scsi_error.c
 */
extern void scsi_add_timer(struct scsi_cmnd *, int,
			   void (*)(struct scsi_cmnd *));
extern int scsi_delete_timer(struct scsi_cmnd *);
extern int scsi_block_when_processing_errors(struct scsi_device *);
extern void scsi_sleep(int);

/*
 * Prototypes for functions in scsicam.c
 */
extern int  scsi_partsize(unsigned char *buf, unsigned long capacity,
                    unsigned int *cyls, unsigned int *hds,
                    unsigned int *secs);

/*
 * Newer request-based interfaces.
 */
extern struct scsi_request *scsi_allocate_request(struct scsi_device *);
extern void scsi_release_request(struct scsi_request *);
extern void scsi_wait_req(struct scsi_request *, const void *cmnd,
			  void *buffer, unsigned bufflen,
			  int timeout, int retries);
extern void scsi_do_req(struct scsi_request *, const void *cmnd,
			void *buffer, unsigned bufflen,
			void (*done) (struct scsi_cmnd *),
			int timeout, int retries);


/*
 * Prototypes for functions in constants.c
 * Some of these used to live in constants.h
 */
extern void print_Scsi_Cmnd(struct scsi_cmnd *);
extern void print_command(unsigned char *);
extern void print_sense(const char *, struct scsi_cmnd *);
extern void print_req_sense(const char *, struct scsi_request *);
extern void print_driverbyte(int scsiresult);
extern void print_hostbyte(int scsiresult);
extern void print_status(unsigned char status);
extern int print_msg(const unsigned char *);
extern const char *scsi_sense_key_string(unsigned char);
extern const char *scsi_extd_sense_format(unsigned char, unsigned char);

/*
 * This is essentially a slimmed down version of Scsi_Cmnd.  The point of
 * having this is that requests that are injected into the queue as result
 * of things like ioctls and character devices shouldn't be using a
 * Scsi_Cmnd until such a time that the command is actually at the head
 * of the queue and being sent to the driver.
 */
struct scsi_request {
	int     sr_magic;
	int     sr_result;	/* Status code from lower level driver */
	unsigned char sr_sense_buffer[SCSI_SENSE_BUFFERSIZE];		/* obtained by REQUEST SENSE
						 * when CHECK CONDITION is
						 * received on original command 
						 * (auto-sense) */

	struct Scsi_Host *sr_host;
	struct scsi_device *sr_device;
	struct scsi_cmnd *sr_command;
	struct request *sr_request;	/* A copy of the command we are
				   working on */
	unsigned sr_bufflen;	/* Size of data buffer */
	void *sr_buffer;		/* Data buffer */
	int sr_allowed;
	unsigned char sr_data_direction;
	unsigned char sr_cmd_len;
	unsigned char sr_cmnd[MAX_COMMAND_SIZE];
	void (*sr_done) (struct scsi_cmnd *);	/* Mid-level done function */
	int sr_timeout_per_command;
	unsigned short sr_use_sg;	/* Number of pieces of scatter-gather */
	unsigned short sr_sglist_len;	/* size of malloc'd scatter-gather list */
	unsigned sr_underflow;	/* Return error if less than
				   this amount is transferred */
 	void * upper_private_data;	/* reserved for owner (usually upper
 					   level driver) of this request */
};

/*
 * Definitions and prototypes used for scsi mid-level queue.
 */
#define SCSI_MLQUEUE_HOST_BUSY   0x1055
#define SCSI_MLQUEUE_DEVICE_BUSY 0x1056
#define SCSI_MLQUEUE_EH_RETRY    0x1057

/*
 * Reset request from external source
 */
#define SCSI_TRY_RESET_DEVICE	1
#define SCSI_TRY_RESET_BUS	2
#define SCSI_TRY_RESET_HOST	3

extern int scsi_reset_provider(struct scsi_device *, int);

#define MSG_SIMPLE_TAG	0x20
#define MSG_HEAD_TAG	0x21
#define MSG_ORDERED_TAG	0x22

#define SCSI_NO_TAG	(-1)    /* identify no tag in use */

/**
 * scsi_activate_tcq - turn on tag command queueing
 * @SDpnt:	device to turn on TCQ for
 * @depth:	queue depth
 *
 * Notes:
 *	Eventually, I hope depth would be the maximum depth
 *	the device could cope with and the real queue depth
 *	would be adjustable from 0 to depth.
 **/
static inline void scsi_activate_tcq(struct scsi_device *sdev, int depth)
{
        if (sdev->tagged_supported) {
		if (!blk_queue_tagged(sdev->request_queue))
			blk_queue_init_tags(sdev->request_queue, depth);
		scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, depth);
        }
}

/**
 * scsi_deactivate_tcq - turn off tag command queueing
 * @SDpnt:	device to turn off TCQ for
 **/
static inline void scsi_deactivate_tcq(struct scsi_device *sdev, int depth)
{
	if (blk_queue_tagged(sdev->request_queue))
		blk_queue_free_tags(sdev->request_queue);
	scsi_adjust_queue_depth(sdev, 0, depth);
}

/**
 * scsi_populate_tag_msg - place a tag message in a buffer
 * @SCpnt:	pointer to the Scsi_Cmnd for the tag
 * @msg:	pointer to the area to place the tag
 *
 * Notes:
 *	designed to create the correct type of tag message for the 
 *	particular request.  Returns the size of the tag message.
 *	May return 0 if TCQ is disabled for this device.
 **/
static inline int scsi_populate_tag_msg(struct scsi_cmnd *cmd, char *msg)
{
        struct request *req = cmd->request;

        if (blk_rq_tagged(req)) {
		if (req->flags & REQ_HARDBARRIER)
        	        *msg++ = MSG_ORDERED_TAG;
        	else
        	        *msg++ = MSG_SIMPLE_TAG;
        	*msg++ = req->tag;
        	return 2;
	}

	return 0;
}

/**
 * scsi_find_tag - find a tagged command by device
 * @SDpnt:	pointer to the ScSI device
 * @tag:	the tag number
 *
 * Notes:
 *	Only works with tags allocated by the generic blk layer.
 **/
static inline struct scsi_cmnd *scsi_find_tag(struct scsi_device *sdev, int tag)
{

        struct request *req;

        if (tag != SCSI_NO_TAG) {
        	req = blk_queue_find_tag(sdev->request_queue, tag);
	        return req ? (struct scsi_cmnd *)req->special : NULL;
	}

	/* single command, look in space */
	return sdev->current_cmnd;
}

extern int scsi_sysfs_modify_sdev_attribute(struct device_attribute ***dev_attrs,
					    struct device_attribute *attr);
extern int scsi_sysfs_modify_shost_attribute(struct class_device_attribute ***class_attrs,
					     struct class_device_attribute *attr);

/*
 * This is the crap from the old error handling code.  We have it in a special
 * place so that we can more easily delete it later on.
 */
#include "scsi_obsolete.h"

/* obsolete typedef junk. */
#include "scsi_typedefs.h"

#endif /* _SCSI_H */
