/*
 * sunzilog.c
 *
 * Driver for Zilog serial chips found on Sun workstations and
 * servers.  This driver could actually be made more generic
 * and anyone wanting to work on doing that should contact
 * me. -DaveM
 *
 * This is based on the old drivers/sbus/char/zs.c code, a lot
 * of code has been simply moved over directly from there but
 * much has been rewritten.  Credits therefore go out to Eddie
 * C. Dost, Peter Zaitcev, Ted Ts'o and Alex Buell for their
 * work there.
 *
 *  Copyright (C) 2002 David S. Miller (davem@redhat.com)
 */

#include "sun.h"
#include "sunzilog.h"

/* On 32-bit sparcs we need to delay after register accesses
 * to accomodate sun4 systems, but we do not need to flush writes.
 * On 64-bit sparc we only need to flush single writes to ensure
 * completion.
 */
#ifndef __sparc_v9__
#define ZSDELAY()		udelay(5)
#define ZSDELAY_LONG()		udelay(20)
#define ZS_WSYNC(channel)	do { } while(0)
#else
#define ZSDELAY()
#define ZSDELAY_LONG()
#define ZS_WSYNC(__channel) \
	sbus_readb(&((__channel)->control))
#endif

/* Default setting is sun4/sun4c/sun4m, two chips on board. */
static int num_sunzilog = 2;
#define NUM_SUNZILOG	num_sunzilog
#define NUM_CHANNELS	(NUM_SUNZILOG * 2)

/*
 * We wrap our port structure around the generic uart_port.
 */
struct uart_sunzilog_port {
	struct uart_port		port;

	/* IRQ servicing chain.  */
	struct uart_sunzilog_port	*next;

	/* Current values of Zilog write registers.  */
	unsigned char			curregs[NUM_ZSREGS];

	unsigned int			flags;
#define SUNZILOG_FLAG_CONS_KEYB		0x00000001
#define SUNZILOG_FLAG_CONS_MOUSE	0x00000002
#define SUNZILOG_FLAG_IS_CONS		0x00000004
#define SUNZILOG_FLAG_IS_KGDB		0x00000008
#define SUNZILOG_FLAG_MODEM_STATUS	0x00000010
#define SUNZILOG_FLAG_IS_CHANNEL_A	0x00000020

	/* L1-A keyboard break state.  */
	int				kbd_id;
	int				l1_down;

	unsigned char			parity_mask;
	unsigned char			prev_status;
};

#define ZILOG_CHANNEL_FROM_PORT(PORT)	((struct zilog_channel *)((PORT)->membase))
#define UART_ZILOG(PORT)		((struct uart_sunzilog_port *)(PORT))
#define SUNZILOG_GET_CURR_REG(PORT, REGNUM)		\
	(UART_ZILOG(PORT)->curregs[REGNUM])
#define SUNZILOG_SET_CURR_REG(PORT, REGNUM, REGVAL)	\
	((UART_ZILOG(PORT)->curregs[REGNUM]) = (REGVAL))

static unsigned char sunzilog_initregs_normal[NUM_ZSREGS] = {
	RES_EXT_INT,			/* R0 */
	0,				/* R1 */
	0,				/* R2 */
	Rx8,				/* R3 */
	PAR_EVEN | X16CLK | SB1,	/* R4 */
	Tx8,				/* R5 */
	0,				/* R6 */
	0,				/* R7 */
	0,				/* R8 */
	NV | MIE,			/* R9 */
	NRZ,				/* R10 */
	RCBR | TCBR,			/* R11 */
	0, 0,				/* R12, R13 Baud Quotient low, high */
	BRSRC | BRENAB,			/* R14 */
	DCDIE | SYNCIE | CTSIE | BRKIE,	/* R15 */
};

