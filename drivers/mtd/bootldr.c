/*
 * Read flash partition table from Compaq Bootloader
 *
 * Copyright 2001 Compaq Computer Corporation.
 *
 * $Id: bootldr.c,v 1.6 2001/10/02 15:05:11 dwmw2 Exp $
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
#include <asm/setup.h>
#include <linux/bootmem.h>

#define FLASH_PARTITION_NAMELEN 32
enum LFR_FLAGS {
   LFR_SIZE_PREFIX = 1,		/* prefix data with 4-byte size */
   LFR_PATCH_BOOTLDR = 2,	/* patch bootloader's 0th instruction */
   LFR_KERNEL = 4,		/* add BOOTIMG_MAGIC, imgsize and VKERNEL_BASE to head of programmed region (see bootldr.c) */
   LFR_EXPAND = 8               /* expand partition size to fit rest of flash */
};

// the tags are parsed too early to malloc or alloc_bootmem so we'll fix it
// for now
#define MAX_NUM_PARTITIONS 8
typedef struct FlashRegion {
   char name[FLASH_PARTITION_NAMELEN];
   unsigned long base;
   unsigned long size;
   enum LFR_FLAGS flags;
} FlashRegion;

typedef struct BootldrFlashPartitionTable {
  int magic; /* should be filled with 0x646c7470 (btlp) BOOTLDR_PARTITION_MAGIC */
  int npartitions;
  struct FlashRegion partition[8];
} BootldrFlashPartitionTable;

#define BOOTLDR_MAGIC      0x646c7462        /* btld: marks a valid bootldr image */
#define BOOTLDR_PARTITION_MAGIC  0x646c7470  /* btlp: marks a valid bootldr partition table in params sector */

#define BOOTLDR_MAGIC_OFFSET 0x20 /* offset 0x20 into the bootldr */
#define BOOTCAP_OFFSET 0X30 /* offset 0x30 into the bootldr */

#define BOOTCAP_WAKEUP	(1<<0)
#define BOOTCAP_PARTITIONS (1<<1) /* partition table stored in params sector */
#define BOOTCAP_PARAMS_AFTER_BOOTLDR (1<<2) /* params sector right after bootldr sector(s), else in last sector */

static struct BootldrFlashPartitionTable Table;
static struct BootldrFlashPartitionTable *partition_table = NULL;


int parse_bootldr_partitions(struct mtd_info *master, struct mtd_partition **pparts)
{
	struct mtd_partition *parts;
	int ret, retlen, i;
	int npartitions = 0;
	long partition_table_offset;
	long bootmagic = 0;
	long bootcap = 0;
	int namelen = 0;

	char *names; 

#if 0
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
	printk(__FUNCTION__ ": ptable_addr=%#lx\n", ptable_addr);


	/* Read the partition table */
	partition_table = (struct BootldrFlashPartitionTable *)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!partition_table)
		return -ENOMEM;

	ret = master->read(master, partition_table_offset,
			   PAGE_SIZE, &retlen, (void *)partition_table);
	if (ret)
	    goto out;

#endif
	if (!partition_table)
	    return -ENOMEM;

	
	printk(__FUNCTION__ ": magic=%#x\n", partition_table->magic);
	printk(__FUNCTION__ ": numPartitions=%#x\n", partition_table->npartitions);


	/* check for partition table magic number */
	if (partition_table->magic != BOOTLDR_PARTITION_MAGIC) 
		goto out;
	npartitions = (partition_table->npartitions > MAX_NUM_PARTITIONS)?
	    MAX_NUM_PARTITIONS:partition_table->npartitions;	

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



	// from here we use the partition table
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
#if 0
	if (partition_table)
		kfree(partition_table);
#endif
	
	return ret;
}


static int __init parse_tag_ptable(const struct tag *tag)
{
    char buf[128];
    int i;
    int j;
    
    partition_table = &Table;

#ifdef CONFIG_DEBUG_LL    
    sprintf(buf,"ptable: magic = = 0x%lx  npartitions= %d \n",
	    tag->u.ptable.magic,tag->u.ptable.npartitions);
    printascii(buf);
    
    for (i=0; i<tag->u.ptable.npartitions; i++){
	sprintf(buf,"ptable: partition name = %s base= 0x%lx  size= 0x%lx flags= 0x%lx\n",
	    (char *) (&tag->u.ptable.partition[i].name[0]),
		tag->u.ptable.partition[i].base,
		tag->u.ptable.partition[i].size,
		tag->u.ptable.partition[i].flags);
	printascii(buf);
    }
#endif

    memcpy((void *)partition_table,(void *) (&(tag->u.ptable)),sizeof(partition_table) +
	sizeof(struct FlashRegion)*tag->u.ptable.npartitions);

    
    return 0;
}

__tagtable(ATAG_PTABLE, parse_tag_ptable);

EXPORT_SYMBOL(parse_bootldr_partitions);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Compaq Computer Corporation");
MODULE_DESCRIPTION("Parsing code for Compaq bootldr partitions");
