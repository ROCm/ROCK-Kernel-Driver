/* linux/arch/arm/mach-s3c2410/gpio.c
 *
 * Copyright (c) 2004 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 GPIO support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Changelog
 *	13-Sep-2004  BJD  Implemented change of MISCCR
 *	14-Sep-2004  BJD  Added getpin call
 *	14-Sep-2004  BJD  Fixed bug in setpin() call
 *	30-Sep-2004  BJD  Fixed cfgpin() mask bug
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/arch/regs-gpio.h>

void s3c2410_gpio_cfgpin(unsigned int pin, unsigned int function)
{
	unsigned long base = S3C2410_GPIO_BASE(pin);
	unsigned long mask;
	unsigned long con;
	unsigned long flags;

	if (pin < S3C2410_GPIO_BANKB) {
		mask = 1 << S3C2410_GPIO_OFFSET(pin);
	} else {
		mask = 3 << S3C2410_GPIO_OFFSET(pin)*2;
	}

	local_irq_save(flags);

	con  = __raw_readl(base + 0x00);
	con &= ~mask;
	con |= function;

	__raw_writel(con, base + 0x00);

	local_irq_restore(flags);
}

void s3c2410_gpio_pullup(unsigned int pin, unsigned int to)
{
	unsigned long base = S3C2410_GPIO_BASE(pin);
	unsigned long offs = S3C2410_GPIO_OFFSET(pin);
	unsigned long flags;
	unsigned long up;

	if (pin < S3C2410_GPIO_BANKB)
		return;

	local_irq_save(flags);

	up = __raw_readl(base + 0x08);
	up &= 1 << offs;
	up |= to << offs;
	__raw_writel(up, base + 0x08);

	local_irq_restore(flags);
}

void s3c2410_gpio_setpin(unsigned int pin, unsigned int to)
{
	unsigned long base = S3C2410_GPIO_BASE(pin);
	unsigned long offs = S3C2410_GPIO_OFFSET(pin);
	unsigned long flags;
	unsigned long dat;

	local_irq_save(flags);

	dat = __raw_readl(base + 0x04);
	dat &= ~(1 << offs);
	dat |= to << offs;
	__raw_writel(dat, base + 0x04);

	local_irq_restore(flags);
}

unsigned int s3c2410_gpio_getpin(unsigned int pin)
{
	unsigned long base = S3C2410_GPIO_BASE(pin);
	unsigned long offs = S3C2410_GPIO_OFFSET(pin);

	return __raw_readl(base + 0x04) & (1<< offs);
}

unsigned int s3c2410_modify_misccr(unsigned int clear, unsigned int change)
{
	unsigned long flags;
	unsigned long misccr;

	local_irq_save(flags);
	misccr = __raw_readl(S3C2410_MISCCR);
	misccr &= ~clear;
	misccr ^= change;
	__raw_writel(misccr, S3C2410_MISCCR);
	local_irq_restore(flags);

	return misccr;
}
