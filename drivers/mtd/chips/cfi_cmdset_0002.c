/*
 * Common Flash Interface support:
 *   AMD & Fujitsu Standard Vendor Command Set (ID 0x0002)
 *
 * Copyright (C) 2000 Crossnet Co. <info@crossnet.co.jp>
 * Copyright (C) 2004 Arcom Control Systems Ltd <linux@arcom.com>
 *
 * 2_by_8 routines added by Simon Munton
 *
 * 4_by_16 work by Carolyn J. Smith
 *
 * Occasionally maintained by Thayne Harbaugh tharbaugh at lnxi dot com
 *
 * This code is GPL
 *
 * $Id: cfi_cmdset_0002.c,v 1.106 2004/08/09 14:02:32 dwmw2 Exp $
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mtd/compatmac.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/cfi.h>

#define AMD_BOOTLOC_BUG
#define FORCE_WORD_WRITE 0

#define MAX_WORD_RETRIES 3

static int cfi_amdstd_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int cfi_amdstd_write_words(struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int cfi_amdstd_write_buffers(struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int cfi_amdstd_erase_chip(struct mtd_info *, struct erase_info *);
static int cfi_amdstd_erase_varsize(struct mtd_info *, struct erase_info *);
static int cfi_amdstd_lock_varsize(struct mtd_info *, loff_t, size_t);
static int cfi_amdstd_unlock_varsize(struct mtd_info *, loff_t, size_t);
static void cfi_amdstd_sync (struct mtd_info *);
static int cfi_amdstd_suspend (struct mtd_info *);
static void cfi_amdstd_resume (struct mtd_info *);
static int cfi_amdstd_secsi_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);

static void cfi_amdstd_destroy(struct mtd_info *);

struct mtd_info *cfi_cmdset_0002(struct map_info *, int);
static struct mtd_info *cfi_amdstd_setup (struct map_info *);


static struct mtd_chip_driver cfi_amdstd_chipdrv = {
	.probe		= NULL, /* Not usable directly */
	.destroy	= cfi_amdstd_destroy,
	.name		= "cfi_cmdset_0002",
	.module		= THIS_MODULE
};


/* #define DEBUG_LOCK_BITS */
/* #define DEBUG_CFI_FEATURES */


#ifdef DEBUG_CFI_FEATURES
static void cfi_tell_features(struct cfi_pri_amdstd *extp)
{
	const char* erase_suspend[3] = {
		"Not supported", "Read only", "Read/write"
	};
	const char* top_bottom[6] = {
		"No WP", "8x8KiB sectors at top & bottom, no WP",
		"Bottom boot", "Top boot",
		"Uniform, Bottom WP", "Uniform, Top WP"
	};

	printk("  Silicon revision: %d\n", extp->SiliconRevision >> 1);
	printk("  Address sensitive unlock: %s\n", 
	       (extp->SiliconRevision & 1) ? "Not required" : "Required");

	if (extp->EraseSuspend < ARRAY_SIZE(erase_suspend))
		printk("  Erase Suspend: %s\n", erase_suspend[extp->EraseSuspend]);
	else
		printk("  Erase Suspend: Unknown value %d\n", extp->EraseSuspend);

	if (extp->BlkProt == 0)
		printk("  Block protection: Not supported\n");
	else
		printk("  Block protection: %d sectors per group\n", extp->BlkProt);


	printk("  Temporary block unprotect: %s\n",
	       extp->TmpBlkUnprotect ? "Supported" : "Not supported");
	printk("  Block protect/unprotect scheme: %d\n", extp->BlkProtUnprot);
	printk("  Number of simultaneous operations: %d\n", extp->SimultaneousOps);
	printk("  Burst mode: %s\n",
	       extp->BurstMode ? "Supported" : "Not supported");
	if (extp->PageMode == 0)
		printk("  Page mode: Not supported\n");
	else
		printk("  Page mode: %d word page\n", extp->PageMode << 2);

	printk("  Vpp Supply Minimum Program/Erase Voltage: %d.%d V\n", 
	       extp->VppMin >> 4, extp->VppMin & 0xf);
	printk("  Vpp Supply Maximum Program/Erase Voltage: %d.%d V\n", 
	       extp->VppMax >> 4, extp->VppMax & 0xf);

	if (extp->TopBottom < ARRAY_SIZE(top_bottom))
		printk("  Top/Bottom Boot Block: %s\n", top_bottom[extp->TopBottom]);
	else
		printk("  Top/Bottom Boot Block: Unknown value %d\n", extp->TopBottom);
}
#endif

#ifdef AMD_BOOTLOC_BUG
/* Wheee. Bring me the head of someone at AMD. */
static void fixup_amd_bootblock(struct map_info *map, void* param)
{
	struct cfi_private *cfi = map->fldrv_priv;
	struct cfi_pri_amdstd *extp = cfi->cmdset_priv;
	__u8 major = extp->MajorVersion;
	__u8 minor = extp->MinorVersion;

	if (((major << 8) | minor) < 0x3131) {
		/* CFI version 1.0 => don't trust bootloc */
		if (cfi->id & 0x80) {
			printk(KERN_WARNING "%s: JEDEC Device ID is 0x%02X. Assuming broken CFI table.\n", map->name, cfi->id);
			extp->TopBottom = 3;	/* top boot */
		} else {
			extp->TopBottom = 2;	/* bottom boot */
		}
	}
}
#endif

static struct cfi_fixup fixup_table[] = {
#ifdef AMD_BOOTLOC_BUG
	{
		0x0001,		/* AMD */
		CFI_ID_ANY,
		fixup_amd_bootblock, NULL
	},
#endif
	{ 0, 0, NULL, NULL }
};


struct mtd_info *cfi_cmdset_0002(struct map_info *map, int primary)
{
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned char bootloc;
	int i;

