/*
 * linux/arch/arm/mach-sa1100/collie.c
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * This file contains all Collie-specific tweaks.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * ChangeLog:
 *  03-06-2004 John Lenz <jelenz@wisc.edu>
 *  06-04-2002 Chris Larson <kergoth@digitalnemesis.net>
 *  04-16-2001 Lineo Japan,Inc. ...
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/timer.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/arch/collie.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include <asm/hardware/locomo.h>

#include "generic.h"

static void __init scoop_init(void)
{

#define	COLLIE_SCP_INIT_DATA(adr,dat)	(((adr)<<16)|(dat))
#define	COLLIE_SCP_INIT_DATA_END	((unsigned long)-1)
	static const unsigned long scp_init[] = {
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_MCR, 0x0140),	// 00
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_MCR, 0x0100),
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_CDR, 0x0000),	// 04
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_CPR, 0x0000),	// 0C
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_CCR, 0x0000),	// 10
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_IMR, 0x0000),	// 18
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_IRM, 0x00FF),	// 14
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_ISR, 0x0000),	// 1C
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_IRM, 0x0000),
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_GPCR, COLLIE_SCP_IO_DIR),	// 20
		COLLIE_SCP_INIT_DATA(COLLIE_SCP_GPWR, COLLIE_SCP_IO_OUT),	// 24
		COLLIE_SCP_INIT_DATA_END
	};
	int i;
	for (i = 0; scp_init[i] != COLLIE_SCP_INIT_DATA_END; i++) {
		int adr = scp_init[i] >> 16;
		COLLIE_SCP_REG(adr) = scp_init[i] & 0xFFFF;
	}

}

static struct resource locomo_resources[] = {
	[0] = {
		.start		= 0x40000000,
		.end		= 0x40001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_GPIO25,
		.end		= IRQ_GPIO25,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device locomo_device = {
	.name		= "locomo",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(locomo_resources),
	.resource	= locomo_resources,
};

static struct platform_device *devices[] __initdata = {
	&locomo_device,
};

static void __init collie_init(void)
{
	int ret = 0;

	/* cpu initialize */
	GAFR = ( GPIO_SSP_TXD | \
		 GPIO_SSP_SCLK | GPIO_SSP_SFRM | GPIO_SSP_CLK | GPIO_TIC_ACK | \
		 GPIO_32_768kHz );

	GPDR = ( GPIO_LDD8 | GPIO_LDD9 | GPIO_LDD10 | GPIO_LDD11 | GPIO_LDD12 | \
		 GPIO_LDD13 | GPIO_LDD14 | GPIO_LDD15 | GPIO_SSP_TXD | \
		 GPIO_SSP_SCLK | GPIO_SSP_SFRM | GPIO_SDLC_SCLK | \
		 GPIO_SDLC_AAF | GPIO_UART_SCLK1 | GPIO_32_768kHz );
	GPLR = GPIO_GPIO18;

	// PPC pin setting
	PPDR = ( PPC_LDD0 | PPC_LDD1 | PPC_LDD2 | PPC_LDD3 | PPC_LDD4 | PPC_LDD5 | \
		 PPC_LDD6 | PPC_LDD7 | PPC_L_PCLK | PPC_L_LCLK | PPC_L_FCLK | PPC_L_BIAS | \
	 	 PPC_TXD1 | PPC_TXD2 | PPC_RXD2 | PPC_TXD3 | PPC_TXD4 | PPC_SCLK | PPC_SFRM );

	PSDR = ( PPC_RXD1 | PPC_RXD2 | PPC_RXD3 | PPC_RXD4 );

	GAFR |= GPIO_32_768kHz;
	GPDR |= GPIO_32_768kHz;
	TUCR  = TUCR_32_768kHz;

	scoop_init();

	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if (ret) {
		printk(KERN_WARNING "collie: Unable to register LoCoMo device\n");
	}
}

static struct map_desc collie_io_desc[] __initdata = {
	/* virtual     physical    length      type */
	{0xe8000000, 0x00000000, 0x02000000, MT_DEVICE},	/* 32M main flash (cs0) */
	{0xea000000, 0x08000000, 0x02000000, MT_DEVICE},	/* 32M boot flash (cs1) */
	{0xf0000000, 0x40000000, 0x01000000, MT_DEVICE},	/* 16M LOCOMO  & SCOOP (cs4) */
};

static void __init collie_map_io(void)
{
	sa1100_map_io();
	iotable_init(collie_io_desc, ARRAY_SIZE(collie_io_desc));
}

MACHINE_START(COLLIE, "Sharp-Collie")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(collie_map_io)
	INITIRQ(sa1100_init_irq)
	INIT_MACHINE(collie_init)
	INITTIME(sa1100_init_time)
MACHINE_END
