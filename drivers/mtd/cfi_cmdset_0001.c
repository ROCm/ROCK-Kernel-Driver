/*
 * Common Flash Interface support:
 *   Intel Extended Vendor Command Set (ID 0x0001)
 *
 * (C) 2000 Red Hat. GPL'd
 *
 * $Id: cfi_cmdset_0001.c,v 1.21 2000/07/13 10:36:14 dwmw2 Exp $
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
static int cfi_intelext_read_1_by_16 (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int cfi_intelext_write_1_by_16(struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int cfi_intelext_erase_1_by_16 (struct mtd_info *, struct erase_info *);
static void cfi_intelext_sync (struct mtd_info *);
static int cfi_intelext_suspend (struct mtd_info *);
static void cfi_intelext_resume (struct mtd_info *);

static void cfi_intelext_destroy(struct mtd_info *);

static void cfi_cmdset_0001(struct map_info *, int, unsigned long);

static struct mtd_info *cfi_intelext_setup (struct map_info *);

static const char im_name[] = "cfi_cmdset_0001";

/* This routine is made available to other mtd code via
 * inter_module_register.  It must only be accessed through
 * inter_module_get which will bump the use count of this module.  The
 * addresses passed back in cfi are valid as long as the use count of
 * this module is non-zero, i.e. between inter_module_get and
 * inter_module_put.  Keith Owens <kaos@ocs.com.au> 29 Oct 2000.
 */
static void cfi_cmdset_0001(struct map_info *map, int primary, unsigned long base)
{
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct cfi_pri_intelext *extp;

	__u16 adr = primary?cfi->cfiq.P_ADR:cfi->cfiq.A_ADR;

	printk(" Intel/Sharp Extended Query Table at 0x%4.4X\n", adr);

	if (!adr)
		return;

	/* Switch it into Query Mode */
	switch(map->buswidth) {
	case 1:
		map->write8(map, 0x98, 0x55);
		break;
	case 2:
		map->write16(map, 0x9898, 0xaa);
		break;
	case 4:
		map->write32(map, 0x98989898, 0x154);
		break;
	}

	extp = kmalloc(sizeof(*extp), GFP_KERNEL);
	if (!extp) {
		printk("Failed to allocate memory\n");
		return;
	}

	/* Read in the Extended Query Table */
	for (i=0; i<sizeof(*extp); i++) {
		((unsigned char *)extp)[i] = 
			map->read8(map, (base+((adr+i)*map->buswidth)));
	}

	if (extp->MajorVersion != '1' || 
	    (extp->MinorVersion < '0' || extp->MinorVersion > '2')) {
		printk("  Unknown IntelExt Extended Query version %c.%c.\n",
		       extp->MajorVersion, extp->MinorVersion);
		kfree(extp);
		return;
	}

	/* Do some byteswapping if necessary */
	extp->FeatureSupport = le32_to_cpu(extp->FeatureSupport);
	extp->BlkStatusRegMask = le32_to_cpu(extp->BlkStatusRegMask);

	
	/* Tell the user about it in lots of lovely detail */
#if 0  
	printk("  Feature/Command Support: %4.4X\n", extp->FeatureSupport);
	printk("     - Chip Erase:         %s\n", extp->FeatureSupport&1?"supported":"unsupported");
	printk("     - Suspend Erase:      %s\n", extp->FeatureSupport&2?"supported":"unsupported");
	printk("     - Suspend Program:    %s\n", extp->FeatureSupport&4?"supported":"unsupported");
	printk("     - Legacy Lock/Unlock: %s\n", extp->FeatureSupport&8?"supported":"unsupported");
	printk("     - Queued Erase:       %s\n", extp->FeatureSupport&16?"supported":"unsupported");
	printk("     - Instant block lock: %s\n", extp->FeatureSupport&32?"supported":"unsupported");
	printk("     - Protection Bits:    %s\n", extp->FeatureSupport&64?"supported":"unsupported");
	printk("     - Page-mode read:     %s\n", extp->FeatureSupport&128?"supported":"unsupported");
	printk("     - Synchronous read:   %s\n", extp->FeatureSupport&256?"supported":"unsupported");
	for (i=9; i<32; i++) {
		if (extp->FeatureSupport & (1<<i)) 
			printk("     - Unknown Bit %X:      supported\n", i);
	}
	
	printk("  Supported functions after Suspend: %2.2X\n", extp->SuspendCmdSupport);
	printk("     - Program after Erase Suspend: %s\n", extp->SuspendCmdSupport&1?"supported":"unsupported");
	for (i=1; i<8; i++) {
		if (extp->SuspendCmdSupport & (1<<i))
			printk("     - Unknown Bit %X:               supported\n", i);
	}
	
	printk("  Block Status Register Mask: %4.4X\n", extp->BlkStatusRegMask);
	printk("     - Lock Bit Active:      %s\n", extp->BlkStatusRegMask&1?"yes":"no");
	printk("     - Valid Bit Active:     %s\n", extp->BlkStatusRegMask&2?"yes":"no");
	for (i=2; i<16; i++) {
		if (extp->BlkStatusRegMask & (1<<i))
			printk("     - Unknown Bit %X Active: yes\n",i);
	}
	
	printk("  Vcc Logic Supply Optimum Program/Erase Voltage: %d.%d V\n", 
	       extp->VccOptimal >> 8, extp->VccOptimal & 0xf);
	if (extp->VppOptimal)
		printk("  Vpp Programming Supply Optimum Program/Erase Voltage: %d.%d V\n", 
		       extp->VppOptimal >> 8, extp->VppOptimal & 0xf);
#endif	
	/* OK. We like it. Take over the control of it. */

	/* Switch it into Read Mode */
	switch(map->buswidth) {
	case 1:
		map->write8(map, 0xff, 0x55);
		break;
	case 2:
		map->write16(map, 0xffff, 0xaa);
		break;
	case 4:
		map->write32(map, 0xffffffff, 0x154);
		break;
	}


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
		

	cfi->cmdset_setup = cfi_intelext_setup;
	cfi->im_name = im_name;
	cfi->cmdset_priv = extp;
	
	return;
}