	if (cfi->cfi_mode==CFI_MODE_CFI){
		/* 
		 * It's a real CFI chip, not one for which the probe
		 * routine faked a CFI structure. So we read the feature
		 * table from it.
		 */
		__u16 adr = primary?cfi->cfiq->P_ADR:cfi->cfiq->A_ADR;
		struct cfi_pri_amdstd *extp;

		extp = (struct cfi_pri_amdstd*)cfi_read_pri(map, adr, sizeof(*extp), "Amd/Fujitsu");
		if (!extp)
			return NULL;

		/* Install our own private info structure */
		cfi->cmdset_priv = extp;	

		cfi_fixup(map, fixup_table);

#ifdef DEBUG_CFI_FEATURES
		/* Tell the user about it in lots of lovely detail */
		cfi_tell_features(extp);
#endif	

		bootloc = extp->TopBottom;
		if ((bootloc != 2) && (bootloc != 3)) {
			printk(KERN_WARNING "%s: CFI does not contain boot "
			       "bank location. Assuming top.\n", map->name);
			bootloc = 2;
		}

		if (bootloc == 3 && cfi->cfiq->NumEraseRegions > 1) {
			printk(KERN_WARNING "%s: Swapping erase regions for broken CFI table.\n", map->name);
			
			for (i=0; i<cfi->cfiq->NumEraseRegions / 2; i++) {
				int j = (cfi->cfiq->NumEraseRegions-1)-i;
				__u32 swap;
				
				swap = cfi->cfiq->EraseRegionInfo[i];
				cfi->cfiq->EraseRegionInfo[i] = cfi->cfiq->EraseRegionInfo[j];
				cfi->cfiq->EraseRegionInfo[j] = swap;
			}
		}
		/*
		 * These might already be setup (more correctly) by
		 * jedec_probe.c - still need it for cfi_probe.c path.
		 */
		if ( ! (cfi->addr_unlock1 && cfi->addr_unlock2) ) {
			switch (cfi->device_type) {
			case CFI_DEVICETYPE_X8:
				cfi->addr_unlock1 = 0x555; 
				cfi->addr_unlock2 = 0x2aa; 
				break;
			case CFI_DEVICETYPE_X16:
				cfi->addr_unlock1 = 0xaaa;
				if (map_bankwidth(map) == cfi_interleave(cfi)) {
					/* X16 chip(s) in X8 mode */
					cfi->addr_unlock2 = 0x555;
				} else {
					cfi->addr_unlock2 = 0x554;
				}
				break;
			case CFI_DEVICETYPE_X32:
				cfi->addr_unlock1 = 0x1554;
				if (map_bankwidth(map) == cfi_interleave(cfi)*2) {
					/* X32 chip(s) in X16 mode */
					cfi->addr_unlock1 = 0xaaa;
				} else {
					cfi->addr_unlock2 = 0xaa8; 
				}
				break;
			default:
				printk(KERN_WARNING
				       "MTD %s(): Unsupported device type %d\n",
				       __func__, cfi->device_type);
				return NULL;
			}
		}

	} /* CFI mode */

	for (i=0; i< cfi->numchips; i++) {
		cfi->chips[i].word_write_time = 1<<cfi->cfiq->WordWriteTimeoutTyp;
		cfi->chips[i].buffer_write_time = 1<<cfi->cfiq->BufWriteTimeoutTyp;
		cfi->chips[i].erase_time = 1<<cfi->cfiq->BlockEraseTimeoutTyp;
	}		
	
	map->fldrv = &cfi_amdstd_chipdrv;

	return cfi_amdstd_setup(map);
}


static struct mtd_info *cfi_amdstd_setup(struct map_info *map)
{
	struct cfi_private *cfi = map->fldrv_priv;
	struct mtd_info *mtd;
	unsigned long devsize = (1<<cfi->cfiq->DevSize) * cfi->interleave;
	unsigned long offset = 0;
	int i,j;

	mtd = kmalloc(sizeof(*mtd), GFP_KERNEL);
	printk(KERN_NOTICE "number of %s chips: %d\n", 
	       (cfi->cfi_mode == CFI_MODE_CFI)?"CFI":"JEDEC",cfi->numchips);

	if (!mtd) {
		printk(KERN_WARNING "Failed to allocate memory for MTD device\n");
		goto setup_err;
	}

	memset(mtd, 0, sizeof(*mtd));
	mtd->priv = map;
	mtd->type = MTD_NORFLASH;
	/* Also select the correct geometry setup too */ 
	mtd->size = devsize * cfi->numchips;

	mtd->numeraseregions = cfi->cfiq->NumEraseRegions * cfi->numchips;
	mtd->eraseregions = kmalloc(sizeof(struct mtd_erase_region_info)
				    * mtd->numeraseregions, GFP_KERNEL);
	if (!mtd->eraseregions) { 
		printk(KERN_WARNING "Failed to allocate memory for MTD erase region info\n");
		goto setup_err;
	}
			
	for (i=0; i<cfi->cfiq->NumEraseRegions; i++) {
		unsigned long ernum, ersize;
		ersize = ((cfi->cfiq->EraseRegionInfo[i] >> 8) & ~0xff) * cfi->interleave;
		ernum = (cfi->cfiq->EraseRegionInfo[i] & 0xffff) + 1;
			
		if (mtd->erasesize < ersize) {
			mtd->erasesize = ersize;
		}
		for (j=0; j<cfi->numchips; j++) {
			mtd->eraseregions[(j*cfi->cfiq->NumEraseRegions)+i].offset = (j*devsize)+offset;
			mtd->eraseregions[(j*cfi->cfiq->NumEraseRegions)+i].erasesize = ersize;
			mtd->eraseregions[(j*cfi->cfiq->NumEraseRegions)+i].numblocks = ernum;
		}
		offset += (ersize * ernum);
	}
	if (offset != devsize) {
		/* Argh */
		printk(KERN_WARNING "Sum of regions (%lx) != total size of set of interleaved chips (%lx)\n", offset, devsize);
		goto setup_err;
	}
#if 0
	// debug
	for (i=0; i<mtd->numeraseregions;i++){
		printk("%d: offset=0x%x,size=0x%x,blocks=%d\n",
		       i,mtd->eraseregions[i].offset,
		       mtd->eraseregions[i].erasesize,
		       mtd->eraseregions[i].numblocks);
	}
#endif

	if (mtd->numeraseregions == 1
	    && ((cfi->cfiq->EraseRegionInfo[0] & 0xffff) + 1) == 1) {
		mtd->erase = cfi_amdstd_erase_chip;
	} else {
		mtd->erase = cfi_amdstd_erase_varsize;
		mtd->lock = cfi_amdstd_lock_varsize;
		mtd->unlock = cfi_amdstd_unlock_varsize;
	}

	if ( cfi->cfiq->BufWriteTimeoutTyp && !FORCE_WORD_WRITE) {
		DEBUG(MTD_DEBUG_LEVEL1, "Using buffer write method\n" );
		mtd->write = cfi_amdstd_write_buffers;
	} else {
		DEBUG(MTD_DEBUG_LEVEL1, "Using word write method\n" );
		mtd->write = cfi_amdstd_write_words;
	}