static unsigned char sunzilog_initregs_console[NUM_ZSREGS] = {
	RES_EXT_INT,			/* R0 */
	EXT_INT_ENAB | INT_ALL_Rx,	/* R1 */
	0,				/* R2 */
	Rx8 | RxENAB,			/* R3 */
	X16CLK,				/* R4 */
	DTR | Tx8 | TxENAB,		/* R5 */
	0,				/* R6 */
	0,				/* R7 */
	0,				/* R8 */
	NV | MIE,			/* R9 */
	NRZ,				/* R10 */
	RCBR | TCBR,			/* R11 */
	0, 0,				/* R12, R13 Baud Quotient low, high */
	BRSRC | BRENAB,			/* R14 */
	DCDIE | SYNCIE | CTSIE | BRKIE,	/* R15 */
};

static unsigned char sunzilog_initregs_kgdb[NUM_ZSREGS] = {
	0, 0, 0				/* R0, R1, R2 */
	Rx8 | RxENAB,			/* R3 */
	PAR_EVEN | X16CLK | SB1,	/* R4 */
	DTR | Tx8 | TxENAB,		/* R5 */
	0, 0, 0,			/* R6, R7, R8 */
	NV,				/* R9 */
	NRZ,				/* R10 */
	RCBR | TCBR,			/* R11 */
	0, 0,				/* R12, R13 Baud Quotient low, high */
	BRSRC | BRENAB,			/* R14 */
	DCDIE,				/* R15 */
};

/* Reading and writing Zilog8530 registers.  The delays are to make this
 * driver work on the Sun4 which needs a settling delay after each chip
 * register access, other machines handle this in hardware via auxiliary
 * flip-flops which implement the settle time we do in software.
 *
 * The port lock must be held and local IRQs must be disabled
 * when {read,write}_zsreg is invoked.
 */
static unsigned char read_zsreg(struct zilog_channel *channel,
				unsigned char reg)
{
	unsigned char retval;

	sbus_writeb(reg, &channel->control);
	ZSDELAY();
	retval = sbus_readb(&channel->control);
	ZSDELAY();

	return retval;
}

static void write_zsreg(struct zilog_channel *channel,
			unsigned char reg, unsigned char value)
{
	sbus_writeb(reg, &channel->control);
	ZSDELAY();
	sbus_writeb(value, &channel->control);
	ZSDELAY();
}

static void load_zsregs(struct zilog_channel *channel, unsigned char *regs,
			int is_channel_a)
{
	int i;

	for (i = 0; i < 1000; i++) {
		unsigned char stat = read_zsreg(channel, R1);
		if (stat & ALL_SNT)
			break;
		udelay(100);
	}
	write_zsreg(channel, R3, 0);
	ZS_CLEARSTAT(channel);
	ZS_CLEARERR(channel);
	ZS_CLEARFIFO(channel);

	if (is_channel_a)
		write_zsreg(channel, R9, CHRA);
	else
		write_zsreg(channel, R9, CHRB);
	ZSDELAY_LONG();

	write_zsreg(channel, R4, regs[R4]);
	write_zsreg(channel, R3, regs[R3] & ~RxENAB);
	write_zsreg(channel, R5, regs[R5] & ~TxENAB);
	write_zsreg(channel, R9, regs[R9] & ~MIE);
	write_zsreg(channel, R10, regs[R10]);
	write_zsreg(channel, R11, regs[R11]);
	write_zsreg(channel, R12, regs[R12]);
	write_zsreg(channel, R13, regs[R13]);
	write_zsreg(channel, R14, regs[R14] & ~BRENAB);
	write_zsreg(channel, R14, regs[R14]);
	write_zsreg(channel, R14,
		    (regs[R14] & ~SNRZI) | BRENAB);
	write_zsreg(channel, R3, regs[R3]);
	write_zsreg(channel, R5, regs[R5]);
	write_zsreg(channel, R15, regs[R15]);
	write_zsreg(channel, R0, RES_EXT_INT);
	write_zsreg(channel, R0, ERR_RES);
	write_zsreg(channel, R1, regs[R1]);
	write_zsreg(channel, R9, regs[R9]);
}

