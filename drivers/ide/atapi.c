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
#include <linux/completion.h>
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


/*
 * This function issues a special IDE device request onto the request queue.
 *
 * If action is ide_wait, then the rq is queued at the end of the request
 * queue, and the function sleeps until it has been processed.  This is for use
 * when invoked from an ioctl handler.
 *
 * If action is ide_preempt, then the rq is queued at the head of the request
 * queue, displacing the currently-being-processed request and this function
 * returns immediately without waiting for the new rq to be completed.  This is
 * VERY DANGEROUS, and is intended for careful use by the ATAPI tape/cdrom
 * driver code.
 *
 * If action is ide_end, then the rq is queued at the end of the request queue,
 * and the function returns immediately without waiting for the new rq to be
 * completed. This is again intended for careful use by the ATAPI tape/cdrom
 * driver code.
 */
int ide_do_drive_cmd(struct ata_device *drive, struct request *rq, ide_action_t action)
{
	unsigned long flags;
	struct ata_channel *ch = drive->channel;
	unsigned int major = ch->major;
	request_queue_t *q = &drive->queue;
	struct list_head *queue_head = &q->queue_head;
	DECLARE_COMPLETION(wait);

#ifdef CONFIG_BLK_DEV_PDC4030
	if (ch->chipset == ide_pdc4030 && rq->buffer)
		return -ENOSYS;  /* special drive cmds not supported */
#endif
	rq->errors = 0;
	rq->rq_status = RQ_ACTIVE;
	rq->rq_dev = mk_kdev(major, (drive->select.b.unit) << PARTN_BITS);
	if (action == ide_wait)
		rq->waiting = &wait;

	spin_lock_irqsave(ch->lock, flags);

	if (action == ide_preempt)
		drive->rq = NULL;
	else if (!blk_queue_empty(&drive->queue))
		queue_head = queue_head->prev;	/* ide_end and ide_wait */

	__elv_add_request(q, rq, queue_head);

	do_ide_request(q);

	spin_unlock_irqrestore(ch->lock, flags);

	if (action == ide_wait) {
		wait_for_completion(&wait);	/* wait for it to be serviced */
		return rq->errors ? -EIO : 0;	/* return -EIO if errors */
	}

	return 0;
}

EXPORT_SYMBOL(ide_do_drive_cmd);
EXPORT_SYMBOL(atapi_discard_data);
EXPORT_SYMBOL(atapi_write_zeros);
EXPORT_SYMBOL(atapi_init_pc);

EXPORT_SYMBOL(atapi_read);
EXPORT_SYMBOL(atapi_write);

MODULE_LICENSE("GPL");
