/*
 * MTD chip driver for M5M29GT320VP
 *
 * Copyright (C) 2003   Takeo Takahashi
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * $Id$
 */

#ifndef __KERNEL__
#  define __KERNEL__
#endif

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/delay.h>

#define M5DRV_DEBUG(n, args...) if ((n) & m5drv_debug) printk(KERN_DEBUG args)

#undef UNLOCK_BEFORE_ERASE

#define M5DRV_PAGE_SIZE		(256)		/* page program size */
#define M5DRV_BLOCK_SIZE8	(8*1024)	/* 8K block size in byte */
#define M5DRV_BLOCK_SIZE64	(64*1024)	/* 64K block size in byte */
#define M5DRV_MAX_BLOCK_NUM	70		/* number of blocks */
#define M5DRV_ERASE_REGION	2		/* 64KB and 8KB */

/*
 * Software commands
 */
#define CMD_READ_ARRAY          0xff
#define CMD_DEVICE_IDENT        0x90
#define CMD_READ_STATUS         0x70
#define CMD_CLEAR_STATUS        0x50
#define CMD_BLOCK_ERASE         0x20
#define CMD_CONFIRM             0xd0
#define CMD_PROGRAM_BYTE        0x40
#define CMD_PROGRAM_WORD        CMD_PROGRAM_BYTE
#define CMD_PROGRAM_PAGE        0x41
#define CMD_SINGLE_LOAD_DATA    0x74
#define CMD_BUFF2FLASH          0x0e
#define CMD_FLASH2BUFF          0xf1
#define CMD_CLEAR_BUFF          0x55
#define CMD_SUSPEND             0xb0
#define CMD_RESUME              0xd0
#define IDENT_OFFSET        	0	/* indent command offset */

/*
 * Status
 */
#define STATUS_READY              0x80 /* 0:busy 1:ready */
#define STATUS_SUSPEND            0x40 /* 0:progress/complete 1:suspend */
#define STATUS_ERASE              0x20 /* 0:pass 1:error */
#define STATUS_PROGRAM            0x10 /* 0:pass 1:error */
#define STATUS_BLOCK              0x08 /* 0:pass 1:error */

/*
 * Device Code
 */
#define MAKER		(0x1c)
#define M5M29GT320VP	(0x20)
#define M5M29GB320VP	(0x21)

static const char version[] = "M5DRV Flash Driver";
static const char date[] = __DATE__;
static const char time[] = __TIME__;
static int m5drv_debug = 0;
MODULE_PARM(m5drv_debug, "i");

struct m5drv_info {
	struct flchip *chip;
	int chipshift;
	int numchips;
	struct flchip chips[1];
	unsigned char buf[M5DRV_BLOCK_SIZE64];
#define M5BUF	(m5drv->buf)
};

struct mtd_info *m5drv_probe(struct map_info *map);
static int m5drv_probe_map(struct map_info *map, struct mtd_info *mtd);
static int m5drv_wait(struct map_info *map, struct flchip *chip, loff_t adr);
static void m5drv_release(struct flchip *chip);
static int m5drv_query_blksize(loff_t ofs);
static int m5drv_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
static int m5drv_read_oneblock(struct map_info *map, loff_t from);
static int m5drv_write(struct mtd_info *mtd, loff_t adr, size_t len, size_t *retlen, const u_char *buf);
static int m5drv_write_oneblock(struct map_info *map, loff_t adr, size_t len, const u_char *buf);
static int m5drv_write_onepage(struct map_info *map, struct flchip *chip, unsigned long adr, const u_char *buf);
static int m5drv_erase(struct mtd_info *mtd, struct erase_info *instr);
static int m5drv_do_wait_for_ready(struct map_info *map, struct flchip *chip, unsigned long adr);
static int m5drv_erase_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr);
static void m5drv_sync(struct mtd_info *mtd);
static int m5drv_suspend(struct mtd_info *mtd);
static void m5drv_resume(struct mtd_info *mtd);
static void m5drv_destroy(struct mtd_info *mtd);
#ifdef UNLOCK_BEFORE_ERASE
static void m5drv_unlock_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr);
#endif

static struct mtd_chip_driver m5drv_chipdrv = {
	probe:		m5drv_probe,
	destroy:	m5drv_destroy,
	name:		"m5drv",
	module:		THIS_MODULE
};

struct mtd_info *m5drv_probe(struct map_info *map)
{
	struct mtd_info *mtd = NULL;
	struct m5drv_info *m5drv = NULL;
	int width;