/* The port->lock must be held and interrupts must be disabled here.  */
static __inline__ unsigned char __sunzilog_read_channel_status(struct uart_port *port)
{
	struct zilog_channel *channel;
	unsigned char status;

	channel = ZILOG_CHANNEL_FROM_PORT(port);
	status = sbus_read(&channel->control);
	ZSDELAY();

	return status;
}

/* A convenient way to quickly get R0 status.  The caller must _not_ hold the
 * port lock, it is acquired here.  If you have the lock already invoke the
 * double-underscore variant above.
 */
static unsigned char sunzilog_read_channel_status(struct uart_port *port)
{
	unsigned long flags;
	unsigned char status;

	spin_lock_irqsave(&port->lock, flags);
	status = __sunzilog_read_channel_status(port);
	spin_unlock_irqrestore(&port->lock, flags);

	return status;
}

/* A convenient way to set/clear bits in an arbitrary zilog register.
 * The caller must the port lock and local interrupts must be disabled.
 */
static void __sunzilog_set_clear_bits(struct uart_port *port, int regnum,
				      unsigned char set_bits,
				      unsigned char clear_bits)
{
	unsigned char regval;

	regval = SUNZILOG_GET_CURR_REG(port, regnum);
	regval |= set_bits;
	regval &= ~clear_bits;
	SUNZILOG_SET_CURR_REG(port, regnum, regval);
	write_zsreg(ZILOG_CHANNEL_FROM_PORT(port), regnum, regval);
}

static void sunzilog_receive_chars(struct uart_sunzilog_port *sunzilog_port,
				   struct sunzilog_channel *channel,
				   struct pt_regs *regs)
{
	struct tty_struct *tty = sunzilog_port->port.info->tty;

