/*
 *  drivers/mtd/spia.c
 *
 *  Copyright (C) 2000 Steven J. Hill (sjhill@cotw.com)
 *
 * $Id: spia.c,v 1.9 2001/06/02 14:47:16 dwmw2 Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   SPIA board which utilizes the Toshiba TC58V64AFT part. This is
 *   a 64Mibit (8MiB x 8 bits) NAND flash device.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>

/*
 * MTD structure for SPIA board
 */
static struct mtd_info *spia_mtd = NULL;

/*
 * Module stuff
 */
#if LINUX_VERSION_CODE < 0x20212 && defined(MODULE)
  #define spia_init init_module
  #define spia_cleanup cleanup_module
#endif

/*
 * Define partitions for flash device
 */
const static struct mtd_partition partition_info[] = {
	{ name: "SPIA flash partition 1",
	  offset: 0,
	  size: 2*1024*1024 },
	{ name: "SPIA flash partition 2",
	  offset: 2*1024*1024,
	  size: 6*1024*1024 }
};
#define NUM_PARTITIONS 2

/*
 * Main initialization routine
 */
int __init spia_init (void)
{
	struct nand_chip *this;

	/* Allocate memory for MTD device structure and private data */
	spia_mtd = kmalloc (sizeof(struct mtd_info) + sizeof (struct nand_chip),
				GFP_KERNEL);
	if (!spia_mtd) {
		printk ("Unable to allocate SPIA NAND MTD device structure.\n");
		return -ENOMEM;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *) (&spia_mtd[1]);

	/* Initialize structures */
	memset((char *) spia_mtd, 0, sizeof(struct mtd_info));
	memset((char *) this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	spia_mtd->priv = this;

	/*
	 * Set GPIO Port E control register so that the pins are configured
	 * to be outputs for controlling the NAND flash.
	 */
	(*(volatile unsigned char *) (IO_BASE + PEDDR)) = 0x07;

	/* Set address of NAND IO lines */
	this->IO_ADDR = FIO_BASE;
	this->CTRL_ADDR = IO_BASE + PEDR;
	this->CLE = 0x01;
	this->ALE = 0x02;
	this->NCE = 0x04;

	/* Scan to find existance of the device */
	if (nand_scan (spia_mtd)) {
		kfree (spia_mtd);
		return -ENXIO;
	}

	/* Allocate memory for internal data buffer */
	this->data_buf = kmalloc (sizeof(u_char) * (spia_mtd->oobblock + spia_mtd->oobsize), GFP_KERNEL);
	if (!this->data_buf) {
		printk ("Unable to allocate NAND data buffer for SPIA.\n");
		kfree (spia_mtd);
		return -ENOMEM;
	}

	/* Register the partitions */
	add_mtd_partitions(spia_mtd, partition_info, NUM_PARTITIONS);

	/* Return happy */
	return 0;
}
module_init(spia_init);

/*
 * Clean up routine
 */
#ifdef MODULE
static void __exit spia_cleanup (void)
{
	struct nand_chip *this = (struct nand_chip *) &spia_mtd[1];

	/* Unregister the device */
	del_mtd_device (spia_mtd);

	/* Free internal data buffer */
	kfree (this->data_buf);

	/* Free the MTD device structure */
	kfree (spia_mtd);
}
module_exit(spia_cleanup);
#endif
