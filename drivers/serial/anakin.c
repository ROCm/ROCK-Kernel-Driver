/*
 *  linux/drivers/char/anakin.c
 *
 *  Based on driver for AMBA serial ports, by ARM Limited,
 *  Deep Blue Solutions Ltd., Linus Torvalds and Theodore Ts'o.
 *
 *  Copyright (C) 2001 Aleph One Ltd. for Acunia N.V.
 *
 *  Copyright (C) 2001 Blue Mug, Inc. for Acunia N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   20-Apr-2001 TTC	Created
 *   05-May-2001 W/TTC	Updated for serial_core.c
 *   27-Jun-2001 jonm	Minor changes; add mctrl support, switch to 
 *   			SA_INTERRUPT. Works reliably now. No longer requires
 *   			changes to the serial_core API.
 *
 *  $Id: anakin.c,v 1.32 2002/07/28 10:03:27 rmk Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <linux/serial_core.h>

#include <asm/arch/serial_reg.h>

#define UART_NR			5

#define SERIAL_ANAKIN_NAME	"ttyAN"
#define SERIAL_ANAKIN_MAJOR	204
#define SERIAL_ANAKIN_MINOR	32

static unsigned int txenable[NR_IRQS];		/* Software interrupt register */

static inline unsigned int
anakin_in(struct uart_port *port, unsigned int offset)
{
	return __raw_readl(port->base + offset);
}

static inline void
anakin_out(struct uart_port *port, unsigned int offset, unsigned int value)
{
	__raw_writel(value, port->base + offset);
}

static void
anakin_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	txenable[port->irq] = 0;
}

static inline void
anakin_transmit_buffer(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	while (!(anakin_in(port, 0x10) & TXEMPTY))
		barrier();
	anakin_out(port, 0x14, xmit->buf[xmit->tail]);
	anakin_out(port, 0x18, anakin_in(port, 0x18) | SENDREQUEST);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE-1);
        port->icount.tx++;

	if (uart_circ_empty(xmit))
		anakin_stop_tx(port, 0); 
}

static inline void
anakin_transmit_x_char(struct uart_port *port)
{
	anakin_out(port, 0x14, port->x_char);
	anakin_out(port, 0x18, anakin_in(port, 0x18) | SENDREQUEST);
	port->icount.tx++;
	port->x_char = 0;
}

static void
anakin_start_tx(struct uart_port *port, unsigned int tty_start)
{
	// is it this... or below
	if (!txenable[port->irq]) {
		txenable[port->irq] = TXENABLE;

		if ((anakin_in(port, 0x10) & TXEMPTY)) {
		    anakin_transmit_buffer(port);
		}
	}
}

static void
anakin_stop_rx(struct uart_port *port)
{
	while (anakin_in(port, 0x10) & RXRELEASE) 
	    anakin_in(port, 0x14);
	anakin_out(port, 0x18, anakin_in(port, 0x18) | BLOCKRX);
}

static void
anakin_enable_ms(struct uart_port *port)
{
}

static inline void
anakin_rx_chars(struct uart_port *port)
{
	unsigned int ch;
	struct tty_struct *tty = port->info->tty;

	if (!(anakin_in(port, 0x10) & RXRELEASE))
		return;

	ch = anakin_in(port, 0x14) & 0xff;

	if (tty->flip.count < TTY_FLIPBUF_SIZE) {
		*tty->flip.char_buf_ptr++ = ch;
		*tty->flip.flag_buf_ptr++ = TTY_NORMAL;
		port->icount.rx++;
		tty->flip.count++;
	} 
	tty_flip_buffer_push(tty);
}

static inline void
anakin_overrun_chars(struct uart_port *port)
{
	unsigned int ch;

	ch = anakin_in(port, 0x14);
	port->icount.overrun++;
}

static inline void
anakin_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	if (port->x_char) {
		anakin_transmit_x_char(port);
		return; 
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		anakin_stop_tx(port, 0);
		return;
	}

	anakin_transmit_buffer(port);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static void
anakin_int(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int status;
	struct uart_port *port = dev_id;

	status = anakin_in(port, 0x1c);

	if (status & RX) 
		anakin_rx_chars(port);

	if (status & OVERRUN) 
		anakin_overrun_chars(port);

	if (txenable[port->irq] && (status & TX)) 
		anakin_tx_chars(port);
}

static unsigned int
anakin_tx_empty(struct uart_port *port)
{
	return anakin_in(port, 0x10) & TXEMPTY ? TIOCSER_TEMT : 0;
}

