/*
 * linux/include/asm-arm/arch-nexuspci/system.h
 *
 * Copyright (c) 1996, 97, 98, 99, 2000 FutureTV Labs Ltd.
 */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

static void arch_idle(void)
{
	while (!current->need_resched && !hlt_counter)
		cpu_do_idle(IDLE_WAIT_SLOW);
}

#define arch_reset(mode)	do { } while (0)

#endif