	mtd = kmalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd) {
		printk("m5drv: can not allocate memory for mtd_info\n");
		return NULL;
	}

	m5drv = kmalloc(sizeof(*m5drv), GFP_KERNEL);
	if (!m5drv) {
		printk("m5drv: can not allocate memory for m5drv_info\n");
		kfree(mtd);
		return NULL;
	}

	memset(mtd, 0, sizeof(*mtd));
	width = m5drv_probe_map(map, mtd);
	if (!width) {
		printk("m5drv: m5drv_probe_map error (width=%d)\n", width);
		kfree(mtd);
		kfree(m5drv);
		return NULL;
	}
	mtd->priv = map;
	mtd->type = MTD_OTHER;
	mtd->erase = m5drv_erase;
	mtd->read = m5drv_read;
	mtd->write = m5drv_write;
	mtd->sync = m5drv_sync;
	mtd->suspend = m5drv_suspend;
	mtd->resume = m5drv_resume;
	mtd->flags = MTD_CAP_NORFLASH;	/* ??? */
	mtd->name = map->name;

	memset(m5drv, 0, sizeof(*m5drv));
	m5drv->chipshift = 23;
	m5drv->numchips = 1;
	m5drv->chips[0].start = 0;
	m5drv->chips[0].state = FL_READY;
	m5drv->chips[0].mutex = &m5drv->chips[0]._spinlock;
	m5drv->chips[0].word_write_time = 0;
	init_waitqueue_head(&m5drv->chips[0].wq);
	spin_lock_init(&m5drv->chips[0]._spinlock);

	map->fldrv = &m5drv_chipdrv;
	map->fldrv_priv = m5drv;

	MOD_INC_USE_COUNT;
	return mtd;
}

static int m5drv_probe_map(struct map_info *map, struct mtd_info *mtd)
{
	u16 tmp;
	u16 maker, device;
	int width = 2;
	struct mtd_erase_region_info *einfo;

	map->write16(map, CMD_READ_ARRAY, IDENT_OFFSET);
	tmp = map->read16(map, IDENT_OFFSET);
	map->write16(map, CMD_DEVICE_IDENT, IDENT_OFFSET);
	maker = map->read16(map, IDENT_OFFSET);
	maker &= 0xff;
	if (maker == MAKER) {
		/* FIXME: check device */
		device = maker >> 8;
		printk("m5drv: detected M5M29GT320VP\n");
		einfo = kmalloc(sizeof(*einfo) * M5DRV_ERASE_REGION, GFP_KERNEL);
		if (!einfo) {
			printk("m5drv: cannot allocate memory for erase_region\n");
			return 0;
		}
		/* 64KB erase block (blk no# 0-62) */
		einfo[0].offset = 0;
		einfo[0].erasesize = 0x8000 * width;
		einfo[0].numblocks = (7 + 8 + 24 + 24);
		/* 8KB erase block (blk no# 63-70) */
		einfo[1].offset = 0x3f0000;
		einfo[1].erasesize = 0x1000 * width;
		einfo[1].numblocks = (2 + 8);
		mtd->numeraseregions = M5DRV_ERASE_REGION;
		mtd->eraseregions = einfo;
		mtd->size = 0x200000 * width;		/* total 4MB */
		/*
		 * mtd->erasesize is used in parse_xxx_partitions.
		 * last erase block has a partition table.
		 */
		mtd->erasesize = 0x8000 * width;
		return width;
	} else if (map->read16(map, IDENT_OFFSET) == CMD_DEVICE_IDENT) {
		printk("m5drv: looks like RAM\n");
		map->write16(map, tmp, IDENT_OFFSET);
	} else {
		printk("m5drv: can not detect flash memory (0x%04x)\n", maker);
	}
	map->write16(map, CMD_READ_ARRAY, IDENT_OFFSET);
	return 0;
}

static int m5drv_query_blksize(loff_t ofs)
{
	int blk;

	blk = ofs >> 16;
	if (blk > 0x3f) {
		printk("m5drv: out of block address (0x%08x)\n", (u32)ofs);
		return M5DRV_BLOCK_SIZE64;
	}
	if (blk == 63) blk += ((ofs & 0x0000e000) >> 13);
	if (blk > M5DRV_MAX_BLOCK_NUM) {
		printk("m5drv: out of block address (0x%08x)\n", (u32)ofs);
		return M5DRV_BLOCK_SIZE64;
	}
	return ((blk >= 63)? M5DRV_BLOCK_SIZE8:M5DRV_BLOCK_SIZE64);
}

