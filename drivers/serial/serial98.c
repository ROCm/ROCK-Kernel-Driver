/*
 *  linux/drivers/serial/serial98.c
 *
 *  Driver for NEC PC-9801/PC-9821 standard serial ports
 *
 *  Based on drivers/serial/8250.c, by Russell King.
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2002 Osamu Tomita <tomita@cinet.co.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial_reg.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pc9800.h>
#include <asm/pc9800_sca.h>

#if defined(CONFIG_SERIAL98_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#define SERIAL98_NR		1
#define SERIAL98_ISR_PASS_LIMIT	256
#define SERIAL98_EXT		0x434

//#define RX_8251F		0x130	/* In: Receive buffer */
//#define TX_8251F		0x130	/* Out: Transmit buffer */
//#define LSR_8251F		0x132	/* In: Line Status Register */
//#define MSR_8251F		0x134	/* In: Modem Status Register */
#define IIR_8251F		0x136	/* In: Interrupt ID Register */
#define FCR_8251F		0x138	/* I/O: FIFO Control Register */
#define VFAST_8251F		0x13a	/* I/O: VFAST mode Register */

#define CMD_8251F		0x32	/* Out: 8251 Command Resister */
#define IER2_8251F		0x34	/* I/O: Interrupt Enable Register */
#define IER1_8251F		0x35	/* I/O: Interrupt Enable Register */
#define IER1_CTL		0x37	/* Out: Interrupt Enable Register */
#define DIS_RXR_INT		0x00	/* disable RxRDY Interrupt */
#define ENA_RXR_INT		0x01	/* enable RxRDY Interrupt */
#define DIS_TXE_INT		0x02	/* disable TxEMPTY Interrupt */
#define ENA_TXE_INT		0x03	/* enable TxEMPTY Interrupt */
#define DIS_TXR_INT		0x04	/* disable TxRDY Interrupt */
#define ENA_TXR_INT		0x05	/* enable TxRDY Interrupt */

#define CMD_RESET		0x40	/* Reset Command */
#define CMD_RTS			0x20	/* Set RTS line */
#define CMD_CLR_ERR		0x10	/* Clear error flag */
#define CMD_BREAK		0x08	/* Send Break */
#define CMD_RXE			0x04	/* Enable receive */
#define CMD_DTR			0x02	/* Set DTR line */
#define CMD_TXE			0x01	/* Enable send */
#define CMD_DUMMY		0x00	/* Dummy Command */

#define VFAST_ENABLE		0x80	/* V.Fast mode Enable */

/* Interrupt masks */
#define INTR_8251_TXRE		0x04
#define INTR_8251_TXEE		0x02
#define INTR_8251_RXRE		0x01
/* I/O Port */
//#define PORT_8251_DATA	0
//#define PORT_8251_CMD		2
//#define PORT_8251_MOD		2
//#define PORT_8251_STS		2
/* status read */
#define STAT_8251_TXRDY		0x01
#define STAT_8251_RXRDY		0x02
#define STAT_8251_TXEMP		0x04
#define STAT_8251_PER		0x08
#define STAT_8251_OER		0x10
#define STAT_8251_FER		0x20
#define STAT_8251_BRK		0x40
#define STAT_8251_DSR		0x80
#if 1
#define STAT_8251F_TXEMP	0x01
#define STAT_8251F_TXRDY	0x02
#define STAT_8251F_RXRDY	0x04
#define STAT_8251F_DSR		0x08
#define STAT_8251F_OER		0x10
#define STAT_8251F_PER		0x20
#define STAT_8251F_FER		0x40
#define STAT_8251F_BRK		0x80
#else
#define STAT_8251F_TXEMP	0x01
#define STAT_8251F_TEMT		0x01
#define STAT_8251F_TXRDY	0x02
#define STAT_8251F_THRE		0x02
#define STAT_8251F_RXRDY	0x04
#define STAT_8251F_DSR		0x04
#define STAT_8251F_PER		0x08
#define STAT_8251F_OER		0x10
#define STAT_8251F_FER		0x20
#define STAT_8251F_BRK		0x40
#endif

