/*
 *  linux/arch/arm/mach-integrator/arch.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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
#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

extern void integrator_map_io(void);
extern void integrator_init_irq(void);

MACHINE_START(INTEGRATOR, "ARM-Integrator")
	MAINTAINER("ARM Ltd/Deep Blue Solutions Ltd")
	BOOT_MEM(0x00000000, 0x16000000, 0xf1600000)
	BOOT_PARAMS(0x00000100)
	MAPIO(integrator_map_io)
	INITIRQ(integrator_init_irq)
MACHINE_END