	while (1) {
		unsigned char ch, r1;

		if (unlikely(tty->flip.count >= TTY_FLIPBUF_SIZE)) {
			tty->flip.tqueue.routine((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE)
				return;
		}

		r1 = read_zsreg(channel, R1);
		if (r1 & (PAR_ERR | Rx_OVR | CRC_ERR)) {
			sbus_writeb(ERR_RES, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);
		}

		ch = sbus_readb(&channel->control);
		ZSDELAY();

		/* This funny hack depends upon BRK_ABRT not interfering
		 * with the other bits we care about in R1.
		 */
		if (ch & BRK_ABRT)
			r1 |= BRK_ABRT;

		ch = sbus_readb(&channel->data);
		ZSDELAY();

		ch &= sunzilog_port->parity_mask;

		if (sunzilog_port->flags & SUNZILOG_FLAG_CONS_KEYB) {
			if (ch == SUNKBD_RESET) {
				sunzilog_port->kbd_id = 1;
				sunzilog_port->l1_down = 0;
			} else if (sunzilog_port->kbd_id) {
				sunzilog_port->kbd_id = 0;
			} else if (ch == SUNKBD_l1) {
				sunzilog_port->l1_down = 1;
			} else if (ch == (SUNKBD_l1 | SUNKBD_UP)) {
				sunzilog_port->l1_down = 0;
			} else if (ch == SUNKBD_A && sunzilog_port->l1_down) {
				sun_do_break();
				sunzilog_port->l1_down = 0;
				sunzilog_port->kbd_id = 0;
				return;
			}
			sunkbd_inchar(ch, regs);
			goto next_char;
		}
		if (sunzilog_port->flags & SUNZILOG_FLAG_CONS_MOUSE) {
			sun_mouse_inbyte(ch, 0);
			goto next_char;
		}
		if ((sunzilog_port->flags & SUNZILOG_FLAG_IS_CONS) &&
		    (r1 & BRK_ABRT)) {
			sun_do_break();
			return;
		}
#ifndef CONFIG_SPARC64
		/* Look for kgdb 'stop' character.  */
		if ((sunzilog_port->flags & SUNZILOG_FLAG_IS_KGDB) &&
		    (ch == '\003')) {
			breakpoint();
			return;
		}
#endif

		/* A real serial line, record the character and status.  */
		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		sunzilog_port->port.icount.rx++;
		if (r1 & (BRK_ABRT | PAR_ERR | Rx_OVR | CRC_ERR)) {
			if (r1 & BRK_ABRT) {
				r1 &= ~(PAR_ERR | CRC_ERR);
				sunzilog_port->port.icount.break++;
				if (uart_handle_break(&sunzilog_port->port))
					goto next_char;
			}
			else if (r1 & PAR_ERR)
				sunzilog_port->port.icount.parity++;
			else if (r1 & CRC_ERR)
				sunzilog_port->port.icount.frame++;
			if (r1 & Rx_OVR)
				sunzilog_port->port.icount.overrun++;
			r1 &= sunzilog_port->port.read_status_mask;
			if (r1 & BRK_ABRT)
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			else if (r1 & PAR_ERR)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (r1 & CRC_ERR)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&sunzilog_port->port, ch, regs))
			goto next_char;

		if (up->ignore_status_mask == 0xff ||
		    (r1 & sunzilog_port->port.ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
		if ((r1 & Rx_OVR) &&
		    tty->flip.count < TTY_FLIPBUF_SIZE) {
			*tty->flip.flag_buf_ptr = TTY_OVERRUN;
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
	next_char:
		ch = sbus_readb(&channel->control);
		ZSDELAY();
		if (!(ch & Rx_CH_AV))
			break;
	}

	tty_flip_buffer_push(tty);
}

static void sunzilog_status_handle(struct uart_sunzilog_port *sunzilog_port,
				   struct sunzilog_channel *channel)
{
	unsigned char status;

	status = sbus_readb(&channel->control);
	ZSDELAY();

	sbus_writeb(RES_EXT_INT, &channel->control);
	ZSDELAY();
	ZS_WSYNC(channel);

	if ((status & BRK_ABRT) &&
	    (sunzilog_port->flags & SUNZILOG_FLAG_CONS_MOUSE))
		sun_mouse_inbyte(0, 1);

	if (sunzilog_port->flags & SUNZILOG_FLAG_MODEM_STATUS) {
		if (status & SYNC)
			sunzilog_port->port.icount.dsr++;

		/* The Zilog just gives us an interrupt when DCD/CTS/etc. change.
		 * But it does not tell us which bit has changed, we have to keep
		 * track of this ourselves.
		 */
		if ((status & DCD) ^ sunzilog_port->prev_status)
			uart_handle_dcd_change(&sunzilog_port->port,
				       (status & DCD));
		if ((status & CTS) ^ sunzilog_port->prev_status)
			uart_handle_cts_change(&sunzilog_port->port,
					       (status & CTS));

		wake_up_interruptible(&sunzilog_port->port.info->delta_msr_wait);
	}

	sunzilog_port->prev_status = status;
}

#define ZS_PUT_CHAR_MAX_DELAY	2000	/* 10 ms */

static void sunzilog_put_char(struct sunzilog_channel *channel, unsigned char ch)
{
	int loops = ZS_PUT_CHAR_MAX_DELAY;

	/* This is a timed polling loop so do not switch the explicit
	 * udelay with ZSDELAY as that is a NOP on some platforms.  -DaveM
	 */
	do {
		unsigned char val = sbus_readb(&channel->control);
		if (val & Tx_BUF_EMP)
			break;
		udelay(5);
	} while (--loops);

	sbus_writeb(ch, &channel->data);
	ZSDELAY();
	ZS_WSYNC(channel);
}

static void sunzilog_transmit_chars(struct uart_sunzilog_port *sunzilog_port,
				    struct sunzilog_channel *channel)
{
	struct circ_buf *xmit = &sunzilog_port->port.info->xmit;
	unsigned char status;
	int count;

	status = sbus_readb(&channel->control);
	ZSDELAY();

	/* TX still busy?  Just wait for the next TX done interrupt.
	 * XXX This is a bug bug bug if it happens.  It can occur because
	 * XXX of how we used to do serial consoles on Sparc but we should
	 * XXX transmit console writes just like we normally would for normal
	 * XXX UART lines (ie. buffered and TX interrupt driven).  -DaveM
	 */
	if (!(status & Tx_BUF_EMP))
		return;

	if (sunzilog_port->port.x_char) {
		sbus_writeb(sunzilog->port.x_char, &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		sunzilog_port->port.icount.tx++;
		sunzilog_port->port.x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&sunzilog_port->port)) {
		sunzilog_stop_tx(&sunzilog_port->port, 0);
		return;
	}

	sbus_writeb(xmit->buf[xmit->tail], &channel->data);
	ZSDELAY();
	ZS_WSYNC(channel);

	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	sunzilog_port->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_event(&sunzilog_port->port, EVT_WRITE_WAKEUP);

	if (uart_circ_empty(xmit))
		sunzilog_stop_tx(&sunzilog_port->port, 0);
}

static void sunzilog_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_sunzilog_port *sunzilog_port = dev_id;
	unsigned long flags;

	while (sunzilog_port) {
		struct sunzilog_channel *channel
			= ZILOG_CHANNEL_FROM_PORT(&sunzilog_port.port);
		unsigned char r3;

		spin_lock(&sunzilog_port->port.lock);
		r3 = read_zsreg(channel, 3);

		/* Channel A */
		if (r3 & (CHAEXT | CHATxIP | CHARxIP)) {
			sbus_writeb(RES_H_IUS, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);

			if (r3 & CHARxIP)
				sunzilog_receive_chars(sunzilog_port, channel, regs);
			if (r3 & CHAEXT)
				sunzilog_status_handle(sunzilog_port, channel);
			if (r3 & CHATxIP)
				sunzilog_transmit_chars(sunzilog_port, channel);
		}
		spin_unlock(&sunzilog_port->port.lock);

		/* Channel B */
		sunzilog_port = sunzilog_port->next;
		channel = ZILOG_CHANNEL_FROM_PORT(&sunzilog_port.port);

		spin_lock(&sunzilog_port->port.lock);
		if (r3 & (CHBEXT | CHBTxIP | CHBRxIP)) {
			sbus_writeb(RES_H_IUS, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);

			if (r3 & CHBRxIP)
				sunzilog_receive_chars(sunzilog_port, channel, regs);
			if (r3 & CHBEXT)
				sunzilog_status_handle(sunzilog_port, channel);
			if (r3 & CHBTxIP)
				sunzilog_transmit_chars(sunzilog_port, channel);
		}
		spin_unlock(&sunzilog_port->port.lock);

		sunzilog_port = sunzilog_port->next;
	}
	spin_lock_irqrestore(&sunzilog_lock, flags);
}

/* The port lock is not held.  */
static unsigned int sunzilog_tx_empty(struct uart_port *port)
{
	unsigned char status;
	unsigned int ret;

	status = sunzilog_read_channel_status(port);
	if (status & Tx_BUF_EMP)
		ret = TIOCSER_TEMPT;
	else
		ret = 0;

	return ret;
}

/* The port lock is not held.  */
static unsigned int sunzilog_get_mctrl(struct uart_port *port)
{
	unsigned char status;
	unsigned int ret;

	status = sunzilog_read_channel_status(port);

	ret = 0;
	if (status & DCD)
		ret |= TIOCM_CAR;
	if (status & SYNC)
		ret |= TIOCM_DSR;
	if (status & CTS)
		ret |= TIOCM_CTS;

	return ret;
}

/* The port lock is held and interrupts are disabled.  */
static void sunzilog_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned char set_bits, clear_bits;

	set_bits = clear_bits = 0;

	if (mctrl & TIOCM_RTS)
		set_bits |= RTS;
	else
		clear_bits |= RTS;
	if (mctrl & TIOCM_DTR)
		set_bits |= DTR;
	else
		clear_bits |= DTR;

	__sunzilog_set_clear_bits(port, 5, set_bits, clear_bits);
}

/* The port lock is held and interrupts are disabled.  */
static void sunzilog_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	__sunzilog_set_clear_bits(port, 5, 0, TxENAB);
}

