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
	unsigned long shift = 1;
	unsigned long mask = 3;
	unsigned long con;
	unsigned long flags;

	if (pin < S3C2410_GPIO_BANKB) {
		shift = 0;
		mask  = 1;
	}

	mask <<= S3C2410_GPIO_OFFSET(pin);

	local_irq_save(flags);

	con = __raw_readl(base + 0x00);

	con &= mask << shift;
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
	dat &= 1 << offs;
	dat |= to << offs;
	__raw_writel(dat, base + 0x04);

	local_irq_restore(flags);
}