/*
 * We wrap our port structure around the generic uart_port.
 */
struct serial98_port {
	struct uart_port	port;
	unsigned int		type;
	unsigned int		ext;
	unsigned int		lsr_break_flag;
	unsigned char		cmd;
	unsigned char		mode;
	unsigned char		msr;
	unsigned char		ier;
	unsigned char		rxchk;
	unsigned char		txemp;
	unsigned char		txrdy;
	unsigned char		rxrdy;
	unsigned char		brk;
	unsigned char		fe;
	unsigned char		oe;
	unsigned char		pe;
	unsigned char		dr;
};

#ifdef CONFIG_SERIAL98_CONSOLE
static void
serial98_console_write(struct console *co, const char *s, unsigned int count);
static int __init serial98_console_setup(struct console *co, char *options);

extern struct uart_driver serial98_reg;
static struct console serial98_console = {
	.name		= "ttyS",
	.write		= serial98_console_write,
	.device		= uart_console_device,
	.setup		= serial98_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &serial98_reg,
};

#define SERIAL98_CONSOLE	&serial98_console
#else
#define SERIAL98_CONSOLE	NULL
#endif

static struct uart_driver serial98_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "serial98",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= 64,
	.nr			= SERIAL98_NR,
	.cons			= SERIAL98_CONSOLE,
};

static int serial98_clk;
static char type_str[48];

#define PORT98 ((struct serial98_port *)port)
#define PORT (PORT98->port)

static void serial98_fifo_enable(struct uart_port *port, int enable)
{
	unsigned char fcr;

	if (PORT.type == PORT_FIFO_PC98 || PORT.type == PORT_VFAST_PC98) {
		fcr = inb(FCR_8251F);
		if (enable)
			fcr |= UART_FCR_ENABLE_FIFO;
		else
			fcr &= ~UART_FCR_ENABLE_FIFO;
		outb(fcr, FCR_8251F);
	}

	if (!enable)
		return;

	outb(0, 0x5f);	/* wait */
	outb(0, 0x5f);
	outb(0, 0x5f);
	outb(0, 0x5f);
}

static void serial98_cmd_out(struct uart_port *port, unsigned char cmd)
{
	serial98_fifo_enable(port, 0);
	outb(cmd, CMD_8251F);
	serial98_fifo_enable(port, 1);
}

static void serial98_mode_set(struct uart_port *port)
{
	serial98_cmd_out(port, CMD_DUMMY);
	serial98_cmd_out(port, CMD_DUMMY);
	serial98_cmd_out(port, CMD_DUMMY);
	serial98_cmd_out(port, CMD_RESET);
	serial98_cmd_out(port, PORT98->mode);
}

static unsigned char serial98_msr_in(struct uart_port *port)
{
	unsigned long flags;
	unsigned int ms, st;
	unsigned int tmp;

	spin_lock_irqsave(&PORT.lock, flags);
	if (PORT.type == PORT_FIFO_PC98 || PORT.type == PORT_VFAST_PC98) {
		PORT98->msr = inb(PORT.iobase + 4);
	} else {
		ms = inb(0x33);
		st = inb(0x32);
		tmp = 0;
		if(!(ms & 0x20))
			tmp |= UART_MSR_DCD;
		if(!(ms & 0x80)) {
			tmp |= UART_MSR_RI;
			PORT98->msr |= UART_MSR_RI;
		}
		if(!(ms & 0x40))
			tmp |= UART_MSR_CTS;
		if(st & 0x80)
			tmp |= UART_MSR_DSR;
		PORT98->msr = ((PORT98->msr ^ tmp) >> 4) | tmp;
	}

	spin_unlock_irqrestore(&PORT.lock, flags);
	return PORT98->msr;
}

static void serial98_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	unsigned int ier = inb(IER1_8251F);

	ier &= ~(INTR_8251_TXRE | INTR_8251_TXEE);
	outb(ier, IER1_8251F);
}

