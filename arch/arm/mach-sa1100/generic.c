/*
 * linux/arch/arm/mach-sa1100/generic.c
 *
 * Author: Nicolas Pitre
 *
 * Code common to all SA11x0 machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Since this file should be linked before any other machine specific file,
 * the __initcall() here will be executed first.  This serves as default
 * initialization stuff for SA1100 machines which can be overriden later if
 * need be.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/cpufreq.h>

#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

#include "generic.h"

#define NR_FREQS	16

/*
 * This table is setup for a 3.6864MHz Crystal.
 */
static const unsigned short cclk_frequency_100khz[NR_FREQS] = {
	 590,	/*  59.0 MHz */
	 737,	/*  73.7 MHz */
	 885, 	/*  88.5 MHz */
	1032,	/* 103.2 MHz */
	1180,	/* 118.0 MHz */
	1327,	/* 132.7 MHz */
	1475,	/* 147.5 MHz */
	1622,	/* 162.2 MHz */
	1769,	/* 176.9 MHz */
	1917,	/* 191.7 MHz */
	2064,	/* 206.4 MHz */
	2212,	/* 221.2 MHz */
	2359,   /* 235.9 MHz */
	2507,   /* 250.7 MHz */
	2654,   /* 265.4 MHz */
	2802    /* 280.2 MHz */
};

#ifdef CONFIG_CPU_FREQ
unsigned int sa11x0_freq_to_ppcr(unsigned int khz)
{
	int i;

	khz /= 100;

	for (i = NR_FREQS - 1; i > 0; i--)
		if (cclk_frequency_100khz[i] <= khz)
			break;

	return i;
}

/*
 * Validate the speed in khz.  If we can't generate the precise
 * frequency requested, round it down (to be on the safe side).
 */
unsigned int sa11x0_validatespeed(unsigned int khz)
{
	return cclk_frequency_100khz[sa11x0_freq_to_ppcr(khz)] * 100;
}

unsigned int sa11x0_getspeed(void)
{
	return cclk_frequency_100khz[PPCR & 0xf] * 100;
}
#else
/*
 * We still need to provide this so building without cpufreq works.
 */ 
unsigned int cpufreq_get(int cpu)
{
	return cclk_frequency_100khz[PPCR & 0xf] * 100;
}
EXPORT_SYMBOL(cpufreq_get);
#endif

/*
 * Default power-off for SA1100
 */
static void sa1100_power_off(void)
{
	mdelay(100);
	local_irq_disable();
	/* disable internal oscillator, float CS lines */
	PCFR = (PCFR_OPDE | PCFR_FP | PCFR_FS);
	/* enable wake-up on GPIO0 (Assabet...) */
	PWER = GFER = GRER = 1;
	/*
	 * set scratchpad to zero, just in case it is used as a
	 * restart address by the bootloader.
	 */
	PSPR = 0;
	/* enter sleep mode */
	PMCR = PMCR_SF;
}

static int __init sa1100_init(void)
{
	pm_power_off = sa1100_power_off;
	return 0;
}

__initcall(sa1100_init);

void (*sa1100fb_backlight_power)(int on);
void (*sa1100fb_lcd_power)(int on);

EXPORT_SYMBOL(sa1100fb_backlight_power);
EXPORT_SYMBOL(sa1100fb_lcd_power);


/*
 * Common I/O mapping:
 *
 * Typically, static virtual address mappings are as follow:
 *
 * 0xf0000000-0xf3ffffff:	miscellaneous stuff (CPLDs, etc.)
 * 0xf4000000-0xf4ffffff:	SA-1111
 * 0xf5000000-0xf5ffffff:	reserved (used by cache flushing area)
 * 0xf6000000-0xfffeffff:	reserved (internal SA1100 IO defined above)
 * 0xffff0000-0xffff0fff:	SA1100 exception vectors
 * 0xffff2000-0xffff2fff:	Minicache copy_user_page area
 *
 * Below 0xe8000000 is reserved for vm allocation.
 *
 * The machine specific code must provide the extra mapping beside the
 * default mapping provided here.
 */

static struct map_desc standard_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf8000000, 0x80000000, 0x00100000, MT_DEVICE }, /* PCM */
  { 0xfa000000, 0x90000000, 0x00100000, MT_DEVICE }, /* SCM */
  { 0xfc000000, 0xa0000000, 0x00100000, MT_DEVICE }, /* MER */
  { 0xfe000000, 0xb0000000, 0x00200000, MT_DEVICE }  /* LCD + DMA */
};

void __init sa1100_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
}

/*
 * Disable the memory bus request/grant signals on the SA1110 to
 * ensure that we don't receive spurious memory requests.  We set
 * the MBGNT signal false to ensure the SA1111 doesn't own the
 * SDRAM bus.
 */
void __init sa1110_mb_disable(void)
{
	unsigned long flags;

	local_irq_save(flags);
	
	PGSR &= ~GPIO_MBGNT;
	GPCR = GPIO_MBGNT;
	GPDR = (GPDR & ~GPIO_MBREQ) | GPIO_MBGNT;

	GAFR &= ~(GPIO_MBGNT | GPIO_MBREQ);

	local_irq_restore(flags);
}

/*
 * If the system is going to use the SA-1111 DMA engines, set up
 * the memory bus request/grant pins.
 */
void __init sa1110_mb_enable(void)
{
	unsigned long flags;

	local_irq_save(flags);

	PGSR &= ~GPIO_MBGNT;
	GPCR = GPIO_MBGNT;
	GPDR = (GPDR & ~GPIO_MBREQ) | GPIO_MBGNT;

	GAFR |= (GPIO_MBGNT | GPIO_MBREQ);
	TUCR |= TUCR_MR;

	local_irq_restore(flags);
}

