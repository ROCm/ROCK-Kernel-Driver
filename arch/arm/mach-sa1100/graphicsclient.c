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

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <linux/serial_core.h>

#include <asm/arch/irq.h>

#include "generic.h"


/*
 * Handlers for GraphicsClient's external IRQ logic
 */

static void ADS_IRQ_demux( int irq, void *dev_id, struct pt_regs *regs )
{
	int i;

	while( (irq = ADS_INT_ST1 | (ADS_INT_ST2 << 8)) ){
		for( i = 0; i < 16; i++ )
			if( irq & (1<<i) )
				do_IRQ( ADS_EXT_IRQ(i), regs );
	}
}

static struct irqaction ADS_ext_irq = {
	name:		"ADS_ext_IRQ",
	handler:	ADS_IRQ_demux,
	flags:		SA_INTERRUPT
};

static void ADS_mask_and_ack_irq0(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(0)));
	ADS_INT_EN1 &= ~mask;
	ADS_INT_ST1 = mask;
}

static void ADS_mask_irq0(unsigned int irq)
{
	ADS_INT_ST1 = (1 << (irq - ADS_EXT_IRQ(0)));
}

static void ADS_unmask_irq0(unsigned int irq)
{
	ADS_INT_EN1 |= (1 << (irq - ADS_EXT_IRQ(0)));
}

static void ADS_mask_and_ack_irq1(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(8)));
	ADS_INT_EN2 &= ~mask;
	ADS_INT_ST2 = mask;
}

static void ADS_mask_irq1(unsigned int irq)
{
	ADS_INT_ST2 = (1 << (irq - ADS_EXT_IRQ(8)));
}

static void ADS_unmask_irq1(unsigned int irq)
{
	ADS_INT_EN2 |= (1 << (irq - ADS_EXT_IRQ(8)));
}

static void __init graphicsclient_init_irq(void)
{
	int irq;

	/* First the standard SA1100 IRQs */
	sa1100_init_irq();

	/* disable all IRQs */
	ADS_INT_EN1 = 0;
	ADS_INT_EN2 = 0;
	/* clear all IRQs */
	ADS_INT_ST1 = 0xff;
	ADS_INT_ST2 = 0xff;

	for (irq = ADS_EXT_IRQ(0); irq <= ADS_EXT_IRQ(7); irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= ADS_mask_and_ack_irq0;
		irq_desc[irq].mask	= ADS_mask_irq0;
		irq_desc[irq].unmask	= ADS_unmask_irq0;
	}
	for (irq = ADS_EXT_IRQ(8); irq <= ADS_EXT_IRQ(15); irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= ADS_mask_and_ack_irq1;
		irq_desc[irq].mask	= ADS_mask_irq1;
		irq_desc[irq].unmask	= ADS_unmask_irq1;
	}
	GPDR &= ~GPIO_GPIO0;
	set_GPIO_IRQ_edge(GPIO_GPIO0, GPIO_FALLING_EDGE);
	setup_arm_irq( IRQ_GPIO0, &ADS_ext_irq );
}


/*
 * Initialization fixup
 */

static void __init
fixup_graphicsclient(struct machine_desc *desc, struct param_struct *params,
		     char **cmdline, struct meminfo *mi)
{
	SET_BANK( 0, 0xc0000000, 16*1024*1024 );
	SET_BANK( 1, 0xc8000000, 16*1024*1024 );
	mi->nr_banks = 2;

	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( __phys_to_virt(0xc0800000), 4*1024*1024 );
}

static struct map_desc graphicsclient_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x08000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 */
  { 0xf0000000, 0x10000000, 0x00400000, DOMAIN_IO, 0, 1, 0, 0 }, /* CPLD */
  { 0xf1000000, 0x18000000, 0x00400000, DOMAIN_IO, 0, 1, 0, 0 }, /* CAN */
  LAST_DESC
};

static struct gc_uart_ctrl_data_t gc_uart_ctrl_data[] = {
  { GPIO_GC_UART0_CTS, 0, NULL,NULL },
  { GPIO_GC_UART1_CTS, 0, NULL,NULL },
  { GPIO_GC_UART2_CTS, 0, NULL,NULL }
};

static void
graphicsclient_cts_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct gc_uart_ctrl_data_t * uart_data = (struct gc_uart_ctrl_data_t *)dev_id;
	int cts = !(GPLR & uart_data->cts_gpio);

	/* NOTE: I supose that we will no get any interrupt
	   if the GPIO is not changed, so maybe
	   the cts_prev_state can be removed ... */
	if (cts != uart_data->cts_prev_state) {
		uart_data->cts_prev_state = cts;

		uart_handle_cts_change(uart_data->info, cts);
	}
}

