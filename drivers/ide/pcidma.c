/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 2002	     Marcin Dalecki <martin@dalecki.de>
 *
 *  Based on previous work by:
 *
 *  Copyright (c) 1999-2000  Andre Hedrick <andre@linux-ide.org>
 *  Copyright (c) 1995-1998  Mark Lord
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * Those are the generic BM DMA support functions for PCI bus based systems.
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/delay.h>

#include "ata-timing.h"

#include <asm/io.h>
#include <asm/irq.h>

#define DEFAULT_BMIBA	0xe800	/* in case BIOS did not init it */
#define DEFAULT_BMCRBA	0xcc00	/* VIA's default value */
#define DEFAULT_BMALIBA	0xd400	/* ALI's default value */

/*
 * This is the handler for disk read/write DMA interrupts.
 */
ide_startstop_t ide_dma_intr(struct ata_device *drive, struct request *rq)
{
	u8 dma_stat;
	dma_stat = udma_stop(drive);

	if (ata_status(drive, DRIVE_READY, drive->bad_wstat | DRQ_STAT)) {
		if (!dma_stat) {
			__ata_end_request(drive, rq, 1, rq->nr_sectors);

			return ATA_OP_FINISHED;
		}
		printk(KERN_ERR "%s: dma_intr: bad DMA status (dma_stat=%x)\n",
		       drive->name, dma_stat);
	}

	return ata_error(drive, rq, __FUNCTION__);
}

/*
 * FIXME: taskfiles should be a map of pages, not a long virt address... /jens
 * FIXME: I agree with Jens --mdcki!
 */
static int build_sglist(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	struct scatterlist *sg = ch->sg_table;
	int nents = 0;

	if ((rq->flags & REQ_SPECIAL) && (drive->type == ATA_DISK)) {
		struct ata_taskfile *args = rq->special;
#if 1
		unsigned char *virt_addr = rq->buffer;
		int sector_count = rq->nr_sectors;
#else
		nents = blk_rq_map_sg(&drive->queue, rq, ch->sg_table);

		if (nents > rq->nr_segments)
			printk("ide-dma: received %d segments, build %d\n", rq->nr_segments, nents);
#endif

		if (args->command_type == IDE_DRIVE_TASK_RAW_WRITE)
			ch->sg_dma_direction = PCI_DMA_TODEVICE;
		else
			ch->sg_dma_direction = PCI_DMA_FROMDEVICE;

		/*
		 * FIXME: This depends upon a hard coded page size!
		 */
		if (sector_count > 128) {
			memset(&sg[nents], 0, sizeof(*sg));

			sg[nents].page = virt_to_page(virt_addr);
			sg[nents].offset = (unsigned long) virt_addr & ~PAGE_MASK;
			sg[nents].length = 128  * SECTOR_SIZE;
			++nents;
			virt_addr = virt_addr + (128 * SECTOR_SIZE);
			sector_count -= 128;
		}
		memset(&sg[nents], 0, sizeof(*sg));
		sg[nents].page = virt_to_page(virt_addr);
		sg[nents].offset = (unsigned long) virt_addr & ~PAGE_MASK;
		sg[nents].length =  sector_count  * SECTOR_SIZE;
		++nents;
	} else {
		nents = blk_rq_map_sg(&drive->queue, rq, ch->sg_table);

		if (rq->q && nents > rq->nr_phys_segments)
			printk("ide-dma: received %d phys segments, build %d\n", rq->nr_phys_segments, nents);

		if (rq_data_dir(rq) == READ)
			ch->sg_dma_direction = PCI_DMA_FROMDEVICE;
		else
			ch->sg_dma_direction = PCI_DMA_TODEVICE;

	}

	return pci_map_sg(ch->pci_dev, sg, nents, ch->sg_dma_direction);
}

/*
 * 1 dma-ing, 2 error, 4 intr
 */
