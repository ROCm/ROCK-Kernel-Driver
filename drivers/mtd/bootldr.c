/*
 * Read flash partition table from Compaq Bootloader
 *
 * Copyright 2001 Compaq Computer Corporation.
 *
 * $Id: bootldr.c,v 1.4 2001/06/02 18:24:27 nico Exp $
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * COMPAQ COMPUTER CORPORATION MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 */

/*
 * Maintainer: Jamey Hicks (jamey.hicks@compaq.com)
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define FLASH_PARTITION_NAMELEN 32
enum LFR_FLAGS {
   LFR_SIZE_PREFIX = 1,		/* prefix data with 4-byte size */
   LFR_PATCH_BOOTLDR = 2,	/* patch bootloader's 0th instruction */
   LFR_KERNEL = 4,		/* add BOOTIMG_MAGIC, imgsize and VKERNEL_BASE to head of programmed region (see bootldr.c) */
   LFR_EXPAND = 8               /* expand partition size to fit rest of flash */
};

typedef struct FlashRegion {
   char name[FLASH_PARTITION_NAMELEN];
   unsigned long base;
   unsigned long size;
   enum LFR_FLAGS flags;
} FlashRegion;

typedef struct BootldrFlashPartitionTable {
  int magic; /* should be filled with 0x646c7470 (btlp) BOOTLDR_PARTITION_MAGIC */
  int npartitions;
  struct FlashRegion partition[0];
} BootldrFlashPartitionTable;

#define BOOTLDR_MAGIC      0x646c7462        /* btld: marks a valid bootldr image */
#define BOOTLDR_PARTITION_MAGIC  0x646c7470  /* btlp: marks a valid bootldr partition table in params sector */

#define BOOTLDR_MAGIC_OFFSET 0x20 /* offset 0x20 into the bootldr */
#define BOOTCAP_OFFSET 0X30 /* offset 0x30 into the bootldr */

#define BOOTCAP_WAKEUP	(1<<0)
#define BOOTCAP_PARTITIONS (1<<1) /* partition table stored in params sector */
#define BOOTCAP_PARAMS_AFTER_BOOTLDR (1<<2) /* params sector right after bootldr sector(s), else in last sector */

int parse_bootldr_partitions(struct mtd_info *master, struct mtd_partition **pparts)
{
	struct mtd_partition *parts;
	int ret, retlen, i;
	int npartitions = 0;
	long partition_table_offset;
	long bootmagic = 0;
	long bootcap = 0;
	int namelen = 0;
	struct BootldrFlashPartitionTable *partition_table = NULL; 
	char *names; 

	/* verify bootldr magic */
	ret = master->read(master, BOOTLDR_MAGIC_OFFSET, sizeof(long), &retlen, (void *)&bootmagic);
	if (ret) 
		goto out;
        if (bootmagic != BOOTLDR_MAGIC)
                goto out;
	/* see if bootldr supports partition tables and where to find the partition table */
	ret = master->read(master, BOOTCAP_OFFSET, sizeof(long), &retlen, (void *)&bootcap);
	if (ret) 
		goto out;

	if (!(bootcap & BOOTCAP_PARTITIONS))
		goto out;
	if (bootcap & BOOTCAP_PARAMS_AFTER_BOOTLDR)
		partition_table_offset = master->erasesize;
	else
		partition_table_offset = master->size - master->erasesize;

	printk(__FUNCTION__ ": partition_table_offset=%#lx\n", partition_table_offset);

	/* Read the partition table */
	partition_table = (struct BootldrFlashPartitionTable *)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!partition_table)
		return -ENOMEM;
	ret = master->read(master, partition_table_offset,
			   PAGE_SIZE, &retlen, (void *)partition_table);
	if (ret)
		goto out;

	printk(__FUNCTION__ ": magic=%#x\n", partition_table->magic);

	/* check for partition table magic number */
	if (partition_table->magic != BOOTLDR_PARTITION_MAGIC) 
		goto out;
	npartitions = partition_table->npartitions;

	printk(__FUNCTION__ ": npartitions=%#x\n", npartitions);

	for (i = 0; i < npartitions; i++) {
		namelen += strlen(partition_table->partition[i].name) + 1;
	}

	parts = kmalloc(sizeof(*parts)*npartitions + namelen, GFP_KERNEL);
	if (!parts) {
		ret = -ENOMEM;
		goto out;
	}
	names = (char *)&parts[npartitions];
	memset(parts, 0, sizeof(*parts)*npartitions + namelen);

	for (i = 0; i < npartitions; i++) {
                struct FlashRegion *partition = &partition_table->partition[i];
		const char *name = partition->name;
		parts[i].name = names;
		names += strlen(name) + 1;
		strcpy(parts[i].name, name);

                if (partition->flags & LFR_EXPAND)
                        parts[i].size = MTDPART_SIZ_FULL;
                else
                        parts[i].size = partition->size;
		parts[i].offset = partition->base;
		parts[i].mask_flags = 0;
		
		printk("        partition %s o=%x s=%x\n", 
		       parts[i].name, parts[i].offset, parts[i].size);

	}

	ret = npartitions;
	*pparts = parts;

 out:
	if (partition_table)
		kfree(partition_table);
	return ret;
}

EXPORT_SYMBOL(parse_bootldr_partitions);