static int
graphicsclient_register_cts_intr(int gpio, int irq,
				 struct gc_uart_ctrl_data_t *uart_data)
{
	int ret = 0;

	set_GPIO_IRQ_edge(gpio, GPIO_BOTH_EDGES);

	ret = request_irq(irq, graphicsclient_cts_intr,
			  0, "GC RS232 CTS", uart_data);
	if (ret) {
		printk(KERN_ERR "uart_open: failed to register CTS irq (%d)\n",
		       ret);
		free_irq(irq, uart_data);
	}

	return ret;
}

static int
graphicsclient_uart_open(struct uart_port *port, struct uart_info *info)
{
	int ret = 0;

	if (port->mapbase == _Ser1UTCR0) {
		Ser1SDCR0 |= SDCR0_UART;
		/* Set RTS Output */
		GPDR |= GPIO_GC_UART0_RTS;
		GPSR  = GPIO_GC_UART0_RTS;

		/* Set CTS Input */
		GPDR &= ~GPIO_GC_UART0_CTS;

		gc_uart_ctrl_data[0].cts_prev_state = 0;
		gc_uart_ctrl_data[0].info = info;
		gc_uart_ctrl_data[0].port = port;

		/* register uart0 CTS irq */
		ret = graphicsclient_register_cts_intr(GPIO_GC_UART0_CTS,
							IRQ_GC_UART0_CTS,
							&gc_uart_ctrl_data[0]);
	} else if (port->mapbase == _Ser2UTCR0) {
		Ser2UTCR4 = Ser2HSCR0 = 0;
		/* Set RTS Output */
		GPDR |= GPIO_GC_UART1_RTS;
		GPSR  = GPIO_GC_UART1_RTS;

		/* Set CTS Input */
		GPDR &= ~GPIO_GC_UART1_RTS;

		gc_uart_ctrl_data[1].cts_prev_state = 0;
		gc_uart_ctrl_data[1].info = info;
		gc_uart_ctrl_data[1].port = port;

		/* register uart1 CTS irq */
		ret = graphicsclient_register_cts_intr(GPIO_GC_UART1_CTS,
							IRQ_GC_UART1_CTS,
							&gc_uart_ctrl_data[1]);
	} else if (port->mapbase == _Ser3UTCR0) {
		/* Set RTS Output */
		GPDR |= GPIO_GC_UART2_RTS;
		GPSR =	GPIO_GC_UART2_RTS;

		/* Set CTS Input */
		GPDR &= ~GPIO_GC_UART2_RTS;

		gc_uart_ctrl_data[2].cts_prev_state = 0;
		gc_uart_ctrl_data[2].info = info;
		gc_uart_ctrl_data[2].port = port;

		/* register uart2 CTS irq */
		ret = graphicsclient_register_cts_intr(GPIO_GC_UART2_CTS,
							IRQ_GC_UART2_CTS,
							&gc_uart_ctrl_data[2]);
	}
	return ret;
}

static int
graphicsclient_uart_close(struct uart_port *port, struct uart_info *info)
{
	if (port->mapbase == _Ser1UTCR0) {
		free_irq(IRQ_GC_UART0_CTS, &gc_uart_ctrl_data[0]);
	} else if (port->mapbase == _Ser2UTCR0) {
		free_irq(IRQ_GC_UART1_CTS, &gc_uart_ctrl_data[1]);
	} else if (port->mapbase == _Ser3UTCR0) {
		free_irq(IRQ_GC_UART2_CTS, &gc_uart_ctrl_data[2]);
	}

	return 0;
}

static int graphicsclient_get_mctrl(struct uart_port *port)
{
	int result = TIOCM_CD | TIOCM_DSR;

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
	open:		graphicsclient_uart_open,
	close:		graphicsclient_uart_close,
	get_mctrl:	graphicsclient_get_mctrl,
	set_mctrl:	graphicsclient_set_mctrl,
	pm:		graphicsclient_uart_pm,
};

static void __init graphicsclient_map_io(void)
{
	sa1100_map_io();
	iotable_init(graphicsclient_io_desc);

	sa1100_register_uart_fns(&graphicsclient_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);
}

MACHINE_START(GRAPHICSCLIENT, "ADS GraphicsClient")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_graphicsclient)
	MAPIO(graphicsclient_map_io)
	INITIRQ(graphicsclient_init_irq)
MACHINE_END
