/*
 *  linux/arch/arm/mach-adifcc/arch.c
 *
 *  Copyright (C) 2001 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

extern void adifcc_map_io(void);
extern void adifcc_init_irq(void);

static void __init
fixup_adifcc(struct machine_desc *desc, struct tag *tags,
	      char **cmdline, struct meminfo *mi)
{
#ifdef CONFIG_ARCH_ADI_EVB
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size  = (32*1024*1024);
	mi->bank[0].node  = 0;
	mi->nr_banks      = 1;

	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( 0xc0800000, 3*1024*1024 );
	ROOT_DEV = mk_kdev(RAMDISK_MAJOR,0);
#endif
}

#ifdef CONFIG_ARCH_ADI_EVB
MACHINE_START(ADI_EVB, "ADI 80200FCC Evaluation Board")
	MAINTAINER("MontaVista Software Inc.")
	BOOT_MEM(0xc0000000, 0x00400000, 0xff400000)
	FIXUP(fixup_adifcc)
	MAPIO(adifcc_map_io)
	INITIRQ(adifcc_init_irq)
MACHINE_END
#endif

