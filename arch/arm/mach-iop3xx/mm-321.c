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
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>


/*
 * Standard IO mapping for all IOP321 based systems
 */
static struct map_desc iop80321_std_desc[] __initdata = {
 /* virtual     physical      length      type */

 /* mem mapped registers */
 { IOP321_VIRT_MEM_BASE,  IOP321_PHY_MEM_BASE,   0x00002000,  MT_DEVICE },

 /* PCI IO space */
 { 0xfe000000,  0x90000000,   0x00020000,  MT_DEVICE }
};

void __init iop321_map_io(void)
{
	iotable_init(iop80321_std_desc, ARRAY_SIZE(iop80321_std_desc));
}

/*
 * IQ80321 specific IO mappings
 *
 * We use RedBoot's setup for the onboard devices.
 */
#ifdef CONFIG_ARCH_IQ80321
static struct map_desc iq80321_io_desc[] __initdata = {
 /* virtual     physical      length        type */

 /* on-board devices */
 { 0xfe800000,  IQ80321_UART1,   0x00100000,   MT_DEVICE }
};

void __init iq80321_map_io(void)
{
	iop321_map_io();

	iotable_init(iq80321_io_desc, ARRAY_SIZE(iq80321_io_desc));
}
#endif // CONFIG_ARCH_IQ80321
