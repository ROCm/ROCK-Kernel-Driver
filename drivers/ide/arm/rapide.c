/*
 * linux/drivers/ide/arm/rapide.c
 *
 * Copyright (c) 1996-2002 Russell King.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/ecard.h>

static int __devinit
rapide_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	unsigned long port = ecard_address (ec, ECARD_MEMC, 0);
	hw_regs_t hw;
	int i, ret;

	memset(&hw, 0, sizeof(hw));

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw.io_ports[i] = port;
		port += 1 << 4;
	}
	hw.io_ports[IDE_CONTROL_OFFSET] = port + 0x206;
	hw.irq = ec->irq;

	ret = ide_register_hw(&hw, NULL);

	if (ret)
		ecard_release(ec);
	return ret;
}

static void __devexit rapide_remove(struct expansion_card *ec)
{
	/* need to do more */
}

static struct ecard_id rapide_ids[] = {
	{ MANU_YELLOWSTONE, PROD_YELLOWSTONE_RAPIDE32 },
	{ 0xffff, 0xffff }
};

static struct ecard_driver rapide_driver = {
	.probe		= rapide_probe,
	.remove		= __devexit_p(rapide_remove),
	.id_table	= rapide_ids,
	.drv = {
		.name	= "rapide",
	},
};

static int __init rapide_init(void)
{
	return ecard_register_driver(&rapide_driver);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Yellowstone RAPIDE driver");

module_init(rapide_init);
