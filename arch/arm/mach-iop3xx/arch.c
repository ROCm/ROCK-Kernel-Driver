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

#ifdef CONFIG_ARCH_IQ80321
extern void iq80321_map_io(void);
extern void iop321_init_irq(void);
extern void iop321_init_time(void);
#endif

#ifdef CONFIG_ARCH_IQ80321
static void __init
fixup_iop321(struct machine_desc *desc, struct tag *tags,
	      char **cmdline, struct meminfo *mi)
{
}
#endif

#if defined(CONFIG_ARCH_IQ80321)
MACHINE_START(IQ80321, "Intel IQ80321")
	MAINTAINER("Intel Corporation")
	BOOT_MEM(PHYS_OFFSET, IQ80321_UART1, 0xfe800000)
	FIXUP(fixup_iop321)
	MAPIO(iq80321_map_io)
	INITIRQ(iop321_init_irq)
	INITTIME(iop321_init_time)
MACHINE_END

#else
#error No machine descriptor defined for this IOP3xx implementation
#endif
