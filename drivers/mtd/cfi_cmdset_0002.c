/*
 * Common Flash Interface support:
 *   AMD & Fujitsu Extended Vendor Command Set (ID 0x0002)
 *
 * Copyright (C) 2000 Crossnet Co. <info@crossnet.co.jp>
 *
 * This code is GPL
 *
 * $Id: cfi_cmdset_0002.c,v 1.1 2000/07/11 12:32:09 dwmw2 Exp $
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>

#if LINUX_VERSION_CODE < 0x20300
#define set_current_state(x) current->state = (x);
#endif

static int cfi_amdext_read_2_by_16 (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int cfi_amdext_write_2_by_16(struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int cfi_amdext_erase_2_by_16 (struct mtd_info *, struct erase_info *);
static void cfi_amdext_sync (struct mtd_info *);
static int cfi_amdext_suspend (struct mtd_info *);
static void cfi_amdext_resume (struct mtd_info *);

static void cfi_amdext_destroy(struct mtd_info *);

static void cfi_cmdset_0002(struct map_info *, int, unsigned long);

static struct mtd_info *cfi_amdext_setup (struct map_info *);

static const char im_name[] = "cfi_cmdset_0002";

static void cfi_cmdset_0002(struct map_info *map, int primary, unsigned long base)
{
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
//	struct cfi_pri_intelext *extp;

	__u16 adr = primary?cfi->cfiq.P_ADR:cfi->cfiq.A_ADR;

	printk(" Amd/Fujitsu Extended Query Table at 0x%4.4X\n", adr);


	/* If there was an old setup function, decrease its use count */
	if (cfi->cmdset_setup)
		inter_module_put(cfi->im_name);
	if (cfi->cmdset_priv)
		kfree(cfi->cmdset_priv);

	for (i=0; i< cfi->numchips; i++) {
		cfi->chips[i].word_write_time = 128;
		cfi->chips[i].buffer_write_time = 128;
		cfi->chips[i].erase_time = 1024;
	}		
		

	cfi->cmdset_setup = cfi_amdext_setup;
	cfi->im_name = im_name;
//	cfi->cmdset_priv = extp;
	
	return;
}

static struct mtd_info *cfi_amdext_setup(struct map_info *map)
{
	struct cfi_private *cfi = map->fldrv_priv;
	struct mtd_info *mtd;

	mtd = kmalloc(sizeof(*mtd), GFP_KERNEL);
	printk("number of CFI chips: %d\n", cfi->numchips);

	if (!mtd) {
	  printk("Failed to allocate memory for MTD device\n");
	  kfree(cfi->cmdset_priv);
	  return NULL;
	}

	memset(mtd, 0, sizeof(*mtd));
	mtd->priv = map;
	mtd->type = MTD_NORFLASH;
	mtd->erasesize = 0x20000; /* FIXME */
	/* Also select the correct geometry setup too */ 
	mtd->size = (1 << cfi->cfiq.DevSize) * cfi->numchips * cfi->interleave;
	mtd->erase = cfi_amdext_erase_2_by_16;
	mtd->read = cfi_amdext_read_2_by_16;
	mtd->write = cfi_amdext_write_2_by_16;
	mtd->sync = cfi_amdext_sync;
	mtd->suspend = cfi_amdext_suspend;
	mtd->resume = cfi_amdext_resume;
	mtd->flags = MTD_CAP_NORFLASH;
	map->fldrv_destroy = cfi_amdext_destroy;
	mtd->name = map->name;
	return mtd;
}

static inline int do_read_2_by_16_onechip(struct map_info *map, struct flchip *chip, loff_t adr, size_t len, u_char *buf)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long timeo = jiffies + HZ;

 retry:
	spin_lock_bh(chip->mutex);

	if (chip->state != FL_READY){
printk("Waiting for chip to read, status = %d\n", chip->state);
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
                
		spin_unlock_bh(chip->mutex);

		schedule();
		remove_wait_queue(&chip->wq, &wait);

		if(signal_pending(current))
			return -EINTR;

		timeo = jiffies + HZ;

		goto retry;
	}	

	adr += chip->start;

//     	map->write32(map, cpu_to_le32(0x00F000F0), adr);

	chip->state = FL_READY;

	map->copy_from(map, buf, adr, len);

	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);

	return 0;
}