	mtd->read = cfi_amdstd_read;

	/* FIXME: erase-suspend-program is broken.  See
	   http://lists.infradead.org/pipermail/linux-mtd/2003-December/009001.html */
	printk(KERN_NOTICE "cfi_cmdset_0002: Disabling erase-suspend-program due to code brokenness.\n");

	/* does this chip have a secsi area? */
	if(cfi->mfr==1){
		
		switch(cfi->id){
		case 0x50:
		case 0x53:
		case 0x55:
		case 0x56:
		case 0x5C:
		case 0x5F:
			/* Yes */
			mtd->read_user_prot_reg = cfi_amdstd_secsi_read;
			mtd->read_fact_prot_reg = cfi_amdstd_secsi_read;
		default:		       
			;
		}
	}
	
		
	mtd->sync = cfi_amdstd_sync;
	mtd->suspend = cfi_amdstd_suspend;
	mtd->resume = cfi_amdstd_resume;
	mtd->flags = MTD_CAP_NORFLASH;
	map->fldrv = &cfi_amdstd_chipdrv;
	mtd->name = map->name;
	__module_get(THIS_MODULE);
	return mtd;

 setup_err:
	if(mtd) {
		if(mtd->eraseregions)
			kfree(mtd->eraseregions);
		kfree(mtd);
	}
	kfree(cfi->cmdset_priv);
	kfree(cfi->cfiq);
	return NULL;
}

/*
 * Return true if the chip is ready.
 *
 * Ready is one of: read mode, query mode, erase-suspend-read mode (in any
 * non-suspended sector) and is indicated by no toggle bits toggling.
 *
 * Note that anything more complicated than checking if no bits are toggling
 * (including checking DQ5 for an error status) is tricky to get working
 * correctly and is therefore not done	(particulary with interleaved chips
 * as each chip must be checked independantly of the others).
 */
static int chip_ready(struct map_info *map, unsigned long addr)
{
	map_word d, t;

	d = map_read(map, addr);
	t = map_read(map, addr);

	return map_word_equal(map, d, t);
}

static int get_chip(struct map_info *map, struct flchip *chip, unsigned long adr, int mode)
{
	DECLARE_WAITQUEUE(wait, current);
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long timeo;
	struct cfi_pri_amdstd *cfip = (struct cfi_pri_amdstd *)cfi->cmdset_priv;

 resettime:
	timeo = jiffies + HZ;
 retry:
	switch (chip->state) {

	case FL_STATUS:
		for (;;) {
			if (chip_ready(map, adr))
				break;

			if (time_after(jiffies, timeo)) {
				printk(KERN_ERR "Waiting for chip to be ready timed out.\n");
				cfi_spin_unlock(chip->mutex);
				return -EIO;
			}
			cfi_spin_unlock(chip->mutex);
			cfi_udelay(1);
			cfi_spin_lock(chip->mutex);
			/* Someone else might have been playing with it. */
			goto retry;
		}
				
	case FL_READY:
	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
		return 0;

	case FL_ERASING:
		if (mode == FL_WRITING) /* FIXME: Erase-suspend-program appears broken. */
			goto sleep;

		if (!(mode == FL_READY || mode == FL_POINT
		      || (mode == FL_WRITING && (cfip->EraseSuspend & 0x2))
		      || (mode == FL_WRITING && (cfip->EraseSuspend & 0x1))))
			goto sleep;

		/* We could check to see if we're trying to access the sector
		 * that is currently being erased. However, no user will try
		 * anything like that so we just wait for the timeout. */

		/* Erase suspend */
		/* It's harmless to issue the Erase-Suspend and Erase-Resume
		 * commands when the erase algorithm isn't in progress. */
		map_write(map, CMD(0xB0), chip->in_progress_block_addr);
		chip->oldstate = FL_ERASING;
		chip->state = FL_ERASE_SUSPENDING;
		chip->erase_suspended = 1;
		for (;;) {
			if (chip_ready(map, adr))
				break;

			if (time_after(jiffies, timeo)) {
				/* Should have suspended the erase by now.
				 * Send an Erase-Resume command as either
				 * there was an error (so leave the erase
				 * routine to recover from it) or we trying to
				 * use the erase-in-progress sector. */
				map_write(map, CMD(0x30), chip->in_progress_block_addr);
				chip->state = FL_ERASING;
				chip->oldstate = FL_READY;
				printk(KERN_ERR "MTD %s(): chip not ready after erase suspend\n", __func__);
				return -EIO;
			}
			
			cfi_spin_unlock(chip->mutex);
			cfi_udelay(1);
			cfi_spin_lock(chip->mutex);
			/* Nobody will touch it while it's in state FL_ERASE_SUSPENDING.
			   So we can just loop here. */
		}
		chip->state = FL_READY;
		return 0;

	case FL_POINT:
		/* Only if there's no operation suspended... */
		if (mode == FL_READY && chip->oldstate == FL_READY)
			return 0;

	default:
	sleep:
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
		cfi_spin_unlock(chip->mutex);
		schedule();
		remove_wait_queue(&chip->wq, &wait);
		cfi_spin_lock(chip->mutex);
		goto resettime;
	}
}


static void put_chip(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	struct cfi_private *cfi = map->fldrv_priv;

	switch(chip->oldstate) {
	case FL_ERASING:
		chip->state = chip->oldstate;
		map_write(map, CMD(0x30), chip->in_progress_block_addr);
		chip->oldstate = FL_READY;
		chip->state = FL_ERASING;
		break;

	case FL_READY:
	case FL_STATUS:
		/* We should really make set_vpp() count, rather than doing this */
		DISABLE_VPP(map);
		break;
	default:
		printk(KERN_ERR "MTD: put_chip() called with oldstate %d!!\n", chip->oldstate);
	}
	wake_up(&chip->wq);
}


static inline int do_read_onechip(struct map_info *map, struct flchip *chip, loff_t adr, size_t len, u_char *buf)
{
	unsigned long cmd_addr;
	struct cfi_private *cfi = map->fldrv_priv;
	int ret;

	adr += chip->start;

	/* Ensure cmd read/writes are aligned. */ 
	cmd_addr = adr & ~(map_bankwidth(map)-1); 

	cfi_spin_lock(chip->mutex);
	ret = get_chip(map, chip, cmd_addr, FL_READY);
	if (ret) {
		cfi_spin_unlock(chip->mutex);
		return ret;
	}

	if (chip->state != FL_POINT && chip->state != FL_READY) {
		map_write(map, CMD(0xf0), cmd_addr);
		chip->state = FL_READY;
	}

	map_copy_from(map, buf, adr, len);

	put_chip(map, chip, cmd_addr);

	cfi_spin_unlock(chip->mutex);
	return 0;
}


