/*
 * arch/ppc/platforms/sbc82xx.c
 *
 * SBC82XX platform support
 *
 * Author: Guy Streeter <streeter@redhat.com>
 *
 * Derived from: est8260_setup.c by Allen Curtis, ONZ
 *
 * Copyright 2004 Red Hat, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/seq_file.h>
#include <linux/stddef.h>

#include <asm/mpc8260.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/todc.h>
#include <asm/immap_cpm2.h>

static void (*callback_setup_arch)(void);

extern unsigned char __res[sizeof(bd_t)];

extern void m8260_init(unsigned long r3, unsigned long r4,
	unsigned long r5, unsigned long r6, unsigned long r7);

extern void (*late_time_init)(void);

static int
sbc82xx_show_cpuinfo(struct seq_file *m)
{
	bd_t	*binfo = (bd_t *)__res;

	seq_printf(m, "vendor\t\t: Wind River\n"
		      "machine\t\t: SBC PowerQUICC II\n"
		      "\n"
		      "mem size\t\t: 0x%08lx\n"
		      "console baud\t\t: %ld\n"
		      "\n",
		      binfo->bi_memsize,
		      binfo->bi_baudrate);
	return 0;
}

static void __init
sbc82xx_setup_arch(void)
{
	printk("SBC PowerQUICC II Port\n");
	callback_setup_arch();
}

#ifdef CONFIG_GEN_RTC
TODC_ALLOC();

/*
 * Timer init happens before mem_init but after paging init, so we cannot
 * directly use ioremap() at that time.
 * late_time_init() is call after paging init.
 */

static void sbc82xx_time_init(void)
{
	volatile memctl_cpm2_t *mc = &cpm2_immr->im_memctl;

	/* Set up CS11 for RTC chip */
	mc->memc_br11=0;
	mc->memc_or11=0xffff0836;
	mc->memc_br11=SBC82xx_TODC_NVRAM_ADDR | 0x0801;

	TODC_INIT(TODC_TYPE_MK48T59, 0, 0, SBC82xx_TODC_NVRAM_ADDR, 0);

	todc_info->nvram_data =
		(unsigned int)ioremap(todc_info->nvram_data, 0x2000);
	BUG_ON(!todc_info->nvram_data);
	ppc_md.get_rtc_time	= todc_get_rtc_time;
	ppc_md.set_rtc_time	= todc_set_rtc_time;
	ppc_md.nvram_read_val	= todc_direct_read_val;
	ppc_md.nvram_write_val	= todc_direct_write_val;
	todc_time_init();
}
#endif /* CONFIG_GEN_RTC */

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	/* Generic 8260 platform initialization */
	m8260_init(r3, r4, r5, r6, r7);

	/* u-boot may be using one of the FCC Ethernet devices.
	   Use the MAC address to the SCC. */
	__res[offsetof(bd_t, bi_enetaddr[5])] &= ~3;

	/* Anything special for this platform */
	ppc_md.show_cpuinfo	= sbc82xx_show_cpuinfo;

	callback_setup_arch	= ppc_md.setup_arch;
	ppc_md.setup_arch	= sbc82xx_setup_arch;
#ifdef CONFIG_GEN_RTC
	ppc_md.time_init        = NULL;
	ppc_md.get_rtc_time     = NULL;
	ppc_md.set_rtc_time     = NULL;
	ppc_md.nvram_read_val   = NULL;
	ppc_md.nvram_write_val  = NULL;
	late_time_init		= sbc82xx_time_init;
#endif /* CONFIG_GEN_RTC */
}
