/* linux/arch/arm/mach-s3c2410/s3c2410.c
 *
 * Copyright (c) 2003,2004 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     16-May-2003 BJD  Created initial version
 *     16-Aug-2003 BJD  Fixed header files and copyright, added URL
 *     05-Sep-2003 BJD  Moved to kernel v2.6
 *     18-Jan-2004 BJD  Added serial port configuration
 *     21-Aug-2004 BJD  Added new struct s3c2410_board handler
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-serial.h>

#include "s3c2410.h"
#include "cpu.h"

int s3c2410_clock_tick_rate = 12*1000*1000;  /* current timers at 12MHz */

/* serial port setup */

struct s3c2410_uartcfg *s3c2410_uartcfgs;

/* clock info */

unsigned long s3c2410_fclk;
unsigned long s3c2410_hclk;
unsigned long s3c2410_pclk;

static struct map_desc s3c2410_iodesc[] __initdata = {
	IODESC_ENT(USBHOST),
	IODESC_ENT(CLKPWR),
	IODESC_ENT(LCD),
	IODESC_ENT(UART),
	IODESC_ENT(TIMER),
	IODESC_ENT(ADC),
	IODESC_ENT(WATCHDOG)
};

static struct resource s3c_uart0_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART0,
		.end   = S3C2410_PA_UART0 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX0,
		.end   = IRQ_S3CUART_ERR0,
		.flags = IORESOURCE_IRQ,
	}

};

static struct resource s3c_uart1_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART1,
		.end   = S3C2410_PA_UART1 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX1,
		.end   = IRQ_S3CUART_ERR1,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource s3c_uart2_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART2,
		.end   = S3C2410_PA_UART2 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX2,
		.end   = IRQ_S3CUART_ERR2,
		.flags = IORESOURCE_IRQ,
	}
};

/* our uart devices */

static struct platform_device s3c_uart0 = {
	.name		  = "s3c2410-uart",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c_uart0_resource),
	.resource	  = s3c_uart0_resource,
};


static struct platform_device s3c_uart1 = {
	.name		  = "s3c2410-uart",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c_uart1_resource),
	.resource	  = s3c_uart1_resource,
};

static struct platform_device s3c_uart2 = {
	.name		  = "s3c2410-uart",
	.id		  = 2,
	.num_resources	  = ARRAY_SIZE(s3c_uart2_resource),
	.resource	  = s3c_uart2_resource,
};

static struct platform_device *uart_devices[] __initdata = {
	&s3c_uart0,
	&s3c_uart1,
	&s3c_uart2
};

void __init s3c2410_map_io(struct map_desc *mach_desc, int mach_size)
{
	unsigned long tmp;

	/* register our io-tables */

	iotable_init(s3c2410_iodesc, ARRAY_SIZE(s3c2410_iodesc));
	iotable_init(mach_desc, mach_size);

	printk("machine_initted %p,%d\n", mach_desc, mach_size);

	/* now we've got our machine bits initialised, work out what
	 * clocks we've got */

	s3c2410_fclk = s3c2410_get_pll(__raw_readl(S3C2410_MPLLCON), 12*MHZ);

	tmp = __raw_readl(S3C2410_CLKDIVN);
	//printk("tmp=%08x, fclk=%d\n", tmp, s3c2410_fclk);

	/* work out clock scalings */

	s3c2410_hclk = s3c2410_fclk / ((tmp & S3C2410_CLKDIVN_HDIVN) ? 2 : 1);
	s3c2410_pclk = s3c2410_hclk / ((tmp & S3C2410_CLKDIVN_PDIVN) ? 2 : 1);

	/* print brieft summary of clocks, etc */

	printk("S3C2410: core %ld.%03ld MHz, memory %ld.%03ld MHz, peripheral %ld.%03ld MHz\n",
	       print_mhz(s3c2410_fclk), print_mhz(s3c2410_hclk),
	       print_mhz(s3c2410_pclk));
}

static struct s3c2410_board *board;

void s3c2410_set_board(struct s3c2410_board *b)
{
	board = b;
}

void s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c2410_uartcfgs = cfg;
}

int __init s3c2410_init(void)
{
	int ret;

	printk("S3C2410: Initialising architecture\n");

	ret = platform_add_devices(uart_devices, ARRAY_SIZE(uart_devices));
	if (ret)
		return ret;

	if (board != NULL) {
		if (board->devices != NULL) {
			ret = platform_add_devices(board->devices,
						   board->devices_count);

			if (ret) {
				printk(KERN_ERR "s3c2410: failed to add board devices (%d)\n", ret);
			}
		}

		/* not adding board devices may not be fatal */
		ret = 0;
	}

	return ret;
}
