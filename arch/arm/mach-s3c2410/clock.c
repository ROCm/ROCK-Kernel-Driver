/* linux/arch/arm/mach-s3c2410/gpio.c
 *
 * Copyright (c) 2004 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 Clock control support
 *
 * Based on, and code from linux/arch/arm/mach-versatile/clock.c
 **
 **  Copyright (C) 2004 ARM Limited.
 **  Written by Deep Blue Solutions Limited.
 *
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
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>

#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <asm/hardware.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/hardware/clock.h>
#include <asm/arch/regs-clock.h>

#include "clock.h"


static LIST_HEAD(clocks);
static DECLARE_MUTEX(clocks_sem);


/* old functions */

void s3c2410_clk_enable(unsigned int clocks, unsigned int enable)
{
	unsigned long clkcon;
	unsigned long flags;

	local_irq_save(flags);

	clkcon = __raw_readl(S3C2410_CLKCON);
	clkcon &= ~clocks;

	if (enable)
		clkcon |= clocks;

	__raw_writel(clkcon, S3C2410_CLKCON);

	local_irq_restore(flags);
}


/* Clock API calls */

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p;
	struct clk *clk = ERR_PTR(-ENOENT);

	down(&clocks_sem);
	list_for_each_entry(p, &clocks, list) {
		if (strcmp(id, p->name) == 0 &&
		    try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}
	up(&clocks_sem);

	return clk;
}

void clk_put(struct clk *clk)
{
	module_put(clk->owner);
}

int clk_enable(struct clk *clk)
{
	s3c2410_clk_enable(clk->ctrlbit, 1);
	return 0;
}

void clk_disable(struct clk *clk)
{
	s3c2410_clk_enable(clk->ctrlbit, 0);
}


int clk_use(struct clk *clk)
{
	atomic_inc(&clk->used);
	return 0;
}


void clk_unuse(struct clk *clk)
{
	atomic_dec(&clk->used);
}

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk->parent != NULL)
		return clk->parent->rate;

	return clk->rate;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return rate;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return -EINVAL;
}

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}

EXPORT_SYMBOL(clk_get);
EXPORT_SYMBOL(clk_put);
EXPORT_SYMBOL(clk_enable);
EXPORT_SYMBOL(clk_disable);
EXPORT_SYMBOL(clk_use);
EXPORT_SYMBOL(clk_unuse);
EXPORT_SYMBOL(clk_get_rate);
EXPORT_SYMBOL(clk_round_rate);
EXPORT_SYMBOL(clk_set_rate);
EXPORT_SYMBOL(clk_get_parent);

/* base clocks */

static struct clk clk_f = {
	.name          = "fclk",
	.rate          = 0,
	.parent        = NULL,
	.ctrlbit       = 0
};

static struct clk clk_h = {
	.name          = "hclk",
	.rate          = 0,
	.parent        = NULL,
	.ctrlbit       = 0
};

static struct clk clk_p = {
	.name          = "pclk",
	.rate          = 0,
	.parent        = NULL,
	.ctrlbit       = 0
};

/* clock definitions */

static struct clk init_clocks[] = {
	{ .name    = "nand",
	  .parent  = &clk_h,
	  .ctrlbit = S3C2410_CLKCON_NAND
	},
	{ .name    = "lcd",
	  .parent  = &clk_h,
	  .ctrlbit = S3C2410_CLKCON_LCDC
	},
	{ .name    = "usb-host",
	  .parent  = &clk_h,
	  .ctrlbit =   S3C2410_CLKCON_USBH
	},
	{ .name    = "usb-device",
	  .parent  = &clk_h,
	  .ctrlbit = S3C2410_CLKCON_USBD
	},
	{ .name    = "timers",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_PWMT
	},
	{ .name    = "sdi",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_SDI
	},
	{ .name    = "uart0",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_UART0
	},
	{ .name    = "uart1",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_UART1
	},
	{ .name    = "uart2",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_UART2
	},
	{ .name    = "gpio",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_GPIO
	},
	{ .name    = "rtc",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_RTC
	},
	{ .name    = "adc",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_ADC
	},
	{ .name    = "i2c",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_IIC
	},
	{ .name    = "iis",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_IIS
	},
	{ .name    = "spi",
	  .parent  = &clk_p,
	  .ctrlbit = S3C2410_CLKCON_SPI
	}
};

/* initialise the clock system */

int s3c2410_register_clock(struct clk *clk)
{
	clk->owner = THIS_MODULE;
	atomic_set(&clk->used, 0);

	/* add to the list of available clocks */

	down(&clocks_sem);
	list_add(&clk->list, &clocks);
	up(&clocks_sem);

	return 0;
}

/* initalise all the clocks */

static int __init s3c2410_init_clocks(void)
{
	struct clk *clkp = init_clocks;
	int ptr;
	int ret;

	printk(KERN_INFO "S3C2410 Clock control, (c) 2004 Simtec Electronics\n");

	/* initialise the main system clocks */

	clk_h.rate = s3c2410_hclk;
	clk_p.rate = s3c2410_pclk;
	clk_f.rate = s3c2410_fclk;

	/* set the enabled clocks to a minimal (known) state */
	__raw_writel(S3C2410_CLKCON_PWMT | S3C2410_CLKCON_UART0 | S3C2410_CLKCON_UART1 | S3C2410_CLKCON_UART2 | S3C2410_CLKCON_GPIO | S3C2410_CLKCON_RTC, S3C2410_CLKCON);

	/* register our clocks */

	if (s3c2410_register_clock(&clk_f) < 0)
		printk(KERN_ERR "failed to register cpu fclk\n");

	if (s3c2410_register_clock(&clk_h) < 0)
		printk(KERN_ERR "failed to register cpu fclk\n");

	if (s3c2410_register_clock(&clk_p) < 0)
		printk(KERN_ERR "failed to register cpu fclk\n");

	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks); ptr++, clkp++) {
		ret = s3c2410_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	return 0;
}

arch_initcall(s3c2410_init_clocks);