static int cfi_amdstd_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long ofs;
	int chipnum;
	int ret = 0;

	/* ofs: offset within the first chip that the first read should start */

	chipnum = (from >> cfi->chipshift);
	ofs = from - (chipnum <<  cfi->chipshift);


	*retlen = 0;

	while (len) {
		unsigned long thislen;

		if (chipnum >= cfi->numchips)
			break;

		if ((len + ofs -1) >> cfi->chipshift)
			thislen = (1<<cfi->chipshift) - ofs;
		else
			thislen = len;

		ret = do_read_onechip(map, &cfi->chips[chipnum], ofs, thislen, buf);
		if (ret)
			break;

		*retlen += thislen;
		len -= thislen;
		buf += thislen;

		ofs = 0;
		chipnum++;
	}
	return ret;
}


static inline int do_read_secsi_onechip(struct map_info *map, struct flchip *chip, loff_t adr, size_t len, u_char *buf)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long timeo = jiffies + HZ;
	struct cfi_private *cfi = map->fldrv_priv;

 retry:
	cfi_spin_lock(chip->mutex);

	if (chip->state != FL_READY){
#if 0
		printk(KERN_DEBUG "Waiting for chip to read, status = %d\n", chip->state);
#endif
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
		
		cfi_spin_unlock(chip->mutex);

		schedule();
		remove_wait_queue(&chip->wq, &wait);
#if 0
		if(signal_pending(current))
			return -EINTR;
#endif
		timeo = jiffies + HZ;

		goto retry;
	}	

	adr += chip->start;

	chip->state = FL_READY;

	/* should these be CFI_DEVICETYPE_X8 instead of cfi->device_type? */
	cfi_send_gen_cmd(0xAA, cfi->addr_unlock1, chip->start, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, chip->start, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x88, cfi->addr_unlock1, chip->start, map, cfi, cfi->device_type, NULL);
	
	map_copy_from(map, buf, adr, len);

	/* should these be CFI_DEVICETYPE_X8 instead of cfi->device_type? */
	cfi_send_gen_cmd(0xAA, cfi->addr_unlock1, chip->start, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, chip->start, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x90, cfi->addr_unlock1, chip->start, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x00, cfi->addr_unlock1, chip->start, map, cfi, cfi->device_type, NULL);
	
	wake_up(&chip->wq);
	cfi_spin_unlock(chip->mutex);

	return 0;
}

static int cfi_amdstd_secsi_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long ofs;
	int chipnum;
	int ret = 0;


	/* ofs: offset within the first chip that the first read should start */

	/* 8 secsi bytes per chip */
	chipnum=from>>3;
	ofs=from & 7;


	*retlen = 0;

	while (len) {
		unsigned long thislen;

		if (chipnum >= cfi->numchips)
			break;

		if ((len + ofs -1) >> 3)
			thislen = (1<<3) - ofs;
		else
			thislen = len;

		ret = do_read_secsi_onechip(map, &cfi->chips[chipnum], ofs, thislen, buf);
		if (ret)
			break;

		*retlen += thislen;
		len -= thislen;
		buf += thislen;

		ofs = 0;
		chipnum++;
	}
	return ret;
}