/* The port lock is held and interrupts are disabled.  */
static void sunzilog_start_tx(struct uart_port *port, unsigned int tty_start)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct sunzilog_channel *channel;
	unsigned char status;

	/* Enable the transmitter.  */
	__sunzilog_set_clear_bits(port, 5, TxENAB, 0);

	channel = ZILOG_CHANNEL_FROM_PORT(port);

	status = sbus_readb(&channel->control);
	ZSDELAY();

	/* TX busy?  Just wait for the TX done interrupt.  */
	if (!(status & Tx_BUF_EMP))
		return;

	/* Send the first character to jump-start the TX done
	 * IRQ sending engine.
	 */
	if (port->x_char) {
		sbus_writeb(port->x_char, &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		port->icount.tx++;
		port->x_char = 0;
	} else {
		struct circ_buf *xmit = &port->info->xmit;

		sbus_writeb(xmit->but[xmit->tail], &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_event(&sunzilog_port->port, EVT_WRITE_WAKEUP);

		if (uart_circ_empty(xmit))
			sunzilog_stop_tx(&sunzilog_port->port, 0);
	}
}

/* The port lock is not held.  */
static void sunzilog_stop_rx(struct uart_port *port)
{
}

/* The port lock is not held.  */
static void sunzilog_enable_ms(struct uart_port *port)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	__sunzilog_set_clear_bits(port, 15,
				  (DCDIE | SYNCIE | CTSIE), 0);
	spin_unlock_irqrestore(&port->lock, flags);
}

