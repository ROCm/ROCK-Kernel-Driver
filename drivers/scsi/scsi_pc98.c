/*
 *  Copyright (C) 2003  Osamu Tomita <tomita@cinet.co.jp>
 *
 *  PC9801 BIOS geometry handling.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <asm/pc9800.h>

#include "scsi.h"
#include "hosts.h"


static int pc98_first_bios_param(struct scsi_device *sdev, int *ip)
{
	const u8 *p = (&__PC9800SCA(u8, PC9800SCA_SCSI_PARAMS) + sdev->id * 4);

	ip[0] = p[1];   /* # of heads */
	ip[1] = p[0];   /* # of sectors/track */
	ip[2] = *(u16 *)&p[2] & 0x0fff; /* # of cylinders */
	if (p[3] & (1 << 6)) { /* #-of-cylinders is 16-bit */
		ip[2] |= (ip[0] & 0xf0) << 8;
		ip[0] &= 0x0f;
	}

	return 0;
}

int pc98_bios_param(struct scsi_device *sdev, struct block_device *bdev,
			sector_t capacity, int *ip)
{
	struct Scsi_Host *first_real = first_real_host();

	/*
	 * XXX
	 * XXX This needs to become a sysfs attribute that's set
	 * XXX by code that knows which host is the first one.
	 * XXX
	 * XXX Currently we support only one host on with a
	 * XXX PC98ish HBA.
	 * XXX
	 */
	if (1 || sdev->host == first_real && sdev->id < 7 &&
	    __PC9800SCA_TEST_BIT(PC9800SCA_DISK_EQUIPS, sdev->id))
	    	return pc98_first_bios_param(sdev, ip);

	/* Assume PC-9801-92 compatible parameters for HAs without BIOS.  */
	ip[0] = 8;
	ip[1] = 32;
	ip[2] = capacity / (8 * 32);
	if (ip[2] > 65535) {    /* if capacity >= 8GB */
		/* Recent on-board adapters seem to use this parameter. */
		ip[1] = 128;
		ip[2] = capacity / (8 * 128);
		if (ip[2] > 65535) { /* if capacity >= 32GB  */
			/* Clip the number of cylinders.  Currently
			   this is the limit that we deal with.  */
			ip[2] = 65535;
		}
	}

	return 0;
}

EXPORT_SYMBOL(pc98_bios_param);
