/*
 * linux/arch/arm/mach-iop310/arch.c
 *
 * Author: Nicolas Pitre <nico@cam.org>
 * Copyright (C) 2001 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#ifdef CONFIG_ARCH_IQ80310
extern void iq80310_map_io(void);
extern void iq80310_init_irq(void);

static void __init
fixup_iq80310(struct machine_desc *desc, struct tag *tags,
	      char **cmdline, struct meminfo *mi)
{
	system_rev = (*(volatile unsigned int*)0xfe830000) & 0x0f;

	if (system_rev)
		system_rev = 0xF;
}

MACHINE_START(IQ80310, "Cyclone IQ80310")
	MAINTAINER("MontaVista Software Inc.")
	BOOT_MEM(0xa0000000, 0xfe000000, 0xfe000000)
	FIXUP(fixup_iq80310)
	MAPIO(iq80310_map_io)
	INITIRQ(iq80310_init_irq)
MACHINE_END

#else
#error No machine descriptor defined for this IOP310 implementation
#endif
