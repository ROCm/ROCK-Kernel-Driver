/* linux/arch/arm/mach-s3c2410/s3c2440.c
 *
 * Copyright (c) 2004 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2440 Mobile CPU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     24-Aug-2004 BJD  Start of s3c2440 support
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

#include "s3c2440.h"
#include "cpu.h"

int s3c2440_clock_tick_rate = 12*1000*1000;  /* current timers at 12MHz */

/* clock info */

unsigned long s3c2440_baseclk = 12*1000*1000;  /* assume base is 12MHz */
unsigned long s3c2440_hdiv;

unsigned long s3c2440_fclk;
unsigned long s3c2440_hclk;
unsigned long s3c2440_pclk;

static struct map_desc s3c2440_iodesc[] __initdata = {
	IODESC_ENT(USBHOST),
	IODESC_ENT(CLKPWR),
	IODESC_ENT(LCD),
	IODESC_ENT(TIMER),
	IODESC_ENT(ADC),
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
	.name		  = "s3c2440-uart",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c_uart0_resource),
	.resource	  = s3c_uart0_resource,
};


static struct platform_device s3c_uart1 = {
	.name		  = "s3c2440-uart",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c_uart1_resource),
	.resource	  = s3c_uart1_resource,
};

static struct platform_device s3c_uart2 = {
	.name		  = "s3c2440-uart",
	.id		  = 2,
	.num_resources	  = ARRAY_SIZE(s3c_uart2_resource),
	.resource	  = s3c_uart2_resource,
};

static struct platform_device *uart_devices[] __initdata = {
	&s3c_uart0,
	&s3c_uart1,
	&s3c_uart2
};

void __init s3c2440_map_io(struct map_desc *mach_desc, int size)
{
	unsigned long tmp;
	unsigned long camdiv;

	/* register our io-tables */

	iotable_init(s3c2440_iodesc, ARRAY_SIZE(s3c2440_iodesc));
	iotable_init(mach_desc, size);

	/* now we've got our machine bits initialised, work out what
	 * clocks we've got */

	s3c2440_fclk = s3c2410_get_pll(__raw_readl(S3C2410_MPLLCON),
				       s3c2440_baseclk);

	tmp = __raw_readl(S3C2410_CLKDIVN);
	camdiv = __raw_readl(S3C2440_CAMDIVN);

	/* work out clock scalings */

	switch (tmp & S3C2440_CLKDIVN_HDIVN_MASK) {
	case S3C2440_CLKDIVN_HDIVN_1:
		s3c2440_hdiv = 1;
		break;

	case S3C2440_CLKDIVN_HDIVN_2:
		s3c2440_hdiv = 1;
		break;

	case S3C2440_CLKDIVN_HDIVN_4_8:
		s3c2440_hdiv = (camdiv & S3C2440_CAMDIVN_HCLK4_HALF) ? 8 : 4;
		break;

	case S3C2440_CLKDIVN_HDIVN_3_6:
		s3c2440_hdiv = (camdiv & S3C2440_CAMDIVN_HCLK4_HALF) ? 6 : 3;
		break;
	}

	s3c2440_hclk = s3c2440_fclk / s3c2440_hdiv;
	s3c2440_pclk = s3c2440_hclk / ((tmp & S3C2440_CLKDIVN_PDIVN) ? 2 : 1);

	/* print brieft summary of clocks, etc */

	printk("S3C2440: core %ld.%03ld MHz, memory %ld.%03ld MHz, peripheral %ld.%03ld MHz\n",
	       print_mhz(s3c2440_fclk), print_mhz(s3c2440_hclk),
	       print_mhz(s3c2440_pclk));
}



int __init s3c2440_init(void)
{
	int ret;

	printk("S3C2440: Initialising architecture\n");

	ret = platform_add_devices(uart_devices, ARRAY_SIZE(uart_devices));
	if (ret)
		return ret;

	// todo: board specific inits?

	return ret;
}

