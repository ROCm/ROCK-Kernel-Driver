/*
 *  linux/arch/arm26/mach-arc/arch.c
 *
 *  Copyright (C) 1998-2001 Russell King
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Architecture specific fixups.
 */
#include <linux/config.h>
#include <linux/tty.h>
#include <linux/init.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/setup.h>

#include <asm/map.h>
#include <asm/arch.h>

extern void arc_init_irq(void);

#ifdef CONFIG_ARCH_ARC
MACHINE_START(ARCHIMEDES, "Acorn-Archimedes")
#elif defined(CONFIG_ARCH_A5K)
MACHINE_START(A5K, "Acorn-A5000")
#endif
	MAINTAINER("Ian Molton")
	BOOT_PARAMS(0x0207c000)
	INITIRQ(arc_init_irq)
MACHINE_END