static int do_write_oneword(struct map_info *map, struct flchip *chip, unsigned long adr, map_word datum)
{
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long timeo = jiffies + HZ;
	/*
	 * We use a 1ms + 1 jiffies generic timeout for writes (most devices
	 * have a max write time of a few hundreds usec). However, we should
	 * use the maximum timeout value given by the chip at probe time
	 * instead.  Unfortunately, struct flchip does have a field for
	 * maximum timeout, only for typical which can be far too short
	 * depending of the conditions.	 The ' + 1' is to avoid having a
	 * timeout of 0 jiffies if HZ is smaller than 1000.
	 */
	unsigned long uWriteTimeout = ( HZ / 1000 ) + 1;
	int ret = 0;
	map_word oldd, curd;
	int retry_cnt = 0;

	adr += chip->start;

	cfi_spin_lock(chip->mutex);
	ret = get_chip(map, chip, adr, FL_WRITING);
	if (ret) {
		cfi_spin_unlock(chip->mutex);
		return ret;
	}

	DEBUG( MTD_DEBUG_LEVEL3, "MTD %s(): WRITE 0x%.8lx(0x%.8lx)\n",
	       __func__, adr, datum.x[0] );

	/*
	 * Check for a NOP for the case when the datum to write is already
	 * present - it saves time and works around buggy chips that corrupt
	 * data at other locations when 0xff is written to a location that
	 * already contains 0xff.
	 */
	oldd = map_read(map, adr);
	if (map_word_equal(map, oldd, datum)) {
		DEBUG( MTD_DEBUG_LEVEL3, "MTD %s(): NOP\n",
		       __func__);
		goto op_done;
	}

	ENABLE_VPP(map);
 retry:
	/*
	 * The CFI_DEVICETYPE_X8 argument is needed even when
	 * cfi->device_type != CFI_DEVICETYPE_X8.  The addresses for
	 * command sequences don't scale even when the device is
	 * wider.  This is the case for many of the cfi_send_gen_cmd()
	 * below.  I'm not sure, however, why some use
	 * cfi->device_type.
	 */
	cfi_send_gen_cmd(0xAA, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0xA0, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	map_write(map, datum, adr);
	chip->state = FL_WRITING;

	cfi_spin_unlock(chip->mutex);
	cfi_udelay(chip->word_write_time);
	cfi_spin_lock(chip->mutex);

	/* See comment above for timeout value. */
	timeo = jiffies + uWriteTimeout; 
	for (;;) {
		if (chip->state != FL_WRITING) {
			/* Someone's suspended the write. Sleep */
			DECLARE_WAITQUEUE(wait, current);

			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			cfi_spin_unlock(chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			timeo = jiffies + (HZ / 2); /* FIXME */
			cfi_spin_lock(chip->mutex);
			continue;
		}

		/* Test to see if toggling has stopped. */
		oldd = map_read(map, adr);
		curd = map_read(map, adr);
		if (map_word_equal(map, curd, oldd)) {
			/* Do we have the correct value? */
			if (map_word_equal(map, curd, datum)) {
				goto op_done;
			}
			/* Nope something has gone wrong. */
			break;
		}

		if (time_after(jiffies, timeo)) {
			printk(KERN_WARNING "MTD %s(): software timeout\n",
				__func__ );
			break;
		}

		/* Latency issues. Drop the lock, wait a while and retry */
		cfi_spin_unlock(chip->mutex);
		cfi_udelay(1);
		cfi_spin_lock(chip->mutex);
	}

	/* reset on all failures. */
	map_write( map, CMD(0xF0), chip->start );
	/* FIXME - should have reset delay before continuing */
	if (++retry_cnt <= MAX_WORD_RETRIES) 
		goto retry;

	ret = -EIO;
 op_done:
	chip->state = FL_READY;
	put_chip(map, chip, adr);
	cfi_spin_unlock(chip->mutex);

	return ret;
}


static int cfi_amdstd_write_words(struct mtd_info *mtd, loff_t to, size_t len,
				  size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int ret = 0;
	int chipnum;
	unsigned long ofs, chipstart;
	DECLARE_WAITQUEUE(wait, current);

	*retlen = 0;
	if (!len)
		return 0;

	chipnum = to >> cfi->chipshift;
	ofs = to  - (chipnum << cfi->chipshift);
	chipstart = cfi->chips[chipnum].start;

	/* If it's not bus-aligned, do the first byte write */
	if (ofs & (map_bankwidth(map)-1)) {
		unsigned long bus_ofs = ofs & ~(map_bankwidth(map)-1);
		int i = ofs - bus_ofs;
		int n = 0;
		map_word tmp_buf;

 retry:
		cfi_spin_lock(cfi->chips[chipnum].mutex);

		if (cfi->chips[chipnum].state != FL_READY) {
#if 0
			printk(KERN_DEBUG "Waiting for chip to write, status = %d\n", cfi->chips[chipnum].state);
#endif
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&cfi->chips[chipnum].wq, &wait);

			cfi_spin_unlock(cfi->chips[chipnum].mutex);

			schedule();
			remove_wait_queue(&cfi->chips[chipnum].wq, &wait);
#if 0
			if(signal_pending(current))
				return -EINTR;
#endif
			goto retry;
		}

		/* Load 'tmp_buf' with old contents of flash */
		tmp_buf = map_read(map, bus_ofs+chipstart);

		cfi_spin_unlock(cfi->chips[chipnum].mutex);

		/* Number of bytes to copy from buffer */
		n = min_t(int, len, map_bankwidth(map)-i);
		
		tmp_buf = map_word_load_partial(map, tmp_buf, buf, i, n);

		ret = do_write_oneword(map, &cfi->chips[chipnum], 
				       bus_ofs, tmp_buf);
		if (ret) 
			return ret;
		
		ofs += n;
		buf += n;
		(*retlen) += n;
		len -= n;

		if (ofs >> cfi->chipshift) {
			chipnum ++; 
			ofs = 0;
			if (chipnum == cfi->numchips)
				return 0;
		}
	}
	
	/* We are now aligned, write as much as possible */
	while(len >= map_bankwidth(map)) {
		map_word datum;

		datum = map_word_load(map, buf);

		ret = do_write_oneword(map, &cfi->chips[chipnum],
				       ofs, datum);
		if (ret)
			return ret;

		ofs += map_bankwidth(map);
		buf += map_bankwidth(map);
		(*retlen) += map_bankwidth(map);
		len -= map_bankwidth(map);

		if (ofs >> cfi->chipshift) {
			chipnum ++; 
			ofs = 0;
			if (chipnum == cfi->numchips)
				return 0;
			chipstart = cfi->chips[chipnum].start;
		}
	}

	/* Write the trailing bytes if any */
	if (len & (map_bankwidth(map)-1)) {
		map_word tmp_buf;

 retry1:
		cfi_spin_lock(cfi->chips[chipnum].mutex);

		if (cfi->chips[chipnum].state != FL_READY) {
#if 0
			printk(KERN_DEBUG "Waiting for chip to write, status = %d\n", cfi->chips[chipnum].state);
#endif
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&cfi->chips[chipnum].wq, &wait);

			cfi_spin_unlock(cfi->chips[chipnum].mutex);

			schedule();
			remove_wait_queue(&cfi->chips[chipnum].wq, &wait);
#if 0
			if(signal_pending(current))
				return -EINTR;
#endif
			goto retry1;
		}

		tmp_buf = map_read(map, ofs + chipstart);

		cfi_spin_unlock(cfi->chips[chipnum].mutex);

		tmp_buf = map_word_load_partial(map, tmp_buf, buf, 0, len);
	
		ret = do_write_oneword(map, &cfi->chips[chipnum], 
				ofs, tmp_buf);
		if (ret) 
			return ret;
		
		(*retlen) += len;
	}

	return 0;
}


/*
 * FIXME: interleaved mode not tested, and probably not supported!
 */
