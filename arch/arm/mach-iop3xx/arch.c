/*
 * linux/arch/arm/mach-iop3xx/arch.c
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

#ifdef CONFIG_ARCH_IQ80331
extern void iq80331_map_io(void);
extern void iop331_init_irq(void);
extern void iop331_init_time(void);
#endif

#ifdef CONFIG_ARCH_IQ80331
static void __init
fixup_iop331(struct machine_desc *desc, struct tag *tags,
	      char **cmdline, struct meminfo *mi)
{
}
#endif

#if defined(CONFIG_ARCH_IQ80331)
MACHINE_START(IQ80331, "Intel IQ80331")
	MAINTAINER("Intel Corp.")
	BOOT_MEM(PHYS_OFFSET, 0xfff01000, 0xfffff000) // virtual, physical
//	BOOT_MEM(PHYS_OFFSET, IQ80331_UART0_VIRT, IQ80331_UART0_PHYS)
	MAPIO(iq80331_map_io)
	INITIRQ(iop331_init_irq)
	INITTIME(iop331_init_time)
	BOOT_PARAMS(0x0100)
MACHINE_END
#else
#error No machine descriptor defined for this IOP3xx implementation
#endif