static unsigned int
anakin_get_mctrl(struct uart_port *port)
{
	unsigned int status = 0;

	status |= (anakin_in(port, 0x10) & CTS ? TIOCM_CTS : 0);
	status |= (anakin_in(port, 0x18) & DCD ? TIOCM_CAR : 0);
	status |= (anakin_in(port, 0x18) & DTR ? TIOCM_DTR : 0);
	status |= (anakin_in(port, 0x18) & RTS ? TIOCM_RTS : 0);
	
	return status;
}

static void
anakin_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned int status;

	status = anakin_in(port, 0x18);

	if (mctrl & TIOCM_RTS) 
		status |= RTS;
	else 
		status &= ~RTS;

	if (mctrl & TIOCM_CAR)
		status |= DCD;
	else 
		status &= ~DCD;

	anakin_out(port, 0x18, status);
}

static void
anakin_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;
	unsigned int status;

	spin_lock_irqsave(&port->lock, flags);
	status = anakin_in(port, 0x20);

	if (break_state == -1)
		status |= SETBREAK;
	else
		status &= ~SETBREAK;

	anakin_out(port, 0x20, status);
	spin_unlock_irqrestore(&port->lock, flags);
}

static int anakin_startup(struct uart_port *port)
{
	int retval;
	unsigned int read,write;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(port->irq, anakin_int, SA_INTERRUPT,
			     "serial_anakin", port);
	if (retval)
		return retval;

	/*
	 * initialise the old status of the modem signals
	 */
	port->old_status = 0;

	/*
	 * Finally, disable IRQ and softIRQs for first byte)
	 */
	txenable[port->irq] = 0;
	read = anakin_in(port, 0x18);
	write = (read & ~(RTS | DTR | BLOCKRX)) | IRQENABLE;
	anakin_out(port, 0x18, write);

	return 0;
}

static void anakin_shutdown(struct uart_port *port)
{
	/*
	 * Free the interrupt
	 */
	free_irq(port->irq, port);

	/*
	 * disable all interrupts, disable the port
	 */
	anakin_out(port, 0x18, anakin_in(port, 0x18) & ~IRQENABLE);
}

static void
anakin_set_termios(struct uart_port *port, struct termios *termios,
		   struct termios *old)
{
	unsigned long flags;
	unsigned int baud, quot;

	/*
	 * We don't support parity, stop bits, or anything other
	 * than 8 bits, so clear these termios flags.
	 */
	termios->c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD | CREAD);
	termios->c_cflag |= CS8;

	/*
	 * We don't appear to support any error conditions either.
	 */
	termios->c_iflag &= ~(INPCK | IGNPAR | IGNBRK | BRKINT);

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
	quot = uart_get_divisor(port, baud);

	spin_lock_irqsave(&port->lock, flags);

	uart_update_timeout(port, termios->c_cflag, baud);

	while (!(anakin_in(port, 0x10) & TXEMPTY))
		barrier();

	anakin_out(port, 0x10, (anakin_in(port, 0x10) & ~PRESCALER)
			| (quot << 3));

	//parity always set to none
	anakin_out(port, 0x18, anakin_in(port, 0x18) & ~PARITY);
	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *anakin_type(struct port *port)
{
	return port->type == PORT_ANAKIN ? "ANAKIN" : NULL;
}

static struct uart_ops anakin_pops = {
	.tx_empty	= anakin_tx_empty,
	.set_mctrl	= anakin_set_mctrl,
	.get_mctrl	= anakin_get_mctrl,
	.stop_tx	= anakin_stop_tx,
	.start_tx	= anakin_start_tx,
	.stop_rx	= anakin_stop_rx,
	.enable_ms	= anakin_enable_ms,
	.break_ctl	= anakin_break_ctl,
	.startup	= anakin_startup,
	.shutdown	= anakin_shutdown,
	.set_termios	= anakin_set_termios,
	.type		= anakin_type,
};