/* The port lock is not held.  */
static void sunzilog_break_ctl(struct uart_port *port, int break_state)
{
	unsigned char set_bits, clear_bits;
	unsigned long flags;

	set_bits = clear_bits = 0;

	if (break_state)
		set_bits |= SND_BRK;
	else
		clear_bits |= SND_BRK;

	spin_lock_irqsave(&port->lock, flags);
	__sunzilog_set_clear_bits(port, 5, set_bits, clear_bits);
	spin_unlock_irqrestore(&port->lock, flags);
}

static int sunzilog_startup(struct uart_port *port)
{
}

static void sunzilog_shutdown(struct uart_port *port)
{
}

/* The port lock is not held.  */
static void
sunzilog_change_speed(struct uart_port *port, unsigned int cflag,
		      unsigned int iflag, unsigned int quot)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);

	/* Program BAUD and clock source. */
	up->curregs[4] &= ~XCLK_MASK;
	up->curregs[4] |= X16CLK;
	up->curregs[11] = TCBR | RCBR;
	/* XXX verify this stuff XXX */
	up->curregs[12] = quot & 0xff;
	up->curregs[13] = (quot >> 8) & 0xff;
	up->curregs[14] = BRSRC | BRENAB;
	up->curregs[15] |= RTS | DTR;

	/* Character size, stop bits, and parity. */
	up->curregs[3] &= ~RxN_MASK;
	up->curregs[5] &= ~TxN_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		up->curregs[3] |= Rx5;
		up->curregs[5] |= Tx5;
		up->parity_mask = 0x1f;
		break;
	case CS6:
		up->curregs[3] |= Rx6;
		up->curregs[5] |= Tx6;
		up->parity_mask = 0x3f;
		break;
	case CS7:
		up->curregs[3] |= Rx7;
		up->curregs[5] |= Tx7;
		up->parity_mask = 0x7f;
		break;
	case CS8:
	default:
		up->curregs[3] |= Rx8;
		up->curregs[5] |= Tx8;
		up->parity_mask = 0xff;
		break;
	};
	up->curregs[4] &= ~0x0c;
	if (cflag & CSTOPB)
		up->curregs[4] |= SB2;
	else
		up->curregs[4] |= SB1;
	if (cflag & PARENB)
		up->curregs[4] |= PAR_ENAB;
	else
		up->curregs[4] &= ~PAR_ENAB;
	if (!(cflag & PARODD))
		up->curregs[4] |= PAR_EVEN;
	else
		up->curregs[4] &= ~PAR_EVEN;

	up->read_status_mask = Rx_OVR;
	if (iflag & INPCK)
		up->read_status_mask |= CRC_ERR | PAR_ERR;
	if (iflag & (BRKINT | PARMRK))
		up->read_status_mask |= BRK_ABRT;

	up->ignore_status_mask = 0;
	if (iflag & IGNPAR)
		up->ignore_status_mask |= CRC_ERR | PAR_ERR;
	if (iflag & IGNBRK) {
		up->ignore_status_mask |= BRK_ABRT;
		if (iflag & IGNPAR)
			up->ignore_status_mask |= Rx_OVR;
	}

	if ((cflag & CREAD) == 0)
		up->ignore_status_mask = 0xff;

	if (UART_ENABLE_MS(&up->port, cflag))
		up->flags |= SUNZILOG_FLAG_MODEM_STATUS;
	else
		up->flags &= ~SUNZILOG_FLAG_MODEM_STATUS;

	load_zsregs(up, up->curregs);

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static const char *sunzilog_type(struct uart_port *port)
{
	return "SunZilog";
}

