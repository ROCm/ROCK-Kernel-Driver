/*
 * arch/ppc/platforms/rpx8260.c
 *
 * RPC EP8260 platform support
 *
 * Author: Dan Malek <dan@embeddededge.com>
 * Derived from: pq2ads_setup.c by Kumar
 *
 * Copyright 2004 Embedded Edge, LLC
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
ep8260_show_cpuinfo(struct seq_file *m)
{
	bd_t	*binfo = (bd_t *)__res;

	seq_printf(m, "vendor\t\t: RPC\n"
		      "machine\t\t: EP8260 PPC\n"
		      "\n"
		      "mem size\t\t: 0x%08x\n"
		      "console baud\t\t: %d\n"
		      "\n",
		      binfo->bi_memsize,
		      binfo->bi_baudrate);
	return 0;
}

static void __init
ep8260_setup_arch(void)
{
	printk("RPC EP8260 Port\n");
	callback_setup_arch();
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	/* Generic 8260 platform initialization */
	m8260_init(r3, r4, r5, r6, r7);

	/* Anything special for this platform */
	ppc_md.show_cpuinfo	= ep8260_show_cpuinfo;

	callback_setup_arch	= ppc_md.setup_arch;
	ppc_md.setup_arch	= ep8260_setup_arch;
}
