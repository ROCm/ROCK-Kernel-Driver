/*
 * linux/arch/arm/mach-iop3xx/mm.c
 *
 * Low level memory initialization for IOP321 based systems
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
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
 * Standard IO mapping for all IOP321 based systems
 */
static struct map_desc iop321_std_desc[] __initdata = {
 /* virtual     physical      length      type */

 /* mem mapped registers */
 { IOP321_VIRT_MEM_BASE,  IOP321_PHY_MEM_BASE,   0x00002000,  MT_DEVICE },

 /* PCI IO space */
 { 0xfe000000,  0x90000000,   0x00020000,  MT_DEVICE }
};

void __init iop321_map_io(void)
{
	iotable_init(iop321_std_desc, ARRAY_SIZE(iop321_std_desc));
}
