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
 * Revision 1.2  2001/12/18 13:35:15  bjornw
 * Applied the 2.4.13->2.4.16 CRIS patch to 2.5.1 (is a copy of 2.4.15).
 *
 * Revision 1.17  2001/11/12 19:42:38  pkj
 * Fixed compiler warnings.
 *
 * Revision 1.16  2001/11/08 11:18:58  jonashg
 * Always read from uncached address to avoid problems with flushing
 * cachelines after write and MTD-erase. No performance loss have been
 * seen yet.
 *
 * Revision 1.15  2001/10/19 12:41:04  jonashg
 * Name of probe has changed in MTD.
 *
 * Revision 1.14  2001/09/21 07:14:10  jonashg
 * Made root filesystem (cramfs) use mtdblock driver when booting from flash.
 *
 * Revision 1.13  2001/08/15 13:57:35  jonashg
 * Entire MTD updated to the linux 2.4.7 version.
 *
 * Revision 1.12  2001/06/11 09:50:30  jonashg
 * Oops, 2MB is 0x200000 bytes.
 *
 * Revision 1.11  2001/06/08 11:39:44  jonashg
 * Changed sizes and offsets in axis_default_partitions to use
 * CONFIG_ETRAX_PTABLE_SECTOR.
 *
 * Revision 1.10  2001/05/29 09:42:03  jonashg
 * Use macro for end marker length instead of sizeof.
 *
 * Revision 1.9  2001/05/29 08:52:52  jonashg
 * Gave names to the magic fours (size of the ptable end marker).
 *
 * Revision 1.8  2001/05/28 15:36:20  jonashg
 * * Removed old comment about ptable location in flash (it's a CONFIG_ option).
 * * Variable ptable was initialized twice to the same value.
 *
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

extern unsigned long romfs_start, romfs_length, romfs_in_flash; /* From head.S */

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
	memcpy(to, (void *)(FLASH_UNCACHED_ADDR + from), len);
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
	.name = "Axis flash",
	.size = WINDOW_SIZE,
	.buswidth = CONFIG_ETRAX_FLASH_BUSWIDTH,
	.read8 = flash_read8,
	.read16 = flash_read16,
	.read32 = flash_read32,
	.copy_from = flash_copy_from,
	.write8 = flash_write8,
	.write16 = flash_write16,
	.write32 = flash_write32,
};

/* If no partition-table was found, we use this default-set.
 */

#define MAX_PARTITIONS         7  
#define NUM_DEFAULT_PARTITIONS 3

/* Default flash size is 2MB. CONFIG_ETRAX_PTABLE_SECTOR is most likely the
 * size of one flash block and "filesystem"-partition needs 5 blocks to be able
 * to use JFFS.
 */
static struct mtd_partition axis_default_partitions[NUM_DEFAULT_PARTITIONS] = {
	{
		.name = "boot firmware",
		.size = CONFIG_ETRAX_PTABLE_SECTOR,
		.offset = 0
	},
	{
		.name = "kernel",
		.size = 0x200000 - (6 * CONFIG_ETRAX_PTABLE_SECTOR),
		.offset = CONFIG_ETRAX_PTABLE_SECTOR
	},
	{
		.name = "filesystem",
		.size = 5 * CONFIG_ETRAX_PTABLE_SECTOR,
		.offset = 0x200000 - (5 * CONFIG_ETRAX_PTABLE_SECTOR)
	}
};

static struct mtd_partition axis_partitions[MAX_PARTITIONS] = {
	{
		.name = "part0",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part1",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part2",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part3",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part4",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part5",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part6",
		.size = 0,
		.offset = 0
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

	printk(KERN_NOTICE "Axis flash mapping: %x at %lx\n",
	       WINDOW_SIZE, FLASH_CACHED_ADDR);

#ifdef CONFIG_MTD_CFI
	mymtd = (struct mtd_info *)do_map_probe("cfi_probe", &axis_map);
#endif

#ifdef CONFIG_MTD_AMDSTD
	if (!mymtd) {
		mymtd = (struct mtd_info *)do_map_probe("amd_flash", &axis_map);
	}
#endif

	if(!mymtd) {
		printk("%s: No flash chip found!\n", axis_map.name);
		return -ENXIO;
	}

	mymtd->module = THIS_MODULE;

	ptable_head = (struct partitiontable_head *)(FLASH_CACHED_ADDR +
		CONFIG_ETRAX_PTABLE_SECTOR + PARTITION_TABLE_OFFSET);
	pidx++;  /* first partition is always set to the default */

	if ((ptable_head->magic == PARTITION_TABLE_MAGIC)
	    && (ptable_head->size <
		(MAX_PARTITIONS * sizeof(struct partitiontable_entry) +
		PARTITIONTABLE_END_MARKER_SIZE))
	    && (*(unsigned long*)((void*)ptable_head + sizeof(*ptable_head) +
				  ptable_head->size -
				  PARTITIONTABLE_END_MARKER_SIZE)
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

	if (use_default_ptable) {
		printk(" Using default partition table\n");
		return add_mtd_partitions(mymtd, axis_default_partitions,
					  NUM_DEFAULT_PARTITIONS);
	} else {
		if (romfs_in_flash) {
			axis_partitions[pidx].name = "romfs";
			axis_partitions[pidx].size = romfs_length;
			axis_partitions[pidx].offset = romfs_start -
						       FLASH_CACHED_ADDR;
			axis_partitions[pidx].mask_flags |= MTD_WRITEABLE;

			printk(" Adding readonly partition for romfs image:\n");
			printk(pmsg, pidx, axis_partitions[pidx].offset,
			       axis_partitions[pidx].size);
			pidx++;
		}
		return add_mtd_partitions(mymtd, axis_partitions, pidx);
	}		
}

/* This adds the above to the kernels init-call chain */

module_init(init_axis_flash);