static int m5drv_wait(struct map_info *map, struct flchip *chip, loff_t adr)
{
	__u16 status;
	unsigned long timeo;
	DECLARE_WAITQUEUE(wait, current);

 	timeo = jiffies + HZ;
	adr &= ~1;	/* align 2 */

retry:
	spin_lock_bh(chip->mutex);

	switch (chip->state) {
	case FL_READY:
		map->write16(map, CMD_READ_STATUS, adr);
		chip->state = FL_STATUS;
	case FL_STATUS:
		status = map->read16(map, adr);
		if ((status & STATUS_READY) != STATUS_READY) {
			udelay(100);
		}
		break;
	default:
		printk("m5drv: waiting for chip\n");
		if (time_after(jiffies, timeo)) { /* jiffies is after timeo */
			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			spin_unlock_bh(chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			spin_lock_bh(chip->mutex);	// by takeo
			if (signal_pending(current)) {
				printk("m5drv: canceled\n");
				map->write16(map, CMD_CLEAR_STATUS, adr);
				map->write16(map, CMD_READ_ARRAY, adr);
				chip->state = FL_READY;
				return -EINTR;
			}
		}
		timeo = jiffies + HZ;
		goto retry;
	}
	map->write16(map, CMD_READ_ARRAY, adr);
	chip->state = FL_READY;
	return 0;
}

static void m5drv_release(struct flchip *chip)
{
	M5DRV_DEBUG(1, "m5drv_release\n");
	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);
}

static int m5drv_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct m5drv_info *m5drv = map->fldrv_priv;
	int chipnum;
	int ret;

	*retlen = 0;

	chipnum = (from >> m5drv->chipshift);
	if (chipnum >= m5drv->numchips) {
		printk("m5drv: out of chip number (%d)\n", chipnum);
		return -EIO;
	}

	/* We don't support erase suspend */
	ret = m5drv_wait(map, &m5drv->chips[chipnum], from);
	if (ret < 0) return ret;

	map->copy_from(map, buf, from, len);

	m5drv_release(&m5drv->chips[chipnum]);
	*retlen = len;
	return 0;
}

static int m5drv_read_oneblock(struct map_info *map, loff_t from)
{
	struct m5drv_info *m5drv = map->fldrv_priv;
	int ofs;
	int ret;
	int blksize;
	int chipnum;

	M5DRV_DEBUG(1, "m5drv_read_oneblock(0x%08x)\n", (u32)from);
	chipnum = (from >> m5drv->chipshift);
	blksize = m5drv_query_blksize(from);
	ofs = (from & ~(blksize - 1));

	ret = m5drv_wait(map, &m5drv->chips[chipnum], from);
	if (ret < 0) return ret;

	map->copy_from(map, M5BUF, ofs, blksize);

	m5drv_release(&m5drv->chips[chipnum]);
	return 0;
}

static int m5drv_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct m5drv_info *m5drv = map->fldrv_priv;
	int ret = 0;
	int blksize;
	int chipnum;
	int thislen;

	M5DRV_DEBUG(1, "m5drv_write(to=0x%08x, len=%d, buf=0x%08x\n", (u32)to, (u32)len, (u32)buf);
	*retlen = 0;
	blksize = m5drv_query_blksize(to);
	chipnum = (to >> m5drv->chipshift);

	/*
	 * we does not support byte/word program yet.
	 */
	for (thislen = len; thislen > 0; thislen -= blksize) {
		thislen = ((thislen >= blksize)? blksize:thislen);
		ret = m5drv_write_oneblock(map, to, thislen, buf);
		if (ret < 0) return ret;
		to += blksize;
		buf += blksize;
		*retlen += thislen;
	}
	return 0;
}

