/*
 *  linux/arch/arm/mach-integrator/impd1.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file provides the core support for the IM-PD1 module.
 *
 * Module / boot parameters.
 *   lmid=n   impd1.lmid=n - set the logic module position in stack to 'n'
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>

#include <asm/io.h>
#include <asm/hardware/icst525.h>
#include <asm/hardware/amba.h>
#include <asm/arch/lm.h>
#include <asm/arch/impd1.h>
#include <asm/sizes.h>

#include "clock.h"

static int module_id;

module_param_named(lmid, module_id, int, 0444);
MODULE_PARM_DESC(lmid, "logic module stack position");

struct impd1_module {
	void		*base;
	struct clk	vcos[2];
};

static const struct icst525_params impd1_vco_params = {
	.ref		= 24000,	/* 24 MHz */
	.vco_max	= 200000,	/* 200 MHz */
	.vd_min		= 12,
	.vd_max		= 519,
	.rd_min		= 3,
	.rd_max		= 120,
};

static void impd1_setvco(struct clk *clk, struct icst525_vco vco)
{
	struct impd1_module *impd1 = clk->data;
	int vconr = clk - impd1->vcos;
	u32 val;

	val = vco.v | (vco.r << 9) | (vco.s << 16);

	writel(0xa05f, impd1->base + IMPD1_LOCK);
	switch (vconr) {
	case 0:
		writel(val, impd1->base + IMPD1_OSC1);
		break;
	case 1:
		writel(val, impd1->base + IMPD1_OSC2);
		break;
	}
	writel(0, impd1->base + IMPD1_LOCK);

#if DEBUG
	vco.v = val & 0x1ff;
	vco.r = (val >> 9) & 0x7f;
	vco.s = (val >> 16) & 7;

	pr_debug("IM-PD1: VCO%d clock is %ld kHz\n",
		 vconr, icst525_khz(&impd1_vco_params, vco));
#endif
}

void impd1_tweak_control(struct device *dev, u32 mask, u32 val)
{
	struct impd1_module *impd1 = dev_get_drvdata(dev);
	u32 cur;

	val &= mask;
	cur = readl(impd1->base + IMPD1_CTRL) & ~mask;
	writel(cur | val, impd1->base + IMPD1_CTRL);
}

EXPORT_SYMBOL(impd1_tweak_control);

struct impd1_device {
	unsigned long	offset;
	unsigned int	irq[2];
	unsigned int	id;
};

static struct impd1_device impd1_devs[] = {
	{
		.offset	= 0x03000000,
		.id	= 0x00041190,
	}, {
		.offset	= 0x00100000,
		.irq	= { 1 },
		.id	= 0x00141011,
	}, {
		.offset	= 0x00200000,
		.irq	= { 2 },
		.id	= 0x00141011,
	}, {
		.offset	= 0x00300000,
		.irq	= { 3 },
		.id	= 0x00041022,
	}, {
		.offset	= 0x00400000,
		.irq	= { 4 },
		.id	= 0x00041061,
	}, {
		.offset	= 0x00500000,
		.irq	= { 5 },
		.id	= 0x00041061,
	}, {
		.offset	= 0x00600000,
		.irq	= { 6 },
		.id	= 0x00041130,
	}, {
		.offset	= 0x00700000,
		.irq	= { 7, 8 },
		.id	= 0x00041181,
	}, {
		.offset	= 0x00800000,
		.irq	= { 9 },
		.id	= 0x00041041,
	}, {
		.offset	= 0x01000000,
		.irq	= { 11 },
		.id	= 0x00041110,
	}
};

static const char *impd1_vconames[2] = {
	"CLCDCLK",
	"AUXVCO2",
};

static int impd1_probe(struct lm_device *dev)
{
	struct impd1_module *impd1;
	int i, ret;

	if (dev->id != module_id)
		return -EINVAL;

	if (!request_mem_region(dev->resource.start, SZ_4K, "LM registers"))
		return -EBUSY;

	impd1 = kmalloc(sizeof(struct impd1_module), GFP_KERNEL);
	if (!impd1) {
		ret = -ENOMEM;
		goto release_lm;
	}
	memset(impd1, 0, sizeof(struct impd1_module));

	impd1->base = ioremap(dev->resource.start, SZ_4K);
	if (!impd1->base) {
		ret = -ENOMEM;
		goto free_impd1;
	}

	lm_set_drvdata(dev, impd1);

	printk("IM-PD1 found at 0x%08lx\n", dev->resource.start);

	for (i = 0; i < ARRAY_SIZE(impd1->vcos); i++) {
		impd1->vcos[i].owner = THIS_MODULE,
		impd1->vcos[i].name = impd1_vconames[i],
		impd1->vcos[i].params = &impd1_vco_params,
		impd1->vcos[i].data = impd1,
		impd1->vcos[i].setvco = impd1_setvco;

		clk_register(&impd1->vcos[i]);
	}

	for (i = 0; i < ARRAY_SIZE(impd1_devs); i++) {
		struct impd1_device *idev = impd1_devs + i;
		struct amba_device *d;
		unsigned long pc_base;

		pc_base = dev->resource.start + idev->offset;

		d = kmalloc(sizeof(struct amba_device), GFP_KERNEL);
		if (!d)
			continue;

		memset(d, 0, sizeof(struct amba_device));

		snprintf(d->dev.bus_id, sizeof(d->dev.bus_id),
			 "lm%x:%5.5lx", dev->id, idev->offset >> 12);

		d->dev.parent	= &dev->dev;
		d->res.start	= dev->resource.start + idev->offset;
		d->res.end	= d->res.start + SZ_4K - 1;
		d->res.flags	= IORESOURCE_MEM;
		d->irq[0]	= dev->irq;
		d->irq[1]	= dev->irq;
		d->periphid	= idev->id;

		ret = amba_device_register(d, &dev->resource);
		if (ret) {
			printk("unable to register device %s: %d\n",
				d->dev.bus_id, ret);
			kfree(d);
		}
	}

	return 0;

 free_impd1:
	if (impd1 && impd1->base)
		iounmap(impd1->base);
	if (impd1)
		kfree(impd1);
 release_lm:
	release_mem_region(dev->resource.start, SZ_4K);
	return ret;
}

static void impd1_remove(struct lm_device *dev)
{
	struct impd1_module *impd1 = lm_get_drvdata(dev);
	struct list_head *l, *n;
	int i;

	list_for_each_safe(l, n, &dev->dev.children) {
		struct device *d = list_to_dev(l);

		device_unregister(d);
	}

	for (i = 0; i < ARRAY_SIZE(impd1->vcos); i++)
		clk_unregister(&impd1->vcos[i]);

	lm_set_drvdata(dev, NULL);

	iounmap(impd1->base);
	kfree(impd1);
	release_mem_region(dev->resource.start, SZ_4K);
}

static struct lm_driver impd1_driver = {
	.drv = {
		.name	= "impd1",
	},
	.probe		= impd1_probe,
	.remove		= impd1_remove,
};

static int __init impd1_init(void)
{
	return lm_driver_register(&impd1_driver);
}

static void __exit impd1_exit(void)
{
	lm_driver_unregister(&impd1_driver);
}

module_init(impd1_init);
module_exit(impd1_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Integrator/IM-PD1 logic module core driver");
MODULE_AUTHOR("Deep Blue Solutions Ltd");