static void serial98_start_tx(struct uart_port *port, unsigned int tty_start)
{
	unsigned int ier = inb(IER1_8251F);

	ier |= INTR_8251_TXRE | INTR_8251_TXEE;
	outb(ier, IER1_8251F);
}

static void serial98_stop_rx(struct uart_port *port)
{
	PORT.read_status_mask &= ~PORT98->dr;
	outb(DIS_RXR_INT, IER1_CTL);
}

static void serial98_enable_ms(struct uart_port *port)
{
	outb(PORT98->ier | 0x80, IER2_8251F);
}

static void serial98_rx_chars(struct uart_port *port, int *status,
				struct pt_regs *regs)
{
	struct tty_struct *tty = PORT.info->tty;
	unsigned char ch;
	int max_count = 256;

	do {
		if (unlikely(tty->flip.count >= TTY_FLIPBUF_SIZE)) {
			tty->flip.work.func((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE)
				return; // if TTY_DONT_FLIP is set
		}
		ch = inb(PORT.iobase);
		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		PORT.icount.rx++;

		if (unlikely(*status & (PORT98->brk | PORT98->pe |
				       PORT98->fe | PORT98->oe))) {
			/*
			 * For statistics only
			 */
			if (*status & PORT98->brk) {
				*status &= ~(PORT98->fe | PORT98->pe);
				PORT.icount.brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				if (uart_handle_break(&PORT))
					goto ignore_char;
			} else if (*status & PORT98->pe)
				PORT.icount.parity++;
			else if (*status & PORT98->fe)
				PORT.icount.frame++;
			if (*status & PORT98->oe)
				PORT.icount.overrun++;

			/*
			 * Mask off conditions which should be ingored.
			 */
			*status &= PORT.read_status_mask;

#ifdef CONFIG_SERIAL98_CONSOLE
			if (PORT.line == PORT.cons->index) {
				/* Recover the break flag from console xmit */
				*status |= PORT98->lsr_break_flag;
				PORT98->lsr_break_flag = 0;
			}
#endif
			if (*status & PORT98->brk) {
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			} else if (*status & PORT98->pe)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (*status & PORT98->fe)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&PORT, ch, regs))
			goto ignore_char;
		if ((*status & PORT.ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
		if ((*status & PORT98->oe) &&
		    tty->flip.count < TTY_FLIPBUF_SIZE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character.
			 */
			*tty->flip.flag_buf_ptr = TTY_OVERRUN;
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
	ignore_char:
		*status = inb(PORT.iobase + 2);
	} while ((*status & PORT98->rxchk) && (max_count-- > 0));
	tty_flip_buffer_push(tty);
}

static void serial98_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &PORT.info->xmit;
	int count;

	if (PORT.x_char) {
		outb(PORT.x_char, PORT.iobase);
		PORT.icount.tx++;
		PORT.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&PORT)) {
		serial98_stop_tx(port, 0);
		return;
	}

	count = PORT.fifosize;
	do {
		outb(xmit->buf[xmit->tail], PORT.iobase);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		PORT.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&PORT);

	if (uart_circ_empty(xmit))
		serial98_stop_tx(&PORT, 0);
}

static void serial98_modem_status(struct uart_port *port)
{
	int status;

	status = serial98_msr_in(port);

	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if (status & UART_MSR_TERI)
		PORT.icount.rng++;
	if (status & UART_MSR_DDSR)
		PORT.icount.dsr++;
	if (status & UART_MSR_DDCD)
		uart_handle_dcd_change(&PORT, status & UART_MSR_DCD);
	if (status & UART_MSR_DCTS)
		uart_handle_cts_change(&PORT, status & UART_MSR_CTS);

	wake_up_interruptible(&PORT.info->delta_msr_wait);
}

static void serial98_int(int irq, void *port, struct pt_regs *regs)
{
	unsigned int status;

	spin_lock(&PORT.lock);
	status = inb(PORT.iobase + 2);
	if (status & PORT98->rxrdy) {
		serial98_rx_chars(port, &status, regs);
	}
	serial98_modem_status(port);
	if (status & PORT98->txrdy) {
		serial98_tx_chars(port);
	}
	spin_unlock(&PORT.lock);
}

static unsigned int serial98_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	unsigned int ret = 0;