static int m5drv_write_oneblock(struct map_info *map, loff_t adr, size_t len, const u_char *buf)
{
	struct m5drv_info *m5drv = map->fldrv_priv;
	int ofs;
	int blksize;
	int ret;
	int chipnum;
	int i;

	M5DRV_DEBUG(1, "m5drv_write_oneblock(0x%08x, %d)\n", (u32)adr, (u32)len);
	chipnum = (adr >> m5drv->chipshift);
	ret = m5drv_read_oneblock(map, adr);
	if (ret < 0) return ret;
	blksize = m5drv_query_blksize(adr);
	ofs = (adr & (blksize - 1));
	adr = adr & ~(blksize - 1);
	memcpy(M5BUF + ofs, buf, len);	/* copy to block buffer */
#if 0	/*
	 * FIXME: erasing is unnecessary.
	 */
	ret = m5drv_erase_oneblock(map, &m5drv->chips[chipnum], adr);
	if (ret < 0) return ret;
#endif
	for (i = 0; i < len; i += M5DRV_PAGE_SIZE) {
		ret = m5drv_write_onepage(map, &m5drv->chips[chipnum], adr, M5BUF+i);
		if (ret < 0) return ret;
		adr += M5DRV_PAGE_SIZE;
	}
	return 0;
}

static int m5drv_write_onepage(struct map_info *map, struct flchip *chip, unsigned long adr, const u_char *buf)
{
	int ret;
	int i;
	u_short data;
	long padr;	/* page address */
	u_short status;
	int chipnum;
	struct m5drv_info *m5drv = map->fldrv_priv;

	M5DRV_DEBUG(1, "m5drv_write_onepage(0x%08x, 0x%08x)\n", (u32)adr, (u32)buf);
	padr = adr;
	padr &= ~1;	/* align 2 */
	chipnum = (adr >> m5drv->chipshift);

	ret = m5drv_wait(map, chip, padr);
	if (ret < 0) return ret;

	map->write16(map, CMD_PROGRAM_PAGE, padr);
	chip->state = FL_WRITING;
	for (i = 0; i < M5DRV_PAGE_SIZE; i += map->buswidth) {
		data = ((*buf << 8)| *(buf + 1));
		/*
		 * FIXME: convert be->le ?
		 */
		map->write16(map, data, adr);
		adr += map->buswidth;
		buf += map->buswidth;
	}

	ret = m5drv_do_wait_for_ready(map, chip, padr);
	if (ret < 0) {
		m5drv_release(&m5drv->chips[chipnum]);
		return ret;
	}

	status = map->read16(map, padr);
	if ((status & STATUS_READY) != STATUS_READY) {
		printk("m5drv: error page writing at addr=0x%08x status=0x%08x\n",
			(u32)padr, (u32)status);
		map->write16(map, CMD_CLEAR_STATUS, padr);
	}
	map->write16(map, CMD_READ_ARRAY, padr);
	chip->state = FL_READY;
	m5drv_release(&m5drv->chips[chipnum]);
	return 0;
}

static int m5drv_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct map_info *map = mtd->priv;
	struct m5drv_info *m5drv = map->fldrv_priv;
	unsigned long adr,len;
	int chipnum, ret=0;
	int erasesize = 0;
	int i;

	M5DRV_DEBUG(2, "m5drv_erase(0x%08x)\n", instr->addr);
	chipnum = instr->addr >> m5drv->chipshift;
	if (chipnum >= m5drv->numchips) {
		printk("m5drv: out of chip number (%d)\n", chipnum);
		return -EIO;
	}
	adr = instr->addr & ((1<<m5drv->chipshift)-1);
	len = instr->len;
	if (mtd->numeraseregions == 0) {
		erasesize = mtd->erasesize;
	} else if (mtd->numeraseregions == 1) {
		erasesize = mtd->eraseregions->erasesize;
	} else {
		for (i = 0; i < (mtd->numeraseregions - 1); i++) {
			if (adr < mtd->eraseregions[i+1].offset) {
				erasesize = mtd->eraseregions[i].erasesize;
				break;
			}
		}
		if (i == (mtd->numeraseregions - 1)) {	/* last region */
			erasesize = mtd->eraseregions[i].erasesize;
		}
	}
	M5DRV_DEBUG(2, "erasesize=%d, len=%ld\n", erasesize, len);
	if (erasesize == 0) return -EINVAL;
	if(instr->addr & (erasesize - 1))
		return -EINVAL;
	if(instr->len & (erasesize - 1))
		return -EINVAL;
	if(instr->len + instr->addr > mtd->size)
		return -EINVAL;

	while (len) {
		ret = m5drv_erase_oneblock(map, &m5drv->chips[chipnum], adr);
		if (ret < 0) return ret;

		adr += erasesize;
		len -= erasesize;
		if(adr >> m5drv->chipshift){
			adr = 0;
			chipnum++;
			if(chipnum >= m5drv->numchips)
				break;
		}
	}
	instr->state = MTD_ERASE_DONE;
	if(instr->callback) {
		M5DRV_DEBUG(1, "m5drv: call callback\n");
		instr->callback(instr);
	}
	return 0;
}

