/*
 * include/asm-arm/arch-ixp4x//system.h 
 *
 * Copyright (C) 2002 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <asm/hardware.h>

static inline void arch_idle(void)
{
#if 0
	if (!hlt_counter)
		cpu_do_idle(0);
#endif
}


static inline void arch_reset(char mode)
{
	if ( 1 && mode == 's') {
		/* Jump into ROM at address 0 */
		cpu_reset(0);
	} else {
		/* Use on-chip reset capability */

		/* set the "key" register to enable access to
		 * "timer" and "enable" registers
		 */
		*IXP4XX_OSWK = 0x482e; 	    

		/* write 0 to the timer register for an immidiate reset */
		*IXP4XX_OSWT = 0;

		/* disable watchdog interrupt, enable reset, enable count */
		*IXP4XX_OSWE = 0x3;
	}
}

