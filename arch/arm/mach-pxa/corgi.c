/*
 * Support for Sharp SL-C7xx PDAs
 * Models: SL-C700 (Corgi), SL-C750 (Shepherd), SL-C760 (Husky)
 *
 * Copyright (c) 2004-2005 Richard Purdie
 *
 * Based on Sharp's 2.4 kernel patches/lubbock.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/irq.h>
#include <asm/arch/corgi.h>

#include <asm/hardware/scoop.h>

#include "generic.h"

extern void corgi_ssp_lcdtg_send (u8 adrs, u8 data);

static void __init corgi_init_irq(void)
{
	pxa_init_irq();
}

static struct resource corgi_scoop_resources[] = {
	[0] = {
		.start		= 0x10800000,
		.end		= 0x10800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config corgi_scoop_setup = {
	.io_dir 	= CORGI_SCOOP_IO_DIR,
	.io_out		= CORGI_SCOOP_IO_OUT,
};

static struct platform_device corgiscoop_device = {
	.name		= "sharp-scoop",
	.id		= -1,
	.dev		= {
 		.platform_data	= &corgi_scoop_setup,
	},
	.num_resources	= ARRAY_SIZE(corgi_scoop_resources),
	.resource	= corgi_scoop_resources,
};

static struct platform_device corgissp_device = {
	.name		= "corgi-ssp",
	.id		= -1,
};

static struct platform_device *devices[] __initdata = {
	&corgiscoop_device,
	&corgissp_device,
};

static struct sharpsl_flash_param_info sharpsl_flash_param;

void corgi_get_param(void)
{
	sharpsl_flash_param.comadj_keyword = readl(FLASH_MEM_BASE + FLASH_COMADJ_MAGIC_ADR);
	sharpsl_flash_param.comadj = readl(FLASH_MEM_BASE + FLASH_COMADJ_DATA_ADR);

	sharpsl_flash_param.phad_keyword = readl(FLASH_MEM_BASE + FLASH_PHAD_MAGIC_ADR);
	sharpsl_flash_param.phadadj = readl(FLASH_MEM_BASE + FLASH_PHAD_DATA_ADR);
}

static void __init corgi_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init fixup_corgi(struct machine_desc *desc,
		struct tag *tags, char **cmdline, struct meminfo *mi)
{
	corgi_get_param();
	mi->nr_banks=1;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	if (machine_is_corgi())
		mi->bank[0].size = (32*1024*1024);
	else
		mi->bank[0].size = (64*1024*1024);
}

static struct map_desc corgi_io_desc[] __initdata = {
/*    virtual     physical    length      */
/*	{ 0xf1000000, 0x08000000, 0x01000000, MT_DEVICE },*/ /* LCDC (readable for Qt driver) */
/*	{ 0xef700000, 0x10800000, 0x00001000, MT_DEVICE },*/  /* SCOOP */
	{ 0xef800000, 0x00000000, 0x00800000, MT_DEVICE }, /* Boot Flash */
};

static void __init corgi_map_io(void)
{
	pxa_map_io();
	iotable_init(corgi_io_desc,ARRAY_SIZE(corgi_io_desc));

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x0158C000;
	PGSR1 = 0x00FF0080;
	PGSR2 = 0x0001C004;
	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;
}

#ifdef CONFIG_MACH_CORGI
MACHINE_START(CORGI, "SHARP Corgi")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	FIXUP(fixup_corgi)
	MAPIO(corgi_map_io)
	INITIRQ(corgi_init_irq)
	.init_machine = corgi_init,
	.timer = &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SHEPHERD
MACHINE_START(SHEPHERD, "SHARP Shepherd")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	FIXUP(fixup_corgi)
	MAPIO(corgi_map_io)
	INITIRQ(corgi_init_irq)
	.init_machine = corgi_init,
	.timer = &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_HUSKY
MACHINE_START(HUSKY, "SHARP Husky")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	FIXUP(fixup_corgi)
	MAPIO(corgi_map_io)
	INITIRQ(corgi_init_irq)
	.init_machine = corgi_init,
	.timer = &pxa_timer,
MACHINE_END
#endif