static inline int do_write_buffer(struct map_info *map, struct flchip *chip, 
				  unsigned long adr, const u_char *buf, int len)
{
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long timeo = jiffies + HZ;
	/* see comments in do_write_oneword() regarding uWriteTimeo. */
	static unsigned long uWriteTimeout = ( HZ / 1000 ) + 1;
	int ret = -EIO;
	unsigned long cmd_adr;
	int z, words;
	map_word datum;

	adr += chip->start;
	cmd_adr = adr;

	cfi_spin_lock(chip->mutex);
	ret = get_chip(map, chip, adr, FL_WRITING);
	if (ret) {
		cfi_spin_unlock(chip->mutex);
		return ret;
	}

	datum = map_word_load(map, buf);

	DEBUG( MTD_DEBUG_LEVEL3, "MTD %s(): WRITE 0x%.8lx(0x%.8lx)\n",
	       __func__, adr, datum.x[0] );

	ENABLE_VPP(map);
	cfi_send_gen_cmd(0xAA, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	//cfi_send_gen_cmd(0xA0, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);

	/* Write Buffer Load */
	map_write(map, CMD(0x25), cmd_adr);

	chip->state = FL_WRITING_TO_BUFFER;

	/* Write length of data to come */
	words = len / map_bankwidth(map);
	map_write(map, CMD(words - 1), cmd_adr);
	/* Write data */
	z = 0;
	while(z < words * map_bankwidth(map)) {
		datum = map_word_load(map, buf);
		map_write(map, datum, adr + z);

		z += map_bankwidth(map);
		buf += map_bankwidth(map);
	}
	z -= map_bankwidth(map);

	adr += z;

	/* Write Buffer Program Confirm: GO GO GO */
	map_write(map, CMD(0x29), cmd_adr);
	chip->state = FL_WRITING;

	cfi_spin_unlock(chip->mutex);
	cfi_udelay(chip->buffer_write_time);
	cfi_spin_lock(chip->mutex);

	timeo = jiffies + uWriteTimeout; 
		
	for (;;) {
		if (chip->state != FL_WRITING) {
			/* Someone's suspended the write. Sleep */
			DECLARE_WAITQUEUE(wait, current);

			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			cfi_spin_unlock(chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			timeo = jiffies + (HZ / 2); /* FIXME */
			cfi_spin_lock(chip->mutex);
			continue;
		}

		if (chip_ready(map, adr))
			goto op_done;
		    
		if( time_after(jiffies, timeo))
			break;

		/* Latency issues. Drop the lock, wait a while and retry */
		cfi_spin_unlock(chip->mutex);
		cfi_udelay(1);
		cfi_spin_lock(chip->mutex);
	}

	printk(KERN_WARNING "MTD %s(): software timeout\n",
	       __func__ );

	/* reset on all failures. */
	map_write( map, CMD(0xF0), chip->start );
	/* FIXME - should have reset delay before continuing */

	ret = -EIO;
 op_done:
	chip->state = FL_READY;
	put_chip(map, chip, adr);
	cfi_spin_unlock(chip->mutex);

	return ret;
}


static int cfi_amdstd_write_buffers(struct mtd_info *mtd, loff_t to, size_t len,
				    size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int wbufsize = cfi_interleave(cfi) << cfi->cfiq->MaxBufWriteSize;
	int ret = 0;
	int chipnum;
	unsigned long ofs;

	*retlen = 0;
	if (!len)
		return 0;

	chipnum = to >> cfi->chipshift;
	ofs = to  - (chipnum << cfi->chipshift);

	/* If it's not bus-aligned, do the first word write */
	if (ofs & (map_bankwidth(map)-1)) {
		size_t local_len = (-ofs)&(map_bankwidth(map)-1);
		if (local_len > len)
			local_len = len;
		ret = cfi_amdstd_write_words(mtd, to, local_len,
					       retlen, buf);
		if (ret)
			return ret;
		ofs += local_len;
		buf += local_len;
		len -= local_len;

		if (ofs >> cfi->chipshift) {
			chipnum ++;
			ofs = 0;
			if (chipnum == cfi->numchips)
				return 0;
		}
	}

	/* Write buffer is worth it only if more than one word to write... */
	while (len >= map_bankwidth(map) * 2) {
		/* We must not cross write block boundaries */
		int size = wbufsize - (ofs & (wbufsize-1));

		if (size > len)
			size = len;
		if (size % map_bankwidth(map))
			size -= size % map_bankwidth(map);

		ret = do_write_buffer(map, &cfi->chips[chipnum], 
				      ofs, buf, size);
		if (ret)
			return ret;

		ofs += size;
		buf += size;
		(*retlen) += size;
		len -= size;

		if (ofs >> cfi->chipshift) {
			chipnum ++; 
			ofs = 0;
			if (chipnum == cfi->numchips)
				return 0;
		}
	}

	if (len) {
		size_t retlen_dregs = 0;

		ret = cfi_amdstd_write_words(mtd, to, len, &retlen_dregs, buf);

		*retlen += retlen_dregs;
		return ret;
	}

	return 0;
}


/*
 * Handle devices with one erase region, that only implement
 * the chip erase command.
 */
static inline int do_erase_chip(struct map_info *map, struct flchip *chip)
{
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long timeo = jiffies + HZ;
	unsigned long int adr;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	adr = cfi->addr_unlock1;

	cfi_spin_lock(chip->mutex);
	ret = get_chip(map, chip, adr, FL_WRITING);
	if (ret) {
		cfi_spin_unlock(chip->mutex);
		return ret;
	}

	DEBUG( MTD_DEBUG_LEVEL3, "MTD %s(): ERASE 0x%.8lx\n",
	       __func__, chip->start );

	ENABLE_VPP(map);
	cfi_send_gen_cmd(0xAA, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x80, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0xAA, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x10, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);

	chip->state = FL_ERASING;
	chip->erase_suspended = 0;
	chip->in_progress_block_addr = adr;

	cfi_spin_unlock(chip->mutex);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((chip->erase_time*HZ)/(2*1000));
	cfi_spin_lock(chip->mutex);

	timeo = jiffies + (HZ*20);

	for (;;) {
		if (chip->state != FL_ERASING) {
			/* Someone's suspended the erase. Sleep */
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			cfi_spin_unlock(chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			cfi_spin_lock(chip->mutex);
			continue;
		}
		if (chip->erase_suspended) {
			/* This erase was suspended and resumed.
			   Adjust the timeout */
			timeo = jiffies + (HZ*20); /* FIXME */
			chip->erase_suspended = 0;
		}

		if (chip_ready(map, adr))
			goto op_done;

		if (time_after(jiffies, timeo))
			break;

		/* Latency issues. Drop the lock, wait a while and retry */
		cfi_spin_unlock(chip->mutex);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
		cfi_spin_lock(chip->mutex);
	}

	printk(KERN_WARNING "MTD %s(): software timeout\n",
	       __func__ );

	/* reset on all failures. */
	map_write( map, CMD(0xF0), chip->start );
	/* FIXME - should have reset delay before continuing */

	ret = -EIO;
 op_done:
	chip->state = FL_READY;
	put_chip(map, chip, adr);
	cfi_spin_unlock(chip->mutex);

	return ret;
}


typedef int (*frob_t)(struct map_info *map, struct flchip *chip,
		      unsigned long adr, void *thunk);


static int cfi_amdstd_varsize_frob(struct mtd_info *mtd, frob_t frob,
				   loff_t ofs, size_t len, void *thunk)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long adr;
	int chipnum, ret = 0;
	int i, first;
	struct mtd_erase_region_info *regions = mtd->eraseregions;

	if (ofs > mtd->size)
		return -EINVAL;

	if ((len + ofs) > mtd->size)
		return -EINVAL;

	/* Check that both start and end of the requested erase are
	 * aligned with the erasesize at the appropriate addresses.
	 */

	i = 0;

	/* Skip all erase regions which are ended before the start of 
	   the requested erase. Actually, to save on the calculations,
	   we skip to the first erase region which starts after the
	   start of the requested erase, and then go back one.
	*/
	
	while (i < mtd->numeraseregions && ofs >= regions[i].offset)
	       i++;
	i--;

	/* OK, now i is pointing at the erase region in which this 
	   erase request starts. Check the start of the requested
	   erase range is aligned with the erase size which is in
	   effect here.
	*/

	if (ofs & (regions[i].erasesize-1))
		return -EINVAL;

	/* Remember the erase region we start on */
	first = i;

	/* Next, check that the end of the requested erase is aligned
	 * with the erase region at that address.
	 */

	while (i<mtd->numeraseregions && (ofs + len) >= regions[i].offset)
		i++;

	/* As before, drop back one to point at the region in which
	   the address actually falls
	*/
	i--;
	
	if ((ofs + len) & (regions[i].erasesize-1))
		return -EINVAL;

	chipnum = ofs >> cfi->chipshift;
	adr = ofs - (chipnum << cfi->chipshift);

	i=first;

	while (len) {
		ret = (*frob)(map, &cfi->chips[chipnum], adr, thunk);
		
		if (ret)
			return ret;

		adr += regions[i].erasesize;
		len -= regions[i].erasesize;

		if (adr % (1<< cfi->chipshift) == ((regions[i].offset + (regions[i].erasesize * regions[i].numblocks)) %( 1<< cfi->chipshift)))
			i++;

		if (adr >> cfi->chipshift) {
			adr = 0;
			chipnum++;
			
			if (chipnum >= cfi->numchips)
			break;
		}
	}

	return 0;
}


static inline int do_erase_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr, void *thunk)
{
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long timeo = jiffies + HZ;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	adr += chip->start;

	cfi_spin_lock(chip->mutex);
	ret = get_chip(map, chip, adr, FL_ERASING);
	if (ret) {
		cfi_spin_unlock(chip->mutex);
		return ret;
	}

	DEBUG( MTD_DEBUG_LEVEL3, "MTD %s(): ERASE 0x%.8lx\n",
	       __func__, adr );

	ENABLE_VPP(map);
	cfi_send_gen_cmd(0xAA, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x80, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0xAA, cfi->addr_unlock1, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, chip->start, map, cfi, CFI_DEVICETYPE_X8, NULL);
	map_write(map, CMD(0x30), adr);

	chip->state = FL_ERASING;
	chip->erase_suspended = 0;
	chip->in_progress_block_addr = adr;
	
	cfi_spin_unlock(chip->mutex);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((chip->erase_time*HZ)/(2*1000));
	cfi_spin_lock(chip->mutex);

	timeo = jiffies + (HZ*20);

	for (;;) {
		if (chip->state != FL_ERASING) {
			/* Someone's suspended the erase. Sleep */
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			cfi_spin_unlock(chip->mutex);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			cfi_spin_lock(chip->mutex);
			continue;
		}
		if (chip->erase_suspended) {
			/* This erase was suspended and resumed.
			   Adjust the timeout */
			timeo = jiffies + (HZ*20); /* FIXME */
			chip->erase_suspended = 0;
		}

		if (chip_ready(map, adr))
			goto op_done;

		if (time_after(jiffies, timeo))
			break;

		/* Latency issues. Drop the lock, wait a while and retry */
		cfi_spin_unlock(chip->mutex);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
		cfi_spin_lock(chip->mutex);
	}
	
	printk(KERN_WARNING "MTD %s(): software timeout\n",
	       __func__ );
	
	/* reset on all failures. */
	map_write( map, CMD(0xF0), chip->start );
	/* FIXME - should have reset delay before continuing */

	ret = -EIO;
 op_done:
	chip->state = FL_READY;
	put_chip(map, chip, adr);
	cfi_spin_unlock(chip->mutex);
	return ret;
}


int cfi_amdstd_erase_varsize(struct mtd_info *mtd, struct erase_info *instr)
{
	unsigned long ofs, len;
	int ret;

	ofs = instr->addr;
	len = instr->len;

	ret = cfi_amdstd_varsize_frob(mtd, do_erase_oneblock, ofs, len, NULL);
	if (ret)
		return ret;

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);
	
	return 0;
}


