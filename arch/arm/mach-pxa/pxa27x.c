/*
 *  linux/arch/arm/mach-pxa/pxa27x.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Nov 05, 2002
 *  Copyright:	MontaVista Software Inc.
 *
 * Code specific to PXA27x aka Bulverde.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm.h>

#include <asm/hardware.h>

#include "generic.h"

/* Crystal clock : 13-MHZ*/
#define BASE_CLK	13000000

/*
 * Get the clock frequency as reflected by CCSR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 *
 * For more details, refer to Bulverde Manual, section 3.8.2.1
 */
unsigned int get_clk_frequency_khz( int info)
{
	unsigned long ccsr, turbo, b, ht;
	unsigned int l, L, m, M, n2, N, S, cccra;

	ccsr = CCSR;
	cccra = CCCR & (0x1 << 25);

	/* Read clkcfg register: it has turbo, b, half-turbo (and f) */
	asm( "mrc\tp14, 0, %0, c6, c0, 0" : "=r" (turbo) );
	b = (turbo & (0x1 << 3));
	ht = (turbo & (0x1 << 2));

	l  = ccsr & 0x1f;
	n2 = (ccsr>>7) & 0xf;
	if (l == 31) {
		/* The calculation from the Yellow Book is incorrect:
		   it says M=4 for L=21-30 (which is easy to calculate
		   by subtracting 1 and then dividing by 10, but not
		   with 31, so we'll do it manually */
		m = 1 << 2;
	} else {
		m = 1 << ((l-1)/10);
	}

	L = l * BASE_CLK;
	N = (n2 * L) / 2;
	S = (b) ? L : (L/2);
	if (cccra == 0)
		M = L/m;
	else
		M = (b) ? L : (L/2);

	if (info) {
		printk( KERN_INFO "Run Mode clock: %d.%02dMHz (*%d)\n",
			L / 1000000, (L % 1000000) / 10000, l );
		printk( KERN_INFO "Memory clock: %d.%02dMHz (/%d)\n",
			M / 1000000, (M % 1000000) / 10000, m );
		printk( KERN_INFO "Turbo Mode clock: %d.%02dMHz (*%d.%d, %sactive)\n",
			N / 1000000, (N % 1000000)/10000, n2 / 2, (n2 % 2)*5,
			(turbo & 1) ? "" : "in" );
		printk( KERN_INFO "System bus clock: %d.%02dMHz \n",
			S / 1000000, (S % 1000000) / 10000 );
	}

	return (turbo & 1) ? (N/1000) : (L/1000);
}

/*
 * Return the current mem clock frequency in units of 10kHz as
 * reflected by CCCR[A], B, and L
 */
unsigned int get_lclk_frequency_10khz(void)
{
	unsigned long ccsr, clkcfg, b;
	unsigned int l, L, m, M, cccra;

	cccra = CCCR & (0x1 << 25);

	/* Read clkcfg register to obtain b */
	asm( "mrc\tp14, 0, %0, c6, c0, 0" : "=r" (clkcfg) );
	b = (clkcfg & (0x1 << 3));

	ccsr = CCSR;
	l  =  ccsr & 0x1f;
	if (l == 31) {
		/* The calculation from the Yellow Book is incorrect:
		   it says M=4 for L=21-30 (which is easy to calculate
		   by subtracting 1 and then dividing by 10, but not
		   with 31, so we'll do it manually */
		m = 1 << 2;
	} else {
		m = 1 << ((l-1)/10);
	}

	L = l * BASE_CLK;
	if (cccra == 0)
		M = L/m;
	else
		M = (b) ? L : L/2;

	return (M / 10000);
}

EXPORT_SYMBOL(get_clk_frequency_khz);
EXPORT_SYMBOL(get_lclk_frequency_10khz);

