/*
 * Physical mapping layer for MTD using the Axis partitiontable format
 *
 * Copyright (c) 2001 Axis Communications AB
 *
 * This file is under the GPL.
 *
 * First partition is always sector 0 regardless of if we find a partitiontable
 * or not. In the start of the next sector, there can be a partitiontable that
 * tells us what other partitions to define. If there isn't, we use a default
 * partition split defined below.
 *
 * $Log: axisflashmap.c,v $
 * Revision 1.7  2001/04/05 13:41:46  markusl
 * Updated according to review remarks
 *
 * Revision 1.6  2001/03/07 09:21:21  bjornw
 * No need to waste .data
 *
 * Revision 1.5  2001/03/06 16:27:01  jonashg
 * Probe the entire flash area for flash devices.
 *
 * Revision 1.4  2001/02/23 12:47:15  bjornw
 * Uncached flash in LOW_MAP moved from 0xe to 0x8
 *
 * Revision 1.3  2001/02/16 12:11:45  jonashg
 * MTD driver amd_flash is now included in MTD CVS repository.
 * (It's now in drivers/mtd).
 *
 * Revision 1.2  2001/02/09 11:12:22  jonashg
 * Support for AMD compatible non-CFI flash chips.
 * Only tested with Toshiba TC58FVT160 so far.
 *
 * Revision 1.1  2001/01/12 17:01:18  bjornw
 * * Added axisflashmap.c, a physical mapping for MTD that reads and understands
 *   Axis partition-table format.
 *
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/config.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/axisflashmap.h>
#include <asm/mmu.h>

#ifdef CONFIG_CRIS_LOW_MAP
#define FLASH_UNCACHED_ADDR  KSEG_8
#define FLASH_CACHED_ADDR    KSEG_5
#else
#define FLASH_UNCACHED_ADDR  KSEG_E
#define FLASH_CACHED_ADDR    KSEG_F
#endif

/*
 * WINDOW_SIZE is the total size where the flash chips may be mapped.
 * MTD probes should find all devices there and it does not matter
 * if there are unmapped gaps or aliases (mirrors of flash devices).
 * The MTD probes will ignore them.
 */

#define WINDOW_SIZE  (128 * 1024 * 1024)

/* 
 * Map driver
 *
 * Ok this is the scoop - we need to access the flash both with and without
 * the cache - without when doing all the fancy flash interfacing, and with
 * when we do actual copying because otherwise it will be slow like molasses.
 */

static __u8 flash_read8(struct map_info *map, unsigned long ofs)
{
	return *(__u8 *)(FLASH_UNCACHED_ADDR + ofs);
}

static __u16 flash_read16(struct map_info *map, unsigned long ofs)
{
	return *(__u16 *)(FLASH_UNCACHED_ADDR + ofs);
}

static __u32 flash_read32(struct map_info *map, unsigned long ofs)
{
	return *(volatile unsigned int *)(FLASH_UNCACHED_ADDR + ofs);
}

static void flash_copy_from(struct map_info *map, void *to,
			    unsigned long from, ssize_t len)
{
	memcpy(to, (void *)(FLASH_CACHED_ADDR + from), len);
}

static void flash_write8(struct map_info *map, __u8 d, unsigned long adr)
{
	*(__u8 *)(FLASH_UNCACHED_ADDR + adr) = d;
}

static void flash_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	*(__u16 *)(FLASH_UNCACHED_ADDR + adr) = d;
}

static void flash_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	*(__u32 *)(FLASH_UNCACHED_ADDR + adr) = d;
}

static struct map_info axis_map = {
	name: "Axis flash",
	size: WINDOW_SIZE,
	buswidth: CONFIG_ETRAX_FLASH_BUSWIDTH,
	read8: flash_read8,
	read16: flash_read16,
	read32: flash_read32,
	copy_from: flash_copy_from,
	write8: flash_write8,
	write16: flash_write16,
	write32: flash_write32,
};

/* If no partition-table was found, we use this default-set.
 */

#define MAX_PARTITIONS         7  
#define NUM_DEFAULT_PARTITIONS 3

static struct mtd_partition axis_default_partitions[NUM_DEFAULT_PARTITIONS] = {
	{
		name: "boot firmware",
		size: CONFIG_ETRAX_PTABLE_SECTOR,
		offset: 0
	},
	{
		name: "kernel",
		size: 0x1a0000,
		offset: CONFIG_ETRAX_PTABLE_SECTOR
	},
	{
		name: "filesystem",
		size: 0x50000,
		offset: (0x1a0000 + CONFIG_ETRAX_PTABLE_SECTOR)
	}
};

