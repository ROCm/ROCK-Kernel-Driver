/*
 * drivers/mtd/nand/sharpsl.c
 *
 *  Copyright (C) 2004 Richard Purdie
 *
 *  $Id: sharpsl.c,v 1.2 2004/11/24 20:38:07 rpurdie Exp $
 *
 *  Based on Sharp's NAND driver sharp_sl.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/hardware.h>

static void __iomem *sharpsl_io_base;
static int sharpsl_phys_base = 0x0C000000;

/* register offset */
#define ECCLPLB	 	sharpsl_io_base+0x00	/* line parity 7 - 0 bit */
#define ECCLPUB	 	sharpsl_io_base+0x04	/* line parity 15 - 8 bit */
#define ECCCP	   	sharpsl_io_base+0x08	/* column parity 5 - 0 bit */
#define ECCCNTR	 	sharpsl_io_base+0x0C	/* ECC byte counter */
#define ECCCLRR	 	sharpsl_io_base+0x10	/* cleare ECC */
#define FLASHIO	 	sharpsl_io_base+0x14	/* Flash I/O */
#define FLASHCTL	sharpsl_io_base+0x18	/* Flash Control */

/* Flash control bit */
#define FLRYBY		(1 << 5)
#define FLCE1		(1 << 4)
#define FLWP		(1 << 3)
#define FLALE		(1 << 2)
#define FLCLE		(1 << 1)
#define FLCE0		(1 << 0)


/*
 * MTD structure for SharpSL
 */
static struct mtd_info *sharpsl_mtd = NULL;

/*
 * Define partitions for flash device
 */
#define DEFAULT_NUM_PARTITIONS 3

#if defined CONFIG_MACH_POODLE
#define SHARPSL_ROOTFS_SIZE 22
#define SHARPSL_FLASH_SIZE 64
#elif defined CONFIG_MACH_CORGI 
#define SHARPSL_ROOTFS_SIZE 25
#define SHARPSL_FLASH_SIZE 32
#elif defined CONFIG_MACH_SHEPHERD
#define SHARPSL_ROOTFS_SIZE 25
#define SHARPSL_FLASH_SIZE 64
#elif defined CONFIG_MACH_HUSKY
#define SHARPSL_ROOTFS_SIZE 53
#define SHARPSL_FLASH_SIZE 128
#elif defined CONFIG_MACH_TOSA
#define SHARPSL_ROOTFS_SIZE 28
#define SHARPSL_FLASH_SIZE 64
#else
#define SHARPSL_ROOTFS_SIZE 30
#define SHARPSL_FLASH_SIZE 64
#endif

static int nr_partitions;
static struct mtd_partition sharpsl_nand_default_partition_info[] = {
	{
	.name = "NAND flash partition 0",
	.offset = 0,
	.size = 7 * 1024 * 1024,
	},
	
	{
	.name = "NAND flash partition 1",
	.offset = 7 * 1024 * 1024,
	.size = SHARPSL_ROOTFS_SIZE * 1024 * 1024,
	},
	{
	.name = "NAND flash partition 2",
	.offset = (SHARPSL_ROOTFS_SIZE+7) * 1024 * 1024,
	.size = (SHARPSL_FLASH_SIZE - SHARPSL_ROOTFS_SIZE - 7) * 1024 * 1024,
	},
};

/* 
 *	hardware specific access to control-lines
 */
static void
sharpsl_nand_hwcontrol(struct mtd_info* mtd, int cmd)
{
	switch (cmd) {
	case NAND_CTL_SETCLE: 
		writeb(readb(FLASHCTL) | FLCLE, FLASHCTL);
		break;
	case NAND_CTL_CLRCLE:
		writeb(readb(FLASHCTL) & ~FLCLE, FLASHCTL);
		break;

	case NAND_CTL_SETALE:
		writeb(readb(FLASHCTL) | FLALE, FLASHCTL);
		break;
	case NAND_CTL_CLRALE:
		writeb(readb(FLASHCTL) & ~FLALE, FLASHCTL);
		break;

	case NAND_CTL_SETNCE: 
		writeb(readb(FLASHCTL) & ~(FLCE0|FLCE1), FLASHCTL);
		break;
	case NAND_CTL_CLRNCE: 
		writeb(readb(FLASHCTL) | (FLCE0|FLCE1), FLASHCTL);
		break;
	}
}

static uint8_t scan_ff_pattern[] = { 0xff, 0xff };

