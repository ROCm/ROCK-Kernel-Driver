/*
 * arch/ppc/platforms/pq2ads.c
 *
 * PQ2ADS platform support
 *
 * Author: Kumar Gala <kumar.gala@freescale.com>
 * Derived from: est8260_setup.c by Allen Curtis
 *
 * Copyright 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/seq_file.h>

#include <asm/mpc8260.h>
#include <asm/machdep.h>

static void (*callback_setup_arch)(void);

extern void m8260_init(unsigned long r3, unsigned long r4,
	unsigned long r5, unsigned long r6, unsigned long r7);

static void __init
pq2ads_setup_arch(void)
{
	printk("PQ2 ADS Port\n");
	callback_setup_arch();
        *(volatile uint *)(BCSR_ADDR + 4) &= ~BCSR1_RS232_EN2;
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	/* Generic 8260 platform initialization */
	m8260_init(r3, r4, r5, r6, r7);

	/* Anything special for this platform */
	callback_setup_arch	= ppc_md.setup_arch;
	ppc_md.setup_arch	= pq2ads_setup_arch;
}
