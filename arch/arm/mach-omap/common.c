/*
 * linux/arch/arm/mach-omap/common.c
 *
 * Code common to all OMAP machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>

#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <asm/arch/clocks.h>
#include <asm/io.h>

#include "common.h"

/*
 * Common OMAP I/O mapping
 *
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */

static struct map_desc standard_io_desc[] __initdata = {
 { IO_BASE,          IO_START,          IO_SIZE,          MT_DEVICE },
 { OMAP_DSP_BASE,    OMAP_DSP_START,    OMAP_DSP_SIZE,    MT_DEVICE },
 { OMAP_DSPREG_BASE, OMAP_DSPREG_START, OMAP_DSPREG_SIZE, MT_DEVICE },
 { OMAP_SRAM_BASE,   OMAP_SRAM_START,   OMAP_SRAM_SIZE,   MT_DEVICE }
};

static int initialized = 0;

static void __init _omap_map_io(void)
{
	initialized = 1;

	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));

	/* REVISIT: Refer to OMAP5910 Errata, Advisory SYS_1: "Timeout Abort
	 * on a Posted Write in the TIPB Bridge".
	 */
	__raw_writew(0x0, MPU_PUBLIC_TIPB_CNTL_REG);
	__raw_writew(0x0, MPU_PRIVATE_TIPB_CNTL_REG);

	/* Must init clocks early to assure that timer interrupt works
	 */
	init_ck();
}

/*
 * This should only get called from board specific init
 */
void omap_map_io(void)
{
	if (!initialized)
		_omap_map_io();
}

EXPORT_SYMBOL(omap_map_io);

