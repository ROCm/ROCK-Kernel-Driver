/*
 * linux/arch/arm/mach-sa1100/trizeps.c
 *
 * Authors:
 * Andreas Hofer <ho@dsa-ac.de>,
 * Peter Lueg <pl@dsa-ac.de>,
 * Guennadi Liakhovetski <gl@dsa-ac.de>
 *
 * This file contains all Trizeps-specific tweaks.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/pm.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/arch/trizeps.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/serial_sa1100.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <asm/arch/serial.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/arch/irqs.h>

#include "generic.h"

#undef DEBUG_TRIZEPS
#ifdef DEBUG_TRIZEPS
#define DPRINTK(fmt, args...) printk("%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK( x... )
#endif

static struct tri_uart_cts_data_t tri_uart_cts_data[] = {
	{ TRIZEPS_GPIO_UART1_CTS, 0, NULL, NULL,"int. UART1 cts" },
	{ TRIZEPS_GPIO_UART2_CTS, 0, NULL, NULL,"int. UART2 cts" },
	{ TRIZEPS_GPIO_UART3_CTS, 0, NULL, NULL,"int. UART3 cts" }
};

static void trizeps_cts_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct tri_uart_cts_data_t * uart_data = (struct tri_uart_cts_data_t *)dev_id;
	int cts = (!(GPLR & uart_data->cts_gpio));

	/* NOTE: I suppose that we will not get any interrupts
	   if the GPIO is not changed, so maybe
	   the cts_prev_state can be removed ... */
	if (cts != uart_data->cts_prev_state) {

		uart_data->cts_prev_state = cts;
		uart_handle_cts_change(uart_data->port, cts);
		DPRINTK("(IRQ %d) changed (cts=%d) stop=%d\n",
			irq, cts, uart_data->info->tty->hw_stopped);
	}
}

static int
trizeps_register_cts_intr(int gpio,
			  int irq,
			  struct tri_uart_cts_data_t *uart_data)
{
	int ret = 0;

	if(irq != NO_IRQ)
	{
		set_irq_type(irq, IRQT_BOTHEDGE);

		ret = request_irq(irq, trizeps_cts_intr,
				  SA_INTERRUPT, uart_data->name, uart_data);
		if (ret)
			printk(KERN_ERR "uart_open: failed to register CTS irq (%d)\n", ret);
	}
	return ret;
}

static void trizeps_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser1UTCR0)
	{
		/**** ttySA1  ****/
		if (mctrl & TIOCM_RTS)
			GPCR |= TRIZEPS_GPIO_UART1_RTS;
		else
			GPSR |= TRIZEPS_GPIO_UART1_RTS;

		DPRINTK("2 ttySA%d Set RTS %s\n",port->line,
			mctrl & TIOCM_RTS ? "low" : "high");

	}
	else if (port->mapbase == _Ser3UTCR0)
	{
		/**** ttySA0  ****/
	}
	else
	{
		/**** ttySA2  ****/
	}
}

static u_int trizeps_get_mctrl(struct uart_port *port)
{
	int result = TIOCM_CD | TIOCM_DSR;

	if (port->mapbase == _Ser1UTCR0)
	{
		if (!(GPLR & TRIZEPS_GPIO_UART1_CTS))
			result |= TIOCM_CTS;
	}
	else if (port->mapbase == _Ser2UTCR0)
	{
		result |= TIOCM_CTS;
	}
	else if (port->mapbase == _Ser3UTCR0)
	{
		result |= TIOCM_CTS;
	}
	else
	{
		result = TIOCM_CTS;
	}

	DPRINTK(" ttySA%d %s%s%s\n",port->line,
		result & TIOCM_CD  ? "CD "  : "",
		result & TIOCM_CTS ? "CTS " : "",
		result & TIOCM_DSR ? "DSR " : "");

	return result;
}

static struct sa1100_port_fns trizeps_port_fns __initdata = {
	.set_mctrl =	trizeps_set_mctrl,
	.get_mctrl =	trizeps_get_mctrl,
};

static void trizeps_power_off(void)
{
	printk("trizeps power off\n");
	mdelay(100);
	cli();
	/* disable internal oscillator, float CS lines */
	PCFR = (PCFR_OPDE | PCFR_FP | PCFR_FS);
	/* enable wake-up on GPIO0 (Assabet...) */
	PWER = GFER = GRER = 1;
	/*
	 * set scratchpad to zero, just in case it is used as a
	 * restart address by the bootloader.
	 */
	PSPR = 0;

	/*
	 *  Power off
	 *  -> disconnect AKku
	 */
	TRIZEPS_BCR_set(TRIZEPS_BCR0, TRIZEPS_MFT_OFF);

	/*
	 * if power supply no Akku
	 * -> enter sleep mode
	 */
	PMCR = PMCR_SF;
}

static int __init trizeps_init(void)
{
	if (!machine_is_trizeps())
		return -EINVAL;

	DPRINTK(" \n");
	pm_power_off = trizeps_power_off;

	// Init UART2 for IrDA
//	PPDR |= PPC_TXD2;           // Set TXD2 as output
	Ser2UTCR4 = UTCR4_HSE;      // enable HSE
	Ser2HSCR0 = 0;
	Ser2HSSR0 = HSSR0_EIF | HSSR0_TUR | HSSR0_RAB | HSSR0_FRE;

	/* Init MECR */
	MECR = 0x00060006;

	/* Set up external serial IRQs */
	GAFR &= ~(GPIO_GPIO16 | GPIO_GPIO17);  // no alternate function
	GPDR &= ~(GPIO_GPIO16 | GPIO_GPIO17);  // Set to Input
	set_irq_type(IRQ_GPIO16, IRQT_RISING);
	set_irq_type(IRQ_GPIO17, IRQT_RISING);

	return 0;
}

__initcall(trizeps_init);

static struct map_desc trizeps_io_desc[] __initdata = {
	/* virtual	physical	length	type */
	{ 0xF0000000l, 0x30000000l, 0x00800000l, MT_DEVICE },
	{ 0xF2000000l, 0x38000000l, 0x00800000l, MT_DEVICE },
};

static void __init trizeps_map_io(void)
{
	sa1100_map_io();
	iotable_init(trizeps_io_desc, ARRAY_SIZE(trizeps_io_desc));

	sa1100_register_uart_fns(&trizeps_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);
}

MACHINE_START(TRIZEPS, "TRIZEPS")
	MAINTAINER("DSA")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(trizeps_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
