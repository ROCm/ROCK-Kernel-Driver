/*
 *  linux/arch/arm/mach-pxa/generic.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * Code common to all PXA machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Since this file should be linked before any other machine specific file,
 * the __initcall() here will be executed first.  This serves as default
 * initialization stuff for PXA machines which can be overriden later if
 * need be.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>

#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

#include "generic.h"

/*
 * Return the current lclk requency in units of 10kHz
 */
unsigned int get_lclk_frequency_10khz(void)
{
	unsigned int l;

	l = CCCR & 0x1f;

	switch(l)
	{
		case 1:
			return 9953;
		case 2:
			return 11796;
		case 3:
			return 13271;
		case 4:
			return 14746;
		case 5:
			return 16589;
		case 0xf:
			return 3320;
		default:
			return 0;
	}
}

EXPORT_SYMBOL(get_lclk_frequency_10khz);

/*
 * Handy function to set GPIO alternate functions
 */

void pxa_gpio_mode(int gpio_mode)
{
	long flags;
	int gpio = gpio_mode & GPIO_MD_MASK_NR;
	int fn = (gpio_mode & GPIO_MD_MASK_FN) >> 8;
	int gafr;

	local_irq_save(flags);
	if (gpio_mode & GPIO_MD_MASK_DIR)
		GPDR(gpio) |= GPIO_bit(gpio);
	else
		GPDR(gpio) &= ~GPIO_bit(gpio);
	gafr = GAFR(gpio) & ~(0x3 << (((gpio) & 0xf)*2));
	GAFR(gpio) = gafr |  (fn  << (((gpio) & 0xf)*2));
	local_irq_restore(flags);
}

EXPORT_SYMBOL(pxa_gpio_mode);

/*
 * Note that 0xfffe0000-0xffffffff is reserved for the vector table and
 * cache flush area.
 */
static struct map_desc standard_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf6000000, 0x20000000, 0x01000000, MT_DEVICE }, /* PCMCIA0 IO */
  { 0xf7000000, 0x30000000, 0x01000000, MT_DEVICE }, /* PCMCIA1 IO */
  { 0xf8000000, 0x40000000, 0x01400000, MT_DEVICE }, /* Devs */
  { 0xfa000000, 0x44000000, 0x00100000, MT_DEVICE }, /* LCD */
  { 0xfc000000, 0x48000000, 0x00100000, MT_DEVICE }, /* Mem Ctl */
  { 0xff000000, 0x00000000, 0x00100000, MT_DEVICE }  /* UNCACHED_PHYS_0 */
};

void __init pxa_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
}
