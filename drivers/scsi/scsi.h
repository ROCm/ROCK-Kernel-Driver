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

#ifdef DEBUG
#define SCSI_TIMEOUT (5*HZ)
#else
#define SCSI_TIMEOUT (2*HZ)
#endif

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
