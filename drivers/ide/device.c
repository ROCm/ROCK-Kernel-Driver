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
 * Common low leved device access code. This is the lowest layer of hardware
 * access.
 *
 * This is the place where register set access portability will be handled in
 * the future.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

/*
 * Select a device for operation with possible busy waiting for the operation
 * to complete.
 */
void ata_select(struct ata_device *drive, unsigned long delay)
{
	struct ata_channel *ch = drive->channel;

	if (!ch)
		return;

	if (ch->selectproc)
		ch->selectproc(drive);
	OUT_BYTE(drive->select.all, ch->io_ports[IDE_SELECT_OFFSET]);

	/* The delays during probing for drives can be georgeous.  Deal with
	 * it.
	 */
	if (delay) {
		if (delay >= 1000)
			mdelay(delay / 1000);
		else
			udelay(delay);
	}
}

EXPORT_SYMBOL(ata_select);

/*
 * Handle quirky routing of interrupts.
 */
void ata_mask(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;

	if (!ch)
		return;

	if (ch->maskproc)
		ch->maskproc(drive);
}

/*
 * Check the state of the status register.
 */
int ata_status(struct ata_device *drive, u8 good, u8 bad)
{
	struct ata_channel *ch = drive->channel;

	drive->status = IN_BYTE(ch->io_ports[IDE_STATUS_OFFSET]);

	return (drive->status & (good | bad)) == good;
}

EXPORT_SYMBOL(ata_status);

/*
 * Handle the nIEN - negated Interrupt ENable of the drive.
 * This is controlling whatever the drive will acnowlenge commands
 * with interrupts or not.
 */
int ata_irq_enable(struct ata_device *drive, int on)
{
	struct ata_channel *ch = drive->channel;

	if (!ch->io_ports[IDE_CONTROL_OFFSET])
		return 0;

	if (on)
		OUT_BYTE(0x00, ch->io_ports[IDE_CONTROL_OFFSET]);
	else {
		if (!ch->intrproc)
			OUT_BYTE(0x02, ch->io_ports[IDE_CONTROL_OFFSET]);
		else
			ch->intrproc(drive);
	}

	return 1;
}

EXPORT_SYMBOL(ata_irq_enable);

/*
 * Perform a reset operation on the currently selected drive.
 */
void ata_reset(struct ata_channel *ch)
{
	unsigned long timeout = jiffies + WAIT_WORSTCASE;
	u8 stat;

	if (!ch->io_ports[IDE_CONTROL_OFFSET])
		return;

	printk("%s: reset\n", ch->name);
	OUT_BYTE(0x04, ch->io_ports[IDE_CONTROL_OFFSET]);
	udelay(10);
	OUT_BYTE(0x00, ch->io_ports[IDE_CONTROL_OFFSET]);
	do {
		mdelay(50);
		stat = IN_BYTE(ch->io_ports[IDE_STATUS_OFFSET]);
	} while ((stat & BUSY_STAT) && time_before(jiffies, timeout));
}

EXPORT_SYMBOL(ata_reset);

MODULE_LICENSE("GPL");
