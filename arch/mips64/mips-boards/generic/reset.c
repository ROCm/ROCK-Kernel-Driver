/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Reset the MIPS boards.
 *
 */
#include <linux/config.h>

#include <asm/mips-boards/generic.h>
#if defined(CONFIG_MIPS_ATLAS)
#include <asm/mips-boards/atlas.h>
#endif

void machine_restart(char *command) __attribute__((noreturn));
void machine_halt(void) __attribute__((noreturn));
#if defined(CONFIG_MIPS_ATLAS)
void machine_power_off(void) __attribute__((noreturn));
#endif

void machine_restart(char *command)
{
        volatile unsigned int *softres_reg = (void *)SOFTRES_REG;

	*softres_reg = GORESET;
}

void machine_halt(void)
{
        volatile unsigned int *softres_reg = (void *)SOFTRES_REG;

	*softres_reg = GORESET;
}

void machine_power_off(void)
{
#if defined(CONFIG_MIPS_ATLAS)
        volatile unsigned int *psustby_reg = (void *)ATLAS_PSUSTBY_REG;

	*psustby_reg = ATLAS_GOSTBY;
#else
	machine_halt();
#endif
}
