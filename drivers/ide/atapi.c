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

EXPORT_SYMBOL(atapi_discard_data);
EXPORT_SYMBOL(atapi_write_zeros);
EXPORT_SYMBOL(atapi_init_pc);

MODULE_LICENSE("GPL");
