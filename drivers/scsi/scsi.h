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
#include <scsi/scsi_eh.h>
#include <scsi/scsi_request.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>

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
 * Definitions and prototypes used for scsi mid-level queue.
 */
#define SCSI_MLQUEUE_HOST_BUSY   0x1055
#define SCSI_MLQUEUE_DEVICE_BUSY 0x1056
#define SCSI_MLQUEUE_EH_RETRY    0x1057

extern int scsi_sysfs_modify_sdev_attribute(struct device_attribute ***dev_attrs,
					    struct device_attribute *attr);
extern int scsi_sysfs_modify_shost_attribute(struct class_device_attribute ***class_attrs,
					     struct class_device_attribute *attr);

/*
 * Legacy dma direction interfaces.
 *
 * This assumes the pci/sbus dma mapping flags have the same numercial
 * values as the generic dma-mapping ones.  Currently they have but there's
 * no way to check.  Better don't use these interfaces!
 */
#define SCSI_DATA_UNKNOWN	(DMA_BIDIRECTIONAL)
#define SCSI_DATA_WRITE		(DMA_TO_DEVICE)
#define SCSI_DATA_READ		(DMA_FROM_DEVICE)
#define SCSI_DATA_NONE		(DMA_NONE)

#define scsi_to_pci_dma_dir(scsi_dir)	((int)(scsi_dir))
#define scsi_to_sbus_dma_dir(scsi_dir)	((int)(scsi_dir))

/*
 * This is the crap from the old error handling code.  We have it in a special
 * place so that we can more easily delete it later on.
 */
#include "scsi_obsolete.h"

/* obsolete typedef junk. */
#include "scsi_typedefs.h"

#endif /* _SCSI_H */
