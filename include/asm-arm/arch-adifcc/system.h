/*
 * linux/include/asm-arm/arch-adifcc/system.h
 *
 * Copyright (C) 2001 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

static inline void arch_idle(void)
{
#if 0
	cpu_do_idle();
#endif
}


static inline void arch_reset(char mode)
{
	if ( 1 && mode == 's') {
		/* Jump into ROM at address 0 */
		cpu_reset(0);
	} else {
		/* Use on-chip reset capability */
	}
}