static int cfi_amdstd_erase_chip(struct mtd_info *mtd, struct erase_info *instr)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int ret = 0;

	if (instr->addr != 0)
		return -EINVAL;

	if (instr->len != mtd->size)
		return -EINVAL;

	ret = do_erase_chip(map, &cfi->chips[0]);
	if (ret)
		return ret;

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);
	
	return 0;
}


static void cfi_amdstd_sync (struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;
	int ret = 0;
	DECLARE_WAITQUEUE(wait, current);

	for (i=0; !ret && i<cfi->numchips; i++) {
		chip = &cfi->chips[i];

	retry:
		cfi_spin_lock(chip->mutex);

		switch(chip->state) {
		case FL_READY:
		case FL_STATUS:
		case FL_CFI_QUERY:
		case FL_JEDEC_QUERY:
			chip->oldstate = chip->state;
			chip->state = FL_SYNCING;
			/* No need to wake_up() on this state change - 
			 * as the whole point is that nobody can do anything
			 * with the chip now anyway.
			 */
		case FL_SYNCING:
			cfi_spin_unlock(chip->mutex);
			break;

		default:
			/* Not an idle state */
			add_wait_queue(&chip->wq, &wait);
			
			cfi_spin_unlock(chip->mutex);

			schedule();

			remove_wait_queue(&chip->wq, &wait);
			
			goto retry;
		}
	}

	/* Unlock the chips again */

	for (i--; i >=0; i--) {
		chip = &cfi->chips[i];

		cfi_spin_lock(chip->mutex);
		
		if (chip->state == FL_SYNCING) {
			chip->state = chip->oldstate;
			wake_up(&chip->wq);
		}
		cfi_spin_unlock(chip->mutex);
	}
}