static int cfi_amdext_read_2_by_16 (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long ofs;
	int chipnum;
	int ret = 0;

	/* ofs: offset within the first chip that the first read should start */

	chipnum = (from >> cfi->chipshift);
	chipnum /= (cfi->interleave);
	ofs = from - (chipnum <<  cfi->chipshift) * (cfi->interleave);

	*retlen = 0;

	while (len) {
		unsigned long thislen;

		if (chipnum >= cfi->numchips)
			break;

		if (((len + ofs -1) >> cfi->chipshift) / (cfi->interleave))
			thislen = (1<<cfi->chipshift) * (cfi->interleave) - ofs;
		else
			thislen = len;

		ret = do_read_2_by_16_onechip(map, &cfi->chips[chipnum], ofs, thislen, buf);
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

static inline int do_write_2_by_16_oneword(struct map_info *map, struct flchip *chip, unsigned long adr, __u32 datum)
{
	unsigned long timeo = jiffies + HZ;
	unsigned int Last[4];
	unsigned long Count = 0;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

 retry:
	spin_lock_bh(chip->mutex);

	if (chip->state != FL_READY){
printk("Waiting for chip to write, status = %d\n", chip->state);
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
                
		spin_unlock_bh(chip->mutex);

		schedule();
		remove_wait_queue(&chip->wq, &wait);
printk("Wake up to write:\n");
		if(signal_pending(current))
			return -EINTR;

		timeo = jiffies + HZ;

		goto retry;
	}	

	chip->state = FL_WRITING;

	adr += chip->start;

	map->write32(map, cpu_to_le32(0x00AA00AA), 0x555 *4);
	map->write32(map, cpu_to_le32(0x00550055), 0x2AA *4);
	map->write32(map, cpu_to_le32(0x00A000A0), 0x555 *4);
	map->write32(map, cpu_to_le32(datum), adr);

	spin_unlock_bh(chip->mutex);
	udelay(chip->word_write_time);
	spin_lock_bh(chip->mutex);

	Last[0] = map->read32(map, adr);
	Last[1] = map->read32(map, adr);
	Last[2] = map->read32(map, adr);

	for (Count = 3; Last[(Count - 1) % 4] != Last[(Count - 2) % 4] && Count < 10000; Count++){
		udelay(10);

		Last[Count % 4] = map->read32(map, adr);
	}

	if (Last[(Count - 1) % 4] != datum){
	     	map->write32(map, cpu_to_le32(0x00F000F0), adr);
		ret = -EIO;
	}       

	chip->state = FL_READY;
	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);

	return ret;
}


static int cfi_amdext_write_2_by_16 (struct mtd_info *mtd, loff_t to , size_t len, size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int ret = 0;
	int chipnum;
	unsigned long ofs;

	*retlen = 0;

	chipnum = (to >> cfi->chipshift);
	chipnum /= cfi->interleave;
	ofs = to  - (chipnum << cfi->chipshift) * cfi->interleave;

	while(len > 3) {

		ret = do_write_2_by_16_oneword(map, &cfi->chips[chipnum],
					       ofs, *(__u32 *)buf);
		if (ret)
			return ret;

		ofs += 4;
		buf += 4;
		(*retlen) += 4;
		len -= 4;

		if ((ofs >> cfi->chipshift) / cfi->interleave) {
			chipnum ++; 
			ofs = 0;
			if (chipnum == cfi->numchips)
				return 0;
		}
	}

	if (len) {
		unsigned int tmp;

		/* Final byte to write */
#if defined(__LITTLE_ENDIAN)
		tmp = map->read32(map, ofs);

		tmp = 0xffffffff >> (len*8);
		tmp = tmp << (len*8);

		tmp |= *(__u32 *)(buf);

		ret = do_write_2_by_16_oneword(map, &cfi->chips[chipnum],
					       ofs, tmp);

#elif defined(__BIG_ENDIAN) 
#error not support big endian yet
#else
#error define a sensible endianness
#endif

		if (ret) 
			return ret;
		
		(*retlen)+=len;
	}

	return 0;
}


static inline int do_erase_2_by_16_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	unsigned int status;
	unsigned long timeo = jiffies + HZ;
	DECLARE_WAITQUEUE(wait, current);

 retry:
	spin_lock_bh(chip->mutex);

	if (chip->state != FL_READY){
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
                
		spin_unlock_bh(chip->mutex);

		schedule();
		remove_wait_queue(&chip->wq, &wait);

		if(signal_pending(current))
			return -EINTR;

		timeo = jiffies + HZ;

		goto retry;
	}	

	chip->state = FL_ERASING;

	adr += chip->start;

	map->write32(map, cpu_to_le32(0x00AA00AA), 0x555 *4);
	map->write32(map, cpu_to_le32(0x00550055), 0x2AA *4);
	map->write32(map, cpu_to_le32(0x00800080), 0x555 *4);
	map->write32(map, cpu_to_le32(0x00AA00AA), 0x555 *4);
	map->write32(map, cpu_to_le32(0x00550055), 0x2AA *4);
	map->write32(map, cpu_to_le32(0x00300030), adr);

	
	timeo = jiffies + (HZ*20);

	spin_unlock_bh(chip->mutex);
	schedule_timeout(HZ);
	spin_lock_bh(chip->mutex);

	/* FIXME. Use a timer to check this, and return immediately. */
	/* Once the state machine's known to be working I'll do that */

	while ( ( (status = le32_to_cpu(map->read32(map, 0x00))) & 0x80808080 ) != 0x80808080 ) {
		static int z=0;

		if (chip->state != FL_ERASING) {
			/* Someone's suspended the erase. Sleep */
			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			
			spin_unlock_bh(chip->mutex);
			printk("erase suspended. Sleeping\n");
			
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			
			if (signal_pending(current))
				return -EINTR;
			
			timeo = jiffies + (HZ*2); /* FIXME */
			spin_lock_bh(chip->mutex);
			continue;
		}

		/* OK Still waiting */
		if (time_after(jiffies, timeo)) {
			chip->state = FL_READY;
			spin_unlock_bh(chip->mutex);
			printk("waiting for erase to complete timed out.");
			return -EIO;
		}
		
		/* Latency issues. Drop the lock, wait a while and retry */
		spin_unlock_bh(chip->mutex);

		z++;
		if ( 0 && !(z % 100 )) 
			printk("chip not ready yet after erase. looping\n");

		udelay(1);
		
		spin_lock_bh(chip->mutex);
		continue;
	}
	
	/* Done and happy. */
	chip->state = FL_READY;
	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);
	printk("erase ret OK\n");
	return 0;
}