	spin_lock_irqsave(&PORT.lock, flags);
	if (inb(PORT.iobase + 2) & PORT98->txemp)
			ret = TIOCSER_TEMT;

	spin_unlock_irqrestore(&PORT.lock, flags);
	return ret;
}

static unsigned int serial98_get_mctrl(struct uart_port *port)
{
	unsigned char status;
	unsigned int ret = 0;

	status = serial98_msr_in(port);
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

static void serial98_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	PORT98->cmd &= 0xdd;
	if (mctrl & TIOCM_RTS)
		PORT98->cmd |= CMD_RTS;

	if (mctrl & TIOCM_DTR)
		PORT98->cmd |= CMD_DTR;

	serial98_cmd_out(port, PORT98->cmd);
}

static void serial98_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;

	spin_lock_irqsave(&PORT.lock, flags);
	if (break_state == -1)
		PORT98->cmd |= CMD_BREAK;
	else
		PORT98->cmd &= ~CMD_BREAK;

	serial98_cmd_out(port, PORT98->cmd);
	spin_unlock_irqrestore(&PORT.lock, flags);
}

static int serial98_startup(struct uart_port *port)
{
	int retval;

	if (PORT.type == PORT_8251_PC98) {
		/* Wake up UART */
		PORT98->mode = 0xfc;
		serial98_mode_set(port);
		outb(DIS_RXR_INT, IER1_CTL);
		outb(DIS_TXE_INT, IER1_CTL);
		outb(DIS_TXR_INT, IER1_CTL);
		PORT98->mode = 0;
		serial98_mode_set(port);
	}

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reeanbled in set_termios())
	 */
	if (PORT.type == PORT_FIFO_PC98 || PORT.type == PORT_VFAST_PC98) {
		outb(UART_FCR_ENABLE_FIFO, FCR_8251F);
		outb((UART_FCR_ENABLE_FIFO
			| UART_FCR_CLEAR_RCVR
			| UART_FCR_CLEAR_XMIT), FCR_8251F);
		outb(0, FCR_8251F);
	}

	/* Clear the interrupt registers. */
	inb(0x30);
	inb(0x32);
	if (PORT.type == PORT_FIFO_PC98 || PORT.type == PORT_VFAST_PC98) {
		inb(PORT.iobase);
		inb(PORT.iobase + 2);
		inb(PORT.iobase + 4);
		inb(PORT.iobase + 6);
	}

	/* Allocate the IRQ */
	retval = request_irq(PORT.irq, serial98_int, 0,
				serial98_reg.driver_name, port);
	if (retval)
		return retval;

	/*
	 * Now, initialize the UART
	 */
	PORT98->mode = 0x4e;
	serial98_mode_set(port);
	PORT98->cmd = 0x15;
	serial98_cmd_out(port, PORT98->cmd);
	PORT98->cmd = 0x05;

	/*
	 * Finally, enable interrupts
	 */
	outb(0x00, IER2_8251F);
	outb(ENA_RXR_INT, IER1_CTL);

	/*
	 * And clear the interrupt registers again for luck.
	 */
	inb(0x30);
	inb(0x32);
	if (PORT.type == PORT_FIFO_PC98 || PORT.type == PORT_VFAST_PC98) {
		inb(PORT.iobase);
		inb(PORT.iobase + 2);
		inb(PORT.iobase + 4);
		inb(PORT.iobase + 6);
	}

	return 0;
}

