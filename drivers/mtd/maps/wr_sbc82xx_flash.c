/*
 * $Id: wr_sbc82xx_flash.c,v 1.1 2004/06/07 10:21:32 dwmw2 Exp $
 *
 * Map for flash chips on Wind River PowerQUICC II SBC82xx board.
 *
 * Copyright (C) 2004 Red Hat, Inc.
 *
 * Author: David Woodhouse <dwmw2@infradead.org>
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/config.h>
#include <linux/mtd/partitions.h>

#include <asm/immap_cpm2.h>

static struct mtd_info *sbcmtd[3];
static struct mtd_partition *sbcmtd_parts[3];

struct map_info sbc82xx_flash_map[3] = {
	{.name = "Boot flash"},
	{.name = "Alternate boot flash"},
	{.name = "User flash"}
};

static struct mtd_partition smallflash_parts[] = {
	{
		.name =		"space",
		.size =		0x100000,
		.offset =	0,
	}, {
		.name =		"bootloader",
		.size =		MTDPART_SIZ_FULL,
		.offset =	MTDPART_OFS_APPEND,
	}
};

static struct mtd_partition bigflash_parts[] = {
	{
		.name =		"bootloader",
		.size =		0x80000,
		.offset =	0,
	}, {
		.name =		"file system",
		.size =		MTDPART_SIZ_FULL,
		.offset =	MTDPART_OFS_APPEND,
	}
};

static const char *part_probes[] __initdata = {"cmdlinepart", "RedBoot", NULL};

int __init init_sbc82xx_flash(void)
{
	volatile  memctl_cpm2_t *mc = &cpm2_immr->im_memctl;
	int bigflash;
	int i;

	/* First, register the boot flash, whichever we're booting from */
	if ((mc->memc_br0 & 0x00001800) == 0x00001800) {
		bigflash = 0;
	} else if ((mc->memc_br0 & 0x00001800) == 0x00000800) {
		bigflash = 1;
	} else {
		printk(KERN_WARNING "Bus Controller register BR0 is %08x. Cannot determine flash configuration\n", mc->memc_br0);
		return 1;
	}

	/* Set parameters for the big flash chip (CS6 or CS0) */
	sbc82xx_flash_map[bigflash].buswidth = 4;
	sbc82xx_flash_map[bigflash].size = 0x4000000;

	/* Set parameters for the small flash chip (CS0 or CS6) */
	sbc82xx_flash_map[!bigflash].buswidth = 1;
	sbc82xx_flash_map[!bigflash].size = 0x200000;

	/* Set parameters for the user flash chip (CS1) */
	sbc82xx_flash_map[2].buswidth = 4;
	sbc82xx_flash_map[2].size = 0x4000000;

	sbc82xx_flash_map[0].phys = mc->memc_br0 & 0xffff8000;
	sbc82xx_flash_map[1].phys = mc->memc_br6 & 0xffff8000;
	sbc82xx_flash_map[2].phys = mc->memc_br1 & 0xffff8000;

	for (i=0; i<3; i++) {
		int8_t flashcs[3] = { 0, 6, 1 };
		int nr_parts;

		printk(KERN_NOTICE "PowerQUICC II %s (%ld MiB on CS%d",
		       sbc82xx_flash_map[i].name, sbc82xx_flash_map[i].size >> 20, flashcs[i]);
		if (!sbc82xx_flash_map[i].phys) {
			/* We know it can't be at zero. */
			printk("): disabled by bootloader.\n");
			continue;
		}
		printk(" at %08lx)\n",  sbc82xx_flash_map[i].phys);

		sbc82xx_flash_map[i].virt = (unsigned long)ioremap(sbc82xx_flash_map[i].phys, sbc82xx_flash_map[i].size);

		if (!sbc82xx_flash_map[i].virt) {
			printk("Failed to ioremap\n");
			continue;
		}

		simple_map_init(&sbc82xx_flash_map[i]);

		sbcmtd[i] = do_map_probe("cfi_probe", &sbc82xx_flash_map[i]);

		if (!sbcmtd[i])
			continue;

		sbcmtd[i]->owner = THIS_MODULE;

		nr_parts = parse_mtd_partitions(sbcmtd[i], part_probes,
						&sbcmtd_parts[i], 0);
		if (nr_parts > 0) {
			add_mtd_partitions (sbcmtd[i], sbcmtd_parts[i], nr_parts);
			continue;
		}

		/* No partitioning detected. Use default */
		if (i == 2) {
			add_mtd_device(sbcmtd[i]);
		} else if (i == bigflash) {
			add_mtd_partitions (sbcmtd[i], bigflash_parts, ARRAY_SIZE(bigflash_parts));
		} else {
			add_mtd_partitions (sbcmtd[i], smallflash_parts, ARRAY_SIZE(smallflash_parts));
		}
	}
	return 0;
}

static void __exit cleanup_sbc82xx_flash(void)
{
	int i;

	for (i=0; i<3; i++) {
		if (!sbcmtd[i])
			continue;

		if (i<2 || sbcmtd_parts[i])
			del_mtd_partitions(sbcmtd[i]);
		else
			del_mtd_device(sbcmtd[i]);
			
		kfree(sbcmtd_parts[i]);
		map_destroy(sbcmtd[i]);
		
		iounmap((void *)sbc82xx_flash_map[i].virt);
		sbc82xx_flash_map[i].virt = 0;
	}
}

module_init(init_sbc82xx_flash);
module_exit(cleanup_sbc82xx_flash);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Flash map driver for WindRiver PowerQUICC II");