static struct uart_port anakin_ports[UART_NR] = {
	{
		.base		= IO_BASE + UART0,
		.irq		= IRQ_UART0,
		.uartclk	= 3686400,
		.fifosize	= 0,
		.ops		= &anakin_pops,
		.flags		= ASYNC_BOOT_AUTOCONF,
		.line		= 0,
	},
	{
		.base		= IO_BASE + UART1,
		.irq		= IRQ_UART1,
		.uartclk	= 3686400,
		.fifosize	= 0,
		.ops		= &anakin_pops,
		.flags		= ASYNC_BOOT_AUTOCONF,
		.line		= 1,
	},
	{
		.base		= IO_BASE + UART2,
		.irq		= IRQ_UART2,
		.uartclk	= 3686400,
		.fifosize	= 0,
		.ops		= &anakin_pops,
		.flags		= ASYNC_BOOT_AUTOCONF,
		.line		= 2,
	},
	{
		.base		= IO_BASE + UART3,
		.irq		= IRQ_UART3,
		.uartclk	= 3686400,
		.fifosize	= 0,
		.ops		= &anakin_pops,
		.flags		= ASYNC_BOOT_AUTOCONF,
		.line		= 3,
	},
	{
		.base		= IO_BASE + UART4,
		.irq		= IRQ_UART4,
		.uartclk	= 3686400,
		.fifosize	= 0,
		.ops		= &anakin_pops,
		.flags		= ASYNC_BOOT_AUTOCONF,
		.line		= 4,
	},
};


#ifdef CONFIG_SERIAL_ANAKIN_CONSOLE

static void
anakin_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *port = &anakin_ports[co->index];
	unsigned int flags, status, i;

	/*
	 *	First save the status then disable the interrupts
	 */
	local_irq_save(flags);
	status = anakin_in(port, 0x18);
	anakin_out(port, 0x18, status & ~IRQENABLE);
	local_irq_restore(flags);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++, s++) {
		while (!(anakin_in(port, 0x10) & TXEMPTY))
			barrier();

		/*
		 *	Send the character out.
		 *	If a LF, also do CR...
		 */
		anakin_out(port, 0x14, *s);
		anakin_out(port, 0x18, anakin_in(port, 0x18) | SENDREQUEST);

		if (*s == 10) {
			while (!(anakin_in(port, 0x10) & TXEMPTY))
				barrier();
			anakin_out(port, 0x14, 13);
			anakin_out(port, 0x18, anakin_in(port, 0x18)
					| SENDREQUEST);
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the interrupts
	 */
	while (!(anakin_in(port, 0x10) & TXEMPTY))
		barrier();

	if (status & IRQENABLE) {
		local_irq_save(flags);
 		anakin_out(port, 0x18, anakin_in(port, 0x18) | IRQENABLE);
		local_irq_restore(flags);
	}
}

/*
 * Read the current UART setup.
 */
static void __init
anakin_console_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
	int paritycode;

	*baud = GETBAUD (anakin_in(port, 0x10) & PRESCALER);
	paritycode = GETPARITY(anakin_in(port, 0x18) & PARITY);
	switch (paritycode) {
	  case NONEPARITY: *parity = 'n'; break;
	  case ODDPARITY: *parity = 'o'; break;
	  case EVENPARITY: *parity = 'e'; break;
	}
	*bits = 8;
}

static int __init
anakin_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = CONFIG_ANAKIN_DEFAULT_BAUDRATE;
	int bits = 8;
	int parity = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR)
		co->index = 0;
	port = &anakin_ports[co->index];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits);
	else
		anakin_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits);
}

extern struct uart_driver anakin_reg;
static struct console anakin_console = {
	.name		= SERIAL_ANAKIN_NAME,
	.write		= anakin_console_write,
	.device		= uart_console_device,
	.setup		= anakin_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

static int __init anakin_console_init(void)
{
	register_console(&anakin_console);
	return 0;
}
console_initcall(anakin_console_init);

#define ANAKIN_CONSOLE		&anakin_console
#else
#define ANAKIN_CONSOLE		NULL
#endif

static struct uart_driver anakin_reg = {
	.driver_name		= SERIAL_ANAKIN_NAME,
	.dev_name		= SERIAL_ANAKIN_NAME,
	.major			= SERIAL_ANAKIN_MAJOR,
	.minor			= SERIAL_ANAKIN_MINOR,
	.nr			= UART_NR,
	.cons			= ANAKIN_CONSOLE,
};

static int __init
anakin_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: Anakin driver $Revision: 1.32 $\n");

	ret = uart_register_driver(&anakin_reg);
	if (ret == 0) {
		int i;

		for (i = 0; i < UART_NR; i++)
			uart_add_one_port(&anakin_reg, &anakin_ports[i]);
	}
	return ret;
}

__initcall(anakin_init);

MODULE_DESCRIPTION("Anakin serial driver");
MODULE_AUTHOR("Tak-Shing Chan <chan@aleph1.co.uk>");
MODULE_SUPPORTED_DEVICE("ttyAN");
MODULE_LICENSE("GPL");