static void serial98_shutdown(struct uart_port *port)
{
	unsigned long flags;

	/*
	 * disable all interrupts
	 */
	spin_lock_irqsave(&PORT.lock, flags);
	if (PORT.type == PORT_VFAST_PC98)
		outb(0, VFAST_8251F);		/* V.FAST mode off */

	/* disnable all modem status interrupt */
	outb(0x80, IER2_8251F);

	/* disnable TX/RX interrupt */
	outb(0x00, IER2_8251F);
	outb(DIS_RXR_INT, IER1_CTL);
	outb(DIS_TXE_INT, IER1_CTL);
	outb(DIS_TXR_INT, IER1_CTL);
	PORT98->ier = 0;

	spin_unlock_irqrestore(&PORT.lock, flags);

	/*
	 * Free the interrupt
	 */
	free_irq(PORT.irq, port);

	/* disable break condition and disable the port */
	serial98_mode_set(port);

	/* disable FIFO's */	
	if (PORT.type == PORT_FIFO_PC98 || PORT.type == PORT_VFAST_PC98) {
		outb((UART_FCR_ENABLE_FIFO
			| UART_FCR_CLEAR_RCVR
			| UART_FCR_CLEAR_XMIT), FCR_8251F);
		outb(0, FCR_8251F);
	}

	inb(PORT.iobase);
}

static void
serial98_set_termios(struct uart_port *port, struct termios *termios,
		       struct termios *old)
{
	unsigned char stopbit, cval, fcr = 0, ier = 0;
	unsigned long flags;
	unsigned int baud, quot;

	stopbit = 0x80;
	switch (termios->c_cflag & CSIZE) {
		case CS5:
			cval = 0x42;
			stopbit = 0xc0;
			break;
		case CS6:
			cval = 0x46;
			break;
		case CS7:
			cval = 0x4a;
			break;
		default:
		case CS8:
			cval = 0x4e;
			break;
	}

	if (termios->c_cflag & CSTOPB)
		cval ^= stopbit;
	if (termios->c_cflag & PARENB)
		cval |= 0x10;
	if (!(termios->c_cflag & PARODD))
		cval |= 0x20;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
	quot = uart_get_divisor(port, baud);

	if (PORT.type == PORT_FIFO_PC98 || PORT.type == PORT_VFAST_PC98) {
		if ((PORT.uartclk / quot) < (2400 * 16))
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
		else
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_8;
	}

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&PORT.lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	PORT.read_status_mask = PORT98->oe | PORT98->txemp | PORT98->dr;
	if (termios->c_iflag & INPCK)
		PORT.read_status_mask |= PORT98->fe | PORT98->pe;

	if (termios->c_iflag & (BRKINT | PARMRK))
		PORT.read_status_mask |= PORT98->brk;
	/*
	 * Characters to ignore
	 */
	PORT.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		PORT.ignore_status_mask |= PORT98->fe | PORT98->pe;

