/*
 *  linux/include/asm-arm/arch-arc/system.h
 *
 *  Copyright (C) 1996-1999 Russell King and Dave Gilbert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

static void arch_idle(void)
{
	while (!current->need_resched && !hlt_counter);
}

extern __inline__ void arch_reset(char mode)
{
	extern void ecard_reset(int card);

	/*
	 * Reset all expansion cards.
	 */
	ecard_reset(-1);

	/*
	 * copy branch instruction to reset location and call it
	 */
	*(unsigned long *)0 = *(unsigned long *)0x03800000;
	((void(*)(void))0)();
}
