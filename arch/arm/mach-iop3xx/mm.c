/*
 * linux/arch/arm/mach-iop3xx/mm.c
 *
 * Low level memory initialization for IOP310 based systems
 *
 * Author: Nicolas Pitre <npitre@mvista.com>
 *
 * Copyright 2000-2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>

#ifdef CONFIG_IOP310_MU
#include "message.h"
#endif

/*
 * Standard IO mapping for all IOP310 based systems
 */
static struct map_desc iop80310_std_desc[] __initdata = {
 /* virtual     physical      length       type */
 // IOP310 Memory Mapped Registers
 { 0xe8001000,  0x00001000,   0x00001000,  MT_DEVICE },
 // PCI I/O Space
 { 0xfe000000,  0x90000000,   0x00020000,  MT_DEVICE }
};

void __init iop310_map_io(void)
{
	iotable_init(iop80310_std_desc, ARRAY_SIZE(iop80310_std_desc));
}

/*
 * IQ80310 specific IO mappings
 */
#ifdef CONFIG_ARCH_IQ80310
static struct map_desc iq80310_io_desc[] __initdata = {
 /* virtual     physical      length        type */
 // IQ80310 On-Board Devices
 { 0xfe800000,  0xfe800000,   0x00100000,   MT_DEVICE }
};

void __init iq80310_map_io(void)
{
#ifdef CONFIG_IOP310_MU
	/* acquiring 1MB of memory aligned on 1MB boundary for MU */
	mu_mem = __alloc_bootmem(0x100000, 0x100000, 0);
#endif

	iop310_map_io();

	iotable_init(iq80310_io_desc, ARRAY_SIZE(iq80310_io_desc));
}
#endif // CONFIG_ARCH_IQ80310