	if (termios->c_iflag & IGNBRK) {
		PORT.ignore_status_mask |= PORT98->brk;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			PORT.ignore_status_mask |= PORT98->oe;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		PORT.ignore_status_mask |= PORT98->dr;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	if (PORT.flags & UPF_HARDPPS_CD)
		ier |= 0x80;	/* enable modem status interrupt */
	if (termios->c_cflag & CRTSCTS) {
		ier |= 0x08;	/* enable CTS interrupt */
		ier |= 0x80;	/* enable modem status interrupt */
	}
	if (!(termios->c_cflag & CLOCAL)) {
		ier |= 0x20;	/* enable CD interrupt */
		ier |= 0x80;	/* enable modem status interrupt */
	}
	PORT98->ier = ier;

	PORT98->mode = cval;
	serial98_mode_set(port);
	if (PORT.type == PORT_VFAST_PC98 && quot <= 48) {
		quot /= 4;
		if (quot < 1)
			quot = 1;
		outb(quot | VFAST_ENABLE, VFAST_8251F);
	} else {
		quot /= 3;
		if (quot < 1)
			quot = 1;
		if (PORT.type == PORT_VFAST_PC98)
			outb(0, VFAST_8251F);		/* V.FAST mode off */
		outb(0xb6, 0x77);
		outb(quot & 0xff, 0x75);		/* LS of divisor */
		outb(quot >> 8, 0x75);			/* MS of divisor */
	}

	if (fcr & UART_FCR_ENABLE_FIFO) {
		outb(UART_FCR_ENABLE_FIFO, FCR_8251F);
		outb(fcr, FCR_8251F);
	}

	/* enable RX/TX */
	PORT98->cmd = 0x15;
	serial98_cmd_out(port, PORT98->cmd);
	PORT98->cmd = 0x05;
	/* enable interrupts */
	outb(0x00, IER2_8251F);
	outb(ENA_RXR_INT, IER1_CTL);
	spin_unlock_irqrestore(&PORT.lock, flags);
}

static const char *serial98_type(struct uart_port *port)
{
	char *p;

	switch (PORT.type) {
		case PORT_8251_PC98:
			p = "PC98 onboard legacy 8251";
			break;
		case PORT_19K_PC98:
			p =  "PC98 onboard max 19200bps";
			break;
		case PORT_FIFO_PC98:
			p = "PC98 onboard with FIFO";
			break;
		case PORT_VFAST_PC98:
			p = "PC98 onboard V.FAST";
			break;
		case PORT_PC9861:
			p = "PC-9861K RS-232C ext. board";
			break;
		case PORT_PC9801_101:
			p = "PC-9801-101 RS-232C ext. board";
			break;
		default:
			return NULL;
	}

	sprintf(type_str, "%s  Clock %dMHz", p, serial98_clk);
	return type_str;
}

/* Release the region(s) being used by 'port' */
static void serial98_release_port(struct uart_port *port)
{
	switch (PORT.type) {
		case PORT_VFAST_PC98:
			release_region(PORT.iobase + 0xa, 1);
		case PORT_FIFO_PC98:
			release_region(PORT.iobase + 8, 1);
			release_region(PORT.iobase + 6, 1);
			release_region(PORT.iobase + 4, 1);
			release_region(PORT.iobase + 2, 1);
			release_region(PORT.iobase, 1);
		case PORT_19K_PC98:
			release_region(SERIAL98_EXT, 1);
			release_region(0x34, 1);
		case PORT_8251_PC98:
			release_region(0x32, 1);
			release_region(0x30, 1);
	}
}

/* Request the region(s) being used by 'port' */
#define REQ_REGION98(base) (request_region((base), 1, serial98_reg.driver_name))
static int serial98_request_region(unsigned int type)
{
	if (!REQ_REGION98(0x30))
		return -EBUSY;
	if (REQ_REGION98(0x32)) {
		if (type == PORT_8251_PC98)
			return 0;
		if (REQ_REGION98(0x34)) {
			if (REQ_REGION98(SERIAL98_EXT)) {
				unsigned long base;

				if (type == PORT_19K_PC98)
					return 0;
				for (base = 0x130; base <= 0x138; base += 2) {
					if (!REQ_REGION98(base)) {
						base -= 2;
						goto err;
					}
				}
				if (type == PORT_FIFO_PC98)
					return 0;
				if (type == PORT_VFAST_PC98) {
					if (REQ_REGION98(0x13a))
						return 0;
				}
				err:
				while (base >= 0x130) {
					release_region(base, 1);
					base -= 2;
				}
				release_region(SERIAL98_EXT, 1);
			}
			release_region(0x34, 1);
		}
		release_region(0x32, 1);
	}
	release_region(0x30, 1);
	return -EBUSY;
}

static int serial98_request_port(struct uart_port *port)
{
	return serial98_request_region(PORT.type);
}

/*
 * Configure/autoconfigure the port.
 */
static void serial98_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		PORT.type = PORT98->type;
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int serial98_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	switch (ser->type) {
		case PORT_VFAST_PC98:
		case PORT_FIFO_PC98:
		case PORT_19K_PC98:
		case PORT_8251_PC98:
		/* not implemented yet
		case PORT_PC9861:
		case PORT_PC9801_101:
		*/
		case PORT_UNKNOWN:
			break;
		default:
			return -EINVAL;
	}
	if (ser->irq < 0 || ser->irq >= NR_IRQS)
		return -EINVAL;
	if (ser->baud_base < 9600)
		return -EINVAL;
	return 0;
}