static struct mtd_info *cfi_intelext_setup(struct map_info *map)
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
	mtd->size = (1 << cfi->cfiq.DevSize) * cfi->numchips;
	mtd->erase = cfi_intelext_erase_1_by_16;
	mtd->read = cfi_intelext_read_1_by_16;
	mtd->write = cfi_intelext_write_1_by_16;
	mtd->sync = cfi_intelext_sync;
	mtd->suspend = cfi_intelext_suspend;
	mtd->resume = cfi_intelext_resume;
	mtd->flags = MTD_CAP_NORFLASH;
	map->fldrv_destroy = cfi_intelext_destroy;
	mtd->name = map->name;
	return mtd;
}

static inline int do_read_1_by_16_onechip(struct map_info *map, struct flchip *chip, loff_t adr, size_t len, u_char *buf)
{
	__u16 status;
	unsigned long timeo = jiffies + HZ;
	DECLARE_WAITQUEUE(wait, current);

	adr += chip->start;

 retry:
	spin_lock_bh(chip->mutex);

	/* Check that the chip's ready to talk to us.
	 * Later, we can actually think about interrupting it
	 * if it's in FL_ERASING or FL_WRITING state.
	 * Not just yet, though.
	 */
	switch (chip->state) {
#if 0
	case FL_ERASING:
	case FL_WRITING:
		/* Suspend the operation, set state to FL_xxx_SUSPENDED */
#endif

	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
	case FL_READY:
		map->write16(map, cpu_to_le16(0x0070), adr);
		chip->state = FL_STATUS;

	case FL_STATUS:
		status = le16_to_cpu(map->read16(map, adr));

		if (!(status & (1<<7))) {
			static int z=0;
			/* Urgh. Chip not yet ready to talk to us. */
			if (time_after(jiffies, timeo)) {
				spin_unlock_bh(chip->mutex);
				printk("waiting for chip to be ready timed out in read");
				return -EIO;
			}

			/* Latency issues. Drop the lock, wait a while and retry */
			spin_unlock_bh(chip->mutex);

			z++;
			if ( 0 && !(z % 100 )) 
				printk("chip not ready yet before read. looping\n");

			udelay(1);

			goto retry;
		}
		break;

	default:
		printk("Waiting for chip, status = %d\n", chip->state);

		/* Stick ourselves on a wait queue to be woken when
		   someone changes the status */

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

	map->write16(map, cpu_to_le16(0x00ff), adr);
	chip->state = FL_READY;

	map->copy_from(map, buf, adr, len);

	if (chip->state == FL_ERASE_SUSPENDED || 
	    chip->state == FL_WRITE_SUSPENDED) {
		printk("Who in hell suspended the pending operation? I didn't write that code yet!\n");
		/* Restart it and set the state accordingly */
	}

	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);

	return 0;
}