static ide_startstop_t dma_timer_expiry(struct ata_device *drive, struct request *rq, unsigned long *wait)
{
	/* FIXME: What's that? */
	u8 dma_stat = inb(drive->channel->dma_base + 2);

#ifdef DEBUG
	printk("%s: dma_timer_expiry: dma status == 0x%02x\n", drive->name, dma_stat);
#endif

#if 0
	drive->expiry = NULL;	/* one free ride for now */
#endif
	*wait = 0;
	if (dma_stat & 2) {	/* ERROR */
		ata_status(drive, 0, 0);
		return ata_error(drive, rq, __FUNCTION__);
	}
	if (dma_stat & 1) {	/* DMAing */
		*wait = WAIT_CMD;
		return ATA_OP_CONTINUES;
	}

	return ATA_OP_FINISHED;
}

int ata_start_dma(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;
	unsigned int reading = 0;

	if (rq_data_dir(rq) == READ)
		reading = 1 << 3;

	/* try PIO instead of DMA */
	if (!udma_new_table(drive, rq))
		return 1;

	outl(ch->dmatable_dma, dma_base + 4); /* PRD table */
	outb(reading, dma_base);		/* specify r/w */
	outb(inb(dma_base+2)|6, dma_base+2);	/* clear INTR & ERROR flags */

	return 0;
}

/* generic udma_setup() function for drivers having ->speedproc/tuneproc */
int udma_generic_setup(struct ata_device *drive, int map)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *ch = drive->channel;
	int on = 0;
	u8 mode;

	if (!id || (drive->type != ATA_DISK && ch->no_atapi_autodma))
		return 0;

	if ((map & XFER_UDMA_80W) && !eighty_ninty_three(drive))
		map &= ~XFER_UDMA_80W;

	if ((id->capability & 1) && ch->autodma && ch->speedproc) {

		/* Consult the list of known "bad" devices. */
		if (udma_black_list(drive))
			goto set_dma;

		mode = ata_timing_mode(drive, map);

		/* Device is UltraDMA capable. */
		if (mode & XFER_UDMA) {
			if((on = !ch->speedproc(drive, mode)))
				goto set_dma;

			printk(KERN_WARNING "%s: UDMA auto-tune failed.\n", drive->name);

			map &= ~XFER_UDMA_ALL;
			mode = ata_timing_mode(drive, map);
		}

		/* Device is regular DMA capable. */
		if (mode & (XFER_SWDMA | XFER_MWDMA)) {
			if((on = !ch->speedproc(drive, mode)))
				goto set_dma;

			printk(KERN_WARNING "%s: DMA auto-tune failed.\n", drive->name);
		}

		/* FIXME: this seems non-functional  --bkz */
		/* Consult the list of known "good" devices. */
		if (udma_white_list(drive)) {

			if (id->eide_dma_time > 150)
				goto set_dma;

			printk(KERN_INFO "%s: device is on DMA whitelist.\n", drive->name);
//			on = 1;
		}

		/* Revert to PIO. */
		if (!on && ch->tuneproc)
			ch->tuneproc(drive, 255);
	}

set_dma:
	udma_enable(drive, on, !on);

	return 0;
}

/*
 * Configure a device for DMA operation.
 */