static struct uart_ops serial98_ops = {
	.tx_empty	= serial98_tx_empty,
	.set_mctrl	= serial98_set_mctrl,
	.get_mctrl	= serial98_get_mctrl,
	.stop_tx	= serial98_stop_tx,
	.start_tx	= serial98_start_tx,
	.stop_rx	= serial98_stop_rx,
	.enable_ms	= serial98_enable_ms,
	.break_ctl	= serial98_break_ctl,
	.startup	= serial98_startup,
	.shutdown	= serial98_shutdown,
	.set_termios	= serial98_set_termios,
	.type		= serial98_type,
	.release_port	= serial98_release_port,
	.request_port	= serial98_request_port,
	.config_port	= serial98_config_port,
	.verify_port	= serial98_verify_port,
};

static struct serial98_port serial98_ports[SERIAL98_NR] = {
	{
		.port =	{
				.iobase		= 0x30,
				.iotype		= SERIAL_IO_PORT,
				.irq		= 4,
				.fifosize	= 1,
				.ops		= &serial98_ops,
				.flags		= ASYNC_BOOT_AUTOCONF,
				.line		= 0,
			},
		.rxchk = STAT_8251_RXRDY,
		.txemp = STAT_8251_TXEMP,
		.txrdy = STAT_8251_TXRDY,
		.rxrdy = STAT_8251_RXRDY,
		.brk = STAT_8251_BRK,
		.fe = STAT_8251_FER,
		.oe = STAT_8251_OER,
		.pe = STAT_8251_PER,
		.dr = STAT_8251_DSR,
	},
};

#ifdef CONFIG_SERIAL98_CONSOLE

#define BOTH_EMPTY (PORT98->txemp | PORT98->txrdy)

/*
 *	Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct uart_port *port)
{
	unsigned int status, tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	do {
		status = inb(PORT.iobase + 2);

		if (status & PORT98->brk)
			PORT98->lsr_break_flag = PORT98->brk;

		if (--tmout == 0)
			break;
		udelay(1);
	} while ((status & BOTH_EMPTY) != BOTH_EMPTY);

	/* Wait up to 1s for flow control if necessary */
	if (PORT.flags & UPF_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout &&
		       ((serial98_msr_in(port) & UART_MSR_CTS) == 0))
			udelay(1);
	}
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void
serial98_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *port = (struct uart_port *)&serial98_ports[co->index];
	unsigned int ier1, ier2;
	int i;

	/*
	 *	First save the UER then disable the interrupts
	 */
	ier1 = inb(IER1_8251F);
	ier2 = inb(IER2_8251F);
	/* disnable all modem status interrupt */
	outb(0x80, IER2_8251F);

	/* disnable TX/RX interrupt */
	outb(0x00, IER2_8251F);
	outb(DIS_RXR_INT, IER1_CTL);
	outb(DIS_TXE_INT, IER1_CTL);
	outb(DIS_TXR_INT, IER1_CTL);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++, s++) {
		wait_for_xmitr(port);

		/*
		 *	Send the character out.
		 *	If a LF, also do CR...
		 */
		outb(*s, PORT.iobase);
		if (*s == 10) {
			wait_for_xmitr(port);
			outb(13, PORT.iobase);
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(port);

	/* restore TX/RX interrupt */
	outb(0x00, IER2_8251F);
	if (ier1 & 0x01)
		outb(ENA_RXR_INT, IER1_CTL);
	if (ier1 & 0x02)
		outb(ENA_TXE_INT, IER1_CTL);
	if (ier1 & 0x04)
		outb(ENA_TXR_INT, IER1_CTL);

	/* restore modem status interrupt */
	outb(ier2, IER2_8251F);
}

static int __init serial98_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= SERIAL98_NR)
		co->index = 0;
	port = &serial98_ports[co->index].port;

	/*
	 * Temporary fix.
	 */
	spin_lock_init(&port->lock);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

