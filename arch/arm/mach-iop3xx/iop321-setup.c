/*
 * linux/arch/arm/mach-iop3xx/iop321-setup.c
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

#ifdef CONFIG_ARCH_IQ31244
extern void iq31244_map_io(void);
extern void iop321_init_irq(void);
extern void iop321_init_time(void);
#endif

static void __init
fixup_iop321(struct machine_desc *desc, struct tag *tags,
	      char **cmdline, struct meminfo *mi)
{
}

#if defined(CONFIG_ARCH_IQ80321)
MACHINE_START(IQ80321, "Intel IQ80321")
	MAINTAINER("Intel Corporation")
	BOOT_MEM(PHYS_OFFSET, IQ80321_UART, 0xfe800000)
	FIXUP(fixup_iop321)
	MAPIO(iq80321_map_io)
	INITIRQ(iop321_init_irq)
	INITTIME(iop321_init_time)
    BOOT_PARAMS(0xa0000100)
MACHINE_END
#elif defined(CONFIG_ARCH_IQ31244)
    MACHINE_START(IQ31244, "Intel IQ31244")
    MAINTAINER("Intel Corp.")
    BOOT_MEM(PHYS_OFFSET, IQ31244_UART, IQ31244_UART)
    MAPIO(iq31244_map_io)
    INITIRQ(iop321_init_irq)
	INITTIME(iop321_init_time)
    BOOT_PARAMS(0xa0000100)
MACHINE_END
#else
#error No machine descriptor defined for this IOP3XX implementation
#endif
