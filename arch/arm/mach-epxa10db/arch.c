/*
 *  linux/arch/arm/mach-epxa10db/arch.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/types.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

extern void epxa10db_map_io(void);
extern void epxa10db_init_irq(void);
extern void epxa10db_init_time(void);

MACHINE_START(CAMELOT, "Altera Epxa10db")
	MAINTAINER("Altera Corporation")
	BOOT_MEM(0x00000000, 0x7fffc000, 0xffffc000)
	MAPIO(epxa10db_map_io)
	INITIRQ(epxa10db_init_irq)
	INITTIME(epxa10db_init_time)
MACHINE_END

