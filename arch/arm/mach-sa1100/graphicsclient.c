/*
 * linux/arch/arm/mach-sa1100/graphicsclient.c
 *
 * Author: Nicolas Pitre
 *
 * Pieces specific to the GraphicsClient board
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <linux/serial_core.h>

#include "generic.h"


/*
 * Handlers for GraphicsClient's external IRQ logic
 */

static void
gc_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
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

static void gc_mask_irq1(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(0)));
	ADS_INT_EN1 &= ~mask;
	ADS_INT_ST1 = mask;
}

static void gc_unmask_irq1(unsigned int irq)
{
	ADS_INT_EN1 |= (1 << (irq - ADS_EXT_IRQ(0)));
}

static struct irqchip gc_irq1_chip = {
	.ack	= gc_mask_irq1,
	.mask	= gc_mask_irq1,
	.unmask = gc_unmask_irq1,
};

static void gc_mask_irq2(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(8)));
	ADS_INT_EN2 &= ~mask;
	ADS_INT_ST2 = mask;
}

static void gc_unmask_irq2(unsigned int irq)
{
	ADS_INT_EN2 |= (1 << (irq - ADS_EXT_IRQ(8)));
}

static struct irqchip gc_irq2_chip = {
	.ack	= gc_mask_irq2,
	.mask	= gc_mask_irq2,
	.unmask = gc_unmask_irq2,
};

static void __init graphicsclient_init_irq(void)
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
		set_irq_chip(irq, &gc_irq1_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
	for (irq = ADS_EXT_IRQ(8); irq <= ADS_EXT_IRQ(15); irq++) {
		set_irq_chip(irq, &gc_irq2_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
	set_irq_type(IRQ_GPIO0, IRQT_FALLING);
	set_irq_chained_handler(IRQ_GPIO0, gc_irq_handler);
}


static struct map_desc graphicsclient_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x10000000, 0x00400000, MT_DEVICE }, /* CPLD */
  { 0xf1000000, 0x18000000, 0x00400000, MT_DEVICE }  /* CAN */
};

static u_int graphicsclient_get_mctrl(struct uart_port *port)
{
	u_int result = TIOCM_CD | TIOCM_DSR;

	if (port->mapbase == _Ser1UTCR0) {
		if (!(GPLR & GPIO_GC_UART0_CTS))
			result |= TIOCM_CTS;
	} else if (port->mapbase == _Ser2UTCR0) {
		if (!(GPLR & GPIO_GC_UART1_CTS))
			result |= TIOCM_CTS;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (!(GPLR & GPIO_GC_UART2_CTS))
			result |= TIOCM_CTS;
	} else {
		result = TIOCM_CTS;
	}

	return result;
}

static void graphicsclient_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser1UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_GC_UART0_RTS;
		else
			GPSR = GPIO_GC_UART0_RTS;
	} else if (port->mapbase == _Ser2UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_GC_UART1_RTS;
		else
			GPSR = GPIO_GC_UART1_RTS;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_GC_UART2_RTS;
		else
			GPSR = GPIO_GC_UART2_RTS;
	}
}

static void
graphicsclient_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (!state) {
		/* make serial ports work ... */
		Ser2UTCR4 = 0;
		Ser2HSCR0 = 0; 
		Ser1SDCR0 |= SDCR0_UART;
	}
}

static struct sa1100_port_fns graphicsclient_port_fns __initdata = {
	.get_mctrl	= graphicsclient_get_mctrl,
	.set_mctrl	= graphicsclient_set_mctrl,
	.pm		= graphicsclient_uart_pm,
};

static void __init graphicsclient_map_io(void)
{
	sa1100_map_io();
	iotable_init(graphicsclient_io_desc, ARRAY_SIZE(graphicsclient_io_desc));

	sa1100_register_uart_fns(&graphicsclient_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);
	GPDR |= GPIO_GC_UART0_RTS | GPIO_GC_UART1_RTS | GPIO_GC_UART2_RTS;
	GPDR &= ~(GPIO_GC_UART0_CTS | GPIO_GC_UART1_RTS | GPIO_GC_UART2_RTS);
}

MACHINE_START(GRAPHICSCLIENT, "ADS GraphicsClient")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(graphicsclient_map_io)
	INITIRQ(graphicsclient_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