int udma_pci_setup(struct ata_device *drive, int map)
{
	int config_allows_dma = 1;
	struct hd_driveid *id = drive->id;
	struct ata_channel *ch = drive->channel;

#ifdef CONFIG_IDEDMA_ONLYDISK
	if (drive->type != ATA_DISK)
		config_allows_dma = 0;
#endif

	if (id && (id->capability & 1) && ch->autodma && config_allows_dma) {
		/* Consult the list of known "bad" drives */
		if (udma_black_list(drive)) {
			udma_enable(drive, 0, 1);

			return 0;
		}

		/* Enable DMA on any drive that has UltraDMA (mode 6/7/?) enabled */
		if ((id->field_valid & 4) && (eighty_ninty_three(drive)))
			if ((id->dma_ultra & (id->dma_ultra >> 14) & 2)) {
				udma_enable(drive, 1, 1);

				return 0;
			}
		/* Enable DMA on any drive that has UltraDMA (mode 3/4/5) enabled */
		if ((id->field_valid & 4) && (eighty_ninty_three(drive)))
			if ((id->dma_ultra & (id->dma_ultra >> 11) & 7)) {
				udma_enable(drive, 1, 1);

				return 0;
			}
		/* Enable DMA on any drive that has UltraDMA (mode 0/1/2) enabled */
		if (id->field_valid & 4)	/* UltraDMA */
			if ((id->dma_ultra & (id->dma_ultra >> 8) & 7)) {
				udma_enable(drive, 1, 1);

				return 0;
			}
		/* Enable DMA on any drive that has mode2 DMA (multi or single) enabled */
		if (id->field_valid & 2)	/* regular DMA */
			if ((id->dma_mword & 0x404) == 0x404 || (id->dma_1word & 0x404) == 0x404) {
				udma_enable(drive, 1, 1);

				return 0;
			}
		/* Consult the list of known "good" drives */
		if (udma_white_list(drive)) {
			udma_enable(drive, 1, 1);

			return 0;
		}
	}
	udma_enable(drive, 0, 0);

	return 0;
}

/*
 * Needed for allowing full modular support of ide-driver
 */
void ide_release_dma(struct ata_channel *ch)
{
	if (!ch->dma_base)
		return;

	if (ch->dmatable_cpu) {
		pci_free_consistent(ch->pci_dev,
				    PRD_ENTRIES * PRD_BYTES,
				    ch->dmatable_cpu,
				    ch->dmatable_dma);
		ch->dmatable_cpu = NULL;
	}
	if (ch->sg_table) {
		kfree(ch->sg_table);
		ch->sg_table = NULL;
	}
	if ((ch->dma_extra) && (ch->unit == 0))
		release_region((ch->dma_base + 16), ch->dma_extra);
	release_region(ch->dma_base, 8);
	ch->dma_base = 0;
}

/****************************************************************************
 * PCI specific UDMA channel method implementations.
 */

/*
 * This is the generic part of the DMA setup used by the host chipset drivers
 * in the corresponding DMA setup method.
 *
 * FIXME: there are some places where this gets used driectly for "error
 * recovery" in the ATAPI drivers. This was just plain wrong before, in esp.
 * not portable, and just got uncovered now.
 */
void udma_pci_enable(struct ata_device *drive, int on, int verbose)
{
	struct ata_channel *ch = drive->channel;
	int set_high = 1;
	u8 unit;
	u64 addr;

	/* Fall back to the default implementation. */
	unit = (drive->select.b.unit & 0x01);
	addr = BLK_BOUNCE_HIGH;

	if (!on) {
		if (verbose)
			printk("%s: DMA disabled\n", drive->name);
		set_high = 0;
		outb(inb(ch->dma_base + 2) & ~(1 << (5 + unit)), ch->dma_base + 2);
#ifdef CONFIG_BLK_DEV_IDE_TCQ
		udma_tcq_enable(drive, 0);
#endif
	}

	/* toggle bounce buffers */

	if (on && drive->type == ATA_DISK && drive->channel->highmem) {
		if (!PCI_DMA_BUS_IS_PHYS)
			addr = BLK_BOUNCE_ANY;
		else
			addr = drive->channel->pci_dev->dma_mask;
	}

	blk_queue_bounce_limit(&drive->queue, addr);

	drive->using_dma = on;

	if (on) {
		outb(inb(ch->dma_base + 2) | (1 << (5 + unit)), ch->dma_base + 2);
#ifdef CONFIG_BLK_DEV_IDE_TCQ_DEFAULT
		udma_tcq_enable(drive, 1);
#endif
	}
}

/*
 * This prepares a dma request.  Returns 0 if all went okay, returns 1
 * otherwise.  May also be invoked from trm290.c
 */
