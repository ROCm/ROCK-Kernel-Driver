/*
 *  linux/drivers/char/amba.c
 *
 *  Driver for AMBA serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
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
 *  $Id: amba.c,v 1.41 2002/07/28 10:03:27 rmk Exp $
 *
 * This is a generic driver for ARM AMBA-type serial ports.  They
 * have a lot of 16550-like features, but are not register compatible.
 * Note that although they do have CTS, DCD and DSR inputs, they do
 * not have an RI input, nor do they have DTR or RTS outputs.  If
 * required, these have to be supplied via some other means (eg, GPIO)
 * and hooked into this driver.
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

#if defined(CONFIG_SERIAL_AMBA_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#include <asm/hardware/serial_amba.h>

#define UART_NR		2

#define SERIAL_AMBA_MAJOR	204
#define SERIAL_AMBA_MINOR	16
#define SERIAL_AMBA_NR		UART_NR

#define AMBA_ISR_PASS_LIMIT	256

/*
 * Access macros for the AMBA UARTs
 */
#define UART_GET_INT_STATUS(p)	readb((p)->membase + AMBA_UARTIIR)
#define UART_PUT_ICR(p, c)	writel((c), (p)->membase + AMBA_UARTICR)
#define UART_GET_FR(p)		readb((p)->membase + AMBA_UARTFR)
#define UART_GET_CHAR(p)	readb((p)->membase + AMBA_UARTDR)
#define UART_PUT_CHAR(p, c)	writel((c), (p)->membase + AMBA_UARTDR)
#define UART_GET_RSR(p)		readb((p)->membase + AMBA_UARTRSR)
#define UART_GET_CR(p)		readb((p)->membase + AMBA_UARTCR)
#define UART_PUT_CR(p,c)	writel((c), (p)->membase + AMBA_UARTCR)
#define UART_GET_LCRL(p)	readb((p)->membase + AMBA_UARTLCR_L)
#define UART_PUT_LCRL(p,c)	writel((c), (p)->membase + AMBA_UARTLCR_L)
#define UART_GET_LCRM(p)	readb((p)->membase + AMBA_UARTLCR_M)
#define UART_PUT_LCRM(p,c)	writel((c), (p)->membase + AMBA_UARTLCR_M)
#define UART_GET_LCRH(p)	readb((p)->membase + AMBA_UARTLCR_H)
#define UART_PUT_LCRH(p,c)	writel((c), (p)->membase + AMBA_UARTLCR_H)
#define UART_RX_DATA(s)		(((s) & AMBA_UARTFR_RXFE) == 0)
#define UART_TX_READY(s)	(((s) & AMBA_UARTFR_TXFF) == 0)
#define UART_TX_EMPTY(p)	((UART_GET_FR(p) & AMBA_UARTFR_TMSK) == 0)

#define UART_DUMMY_RSR_RX	256
#define UART_PORT_SIZE		64

/*
 * On the Integrator platform, the port RTS and DTR are provided by
 * bits in the following SC_CTRLS register bits:
 *        RTS  DTR
 *  UART0  7    6
 *  UART1  5    4
 */
#define SC_CTRLC	(IO_ADDRESS(INTEGRATOR_SC_BASE) + INTEGRATOR_SC_CTRLC_OFFSET)
#define SC_CTRLS	(IO_ADDRESS(INTEGRATOR_SC_BASE) + INTEGRATOR_SC_CTRLS_OFFSET)

/*
 * We wrap our port structure around the generic uart_port.
 */
struct uart_amba_port {
	struct uart_port	port;
	unsigned int		dtr_mask;
	unsigned int		rts_mask;
	unsigned int		old_status;
};

static void ambauart_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	unsigned int cr;

	cr = UART_GET_CR(port);
	cr &= ~AMBA_UARTCR_TIE;
	UART_PUT_CR(port, cr);
}

static void ambauart_start_tx(struct uart_port *port, unsigned int tty_start)
{
	unsigned int cr;

	cr = UART_GET_CR(port);
	cr |= AMBA_UARTCR_TIE;
	UART_PUT_CR(port, cr);
}

static void ambauart_stop_rx(struct uart_port *port)
{
	unsigned int cr;

	cr = UART_GET_CR(port);
	cr &= ~(AMBA_UARTCR_RIE | AMBA_UARTCR_RTIE);
	UART_PUT_CR(port, cr);
}

static void ambauart_enable_ms(struct uart_port *port)
{
	unsigned int cr;

	cr = UART_GET_CR(port);
	cr |= AMBA_UARTCR_MSIE;
	UART_PUT_CR(port, cr);
}