static int cfi_intelext_read_1_by_16 (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
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

		ret = do_read_1_by_16_onechip(map, &cfi->chips[chipnum], ofs, thislen, buf);
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

static inline int do_write_1_by_16_oneword(struct map_info *map, struct flchip *chip, unsigned long adr, __u16 datum)
{
	__u16 status;
	unsigned long timeo = jiffies + HZ;
	DECLARE_WAITQUEUE(wait, current);
	int z = 0;
	adr += chip->start;

 retry:
	spin_lock_bh(chip->mutex);

	/* Check that the chip's ready to talk to us.
	 * Later, we can actually think about interrupting it
	 * if it's in FL_ERASING state.
	 * Not just yet, though.
	 */
	switch (chip->state) {
	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
	case FL_READY:
		map->write16(map, cpu_to_le16(0x0070), adr);
		chip->state = FL_STATUS;
		timeo = jiffies + HZ;

	case FL_STATUS:
		status = le16_to_cpu(map->read16(map, adr));

		if (!(status & (1<<7))) {

			/* Urgh. Chip not yet ready to talk to us. */
			if (time_after(jiffies, timeo)) {
				spin_unlock_bh(chip->mutex);
				printk("waiting for chip to be ready timed out in read");
				return -EIO;
			}

			/* Latency issues. Drop the lock, wait a while and retry */
			spin_unlock_bh(chip->mutex);

			z++;
			if ( 0 && !(z % 100 ))
				printk("chip not ready yet before write. looping\n");
			
			udelay(1);

			goto retry;
		}
		break;

	default:
		printk("Waiting for chip, status = %d\n", chip->state);

		/* Stick ourselves on a wait queue to be woken when
		   someone changes the status */

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
	
	map->write16(map, cpu_to_le16(0x0040), adr);
	map->write16(map, datum, adr);
	chip->state = FL_WRITING;

	timeo = jiffies + (HZ/2);

	spin_unlock_bh(chip->mutex);
	udelay(chip->word_write_time);
	spin_lock_bh(chip->mutex);

	z = 0;
	while ( !( (status = le16_to_cpu(map->read16(map, adr)))  & 0x80 ) ) {

		if (chip->state != FL_WRITING) {
			/* Someone's suspended the write. Sleep */
			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			
			spin_unlock_bh(chip->mutex);
			
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			
			if (signal_pending(current))
				return -EINTR;
			
			timeo = jiffies + (HZ / 2); /* FIXME */

			spin_lock_bh(chip->mutex);
			continue;
		}

		/* OK Still waiting */
		if (time_after(jiffies, timeo)) {
			chip->state = FL_STATUS;
			spin_unlock_bh(chip->mutex);
			printk("waiting for chip to be ready timed out in read");
			return -EIO;
		}
		
		/* Latency issues. Drop the lock, wait a while and retry */
		spin_unlock_bh(chip->mutex);
		
		z++;
		if ( 0 && !(z % 100 )) 
		  printk("chip not ready yet after write. looping\n");
		
		udelay(1);
		
		spin_lock_bh(chip->mutex);
		continue;
	}
	if (!z) {
		chip->word_write_time--;
		if (!chip->word_write_time)
			chip->word_write_time++;
	}
	if (z > 1) 
		chip->word_write_time++;

	/* Done and happy. */
	chip->state = FL_STATUS;
	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);
	//	printk("write ret OK at %lx\n", adr);
	return 0;
}


/* This version only uses the 'word write' instruction. We should update it
 * to write using 'buffer write' if it's available 
 */
static int cfi_intelext_write_1_by_16 (struct mtd_info *mtd, loff_t to , size_t len, size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int ret = 0;
	int chipnum;
	unsigned long ofs;

	*retlen = 0;
	chipnum = to >> cfi->chipshift;
	ofs = to  - (chipnum << cfi->chipshift);

	/* If it's not word-aligned, do the first byte write */
	if (ofs & 1) {
#if defined(__LITTLE_ENDIAN)
		ret = do_write_1_by_16_oneword(map, &cfi->chips[chipnum],
					       ofs, 0xFF | (*buf << 8));
#elif defined(__BIG_ENDIAN) 
		ret = do_write_1_by_16_oneword(map, &cfi->chips[chipnum],
					       ofs, 0xFF00 | (*buf));
#else
#error define a sensible endianness
#endif
		if (ret) 
			return ret;
		
		ofs++;
		buf++;
		(*retlen)++;
		len--;

		if (ofs >> cfi->chipshift) {
			chipnum ++; 
			ofs = 0;
			if (chipnum == cfi->numchips)
				return 0;
		}
	}
	
	while(len > 1) {
		ret = do_write_1_by_16_oneword(map, &cfi->chips[chipnum],
					       ofs, *(__u16 *)buf);
		if (ret)
			return ret;

		ofs += 2;
		buf += 2;
		(*retlen) += 2;
		len -= 2;

		if (ofs >> cfi->chipshift) {
			chipnum ++; 
			ofs = 0;
			if (chipnum == cfi->numchips)
				return 0;
		}
	}

	if (len) {
		/* Final byte to write */
#if defined(__LITTLE_ENDIAN)
		ret = do_write_1_by_16_oneword(map, &cfi->chips[chipnum],
					       ofs, 0xFF00 | (*buf));
#elif defined(__BIG_ENDIAN) 
		ret = do_write_1_by_16_oneword(map, &cfi->chips[chipnum],
					       ofs, 0xFF | (*buf << 8));
#else
#error define a sensible endianness
#endif
		if (ret) 
			return ret;
		
		(*retlen)++;
	}

	return 0;
}


static inline int do_erase_1_by_16_oneblock(struct map_info *map, struct flchip *chip, unsigned long adr)
{
	__u16 status;
	unsigned long timeo = jiffies + HZ;
	DECLARE_WAITQUEUE(wait, current);

	adr += chip->start;

 retry:
	spin_lock_bh(chip->mutex);

	/* Check that the chip's ready to talk to us. */
	switch (chip->state) {
	case FL_CFI_QUERY:
	case FL_JEDEC_QUERY:
	case FL_READY:
		map->write16(map, cpu_to_le16(0x0070), adr);
		chip->state = FL_STATUS;
		timeo = jiffies + HZ;

	case FL_STATUS:
		status = le16_to_cpu(map->read16(map, adr));

		if (!(status & (1<<7))) {
			static int z=0;
			/* Urgh. Chip not yet ready to talk to us. */
			if (time_after(jiffies, timeo)) {
				spin_unlock_bh(chip->mutex);
				printk("waiting for chip to be ready timed out in erase");
				return -EIO;
			}

			/* Latency issues. Drop the lock, wait a while and retry */
			spin_unlock_bh(chip->mutex);

			z++;
			if ( 0 && !(z % 100 )) 
				printk("chip not ready yet before erase. looping\n");

			udelay(1);

			goto retry;
		}
		break;

	default:
		printk("Waiting for chip, status = %d\n", chip->state);

		/* Stick ourselves on a wait queue to be woken when
		   someone changes the status */

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
	
	map->write16(map, cpu_to_le16(0x0020), adr);
	map->write16(map, cpu_to_le16(0x00D0), adr);

	chip->state = FL_ERASING;
	
	timeo = jiffies + (HZ*2);
	spin_unlock_bh(chip->mutex);
	schedule_timeout(HZ);
	spin_lock_bh(chip->mutex);

	/* FIXME. Use a timer to check this, and return immediately. */
	/* Once the state machine's known to be working I'll do that */

	while ( !( (status = le16_to_cpu(map->read16(map, adr)))  & 0x80 ) ) {
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
			chip->state = FL_STATUS;
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
	chip->state = FL_STATUS;
	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);
	//printk("erase ret OK\n");
	return 0;
}

static int cfi_intelext_erase_1_by_16 (struct mtd_info *mtd, struct erase_info *instr)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long adr, len;
	int chipnum, ret = 0;

	if (instr->addr & (mtd->erasesize - 1))
		return -EINVAL;

	if (instr->len & (mtd->erasesize -1))
		return -EINVAL;

	if ((instr->len + instr->addr) > mtd->size)
		return -EINVAL;

	chipnum = instr->addr >> cfi->chipshift;
	adr = instr->addr - (chipnum << cfi->chipshift);
	len = instr->len;

	while(len) {
		ret = do_erase_1_by_16_oneblock(map, &cfi->chips[chipnum], adr);
		
		if (ret)
			return ret;

		adr += mtd->erasesize;
		len -= mtd->erasesize;

		if (adr >> cfi->chipshift) {
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



static void cfi_intelext_sync (struct mtd_info *mtd)
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
}


static int cfi_intelext_suspend(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;
	int ret = 0;

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

static void cfi_intelext_resume(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	int i;
	struct flchip *chip;

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

static void cfi_intelext_destroy(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	kfree(cfi->cmdset_priv);
	inter_module_put(cfi->im_name);
	kfree(cfi);
}


static int __init cfi_intelext_init(void)
{
	inter_module_register(im_name, THIS_MODULE, &cfi_cmdset_0001);
	return 0;
}

static void __exit cfi_intelext_exit(void)
{
	inter_module_unregister(im_name);
}

module_init(cfi_intelext_init);
module_exit(cfi_intelext_exit);
