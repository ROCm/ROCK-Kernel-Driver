/*
 *  linux/arch/mips/philips/nino/power.c
 *
 *  Copyright (C) 2000 Jim Pick <jim@jimpick.com>
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Power management routines on the Philips Nino.
 */
#include <asm/tx3912.h>

void nino_wait(void)
{
	/* We stop the CPU to conserve power */
	PowerControl |= PWR_STOPCPU;

	/* 
	 * We wait until an interrupt happens...
	 */

	/* We resume here */
	PowerControl &= ~PWR_STOPCPU;

	/* Give ourselves a little delay */
	__asm__ __volatile__(
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t");
}