void __init serial98_console_init(void)
{
	register_console(&serial98_console);
}

#endif /* CONFIG_SERIAL98_CONSOLE */


static int __init serial98_init(void)
{
	int ret;
	unsigned char iir1, iir2;

	if (PC9800_8MHz_P()) {
		serial98_clk = 8;
		serial98_ports[0].port.uartclk = 374400 * 16;
	} else {
		serial98_clk = 5;
		serial98_ports[0].port.uartclk = 460800 * 16;
	}

	printk(KERN_INFO "serial98: PC-9801 standard serial port driver Version 0.1alpha\n");
	serial98_ports[0].type = PORT_8251_PC98;
	/* Check FIFO exist */
	iir1 = inb(IIR_8251F);
	iir2 = inb(IIR_8251F);
	if ((iir1 & 0x40) != (iir2 & 0x40) && (iir1 & 0x20) == (iir2 & 0x20)) {
		serial98_ports[0].port.iobase = 0x130;
		serial98_ports[0].port.fifosize = 16;
		serial98_ports[0].rxchk = STAT_8251F_DSR;
		serial98_ports[0].txemp = STAT_8251F_TXEMP;
		serial98_ports[0].txrdy = STAT_8251F_TXRDY;
		serial98_ports[0].rxrdy = STAT_8251F_RXRDY;
		serial98_ports[0].brk = STAT_8251F_BRK;
		serial98_ports[0].fe = STAT_8251F_FER;
		serial98_ports[0].oe = STAT_8251F_OER;
		serial98_ports[0].pe = STAT_8251F_PER;
		serial98_ports[0].dr = STAT_8251F_DSR;

		if (*(unsigned char*)__va(PC9821SCA_RSFLAGS) & 0x10)
			serial98_ports[0].type = PORT_VFAST_PC98;
		else {
			outb(serial98_ports[0].ext | 0x40, SERIAL98_EXT);
			serial98_ports[0].port.uartclk *= 4;
			serial98_ports[0].type = PORT_FIFO_PC98;
		}
	} else if ((serial98_ports[0].ext = inb(SERIAL98_EXT)) != 0xff) {
		outb(serial98_ports[0].ext | 0x40, SERIAL98_EXT);
		if (inb(SERIAL98_EXT) == (serial98_ports[0].ext | 0x40)) {
			serial98_ports[0].port.uartclk *= 4;
			serial98_ports[0].type = PORT_19K_PC98;
		} else {
			serial98_ops.enable_ms = NULL;
			outb(serial98_ports[0].ext, SERIAL98_EXT);
		}
	}

	if (serial98_request_region(serial98_ports[0].type))
		return -EBUSY;

	ret = uart_register_driver(&serial98_reg);
	if (ret == 0) {
		int i;

		for (i = 0; i < SERIAL98_NR; i++) {
			uart_add_one_port(&serial98_reg,
					(struct uart_port *)&serial98_ports[i]);
		}
	}

	return ret;
}

static void __exit serial98_exit(void)
{
	int i;

	if (serial98_ports[0].type == PORT_19K_PC98
			|| serial98_ports[0].type == PORT_FIFO_PC98)
		outb(serial98_ports[0].ext, SERIAL98_EXT);

	for (i = 0; i < SERIAL98_NR; i++) {
		uart_remove_one_port(&serial98_reg,
					(struct uart_port *)&serial98_ports[i]);
	}

	uart_unregister_driver(&serial98_reg);
}

module_init(serial98_init);
module_exit(serial98_exit);

MODULE_AUTHOR("Osamu Tomita <tomita@cinet.co.jp>");
MODULE_DESCRIPTION("PC-9801 standard serial port driver Version 0.1alpha");
MODULE_LICENSE("GPL");
