/*
 * arch/ppc/platforms/tqm8260_setup.c
 *
 * TQM8260 platform support
 *
 * Author: Allen Curtis <acurtis@onz.com>
 * Derived from: m8260_setup.c by Dan Malek, MVista
 *
 * Copyright 2002 Ones and Zeros, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/seq_file.h>

#include <asm/immap_cpm2.h>
#include <asm/mpc8260.h>
#include <asm/machdep.h>

static void (*callback_setup_arch)(void);

extern unsigned char __res[sizeof(bd_t)];

extern void m8260_init(unsigned long r3, unsigned long r4,
	unsigned long r5, unsigned long r6, unsigned long r7);

static int
tqm8260_show_cpuinfo(struct seq_file *m)
{
	bd_t	*binfo = (bd_t *)__res;

	seq_printf(m, "vendor\t\t: IN2 Systems\n"
		      "machine\t\t: TQM8260 PowerPC\n"
		      "mem size\t\t: 0x%08x\n"
		      "\n",
		      binfo->bi_memsize);
	return 0;
}

static int
tqm8260_set_rtc_time(unsigned long time)
{
	((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcnt = time;
	((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcntsc = 0x3;

	return(0);
}

static unsigned long
tqm8260_get_rtc_time(void)
{
	return ((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcnt;
}

static void __init
tqm8260_setup_arch(void)
{
	printk("IN2 Systems TQM8260 port\n");
	callback_setup_arch();
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	/* Generic 8260 platform initialization */
	m8260_init(r3, r4, r5, r6, r7);

	/* Anything special for this platform */
	ppc_md.show_cpuinfo	= tqm8260_show_cpuinfo;
	ppc_md.set_rtc_time	= tqm8260_set_rtc_time;
	ppc_md.get_rtc_time	= tqm8260_get_rtc_time;

	callback_setup_arch	= ppc_md.setup_arch;
	ppc_md.setup_arch	= tqm8260_setup_arch;
