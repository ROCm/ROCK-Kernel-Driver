#ifndef _ATP870U_H

/* $Id: atp870u.h,v 1.0 1997/05/07 15:09:00 root Exp root $

 * Header file for the ACARD 870U/W driver for Linux
 *
 * $Log: atp870u.h,v $
 * Revision 1.0  1997/05/07  15:09:00  root
 * Initial revision
 *
 */

#include <linux/types.h>

/* I/O Port */

#define MAX_CDB 12
#define MAX_SENSE 14

static int atp870u_detect(Scsi_Host_Template *);
static int atp870u_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
static int atp870u_abort(Scsi_Cmnd *);
static int atp870u_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int *);
static int atp870u_release(struct Scsi_Host *);

#define qcnt		32
#define ATP870U_SCATTER 128
#define ATP870U_CMDLUN 1

extern const char *atp870u_info(struct Scsi_Host *);

#endif
