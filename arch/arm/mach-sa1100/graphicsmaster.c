/*
 * linux/arch/arm/mach-sa1100/graphicsmaster.c
 *
 * Pieces specific to the GraphicsMaster board
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

static struct resource sa1111_resources[] = {
	[0] = {
		.start		= 0x18000000,
		.end		= 0x18001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= ADS_EXT_IRQ(0),
		.end		= ADS_EXT_IRQ(0),
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 sa1111_dmamask = 0xffffffffUL;

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.dma_mask = &sa1111_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
};

static int __init graphicsmaster_init(void)
{
	int ret;

	if (!machine_is_graphicsmaster())
		return -ENODEV;

	/*
	 * Ensure that the memory bus request/grant signals are setup,
	 * and the grant is held in its inactive state
	 */
	sa1110_mb_disable();

	/*
	 * Probe for SA1111.
	 */
	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if (ret < 0)
		return ret;

	/*
	 * Enable PWM control for LCD
	 */
	sa1111_enable_device(SKPCR_PWMCLKEN);
	SKPWM0 = 0x7F;				// VEE
	SKPEN0 = 1;
	SKPWM1 = 0x01;				// Backlight
	SKPEN1 = 1;

	return 0;
}

arch_initcall(graphicsmaster_init);

/*
 * Handlers for GraphicsMaster's external IRQ logic
 */

static void
gm_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	unsigned int mask;

	while ((mask = ADS_INT_ST1 | (ADS_INT_ST2 << 8))) {
		/* clear the parent IRQ */
		GEDR = GPIO_GPIO0;

		irq = ADS_EXT_IRQ(0);
		desc = irq_desc + irq;

		do {
			if (mask & 1)
				desc->handle(irq, desc, regs);
			mask >>= 1;
			irq++;
			desc++;
		} while (mask);
	}
}

static void gm_mask_irq1(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(0)));
	ADS_INT_EN1 &= ~mask;
	ADS_INT_ST1 = mask;
}

static void gm_unmask_irq1(unsigned int irq)
{
	ADS_INT_EN1 |= (1 << (irq - ADS_EXT_IRQ(0)));
}

static struct irqchip gm_irq1_chip = {
	.ack	= gm_mask_irq1,
	.mask	= gm_mask_irq1,
	.unmask = gm_unmask_irq1,
};

static void gm_mask_irq2(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(8)));
	ADS_INT_EN2 &= ~mask;
	ADS_INT_ST2 = mask;
}

static void gm_unmask_irq2(unsigned int irq)
{
	ADS_INT_EN2 |= (1 << (irq - ADS_EXT_IRQ(8)));
}

static struct irqchip gm_irq2_chip = {
	.ack	= gm_mask_irq2,
	.mask	= gm_mask_irq2,
	.unmask = gm_unmask_irq2,
};

static void __init graphicsmaster_init_irq(void)
{
	unsigned int irq;

	/* First the standard SA1100 IRQs */
	sa1100_init_irq();

	/* disable all IRQs */
	ADS_INT_EN1 = 0;
	ADS_INT_EN2 = 0;

	/* clear all IRQs */
	ADS_INT_ST1 = 0xff;
	ADS_INT_ST2 = 0xff;

	for (irq = ADS_EXT_IRQ(0); irq <= ADS_EXT_IRQ(7); irq++) {
		set_irq_chip(irq, &gm_irq1_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_PROBE | IRQF_VALID);
	}
	for (irq = ADS_EXT_IRQ(8); irq <= ADS_EXT_IRQ(15); irq++) {
		set_irq_chip(irq, &gm_irq2_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_PROBE | IRQF_VALID);
	}
	set_irq_type(IRQ_GPIO0, IRQT_FALLING);
	set_irq_chained_handler(IRQ_GPIO0, gm_irq_handler);
}


static struct map_desc graphicsmaster_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x10000000, 0x00400000, MT_DEVICE }, /* CPLD */
  { 0xf1000000, 0x40000000, 0x00400000, MT_DEVICE }, /* CAN */
  { 0xf4000000, 0x18000000, 0x00800000, MT_DEVICE }  /* SA-1111 */
};

#error Old code.  Someone needs to decide what to do about this.
#if 0
static int graphicsmaster_uart_open(struct uart_port *port, struct uart_info *info)
{
	int	ret = 0;

	if (port->mapbase == _Ser1UTCR0) {
		Ser1SDCR0 |= SDCR0_UART;
		/* Set RTS Output */
		GPSR = GPIO_GPIO15;
	}
	else if (port->mapbase == _Ser2UTCR0) {
		Ser2UTCR4 = Ser2HSCR0 = 0;
		/* Set RTS Output */
		GPSR = GPIO_GPIO17;
	}
	else if (port->mapbase == _Ser3UTCR0) {
	        /* Set RTS Output */
		GPSR = GPIO_GPIO19;
	}
	return ret;
}
#endif

static u_int graphicsmaster_get_mctrl(struct uart_port *port)
{
	u_int result = TIOCM_CD | TIOCM_DSR;

	if (port->mapbase == _Ser1UTCR0) {
		if (!(GPLR & GPIO_GPIO14))
			result |= TIOCM_CTS;
	} else if (port->mapbase == _Ser2UTCR0) {
		if (!(GPLR & GPIO_GPIO16))
			result |= TIOCM_CTS;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (!(GPLR & GPIO_GPIO17))
			result |= TIOCM_CTS;
	} else {
		result = TIOCM_CTS;
	}

	return result;
}

static void graphicsmaster_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser1UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_GPIO15;
		else
			GPSR = GPIO_GPIO15;
	} else if (port->mapbase == _Ser2UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_GPIO17;
		else
			GPSR = GPIO_GPIO17;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_GPIO19;
		else
			GPSR = GPIO_GPIO19;
	}
}

static void
graphicsmaster_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (!state) {
		/* make serial ports work ... */
		Ser2UTCR4 = 0;
		Ser2HSCR0 = 0; 
		Ser1SDCR0 |= SDCR0_UART;
	}
}

static struct sa1100_port_fns graphicsmaster_port_fns __initdata = {
	.get_mctrl	= graphicsmaster_get_mctrl,
	.set_mctrl	= graphicsmaster_set_mctrl,
	.pm		= graphicsmaster_uart_pm,
};

static void __init graphicsmaster_map_io(void)
{
	sa1100_map_io();
	iotable_init(graphicsmaster_io_desc, ARRAY_SIZE(graphicsmaster_io_desc));

	sa1100_register_uart_fns(&graphicsmaster_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);

	/* set GPDR now */
	GPDR |= GPIO_GPIO15 | GPIO_GPIO17 | GPIO_GPIO19;
       	GPDR &= ~(GPIO_GPIO14 | GPIO_GPIO16 | GPIO_GPIO18);
}

MACHINE_START(GRAPHICSMASTER, "ADS GraphicsMaster")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(graphicsmaster_map_io)
	INITIRQ(graphicsmaster_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