static int cfi_amdext_erase_2_by_16 (struct mtd_info *mtd, struct erase_info *instr)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long adr, len;
	int chipnum, ret = 0;

//printk("erase : 0x%x 0x%x 0x%x\n", instr->addr, instr->len, mtd->size);

	if (instr->addr & (mtd->erasesize - 1))
		return -EINVAL;

	if (instr->len & (mtd->erasesize -1))
		return -EINVAL;

	if ((instr->len + instr->addr) > mtd->size)
		return -EINVAL;

	chipnum = instr->addr >> cfi->chipshift;
	chipnum /= cfi->interleave;
	adr = instr->addr - (chipnum << cfi->chipshift) * (cfi->interleave);
	len = instr->len;

	printk("erase : 0x%lx 0x%lx 0x%x 0x%lx\n", adr, len, chipnum, mtd->size);

	while(len) {
//printk("erase : 0x%x 0x%x 0x%x 0x%x\n", chipnum, adr, len, cfi->chipshift);
		ret = do_erase_2_by_16_oneblock(map, &cfi->chips[chipnum], adr);

		if (ret)
			return ret;

		adr += mtd->erasesize;
		len -= mtd->erasesize;

		if ((adr >> cfi->chipshift) / (cfi->interleave)) {
			adr = 0;
			chipnum++;
			
			if (chipnum >= cfi->numchips)
			break;
		}
	}
		
	if (instr->callback)
		instr->callback(instr);
	
	return 0;
}



static void cfi_amdext_sync (struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;
	int ret = 0;
	DECLARE_WAITQUEUE(wait, current);
printk("sync\n");

	for (i=0; !ret && i<cfi->numchips; i++) {
		chip = &cfi->chips[i];

	retry:
		spin_lock_bh(chip->mutex);

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
			spin_unlock_bh(chip->mutex);
			break;

		default:
			/* Not an idle state */
			add_wait_queue(&chip->wq, &wait);
			
			spin_unlock_bh(chip->mutex);

			schedule();
			
			remove_wait_queue(&chip->wq, &wait);

			goto retry;
		}
	}

	/* Unlock the chips again */

	for (i--; i >=0; i--) {
		chip = &cfi->chips[i];

		spin_lock_bh(chip->mutex);
		
		if (chip->state == FL_SYNCING) {
			chip->state = chip->oldstate;
			wake_up(&chip->wq);
		}
		spin_unlock_bh(chip->mutex);
	}
printk("sync end\n");
}


static int cfi_amdext_suspend(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;
	int ret = 0;
//printk("suspend\n");

	for (i=0; !ret && i<cfi->numchips; i++) {
		chip = &cfi->chips[i];

		spin_lock_bh(chip->mutex);

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
			spin_unlock_bh(chip->mutex);
			break;

		default:
			ret = -EAGAIN;
			break;
		}
	}

	/* Unlock the chips again */

	for (i--; i >=0; i--) {
		chip = &cfi->chips[i];

		spin_lock_bh(chip->mutex);
		
		if (chip->state == FL_PM_SUSPENDED) {
			chip->state = chip->oldstate;
			wake_up(&chip->wq);
		}
		spin_unlock_bh(chip->mutex);
	}
	
	return ret;
}

static void cfi_amdext_resume(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;
//printk("resume\n");

	for (i=0; i<cfi->numchips; i++) {
	
		chip = &cfi->chips[i];

		spin_lock_bh(chip->mutex);
		
		if (chip->state == FL_PM_SUSPENDED) {
			chip->state = chip->oldstate;
			wake_up(&chip->wq);
		}
		else
			printk("Argh. Chip not in PM_SUSPENDED state upon resume()\n");

		spin_unlock_bh(chip->mutex);
	}
}

static void cfi_amdext_destroy(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	kfree(cfi->cmdset_priv);
	inter_module_put(cfi->im_name);
	kfree(cfi);
}


static int __init cfi_amdext_init(void)
{
	inter_module_register(im_name, THIS_MODULE, &cfi_cmdset_0002);
	return 0;
}

static void __exit cfi_amdext_exit(void)
{
	inter_module_unregister(im_name);
}

module_init(cfi_amdext_init);
module_exit(cfi_amdext_exit);