int udma_new_table(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	unsigned int *table = ch->dmatable_cpu;
#ifdef CONFIG_BLK_DEV_TRM290
	unsigned int is_trm290_chipset = (ch->chipset == ide_trm290);
#else
	const int is_trm290_chipset = 0;
#endif
	unsigned int count = 0;
	int i;
	struct scatterlist *sg;

	ch->sg_nents = i = build_sglist(drive, rq);
	if (!i)
		return 0;

	sg = ch->sg_table;
	while (i) {
		u32 cur_addr;
		u32 cur_len;

		cur_addr = sg_dma_address(sg);
		cur_len = sg_dma_len(sg);

		/*
		 * Fill in the dma table, without crossing any 64kB boundaries.
		 * Most hardware requires 16-bit alignment of all blocks,
		 * but the trm290 requires 32-bit alignment.
		 */

		while (cur_len) {
			u32 xcount, bcount = 0x10000 - (cur_addr & 0xffff);

			if (count++ >= PRD_ENTRIES) {
				printk("ide-dma: count %d, sg_nents %d, cur_len %d, cur_addr %u\n",
						count, ch->sg_nents, cur_len, cur_addr);
				BUG();
			}

			if (bcount > cur_len)
				bcount = cur_len;
			*table++ = cpu_to_le32(cur_addr);
			xcount = bcount & 0xffff;
			if (is_trm290_chipset)
				xcount = ((xcount >> 2) - 1) << 16;
			if (xcount == 0x0000) {
		        /*
			 * Most chipsets correctly interpret a length of
			 * 0x0000 as 64KB, but at least one (e.g. CS5530)
			 * misinterprets it as zero (!). So here we break
			 * the 64KB entry into two 32KB entries instead.
			 */
				if (count++ >= PRD_ENTRIES) {
					pci_unmap_sg(ch->pci_dev, sg,
						     ch->sg_nents,
						     ch->sg_dma_direction);
					return 0;
				}

				*table++ = cpu_to_le32(0x8000);
				*table++ = cpu_to_le32(cur_addr + 0x8000);
				xcount = 0x8000;
			}
			*table++ = cpu_to_le32(xcount);
			cur_addr += bcount;
			cur_len -= bcount;
		}

		sg++;
		i--;
	}

	if (!count)
		printk(KERN_ERR "%s: empty DMA table?\n", ch->name);
	else if (!is_trm290_chipset)
		*--table |= cpu_to_le32(0x80000000);

	return count;
}

/*
 * Teardown mappings after DMA has completed.
 */
void udma_destroy_table(struct ata_channel *ch)
{
	pci_unmap_sg(ch->pci_dev, ch->sg_table, ch->sg_nents, ch->sg_dma_direction);
}

/*
 * Prepare the channel for a DMA startfer. Please note that only the broken
 * Pacific Digital host chip needs the reques to be passed there to decide
 * about addressing modes.
 */
void udma_pci_start(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;

	/* Note that this is done *after* the cmd has been issued to the drive,
	 * as per the BM-IDE spec.  The Promise Ultra33 doesn't work correctly
	 * when we do this part before issuing the drive cmd.
	 */
	outb(inb(dma_base) | 1, dma_base);	/* start DMA */
}

int udma_pci_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;
	u8 dma_stat;

	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	udma_destroy_table(ch);			/* purge DMA mappings */

	return (dma_stat & 7) != 4 ? (0x10 | dma_stat) : 0;	/* verify good DMA status */
}

/*
 * FIXME: This should be attached to a channel as we can see now!
 */
int udma_pci_irq_status(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	u8 dma_stat;

	/* default action */
	dma_stat = inb(ch->dma_base + 2);

	return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */
}

void udma_pci_timeout(struct ata_device *drive)
{
	printk(KERN_ERR "ATA: UDMA timeout occured %s!\n", drive->name);
}

void udma_pci_irq_lost(struct ata_device *drive)
{
}

/*
 * This can be called for a dynamically installed interface. Don't __init it
 */