static void
#ifdef SUPPORT_SYSRQ
ambauart_rx_chars(struct uart_port *port, struct pt_regs *regs)
#else
ambauart_rx_chars(struct uart_port *port)
#endif
{
	struct tty_struct *tty = port->info->tty;
	unsigned int status, ch, rsr, max_count = 256;

	status = UART_GET_FR(port);
	while (UART_RX_DATA(status) && max_count--) {
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			tty->flip.work.func((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
				printk(KERN_WARNING "TTY_DONT_FLIP set\n");
				return;
			}
		}

		ch = UART_GET_CHAR(port);

		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		port->icount.rx++;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		rsr = UART_GET_RSR(port) | UART_DUMMY_RSR_RX;
		if (rsr & AMBA_UARTRSR_ANY) {
			if (rsr & AMBA_UARTRSR_BE) {
				rsr &= ~(AMBA_UARTRSR_FE | AMBA_UARTRSR_PE);
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			} else if (rsr & AMBA_UARTRSR_PE)
				port->icount.parity++;
			else if (rsr & AMBA_UARTRSR_FE)
				port->icount.frame++;
			if (rsr & AMBA_UARTRSR_OE)
				port->icount.overrun++;

			rsr &= port->read_status_mask;

			if (rsr & AMBA_UARTRSR_BE)
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			else if (rsr & AMBA_UARTRSR_PE)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (rsr & AMBA_UARTRSR_FE)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch, regs))
			goto ignore_char;

		if ((rsr & port->ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
		if ((rsr & AMBA_UARTRSR_OE) &&
		    tty->flip.count < TTY_FLIPBUF_SIZE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character
			 */
			*tty->flip.char_buf_ptr++ = 0;
			*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
			tty->flip.count++;
		}
	ignore_char:
		status = UART_GET_FR(port);
	}
	tty_flip_buffer_push(tty);
	return;
}

static void ambauart_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;
	int count;

	if (port->x_char) {
		UART_PUT_CHAR(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		ambauart_stop_tx(port, 0);
		return;
	}

	count = port->fifosize >> 1;
	do {
		UART_PUT_CHAR(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		ambauart_stop_tx(port, 0);
}

static void ambauart_modem_status(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int status, delta;

	UART_PUT_ICR(&uap->port, 0);

	status = UART_GET_FR(&uap->port) & AMBA_UARTFR_MODEM_ANY;

	delta = status ^ uap->old_status;
	uap->old_status = status;

	if (!delta)
		return;

	if (delta & AMBA_UARTFR_DCD)
		uart_handle_dcd_change(&uap->port, status & AMBA_UARTFR_DCD);

	if (delta & AMBA_UARTFR_DSR)
		uap->port.icount.dsr++;

	if (delta & AMBA_UARTFR_CTS)
		uart_handle_cts_change(&uap->port, status & AMBA_UARTFR_CTS);

	wake_up_interruptible(&uap->port.info->delta_msr_wait);
}

static irqreturn_t ambauart_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = dev_id;
	unsigned int status, pass_counter = AMBA_ISR_PASS_LIMIT;

	status = UART_GET_INT_STATUS(port);
	do {
		if (status & (AMBA_UARTIIR_RTIS | AMBA_UARTIIR_RIS))
#ifdef SUPPORT_SYSRQ
			ambauart_rx_chars(port, regs);
#else
			ambauart_rx_chars(port);
#endif
		if (status & AMBA_UARTIIR_MIS)
			ambauart_modem_status(port);
		if (status & AMBA_UARTIIR_TIS)
			ambauart_tx_chars(port);

		if (pass_counter-- == 0)
			break;

		status = UART_GET_INT_STATUS(port);
	} while (status & (AMBA_UARTIIR_RTIS | AMBA_UARTIIR_RIS |
			   AMBA_UARTIIR_TIS));

	return IRQ_HANDLED;
}

static unsigned int ambauart_tx_empty(struct uart_port *port)
{
	return UART_GET_FR(port) & AMBA_UARTFR_BUSY ? 0 : TIOCSER_TEMT;
}

static unsigned int ambauart_get_mctrl(struct uart_port *port)
{
	unsigned int result = 0;
	unsigned int status;

	status = UART_GET_FR(port);
	if (status & AMBA_UARTFR_DCD)
		result |= TIOCM_CAR;
	if (status & AMBA_UARTFR_DSR)
		result |= TIOCM_DSR;
	if (status & AMBA_UARTFR_CTS)
		result |= TIOCM_CTS;

	return result;
}

static void ambauart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int ctrls = 0, ctrlc = 0;

	if (mctrl & TIOCM_RTS)
		ctrlc |= uap->rts_mask;
	else
		ctrls |= uap->rts_mask;

	if (mctrl & TIOCM_DTR)
		ctrlc |= uap->dtr_mask;
	else
		ctrls |= uap->dtr_mask;

	__raw_writel(ctrls, SC_CTRLS);
	__raw_writel(ctrlc, SC_CTRLC);
}

static void ambauart_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;
	unsigned int lcr_h;

	spin_lock_irqsave(&port->lock, flags);
	lcr_h = UART_GET_LCRH(port);
	if (break_state == -1)
		lcr_h |= AMBA_UARTLCR_H_BRK;
	else
		lcr_h &= ~AMBA_UARTLCR_H_BRK;
	UART_PUT_LCRH(port, lcr_h);
	spin_unlock_irqrestore(&port->lock, flags);
}