/* We do not request/release mappings of the registers here, this
 * happens at early serial probe time.
 */
static void sunzilog_release_port(struct uart_port *port)
{
}

static int sunzilog_request_port(struct uart_port *port)
{
}

/* These do not need to do anything interesting either.  */
static void sunzilog_config_port(struct uart_port *port, int flags)
{
}

static int sunzilog_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return 0;
}

static struct uart_ops sunzilog_pops = {
	tx_empty:	sunzilog_tx_empty,
	set_mctrl:	sunzilog_set_mctrl,
	get_mctrl:	sunzilog_get_mctrl,
	stop_tx:	sunzilog_stop_tx,
	start_tx:	sunzilog_start_tx,
	stop_rx:	sunzilog_stop_rx,
	enable_ms:	sunzilog_enable_ms,
	break_ctl:	sunzilog_break_ctl,
	startup:	sunzilog_startup,
	shutdown:	sunzilog_shutdown,
	change_speed:	sunzilog_change_speed,
	type:		sunzilog_type,
	release_port:	sunzilog_release_port,
	request_port:	sunzilog_request_port,
	config_port:	sunzilog_config_port,
	verify_port:	sunzilog_verify_port,
};

static struct uart_sunzilog_port sunzilog_ports[UART_NR];

static struct uart_driver sunzilog_reg = {
	owner:		THIS_MODULE,
	driver_name:	"ttyS",
#ifdef CONFIG_DEVFS_FS
	dev_name:	"ttyS%d",
#else
	dev_name:	"ttyS",
#endif
	major:		TTY_MAJOR,
	minor:		64,
};

static void * __init sunzilog_alloc_bootmem(unsigned long size)
{
	void *ret;

	ret = __alloc_bootmem(size, SMP_CACHE_BYTES, 0UL);
	if (ret != NULL)
		memset(ret, 0, size);

	return ret;
}