static int m5drv_do_wait_for_ready(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	int ret;
	int timeo;
	u_short status;
	DECLARE_WAITQUEUE(wait, current);

	/* unnecessary CMD_READ_STATUS */
/*
	map->write16(map, CMD_READ_STATUS, adr);
	status = map->read16(map, adr);
*/

	timeo = jiffies + HZ;

	while (time_before(jiffies, timeo)) {
/*
		map->write16(map, CMD_READ_STATUS, adr);
*/
		status = map->read16(map, adr);
		if ((status & STATUS_READY) == STATUS_READY) {
			M5DRV_DEBUG(1, "m5drv_wait_for_ready: ok, ready\n");
			/*
		 	 * FIXME: do full status check
		 	 */
			ret = 0;
			goto out;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);

		// enabled by takeo
		spin_unlock_bh(chip->mutex);

		schedule_timeout(1);
		schedule();
		remove_wait_queue(&chip->wq, &wait);

		// enabled by takeo
		spin_lock_bh(chip->mutex);

		if (signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		//timeo = jiffies + HZ;
	}
	ret = -ETIME;
out:
	if (ret < 0) {
		map->write16(map, CMD_CLEAR_STATUS, adr);
		map->write16(map, CMD_READ_ARRAY, adr);
		chip->state = FL_READY;
	}
	return ret;
}

static int m5drv_erase_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	int ret;
	u_short status;
	struct m5drv_info *m5drv = map->fldrv_priv;
	int chipnum;

	M5DRV_DEBUG(1, "m5drv_erase_oneblock()\n");

#ifdef UNLOCK_BEFORE_ERASE
	m5drv_unlock_oneblock(map, chip, adr);
#endif

	chipnum = (adr >> m5drv->chipshift);
	adr &= ~1;		/* align 2 */

	ret = m5drv_wait(map, chip, adr);
	if (ret < 0) return ret;

	map->write16(map, CMD_BLOCK_ERASE, adr);
	map->write16(map, CMD_CONFIRM, adr);
	chip->state = FL_ERASING;

	ret = m5drv_do_wait_for_ready(map, chip, adr);
	if(ret < 0) {
		m5drv_release(&m5drv->chips[chipnum]);
		return ret;
	}

	status = map->read16(map, adr);
	if ((status & STATUS_READY) == STATUS_READY) {
		M5DRV_DEBUG(1, "m5drv: erase completed status=%04x\n", status);
		map->write16(map, CMD_READ_ARRAY, adr);
		chip->state = FL_READY;
		m5drv_release(&m5drv->chips[chipnum]);
		return 0;		/* ok, erasing completed */
	}

	printk("m5drv: error erasing block at addr=%08lx status=%08x\n",
		adr,status);
	map->write16(map, CMD_READ_ARRAY, adr);		/* cancel erasing */
	chip->state = FL_READY;
	m5drv_release(&m5drv->chips[chipnum]);
	return -EIO;
}


#ifdef UNLOCK_BEFORE_ERASE
/*
 * we don't support unlock yet
 */
static void m5drv_unlock_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	M5DRV_DEBUG(1, "m5drv_unlock_oneblock\n");
}
#endif

static void m5drv_sync(struct mtd_info *mtd)
{
	M5DRV_DEBUG(1, "m5drv_sync()\n");
}

static int m5drv_suspend(struct mtd_info *mtd)
{
	M5DRV_DEBUG(1, "m5drv_suspend()\n");
	return -EINVAL;
}

static void m5drv_resume(struct mtd_info *mtd)
{
	M5DRV_DEBUG(1, "m5drv_resume()\n");
}

static void m5drv_destroy(struct mtd_info *mtd)
{
	M5DRV_DEBUG(1, "m5drv_destroy()\n");
}

int __init m5drv_probe_init(void)
{
	printk("MTD chip driver\n");
	register_mtd_chip_driver(&m5drv_chipdrv);
	return 0;
}

static void __exit m5drv_probe_exit(void)
{
	M5DRV_DEBUG(1, "m5drv_probe_exit()\n");
	unregister_mtd_chip_driver(&m5drv_chipdrv);
}

module_init(m5drv_probe_init);
module_exit(m5drv_probe_exit);

MODULE_AUTHOR("Takeo Takahashi");
MODULE_DESCRIPTION("MTD chip driver for M5M29GT320VP");
MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;