static int ambauart_startup(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	int retval;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(port->irq, ambauart_int, 0, "amba", port);
	if (retval)
		return retval;

	/*
	 * initialise the old status of the modem signals
	 */
	uap->old_status = UART_GET_FR(port) & AMBA_UARTFR_MODEM_ANY;

	/*
	 * Finally, enable interrupts
	 */
	UART_PUT_CR(port, AMBA_UARTCR_UARTEN | AMBA_UARTCR_RIE |
			  AMBA_UARTCR_RTIE);

	return 0;
}

static void ambauart_shutdown(struct uart_port *port)
{
	/*
	 * Free the interrupt
	 */
	free_irq(port->irq, port);

	/*
	 * disable all interrupts, disable the port
	 */
	UART_PUT_CR(port, 0);

	/* disable break condition and fifos */
	UART_PUT_LCRH(port, UART_GET_LCRH(port) &
		~(AMBA_UARTLCR_H_BRK | AMBA_UARTLCR_H_FEN));
}

static void
ambauart_set_termios(struct uart_port *port, struct termios *termios,
		     struct termios *old)
{
	unsigned int lcr_h, old_cr;
	unsigned long flags;
	unsigned int baud, quot;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
	quot = uart_get_divisor(port, baud);

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr_h = AMBA_UARTLCR_H_WLEN_5;
		break;
	case CS6:
		lcr_h = AMBA_UARTLCR_H_WLEN_6;
		break;
	case CS7:
		lcr_h = AMBA_UARTLCR_H_WLEN_7;
		break;
	default: // CS8
		lcr_h = AMBA_UARTLCR_H_WLEN_8;
		break;
	}
	if (termios->c_cflag & CSTOPB)
		lcr_h |= AMBA_UARTLCR_H_STP2;
	if (termios->c_cflag & PARENB) {
		lcr_h |= AMBA_UARTLCR_H_PEN;
		if (!(termios->c_cflag & PARODD))
			lcr_h |= AMBA_UARTLCR_H_EPS;
	}
	if (port->fifosize > 1)
		lcr_h |= AMBA_UARTLCR_H_FEN;

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = AMBA_UARTRSR_OE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= AMBA_UARTRSR_FE | AMBA_UARTRSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= AMBA_UARTRSR_BE;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= AMBA_UARTRSR_FE | AMBA_UARTRSR_PE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= AMBA_UARTRSR_BE;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= AMBA_UARTRSR_OE;
	}

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= UART_DUMMY_RSR_RX;

	/* first, disable everything */
	old_cr = UART_GET_CR(port) & ~AMBA_UARTCR_MSIE;

	if (UART_ENABLE_MS(port, termios->c_cflag))
		old_cr |= AMBA_UARTCR_MSIE;

	UART_PUT_CR(port, 0);

	/* Set baud rate */
	quot -= 1;
	UART_PUT_LCRM(port, ((quot & 0xf00) >> 8));
	UART_PUT_LCRL(port, (quot & 0xff));

	/*
	 * ----------v----------v----------v----------v-----
	 * NOTE: MUST BE WRITTEN AFTER UARTLCR_M & UARTLCR_L
	 * ----------^----------^----------^----------^-----
	 */
	UART_PUT_LCRH(port, lcr_h);
	UART_PUT_CR(port, old_cr);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *ambauart_type(struct uart_port *port)
{
	return port->type == PORT_AMBA ? "AMBA" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void ambauart_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, UART_PORT_SIZE);
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int ambauart_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, UART_PORT_SIZE, "serial_amba")
			!= NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void ambauart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_AMBA;
		ambauart_request_port(port);
	}
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int ambauart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_AMBA)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= NR_IRQS)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops amba_pops = {
	.tx_empty	= ambauart_tx_empty,
	.set_mctrl	= ambauart_set_mctrl,
	.get_mctrl	= ambauart_get_mctrl,
	.stop_tx	= ambauart_stop_tx,
	.start_tx	= ambauart_start_tx,
	.stop_rx	= ambauart_stop_rx,
	.enable_ms	= ambauart_enable_ms,
	.break_ctl	= ambauart_break_ctl,
	.startup	= ambauart_startup,
	.shutdown	= ambauart_shutdown,
	.set_termios	= ambauart_set_termios,
	.type		= ambauart_type,
	.release_port	= ambauart_release_port,
	.request_port	= ambauart_request_port,
	.config_port	= ambauart_config_port,
	.verify_port	= ambauart_verify_port,
};