static struct mtd_partition axis_partitions[MAX_PARTITIONS] = {
	{
		name: "part0",
		size: 0,
		offset: 0
	},
	{
		name: "part1",
		size: 0,
		offset: 0
	},
	{
		name: "part2",
		size: 0,
		offset: 0
	},
	{
		name: "part3",
		size: 0,
		offset: 0
	},
	{
		name: "part4",
		size: 0,
		offset: 0
	},
	{
		name: "part5",
		size: 0,
		offset: 0
	},
	{
		name: "part6",
		size: 0,
		offset: 0
	},
};

/* 
 * This is the master MTD device for which all the others are just
 * auto-relocating aliases.
 */
static struct mtd_info *mymtd;

/* CFI-scan the flash, and if there was a chip, read the partition-table
 * and register the partitions with MTD.
 */

static int __init
init_axis_flash(void)
{
	int pidx = 0;
	struct partitiontable_head *ptable_head;
	struct partitiontable_entry *ptable;
	int use_default_ptable = 1; /* Until proven otherwise */
	const char *pmsg = "  /dev/flash%d at 0x%x, size 0x%x\n";

	printk(KERN_NOTICE "Axis flash mapping: %x at %x\n",
	       WINDOW_SIZE, FLASH_CACHED_ADDR);

	mymtd = (struct mtd_info *)do_cfi_probe(&axis_map);

#ifdef CONFIG_MTD_AMDSTD
	if (!mymtd) {
		mymtd = (struct mtd_info *)do_amd_flash_probe(&axis_map);
	}
#endif

	if(!mymtd) {
		printk("%s: No flash chip found!\n", axis_map.name);
		return -ENXIO;
	}

	mymtd->module = THIS_MODULE;

	/* The partition-table is at an offset within the second 
	 * sector of the flash. We _define_ this to be at offset 64k
	 * even if the actual sector-size in the flash changes.. for
	 * now at least.
	 */

	ptable_head = (struct partitiontable_head *)(FLASH_CACHED_ADDR +
		CONFIG_ETRAX_PTABLE_SECTOR + PARTITION_TABLE_OFFSET);
	pidx++;  /* first partition is always set to the default */

	if ((ptable_head->magic == PARTITION_TABLE_MAGIC)
	    && (ptable_head->size <
		(MAX_PARTITIONS * sizeof(struct partitiontable_entry) + 4))
	    && (*(unsigned long*)((void*)ptable_head + sizeof(*ptable_head) +
				  ptable_head->size - 4)
		== PARTITIONTABLE_END_MARKER)) {
		/* Looks like a start, sane length and end of a
		 * partition table, lets check csum etc.
		 */
		int ptable_ok = 0;
		struct partitiontable_entry *max_addr =
			(struct partitiontable_entry *)
			((unsigned long)ptable_head + sizeof(*ptable_head) +
			 ptable_head->size);
		unsigned long offset = CONFIG_ETRAX_PTABLE_SECTOR;
		unsigned char *p;
		unsigned long csum = 0;
		
		ptable = (struct partitiontable_entry *)
			((unsigned long)ptable_head + sizeof(*ptable_head));

		/* Lets be PARANOID, and check the checksum. */
		p = (unsigned char*) ptable;

		while (p <= (unsigned char*)max_addr) {
			csum += *p++;
			csum += *p++;
			csum += *p++;
			csum += *p++;
		}
		/* printk("  total csum: 0x%08X 0x%08X\n",
		   csum, ptable_head->checksum); */
		ptable_ok = (csum == ptable_head->checksum);

		/* Read the entries and use/show the info.  */
		ptable = (struct partitiontable_entry *)
			((unsigned long)ptable_head + sizeof(*ptable_head));
		
		printk(" Found %s partition table at 0x%08lX-0x%08lX.\n",
		       (ptable_ok ? "valid" : "invalid"),
		       (unsigned long)ptable_head,
		       (unsigned long)max_addr);

		/* We have found a working bootblock.  Now read the
		   partition table.  Scan the table.  It ends when
		   there is 0xffffffff, that is, empty flash.  */
		
		while (ptable_ok
		       && ptable->offset != 0xffffffff
		       && ptable < max_addr
		       && pidx < MAX_PARTITIONS) {

			axis_partitions[pidx].offset = offset + ptable->offset;
			axis_partitions[pidx].size = ptable->size;

			printk(pmsg, pidx, axis_partitions[pidx].offset,
			       axis_partitions[pidx].size);
			pidx++;
			ptable++;
		}
		use_default_ptable = !ptable_ok;
	}

	if(use_default_ptable) {
		printk(" Using default partition table\n");
		return add_mtd_partitions(mymtd, axis_default_partitions,
					  NUM_DEFAULT_PARTITIONS);
	} else {
		return add_mtd_partitions(mymtd, axis_partitions, pidx);
	}		
}

/* This adds the above to the kernels init-call chain */

module_init(init_axis_flash);

