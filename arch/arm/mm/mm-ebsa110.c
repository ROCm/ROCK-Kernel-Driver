/*
 *  linux/arch/arm/mm/mm-ebsa110.c
 *
 *  Copyright (C) 1998-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Extra MM routines for the EBSA-110 architecture
 */
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
 
static struct map_desc ebsa110_io_desc[] __initdata = {
	{ IO_BASE - PGDIR_SIZE, 0xc0000000, PGDIR_SIZE, DOMAIN_IO, 0, 1, 0, 0 },
	{ IO_BASE             , IO_START  , IO_SIZE   , DOMAIN_IO, 0, 1, 0, 0 },
	LAST_DESC
};

void __init ebsa110_map_io(void)
{
	iotable_init(ebsa110_io_desc);
}
