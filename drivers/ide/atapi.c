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
 * Code common among all the ATAPI device drivers.
 *
 * Ideally this should evolve in to a unified driver.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/atapi.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

/*
 * Initializes a packet command. Used by tape and floppy driver.
 */
void atapi_init_pc(struct atapi_packet_command *pc)
{
	memset(pc->c, 0, 12);
	pc->retries = 0;
	pc->flags = 0;
	pc->request_transfer = 0;
	pc->buffer = pc->pc_buffer;
	pc->buffer_size = IDEFLOPPY_PC_BUFFER_SIZE;
	pc->b_data = NULL;
	pc->bio = NULL;
}

/*
 * Too bad. The drive wants to send us data which we are not ready to accept.
 * Just throw it away.
 */
void atapi_discard_data(struct ata_device *drive, unsigned int bcount)
{
	while (bcount--)
		IN_BYTE(IDE_DATA_REG);
}

void atapi_write_zeros(struct ata_device *drive, unsigned int bcount)
{
	while (bcount--)
		OUT_BYTE(0, IDE_DATA_REG);
}

/*
 * The following routines are mainly used by the ATAPI drivers.
 *
 * These routines will round up any request for an odd number of bytes, so if
 * an odd n is specified, be sure that there's at least one extra byte
 * allocated for the buffer.
 */
void atapi_read(struct ata_device *drive, u8 *buf, unsigned int n)
{
	if (drive->channel->atapi_read) {
		drive->channel->atapi_read(drive, buf, n);
		return;
	}

	++n;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		insw_swapw(IDE_DATA_REG, buf, n / 2);
		return;
	}
#endif
	ata_read(drive, buf, n / 4);
	if ((n & 0x03) >= 2)
		insw(IDE_DATA_REG, buf + (n & ~0x03), 1);
}

void atapi_write(struct ata_device *drive, u8 *buf, unsigned int n)
{
	if (drive->channel->atapi_write) {
		drive->channel->atapi_write(drive, buf, n);
		return;
	}

	++n;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		outsw_swapw(IDE_DATA_REG, buf, n / 2);
		return;
	}
#endif
	ata_write(drive, buf, n / 4);
	if ((n & 0x03) >= 2)
		outsw(IDE_DATA_REG, buf + (n & ~0x03), 1);
}

EXPORT_SYMBOL(atapi_discard_data);
EXPORT_SYMBOL(atapi_write_zeros);
EXPORT_SYMBOL(atapi_init_pc);

EXPORT_SYMBOL(atapi_read);
EXPORT_SYMBOL(atapi_write);

MODULE_LICENSE("GPL");
