/*
 * system.c - a driver for reserving pnp system resources
 *
 * Some code is based on pnpbios_core.c
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/pnp.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/ioport.h>

static const struct pnp_id pnp_card_table[] = {
	{	"ANYDEVS",		0	},
	{	"",			0	}
};

static const struct pnp_id pnp_dev_table[] = {
	/* General ID for reserving resources */
	{	"PNP0c02",		0	},
	/* memory controller */
	{	"PNP0c01",		0	},
	{	"",			0	}
};

static void __init reserve_ioport_range(char *pnpid, int start, int end)
{
	struct resource *res;
	char *regionid;

	regionid = kmalloc(16, GFP_KERNEL);
	if ( regionid == NULL )
		return;
	snprintf(regionid, 16, "pnp %s", pnpid);
	res = request_region(start,end-start+1,regionid);
	if ( res == NULL )
		kfree( regionid );
	else
		res->flags &= ~IORESOURCE_BUSY;
	/*
	 * Failures at this point are usually harmless. pci quirks for
	 * example do reserve stuff they know about too, so we may well
	 * have double reservations.
	 */
	printk(KERN_INFO
		"pnp: %s: ioport range 0x%x-0x%x %s reserved\n",
		pnpid, start, end,
		NULL != res ? "has been" : "could not be"
	);

	return;
}

static void __init reserve_resources_of_dev( struct pnp_dev *dev )
{
	int i;

	for (i=0;i<DEVICE_COUNT_RESOURCE;i++) {
		if ( dev->resource[i].flags & IORESOURCE_UNSET )
			/* end of resources */
			break;
		if (dev->resource[i].flags & IORESOURCE_IO) {
			/* ioport */
			if ( dev->resource[i].start == 0 )
				/* disabled */
				/* Do nothing */
				continue;
			if ( dev->resource[i].start < 0x100 )
				/*
				 * Below 0x100 is only standard PC hardware
				 * (pics, kbd, timer, dma, ...)
				 * We should not get resource conflicts there,
				 * and the kernel reserves these anyway
				 * (see arch/i386/kernel/setup.c).
				 * So, do nothing
				 */
				continue;
			if ( dev->resource[i].end < dev->resource[i].start )
				/* invalid endpoint */
				/* Do nothing */
				continue;
			reserve_ioport_range(
				dev->dev.bus_id,
				dev->resource[i].start,
				dev->resource[i].end
			);
		} else if (dev->resource[i].flags & IORESOURCE_MEM) {
			/* iomem */
			/* For now do nothing */
			continue;
		} else {
			/* Neither ioport nor iomem */
			/* Do nothing */
			continue;
		}
	}

	return;
}

static int system_pnp_probe(struct pnp_dev * dev, const struct pnp_id *card_id, const struct pnp_id *dev_id)
{
	reserve_resources_of_dev(dev);
	return 0;
}

static struct pnp_driver system_pnp_driver = {
	.name		= "system",
	.card_id_table	= pnp_card_table,
	.id_table	= pnp_dev_table,
	.probe		= system_pnp_probe,
	.remove		= NULL,
};

static int __init pnp_system_init(void)
{
	return pnp_register_driver(&system_pnp_driver);
}

core_initcall(pnp_system_init);
