/*
 *  linux/arch/arm/mach-anakin/arch.c
 *
 *  Copyright (C) 2001 Aleph One Ltd. for Acunia N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   09-Apr-2001 W/TTC	Created
 */
#include <linux/tty.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

extern void anakin_map_io(void);
extern void genarch_init_irq(void);

MACHINE_START(ANAKIN, "Anakin")
	MAINTAINER("Wookey/Tak-Shing Chan")
	BOOT_MEM(0x20000000, 0x40000000, 0xe0000000)
	VIDEO(0x80000000, 0x8002db40)
	MAPIO(anakin_map_io)
	INITIRQ(genarch_init_irq)
MACHINE_END
