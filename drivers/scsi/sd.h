/*
 *    sd.h Copyright (C) 1992 Drew Eckhardt 
 *      SCSI disk driver header file by
 *              Drew Eckhardt 
 *
 *      <drew@colorado.edu>
 *
 *       Modified by Eric Youngdale eric@andante.org to
 *       add scatter-gather, multiple outstanding request, and other
 *       enhancements.
 */
#ifndef _SD_H
#define _SD_H

#ifndef _SCSI_H
#include "scsi.h"
#endif

#ifndef _GENDISK_H
#include <linux/genhd.h>
#endif

typedef struct scsi_disk {
	unsigned capacity;		/* size in 512-byte sectors */
	Scsi_Device *device;
	unsigned char media_present;
	unsigned char write_prot;
	unsigned has_been_registered:1;
	unsigned WCE:1;         /* state of disk WCE bit */
	unsigned RCD:1;         /* state of disk RCD bit */
} Scsi_Disk;

extern int revalidate_scsidisk(kdev_t dev, int maxusage);

/*
 * Used by pmac to find the device associated with a target.
 */
extern kdev_t sd_find_target(void *host, int tgt);

#define N_SD_MAJORS	8

#define SD_MAJOR_MASK	(N_SD_MAJORS - 1)
#define SD_PARTITION(i)		(((major(i) & SD_MAJOR_MASK) << 8) | (minor(i) & 255))

#endif
