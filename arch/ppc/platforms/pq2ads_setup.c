/*
 * arch/ppc/platforms/pq2ads_setup.c
 *
 * PQ2ADS platform support
 *
 * Author: Kumar Gala <kumar.gala@motorola.com>
 * Derived from: est8260_setup.c by Allen Curtis
 *
 * Copyright 2004 Motorola Inc.
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

extern unsigned char __res[sizeof(bd_t)];

extern void m8260_init(unsigned long r3, unsigned long r4,
	unsigned long r5, unsigned long r6, unsigned long r7);

static int
pq2ads_show_cpuinfo(struct seq_file *m)
{
	bd_t	*binfo = (bd_t *)__res;

	seq_printf(m, "vendor\t\t: Motorola\n"
		      "machine\t\t: PQ2 ADS PowerPC\n"
		      "\n"
		      "mem size\t\t: 0x%08lx\n"
		      "console baud\t\t: %ld\n"
		      "\n",
		      binfo->bi_memsize,
		      binfo->bi_baudrate);
	return 0;
}

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
	ppc_md.show_cpuinfo	= pq2ads_show_cpuinfo;

	callback_setup_arch	= ppc_md.setup_arch;
	ppc_md.setup_arch	= pq2ads_setup_arch;
}