static int cfi_amdstd_suspend(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;
	int ret = 0;

	for (i=0; !ret && i<cfi->numchips; i++) {
		chip = &cfi->chips[i];

		cfi_spin_lock(chip->mutex);

		switch(chip->state) {
		case FL_READY:
		case FL_STATUS:
		case FL_CFI_QUERY:
		case FL_JEDEC_QUERY:
			chip->oldstate = chip->state;
			chip->state = FL_PM_SUSPENDED;
			/* No need to wake_up() on this state change - 
			 * as the whole point is that nobody can do anything
			 * with the chip now anyway.
			 */
		case FL_PM_SUSPENDED:
			break;

		default:
			ret = -EAGAIN;
			break;
		}
		cfi_spin_unlock(chip->mutex);
	}

	/* Unlock the chips again */

	if (ret) {
		for (i--; i >=0; i--) {
			chip = &cfi->chips[i];

			cfi_spin_lock(chip->mutex);
		
			if (chip->state == FL_PM_SUSPENDED) {
				chip->state = chip->oldstate;
				wake_up(&chip->wq);
			}
			cfi_spin_unlock(chip->mutex);
		}
	}
	
	return ret;
}


static void cfi_amdstd_resume(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;

	for (i=0; i<cfi->numchips; i++) {
	
		chip = &cfi->chips[i];

		cfi_spin_lock(chip->mutex);
		
		if (chip->state == FL_PM_SUSPENDED) {
			chip->state = FL_READY;
			map_write(map, CMD(0xF0), chip->start);
			wake_up(&chip->wq);
		}
		else
			printk(KERN_ERR "Argh. Chip not in PM_SUSPENDED state upon resume()\n");

		cfi_spin_unlock(chip->mutex);
	}
}


#ifdef DEBUG_LOCK_BITS

static int do_printlockstatus_oneblock(struct map_info *map,
				       struct flchip *chip,
				       unsigned long adr,
				       void *thunk)
{
	struct cfi_private *cfi = map->fldrv_priv;
	int ofs_factor = cfi->interleave * cfi->device_type;

	cfi_send_gen_cmd(0x90, 0x55, 0, map, cfi, cfi->device_type, NULL);
	printk(KERN_DEBUG "block status register for 0x%08lx is %x\n",
	       adr, cfi_read_query(map, adr+(2*ofs_factor)));
	cfi_send_gen_cmd(0xff, 0x55, 0, map, cfi, cfi->device_type, NULL);
	
	return 0;
}


#define debug_dump_locks(mtd, frob, ofs, len, thunk) \
	cfi_amdstd_varsize_frob((mtd), (frob), (ofs), (len), (thunk))

#else

#define debug_dump_locks(...)

#endif /* DEBUG_LOCK_BITS */


struct xxlock_thunk {
	uint8_t val;
	flstate_t state;
};


#define DO_XXLOCK_ONEBLOCK_LOCK   ((struct xxlock_thunk){0x01, FL_LOCKING})
#define DO_XXLOCK_ONEBLOCK_UNLOCK ((struct xxlock_thunk){0x00, FL_UNLOCKING})


/*
 * FIXME - this is *very* specific to a particular chip.  It likely won't
 * work for all chips that require unlock.  It also hasn't been tested
 * with interleaved chips.
 */
static int do_xxlock_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr, void *thunk)
{
	struct cfi_private *cfi = map->fldrv_priv;
	struct xxlock_thunk *xxlt = (struct xxlock_thunk *)thunk;
	int ret;

	/*
	 * This is easy because these are writes to registers and not writes
	 * to flash memory - that means that we don't have to check status
	 * and timeout.
	 */

	adr += chip->start;
	/*
	 * lock block registers:
	 * - on 64k boundariesand
	 * - bit 1 set high
	 * - block lock registers are 4MiB lower - overflow subtract (danger)
	 */
	adr = ((adr & ~0xffff) | 0x2) + ~0x3fffff;

	cfi_spin_lock(chip->mutex);
	ret = get_chip(map, chip, adr, FL_LOCKING);
	if (ret) {
		cfi_spin_unlock(chip->mutex);
		return ret;
	}

	chip->state = xxlt->state;
	map_write(map, CMD(xxlt->val), adr);
	
	/* Done and happy. */
	chip->state = FL_READY;
	put_chip(map, chip, adr);
	cfi_spin_unlock(chip->mutex);
	return 0;
}


static int cfi_amdstd_lock_varsize(struct mtd_info *mtd,
				   loff_t ofs,
				   size_t len)
{
	int ret;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "%s: lock status before, ofs=0x%08llx, len=0x%08zX\n",
	      __func__, ofs, len);
	debug_dump_locks(mtd, do_printlockstatus_oneblock, ofs, len, 0);

	ret = cfi_amdstd_varsize_frob(mtd, do_xxlock_oneblock, ofs, len,
				      (void *)&DO_XXLOCK_ONEBLOCK_LOCK);
	
	DEBUG(MTD_DEBUG_LEVEL3,
	      "%s: lock status after, ret=%d\n",
	      __func__, ret);

	debug_dump_locks(mtd, do_printlockstatus_oneblock, ofs, len, 0);

	return ret;
}


static int cfi_amdstd_unlock_varsize(struct mtd_info *mtd,
				     loff_t ofs,
				     size_t len)
{
	int ret;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "%s: lock status before, ofs=0x%08llx, len=0x%08zX\n",
	      __func__, ofs, len);
	debug_dump_locks(mtd, do_printlockstatus_oneblock, ofs, len, 0);

	ret = cfi_amdstd_varsize_frob(mtd, do_xxlock_oneblock, ofs, len,
				      (void *)&DO_XXLOCK_ONEBLOCK_UNLOCK);
	
	DEBUG(MTD_DEBUG_LEVEL3,
	      "%s: lock status after, ret=%d\n",
	      __func__, ret);
	debug_dump_locks(mtd, do_printlockstatus_oneblock, ofs, len, 0);
	
	return ret;
}


static void cfi_amdstd_destroy(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	kfree(cfi->cmdset_priv);
	kfree(cfi->cfiq);
	kfree(cfi);
	kfree(mtd->eraseregions);
}

static char im_name[]="cfi_cmdset_0002";


int __init cfi_amdstd_init(void)
{
	inter_module_register(im_name, THIS_MODULE, &cfi_cmdset_0002);
	return 0;
}


static void __exit cfi_amdstd_exit(void)
{
	inter_module_unregister(im_name);
}


module_init(cfi_amdstd_init);
module_exit(cfi_amdstd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Crossnet Co. <info@crossnet.co.jp> et al.");
MODULE_DESCRIPTION("MTD chip driver for AMD/Fujitsu flash chips");