void ata_init_dma(struct ata_channel *ch, unsigned long dma_base)
{
	if (!request_region(dma_base, 8, ch->name)) {
		printk(KERN_ERR "ATA: ERROR: BM DMA portst already in use!\n");

		return;
	}
	printk(KERN_INFO"    %s: BM-DMA at 0x%04lx-0x%04lx", ch->name, dma_base, dma_base + 7);
	ch->dma_base = dma_base;
	ch->dmatable_cpu = pci_alloc_consistent(ch->pci_dev,
						  PRD_ENTRIES * PRD_BYTES,
						  &ch->dmatable_dma);
	if (ch->dmatable_cpu == NULL)
		goto dma_alloc_failure;

	ch->sg_table = kmalloc(sizeof(struct scatterlist) * PRD_ENTRIES,
				 GFP_KERNEL);
	if (ch->sg_table == NULL) {
		pci_free_consistent(ch->pci_dev, PRD_ENTRIES * PRD_BYTES,
				    ch->dmatable_cpu, ch->dmatable_dma);
		goto dma_alloc_failure;
	}

	/*
	 * We could just assign them, and then leave it up to the chipset
	 * specific code to override these after they've called this function.
	 */
	if (!ch->udma_setup)
		ch->udma_setup = udma_pci_setup;
	if (!ch->udma_enable)
		ch->udma_enable = udma_pci_enable;
	if (!ch->udma_start)
		ch->udma_start = udma_pci_start;
	if (!ch->udma_stop)
		ch->udma_stop = udma_pci_stop;
	if (!ch->udma_init)
		ch->udma_init = udma_pci_init;
	if (!ch->udma_irq_status)
		ch->udma_irq_status = udma_pci_irq_status;
	if (!ch->udma_timeout)
		ch->udma_timeout = udma_pci_timeout;
	if (!ch->udma_irq_lost)
		ch->udma_irq_lost = udma_pci_irq_lost;

	if (ch->chipset != ide_trm290) {
		u8 dma_stat = inb(dma_base+2);
		printk(", BIOS settings: %s:%s, %s:%s",
		       ch->drives[0].name, (dma_stat & 0x20) ? "DMA" : "pio",
		       ch->drives[1].name, (dma_stat & 0x40) ? "DMA" : "pio");
	}
	printk("\n");
	return;

dma_alloc_failure:
	printk(" -- ERROR, UNABLE TO ALLOCATE DMA TABLES\n");
}

/*
 * This is the default read write function.
 *
 * It's exported only for host chips which use it for fallback or (too) late
 * capability checking.
 */
int udma_pci_init(struct ata_device *drive, struct request *rq)
{
	u8 cmd;

	if (ata_start_dma(drive, rq))
		return ATA_OP_FINISHED;

	/* No DMA transfers on ATAPI devices. */
	if (drive->type != ATA_DISK)
		return ATA_OP_CONTINUES;

	if (rq_data_dir(rq) == READ)
		cmd = 0x08;
	else
		cmd = 0x00;

	ata_set_handler(drive, ide_dma_intr, WAIT_CMD, dma_timer_expiry);
	if (drive->addressing)
		outb(cmd ? WIN_READDMA_EXT : WIN_WRITEDMA_EXT, IDE_COMMAND_REG);
	else
		outb(cmd ? WIN_READDMA : WIN_WRITEDMA, IDE_COMMAND_REG);

	udma_start(drive, rq);

	return ATA_OP_CONTINUES;
}

EXPORT_SYMBOL(ide_dma_intr);
EXPORT_SYMBOL(udma_pci_enable);
EXPORT_SYMBOL(udma_pci_start);
EXPORT_SYMBOL(udma_pci_stop);
EXPORT_SYMBOL(udma_pci_init);
EXPORT_SYMBOL(udma_pci_irq_status);
EXPORT_SYMBOL(udma_pci_timeout);
EXPORT_SYMBOL(udma_pci_irq_lost);