static struct nand_bbt_descr sharpsl_bbt = {
	.options = 0,
	.offs = 4,
	.len = 2,
	.pattern = scan_ff_pattern
};

static int
sharpsl_nand_dev_ready(struct mtd_info* mtd)
{
	return !((readb(FLASHCTL) & FLRYBY) == 0);
}

static void
sharpsl_nand_enable_hwecc(struct mtd_info* mtd, int mode)
{
	writeb(0 ,ECCCLRR);
}

static int
sharpsl_nand_calculate_ecc(struct mtd_info* mtd, const u_char* dat,
				u_char* ecc_code)
{
	ecc_code[0] = ~readb(ECCLPUB);
	ecc_code[1] = ~readb(ECCLPLB);
	ecc_code[2] = (~readb(ECCCP) << 2) | 0x03;
	return readb(ECCCNTR) != 0;
}


#ifdef CONFIG_MTD_PARTITIONS
const char *part_probes[] = { "cmdlinepart", NULL };
#endif


/*
 * Main initialization routine
 */
int __init
sharpsl_nand_init(void)
{
	struct nand_chip *this;
	struct mtd_partition* sharpsl_partition_info;
	int err = 0;

	/* Allocate memory for MTD device structure and private data */
	sharpsl_mtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip),
				GFP_KERNEL);
	if (!sharpsl_mtd) {
		printk ("Unable to allocate SharpSL NAND MTD device structure.\n");
		return -ENOMEM;
	}
	
	/* map physical adress */
	sharpsl_io_base = ioremap(sharpsl_phys_base, 0x1000);
	if(!sharpsl_io_base){
		printk("ioremap to access Sharp SL NAND chip failed\n");
		kfree(sharpsl_mtd);
		return -EIO;
	}
	
	/* Get pointer to private data */
	this = (struct nand_chip *) (&sharpsl_mtd[1]);

	/* Initialize structures */
	memset((char *) sharpsl_mtd, 0, sizeof(struct mtd_info));
	memset((char *) this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	sharpsl_mtd->priv = this;

	/*
	 * PXA initialize
	 */
	writeb(readb(FLASHCTL) | FLWP, FLASHCTL);

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = FLASHIO;
	this->IO_ADDR_W = FLASHIO;
	/* Set address of hardware control function */
	this->hwcontrol = sharpsl_nand_hwcontrol;
	this->dev_ready = sharpsl_nand_dev_ready;
	/* 15 us command delay time */
	this->chip_delay = 15;
	/* set eccmode using hardware ECC */
	this->eccmode = NAND_ECC_HW3_256;
	this->enable_hwecc = sharpsl_nand_enable_hwecc;
	this->calculate_ecc = sharpsl_nand_calculate_ecc;
	this->correct_data = nand_correct_data;
	this->badblock_pattern = &sharpsl_bbt;

	/* Scan to find existence of the device */
	err=nand_scan(sharpsl_mtd,1);
	if (err) {
		iounmap(sharpsl_io_base);
		kfree(sharpsl_mtd);
		return err;
	}

	/* Register the partitions */
	sharpsl_mtd->name = "sharpsl-nand";
	nr_partitions = parse_mtd_partitions(sharpsl_mtd, part_probes,
						&sharpsl_partition_info, 0);
						 
	if (nr_partitions <= 0) {
		nr_partitions = DEFAULT_NUM_PARTITIONS;
		sharpsl_partition_info = sharpsl_nand_default_partition_info;
	}

#ifdef CONFIG_MACH_HUSKY
	/* Need to use small eraseblock size for backward compatibility */
	sharpsl_mtd->flags |= MTD_NO_VIRTBLOCKS;
#endif

	add_mtd_partitions(sharpsl_mtd, sharpsl_partition_info, nr_partitions);

	/* Return happy */
	return 0;
}
module_init(sharpsl_nand_init);

/*
 * Clean up routine
 */
#ifdef MODULE
static void __exit sharpsl_nand_cleanup(void)
{
	struct nand_chip *this = (struct nand_chip *) &sharpsl_mtd[1];

	/* Release resources, unregister device */
	nand_release(sharpsl_mtd);

	iounmap(sharpsl_io_base);

	/* Free the MTD device structure */
	kfree(sharpsl_mtd);
}
module_exit(sharpsl_nand_cleanup);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("Device specific logic for NAND flash on Sharp SL-C7xx Series");