static struct uart_amba_port amba_ports[UART_NR] = {
	{
		.port	= {
			.membase	= (void *)IO_ADDRESS(INTEGRATOR_UART0_BASE),
			.mapbase	= INTEGRATOR_UART0_BASE,
			.iotype		= SERIAL_IO_MEM,
			.irq		= IRQ_UARTINT0,
			.uartclk	= 14745600,
			.fifosize	= 16,
			.ops		= &amba_pops,
			.flags		= ASYNC_BOOT_AUTOCONF,
			.line		= 0,
		},
		.dtr_mask	= 1 << 5,
		.rts_mask	= 1 << 4,
	},
	{
		.port	= {
			.membase	= (void *)IO_ADDRESS(INTEGRATOR_UART1_BASE),
			.mapbase	= INTEGRATOR_UART1_BASE,
			.iotype		= SERIAL_IO_MEM,
			.irq		= IRQ_UARTINT1,
			.uartclk	= 14745600,
			.fifosize	= 16,
			.ops		= &amba_pops,
			.flags		= ASYNC_BOOT_AUTOCONF,
			.line		= 1,
		},
		.dtr_mask	= 1 << 7,
		.rts_mask	= 1 << 6,
	}
};

#ifdef CONFIG_SERIAL_AMBA_CONSOLE

static void
ambauart_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *port = &amba_ports[co->index].port;
	unsigned int status, old_cr;
	int i;

	/*
	 *	First save the CR then disable the interrupts
	 */
	old_cr = UART_GET_CR(port);
	UART_PUT_CR(port, AMBA_UARTCR_UARTEN);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++) {
		do {
			status = UART_GET_FR(port);
		} while (!UART_TX_READY(status));
		UART_PUT_CHAR(port, s[i]);
		if (s[i] == '\n') {
			do {
				status = UART_GET_FR(port);
			} while (!UART_TX_READY(status));
			UART_PUT_CHAR(port, '\r');
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the TCR
	 */
	do {
		status = UART_GET_FR(port);
	} while (status & AMBA_UARTFR_BUSY);
	UART_PUT_CR(port, old_cr);
}

static void __init
ambauart_console_get_options(struct uart_port *port, int *baud,
			     int *parity, int *bits)
{
	if (UART_GET_CR(port) & AMBA_UARTCR_UARTEN) {
		unsigned int lcr_h, quot;
		lcr_h = UART_GET_LCRH(port);

		*parity = 'n';
		if (lcr_h & AMBA_UARTLCR_H_PEN) {
			if (lcr_h & AMBA_UARTLCR_H_EPS)
				*parity = 'e';
			else
				*parity = 'o';
		}

		if ((lcr_h & 0x60) == AMBA_UARTLCR_H_WLEN_7)
			*bits = 7;
		else
			*bits = 8;

		quot = UART_GET_LCRL(port) | UART_GET_LCRM(port) << 8;
		*baud = port->uartclk / (16 * (quot + 1));
	}
}

static int __init ambauart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR)
		co->index = 0;
	port = &amba_ports[co->index].port;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		ambauart_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

extern struct uart_driver amba_reg;
static struct console amba_console = {
	.name		= "ttyAM",
	.write		= ambauart_console_write,
	.device		= uart_console_device,
	.setup		= ambauart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &amba_reg,
};

static int __init ambauart_console_init(void)
{
	register_console(&amba_console);
	return 0;
}
console_initcall(ambauart_console_init);

#define AMBA_CONSOLE	&amba_console
#else
#define AMBA_CONSOLE	NULL
#endif

static struct uart_driver amba_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "ttyAM",
	.dev_name		= "ttyAM",
	.major			= SERIAL_AMBA_MAJOR,
	.minor			= SERIAL_AMBA_MINOR,
	.nr			= UART_NR,
	.cons			= AMBA_CONSOLE,
};

static int __init ambauart_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: AMBA driver $Revision: 1.41 $\n");

	ret = uart_register_driver(&amba_reg);
	if (ret == 0) {
		int i;

		for (i = 0; i < UART_NR; i++)
			uart_add_one_port(&amba_reg, &amba_ports[i].port);
	}
	return ret;
}

static void __exit ambauart_exit(void)
{
	int i;

	for (i = 0; i < UART_NR; i++)
		uart_remove_one_port(&amba_reg, &amba_ports[i].port);

	uart_unregister_driver(&amba_reg);
}

module_init(ambauart_init);
module_exit(ambauart_exit);

MODULE_AUTHOR("ARM Ltd/Deep Blue Solutions Ltd");
MODULE_DESCRIPTION("ARM AMBA serial port driver $Revision: 1.41 $");
MODULE_LICENSE("GPL");