static void __init sunzilog_alloc_tables(void)
{
	zs_chips = (struct sun_zslayout **)
		zs_alloc_bootmem(NUM_SERIAL * sizeof(struct sun_zslayout *));
	if (zs_chips == NULL)
		zs_init_alloc_failure("zs_chips");
	zs_channels = (struct sun_zschannel **)
		zs_alloc_bootmem(NUM_CHANNELS * sizeof(struct sun_zschannel *));
	if (zs_channels == NULL)
		zs_init_alloc_failure("zs_channels");
	zs_nodes = (int *)
		zs_alloc_bootmem(NUM_SERIAL * sizeof(int));
	if (zs_nodes == NULL)
		zs_init_alloc_failure("zs_nodes");
	zs_soft = (struct sun_serial *)
		zs_alloc_bootmem(NUM_CHANNELS * sizeof(struct sun_serial));
	if (zs_soft == NULL)
		zs_init_alloc_failure("zs_soft");
	zs_ttys = (struct tty_struct *)
		zs_alloc_bootmem(NUM_CHANNELS * sizeof(struct tty_struct));
	if (zs_ttys == NULL)
		zs_init_alloc_failure("zs_ttys");
	serial_table = (struct tty_struct **)
		zs_alloc_bootmem(NUM_CHANNELS * sizeof(struct tty_struct *));
	if (serial_table == NULL)
		zs_init_alloc_failure("serial_table");
	serial_termios = (struct termios **)
		zs_alloc_bootmem(NUM_CHANNELS * sizeof(struct termios *));
	if (serial_termios == NULL)
		zs_init_alloc_failure("serial_termios");
	serial_termios_locked = (struct termios **)
		zs_alloc_bootmem(NUM_CHANNELS * sizeof(struct termios *));
	if (serial_termios_locked == NULL)
		zs_init_alloc_failure("serial_termios_locked");
}

static int __init sunzilog_probe(void)
{
	int node;

	/* Sun4 Zilog setup is hard coded, no probing to do.  */
	if (sparc_cpu_model == sun4)
		goto no_probe;

	NUM_SUNZILOG = 0;
	node = prom_getchild(prom_root_node);
	if (sparc_cpu_model == sun4d) {
		int bbnode;

		while (node &&
		       (node = prom_searchsiblings(node, "cpu-unit"))) {
			bbnode = prom_getchild(node);
			if (bbnode && prom_searchsiblings(bbnode, "bootbus"))
				NUM_SUNZILOG += 2;
			node = prom_getsibling(node);
		}
		goto no_probe;
	}

#ifdef CONFIG_SPARC64
	else if (sparc_cpu_model == sun4u) {
		int central_node;

		/* Central bus zilogs must be checked for first,
		 * since Enterprise boxes might have SBUSes as well.
		 */
		central_node = prom_finddevice("/central");
		if(central_node != 0 && central_node != -1)
			node = prom_searchsiblings(prom_getchild(central_node), "fhc");
		else
			node = prom_searchsiblings(node, "sbus");
		if(node != 0 && node != -1)
			node = prom_getchild(node);
		if(node == 0 || node == -1)
			return -ENODEV;
	}
#endif /* CONFIG_SPARC64 */
	else {
		node = prom_searchsiblings(node, "obio");
		if(node)
			node = prom_getchild(node);
		NUM_SERIAL = 2;
		goto no_probe;
	}

	node = prom_searchsiblings(node, "zs");
	if (!node)
		return -ENODEV;
		
	NUM_SERIAL = 2;

no_probe:
	sunzilog_alloc_tables();

}

static int __init sunzilog_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: Sun Zilog driver.\n");

	/* We can only init this once we have probed the Zilogs
	 * in the system.
	 */
	sunzilog_reg.nr = NUM_CHANNELS;

	/* XXX This is probably where this needs to be setup for
	 * XXX the same reason.
	 */
	sunzilog_reg.cons = XXX;

	ret = uart_register_driver(&sunzilog_reg);
	if (ret == 0) {
		int i;

		for (i = 0; i < UART_NR; i++) {
			/* XXX For each probed Zilog do this... */
			uart_add_one_port(&sunzilog_reg,
					  &sunzilog_ports[i].port);
		}
	}

	return ret;
}

static void __exit sunzilog_exit(void)
{
	int i;

	for (i = 0; i < UART_NR; i++) {
		/* XXX For each probed Zilog do this... */
		uart_remove_one_port(&sunzilog_reg,
				     &sunzilog_ports[i].port);
	}

	uart_unregister_driver(&sunzilog_reg);
}

module_init(sunzilog_init);
module_exit(sunzilog_exit);

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("David S. Miller");
MODULE_DESCRIPTION("Sun Zilog serial port driver");
MODULE_LICENSE("GPL");
