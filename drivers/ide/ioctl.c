/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * Copyright (C) 2002 Marcin Dalecki <martin@dalecki.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/*
 * Generic ioctl handling for all ATA/ATAPI device drivers.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/blkpg.h>
#include <linux/pci.h>
#include <linux/cdrom.h>
#include <linux/device.h>

#include <linux/ide.h>

#include <asm/uaccess.h>

#include "ioctl.h"

/*
 * NOTE: Due to ridiculous coding habbits in the hdparm utility we have to
 * always return unsigned long in case we are returning simple values.
 */
int ata_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int major, minor;
	struct ata_device *drive;
	struct request rq;
	kdev_t dev;

	dev = inode->i_rdev;
	major = major(dev);
	minor = minor(dev);

	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENODEV;


	/* Contrary to popular beleve we disallow even the reading of the ioctl
	 * values for users which don't have permission too. We do this becouse
	 * such information could be used by an attacker to deply a simple-user
	 * attack, which triggers bugs present only on a particular
	 * configuration.
	 */

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	ide_init_drive_cmd(&rq);
	switch (cmd) {
		case HDIO_GET_32BIT: {
			unsigned long val = drive->channel->io_32bit;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;
			return 0;
		}

		case HDIO_SET_32BIT:
			if (arg < 0 || arg > 1)
				return -EINVAL;

			if (drive->channel->no_io_32bit)
				return -EIO;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			drive->channel->io_32bit = arg;
			spin_unlock_irq(drive->channel->lock);

			return 0;

		case HDIO_SET_PIO_MODE:
			if (arg < 0 || arg > 255)
				return -EINVAL;

			if (!drive->channel->tuneproc)
				return -ENOSYS;

			/* FIXME: we can see that tuneproc whould do the
			 * locking!.
			 */

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			drive->channel->tuneproc(drive, (u8) arg);
			spin_unlock_irq(drive->channel->lock);

			return 0;

		case HDIO_GET_UNMASKINTR: {
			unsigned long val = drive->channel->unmask;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;

			return 0;
		}


		case HDIO_SET_UNMASKINTR:
			if (arg < 0 || arg > 1)
				return -EINVAL;

			if (drive->channel->no_unmask)
				return -EIO;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			drive->channel->unmask = arg;
			spin_unlock_irq(drive->channel->lock);

			return 0;

		case HDIO_GET_DMA: {
			unsigned long val = drive->using_dma;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;

			return 0;
		}

		case HDIO_SET_DMA:
			if (arg < 0 || arg > 1)
				return -EINVAL;

			if (!drive->driver)
				return -EPERM;

			if (!drive->id || !(drive->id->capability & 1) || !drive->channel->XXX_udma)
				return -EPERM;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			udma_enable(drive, arg, 1);
			spin_unlock_irq(drive->channel->lock);

			return 0;

		case HDIO_GETGEO: {
			struct hd_geometry *loc = (struct hd_geometry *) arg;
			unsigned short bios_cyl = drive->bios_cyl; /* truncate */

			if (!loc || (drive->type != ATA_DISK && drive->type != ATA_FLOPPY))
				return -EINVAL;

			if (put_user(drive->bios_head, (byte *) &loc->heads))
				return -EFAULT;

			if (put_user(drive->bios_sect, (byte *) &loc->sectors))
				return -EFAULT;

			if (put_user(bios_cyl, (unsigned short *) &loc->cylinders))
				return -EFAULT;

			if (put_user((unsigned)drive->part[minor(inode->i_rdev)&PARTN_MASK].start_sect,
				(unsigned long *) &loc->start))
				return -EFAULT;

			return 0;
		}

		case HDIO_GETGEO_BIG_RAW: {
			struct hd_big_geometry *loc = (struct hd_big_geometry *) arg;

			if (!loc || (drive->type != ATA_DISK && drive->type != ATA_FLOPPY))
				return -EINVAL;

			if (put_user(drive->head, (u8 *) &loc->heads))
				return -EFAULT;

			if (put_user(drive->sect, (u8 *) &loc->sectors))
				return -EFAULT;

			if (put_user(drive->cyl, (unsigned int *) &loc->cylinders))
				return -EFAULT;

			if (put_user((unsigned)drive->part[minor(inode->i_rdev)&PARTN_MASK].start_sect,
				(unsigned long *) &loc->start))
				return -EFAULT;

			return 0;
		}

		case BLKRRPART: /* Re-read partition tables */
			return ata_revalidate(inode->i_rdev);

		case HDIO_GET_IDENTITY:
			if (minor(inode->i_rdev) & PARTN_MASK)
				return -EINVAL;

			if (drive->id == NULL)
				return -ENOMSG;

			if (copy_to_user((char *)arg, (char *)drive->id, sizeof(*drive->id)))
				return -EFAULT;

			return 0;

		case HDIO_GET_NICE:
			return put_user(drive->dsc_overlap << IDE_NICE_DSC_OVERLAP |
					drive->atapi_overlap << IDE_NICE_ATAPI_OVERLAP,
					(long *) arg);

		case HDIO_DRIVE_CMD:
			if (!capable(CAP_SYS_RAWIO))
				return -EACCES;

			return ide_cmd_ioctl(drive, arg);

		case HDIO_SET_NICE:
			if (arg != (arg & ((1 << IDE_NICE_DSC_OVERLAP))))
				return -EPERM;

			drive->dsc_overlap = (arg >> IDE_NICE_DSC_OVERLAP) & 1;
			/* Only CD-ROM's and tapes support DSC overlap. */
			if (drive->dsc_overlap && !(drive->type == ATA_ROM || drive->type == ATA_TAPE)) {
				drive->dsc_overlap = 0;
				return -EPERM;
			}

			return 0;

		case BLKGETSIZE:
		case BLKGETSIZE64:
		case BLKROSET:
		case BLKROGET:
		case BLKFLSBUF:
		case BLKSSZGET:
		case BLKPG:
		case BLKELVGET:
		case BLKELVSET:
		case BLKBSZGET:
		case BLKBSZSET:
			return blk_ioctl(inode->i_bdev, cmd, arg);

		/*
		 * uniform packet command handling
		 */
		case CDROMEJECT:
		case CDROMCLOSETRAY:
			return block_ioctl(inode->i_bdev, cmd, arg);

		case HDIO_GET_BUSSTATE:
			if (put_user(drive->channel->bus_state, (long *)arg))
				return -EFAULT;

			return 0;

		case HDIO_SET_BUSSTATE:
			if (drive->channel->busproc)
				drive->channel->busproc(drive, (int)arg);

			return 0;

		/* Now check whatever this particular ioctl has a device type
		 * specific implementation.
		 */
		default:
			if (ata_ops(drive) && ata_ops(drive)->ioctl)
				return ata_ops(drive)->ioctl(drive, inode, file, cmd, arg);

			return -EINVAL;
	}
}
