/*
 * linux/arch/arm/mach-iop3xx/mm.c
 *
 * Low level memory initialization for IOP331 based systems
 *
 * Author: Dave Jiang <dave.jiang@intel.com>
 * Copyright (C) 2003 Intel Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/mm.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
#include <asm/mach-types.h>


/*
 * Standard IO mapping for all IOP331 based systems
 */
static struct map_desc iop331_std_desc[] __initdata = {
 /* virtual     physical      length      type */

 /* mem mapped registers */
 { IOP331_VIRT_MEM_BASE,  IOP331_PHYS_MEM_BASE,   0x00002000,  MT_DEVICE },

 /* PCI IO space */
 { 0xfe000000,  0x90000000,   0x00020000,  MT_DEVICE }
};

void __init iop331_map_io(void)
{
	iotable_init(iop331_std_desc, ARRAY_SIZE(iop331_std_desc));
}
