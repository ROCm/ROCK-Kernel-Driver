/*
 * sunzilog.c
 *
 * Driver for Zilog serial chips found on Sun workstations and
 * servers.  This driver could actually be made more generic.
 *
 * This is based on the old drivers/sbus/char/zs.c code.  A lot
 * of code has been simply moved over directly from there but
 * much has been rewritten.  Credits therefore go out to Eddie
 * C. Dost, Peter Zaitcev, Ted Ts'o and Alex Buell for their
 * work there.
 *
 *  Copyright (C) 2002 David S. Miller (davem@redhat.com)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/circ_buf.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/spinlock.h>
#ifdef CONFIG_SERIO
#include <linux/serio.h>
#endif
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>
#ifdef CONFIG_SPARC64
#include <asm/fhc.h>
#endif
#include <asm/sbus.h>

#include <linux/serial_core.h>

#include "suncore.h"
#include "sunzilog.h"

/* On 32-bit sparcs we need to delay after register accesses
 * to accomodate sun4 systems, but we do not need to flush writes.
 * On 64-bit sparc we only need to flush single writes to ensure
 * completion.
 */
#ifndef CONFIG_SPARC64
#define ZSDELAY()		udelay(5)
#define ZSDELAY_LONG()		udelay(20)
#define ZS_WSYNC(channel)	do { } while (0)
#else
#define ZSDELAY()
#define ZSDELAY_LONG()
#define ZS_WSYNC(__channel) \
	sbus_readb(&((__channel)->control))
#endif

static int num_sunzilog;
#define NUM_SUNZILOG	num_sunzilog
#define NUM_CHANNELS	(NUM_SUNZILOG * 2)

#define KEYBOARD_LINE 0x2
#define MOUSE_LINE    0x3

#define ZS_CLOCK		4915200 /* Zilog input clock rate. */
#define ZS_CLOCK_DIVISOR	16      /* Divisor this driver uses. */

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
#define SUNZILOG_FLAG_REGS_HELD		0x00000040
#define SUNZILOG_FLAG_TX_STOPPED	0x00000080
#define SUNZILOG_FLAG_TX_ACTIVE		0x00000100

	unsigned int cflag;

	/* L1-A keyboard break state.  */
	int				kbd_id;
	int				l1_down;

	unsigned char			parity_mask;
	unsigned char			prev_status;

#ifdef CONFIG_SERIO
	struct serio			serio;
#endif
};

#define ZILOG_CHANNEL_FROM_PORT(PORT)	((struct zilog_channel *)((PORT)->membase))
#define UART_ZILOG(PORT)		((struct uart_sunzilog_port *)(PORT))
#define SUNZILOG_GET_CURR_REG(PORT, REGNUM)		\
	(UART_ZILOG(PORT)->curregs[REGNUM])
#define SUNZILOG_SET_CURR_REG(PORT, REGNUM, REGVAL)	\
	((UART_ZILOG(PORT)->curregs[REGNUM]) = (REGVAL))
#define ZS_IS_KEYB(UP)	((UP)->flags & SUNZILOG_FLAG_CONS_KEYB)
#define ZS_IS_MOUSE(UP)	((UP)->flags & SUNZILOG_FLAG_CONS_MOUSE)
#define ZS_IS_CONS(UP)	((UP)->flags & SUNZILOG_FLAG_IS_CONS)
#define ZS_IS_KGDB(UP)	((UP)->flags & SUNZILOG_FLAG_IS_KGDB)
#define ZS_WANTS_MODEM_STATUS(UP)	((UP)->flags & SUNZILOG_FLAG_MODEM_STATUS)
#define ZS_IS_CHANNEL_A(UP)	((UP)->flags & SUNZILOG_FLAG_IS_CHANNEL_A)
#define ZS_REGS_HELD(UP)	((UP)->flags & SUNZILOG_FLAG_REGS_HELD)
#define ZS_TX_STOPPED(UP)	((UP)->flags & SUNZILOG_FLAG_TX_STOPPED)
#define ZS_TX_ACTIVE(UP)	((UP)->flags & SUNZILOG_FLAG_TX_ACTIVE)

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

static void sunzilog_clear_fifo(struct zilog_channel *channel)
{
	int i;

	for (i = 0; i < 32; i++) {
		unsigned char regval;

		regval = sbus_readb(&channel->control);
		ZSDELAY();
		if (regval & Rx_CH_AV)
			break;

		regval = read_zsreg(channel, R1);
		sbus_readb(&channel->data);
		ZSDELAY();

		if (regval & (PAR_ERR | Rx_OVR | CRC_ERR)) {
			sbus_writeb(ERR_RES, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);
		}
	}
}

/* This function must only be called when the TX is not busy.  The UART
 * port lock must be held and local interrupts disabled.
 */
static void __load_zsregs(struct zilog_channel *channel, unsigned char *regs)
{
	int i;

	/* Let pending transmits finish.  */
	for (i = 0; i < 1000; i++) {
		unsigned char stat = read_zsreg(channel, R1);
		if (stat & ALL_SNT)
			break;
		udelay(100);
	}

	sbus_writeb(ERR_RES, &channel->control);
	ZSDELAY();
	ZS_WSYNC(channel);

	sunzilog_clear_fifo(channel);

	/* Disable all interrupts.  */
	write_zsreg(channel, R1,
		    regs[R1] & ~(RxINT_MASK | TxINT_ENAB | EXT_INT_ENAB));

	/* Set parity, sync config, stop bits, and clock divisor.  */
	write_zsreg(channel, R4, regs[R4]);

	/* Set misc. TX/RX control bits.  */
	write_zsreg(channel, R10, regs[R10]);

	/* Set TX/RX controls sans the enable bits.  */
	write_zsreg(channel, R3, regs[R3] & ~RxENAB);
	write_zsreg(channel, R5, regs[R5] & ~TxENAB);

	/* Synchronous mode config.  */
	write_zsreg(channel, R6, regs[R6]);
	write_zsreg(channel, R7, regs[R7]);

	/* Don't mess with the interrupt vector (R2, unused by us) and
	 * master interrupt control (R9).  We make sure this is setup
	 * properly at probe time then never touch it again.
	 */

	/* Disable baud generator.  */
	write_zsreg(channel, R14, regs[R14] & ~BRENAB);

	/* Clock mode control.  */
	write_zsreg(channel, R11, regs[R11]);

	/* Lower and upper byte of baud rate generator divisor.  */
	write_zsreg(channel, R12, regs[R12]);
	write_zsreg(channel, R13, regs[R13]);
	
	/* Now rewrite R14, with BRENAB (if set).  */
	write_zsreg(channel, R14, regs[R14]);

	/* External status interrupt control.  */
	write_zsreg(channel, R15, regs[R15]);

	/* Reset external status interrupts.  */
	write_zsreg(channel, R0, RES_EXT_INT);
	write_zsreg(channel, R0, RES_EXT_INT);

	/* Rewrite R3/R5, this time without enables masked.  */
	write_zsreg(channel, R3, regs[R3]);
	write_zsreg(channel, R5, regs[R5]);

	/* Rewrite R1, this time without IRQ enabled masked.  */
	write_zsreg(channel, R1, regs[R1]);
}

/* Reprogram the Zilog channel HW registers with the copies found in the
 * software state struct.  If the transmitter is busy, we defer this update
 * until the next TX complete interrupt.  Else, we do it right now.
 *
 * The UART port lock must be held and local interrupts disabled.
 */
static void sunzilog_maybe_update_regs(struct uart_sunzilog_port *up,
				       struct zilog_channel *channel)
{
	if (!ZS_REGS_HELD(up)) {
		if (ZS_TX_ACTIVE(up)) {
			up->flags |= SUNZILOG_FLAG_REGS_HELD;
		} else {
			__load_zsregs(channel, up->curregs);
		}
	}
}

static void sunzilog_change_mouse_baud(struct uart_sunzilog_port *up)
{
	unsigned int cur_cflag = up->cflag;
	int brg, new_baud;

	up->cflag &= ~CBAUD;
	up->cflag |= suncore_mouse_baud_cflag_next(cur_cflag, &new_baud);

	brg = BPS_TO_BRG(new_baud,
			 (ZS_CLOCK / ZS_CLOCK_DIVISOR));
	up->curregs[R12] = (brg & 0xff);
	up->curregs[R13] = (brg >> 8) & 0xff;
	sunzilog_maybe_update_regs(up, ZILOG_CHANNEL_FROM_PORT(&up->port));
}

static void sunzilog_kbdms_receive_chars(struct uart_sunzilog_port *up,
					 unsigned char ch, int is_break,
					 struct pt_regs *regs)
{
	if (ZS_IS_KEYB(up)) {
		if (ch == SUNKBD_RESET) {
			up->kbd_id = 1;
			up->l1_down = 0;
		} else if (up->kbd_id) {
			up->kbd_id = 0;
		} else if (ch == SUNKBD_L1) {
			up->l1_down = 1;
		} else if (ch == (SUNKBD_L1 | SUNKBD_UP)) {
			up->l1_down = 0;
		} else if (ch == SUNKBD_A && up->l1_down) {
			sun_do_break();
			up->l1_down = 0;
			up->kbd_id = 0;
			return;
		}
		kbd_pt_regs = regs;
#ifdef CONFIG_SERIO
		serio_interrupt(&up->serio, ch, 0);
#endif
	} else if (ZS_IS_MOUSE(up)) {
		int ret = suncore_mouse_baud_detection(ch, is_break);

		switch (ret) {
		case 2:
			sunzilog_change_mouse_baud(up);
			/* fallthru */
		case 1:
			break;

		case 0:
#ifdef CONFIG_SERIO
			serio_interrupt(&up->serio, ch, 0);
#endif
			break;
		};
	}
}

static void sunzilog_receive_chars(struct uart_sunzilog_port *up,
				   struct zilog_channel *channel,
				   struct pt_regs *regs)
{
	struct tty_struct *tty = up->port.info->tty;

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

		ch &= up->parity_mask;

		if (unlikely(ZS_IS_KEYB(up)) || unlikely(ZS_IS_MOUSE(up))) {
			sunzilog_kbdms_receive_chars(up, ch, 0, regs);
			goto next_char;
		}

		if (ZS_IS_CONS(up) && (r1 & BRK_ABRT)) {
			/* Wait for BREAK to deassert to avoid potentially
			 * confusing the PROM.
			 */
			while (1) {
				ch = sbus_readb(&channel->control);
				ZSDELAY();
				if (!(ch & BRK_ABRT))
					break;
			}
			sun_do_break();
			return;
		}
#ifndef CONFIG_SPARC64
		/* Look for kgdb 'stop' character.  */
		if (ZS_IS_KGDB(up) && (ch == '\003')) {
			breakpoint();
			return;
		}
#endif

		/* A real serial line, record the character and status.  */
		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		up->port.icount.rx++;
		if (r1 & (BRK_ABRT | PAR_ERR | Rx_OVR | CRC_ERR)) {
			if (r1 & BRK_ABRT) {
				r1 &= ~(PAR_ERR | CRC_ERR);
				up->port.icount.brk++;
				if (uart_handle_break(&up->port))
					goto next_char;
			}
			else if (r1 & PAR_ERR)
				up->port.icount.parity++;
			else if (r1 & CRC_ERR)
				up->port.icount.frame++;
			if (r1 & Rx_OVR)
				up->port.icount.overrun++;
			r1 &= up->port.read_status_mask;
			if (r1 & BRK_ABRT)
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			else if (r1 & PAR_ERR)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (r1 & CRC_ERR)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&up->port, ch, regs))
			goto next_char;

		if (up->port.ignore_status_mask == 0xff ||
		    (r1 & up->port.ignore_status_mask) == 0) {
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

static void sunzilog_status_handle(struct uart_sunzilog_port *up,
				   struct zilog_channel *channel,
				   struct pt_regs *regs)
{
	unsigned char status;

	status = sbus_readb(&channel->control);
	ZSDELAY();

	sbus_writeb(RES_EXT_INT, &channel->control);
	ZSDELAY();
	ZS_WSYNC(channel);

	if ((status & BRK_ABRT) && ZS_IS_MOUSE(up))
		sunzilog_kbdms_receive_chars(up, 0, 1, regs);

	if (ZS_WANTS_MODEM_STATUS(up)) {
		if (status & SYNC)
			up->port.icount.dsr++;

		/* The Zilog just gives us an interrupt when DCD/CTS/etc. change.
		 * But it does not tell us which bit has changed, we have to keep
		 * track of this ourselves.
		 */
		if ((status & DCD) ^ up->prev_status)
			uart_handle_dcd_change(&up->port,
					       (status & DCD));
		if ((status & CTS) ^ up->prev_status)
			uart_handle_cts_change(&up->port,
					       (status & CTS));

		wake_up_interruptible(&up->port.info->delta_msr_wait);
	}

	up->prev_status = status;
}

static void sunzilog_transmit_chars(struct uart_sunzilog_port *up,
				    struct zilog_channel *channel)
{
	struct circ_buf *xmit = &up->port.info->xmit;

	if (ZS_IS_CONS(up)) {
		unsigned char status = sbus_readb(&channel->control);
		ZSDELAY();

		/* TX still busy?  Just wait for the next TX done interrupt.
		 *
		 * It can occur because of how we do serial console writes.  It would
		 * be nice to transmit console writes just like we normally would for
		 * a TTY line. (ie. buffered and TX interrupt driven).  That is not
		 * easy because console writes cannot sleep.  One solution might be
		 * to poll on enough port->xmit space becomming free.  -DaveM
		 */
		if (!(status & Tx_BUF_EMP))
			return;
	}

	if (ZS_REGS_HELD(up)) {
		__load_zsregs(channel, up->curregs);
		up->flags &= ~SUNZILOG_FLAG_REGS_HELD;
	}

	if (ZS_TX_STOPPED(up)) {
		up->flags &= ~SUNZILOG_FLAG_TX_STOPPED;
		goto disable_tx_int;
	}

	if (up->port.x_char) {
		sbus_writeb(up->port.x_char, &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port))
		goto disable_tx_int;

	sbus_writeb(xmit->buf[xmit->tail], &channel->data);
	ZSDELAY();
	ZS_WSYNC(channel);

	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	up->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_event(&up->port, EVT_WRITE_WAKEUP);

	if (!uart_circ_empty(xmit))
		return;

disable_tx_int:
	up->curregs[R5] &= ~TxENAB;
	write_zsreg(ZILOG_CHANNEL_FROM_PORT(&up->port), R5, up->curregs[R5]);
}

static void sunzilog_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_sunzilog_port *up = dev_id;

	while (up) {
		struct zilog_channel *channel
			= ZILOG_CHANNEL_FROM_PORT(&up->port);
		unsigned char r3;

		spin_lock(&up->port.lock);
		r3 = read_zsreg(channel, 3);

		/* Channel A */
		if (r3 & (CHAEXT | CHATxIP | CHARxIP)) {
			sbus_writeb(RES_H_IUS, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);

			if (r3 & CHARxIP)
				sunzilog_receive_chars(up, channel, regs);
			if (r3 & CHAEXT)
				sunzilog_status_handle(up, channel, regs);
			if (r3 & CHATxIP)
				sunzilog_transmit_chars(up, channel);
		}
		spin_unlock(&up->port.lock);

		/* Channel B */
		up = up->next;
		channel = ZILOG_CHANNEL_FROM_PORT(&up->port);

		spin_lock(&up->port.lock);
		if (r3 & (CHBEXT | CHBTxIP | CHBRxIP)) {
			sbus_writeb(RES_H_IUS, &channel->control);
			ZSDELAY();
			ZS_WSYNC(channel);

			if (r3 & CHBRxIP)
				sunzilog_receive_chars(up, channel, regs);
			if (r3 & CHBEXT)
				sunzilog_status_handle(up, channel, regs);
			if (r3 & CHBTxIP)
				sunzilog_transmit_chars(up, channel);
		}
		spin_unlock(&up->port.lock);

		up = up->next;
	}
}

/* A convenient way to quickly get R0 status.  The caller must _not_ hold the
 * port lock, it is acquired here.
 */
static __inline__ unsigned char sunzilog_read_channel_status(struct uart_port *port)
{
	struct zilog_channel *channel;
	unsigned long flags;
	unsigned char status;

	spin_lock_irqsave(&port->lock, flags);

	channel = ZILOG_CHANNEL_FROM_PORT(port);
	status = sbus_readb(&channel->control);
	ZSDELAY();

	spin_unlock_irqrestore(&port->lock, flags);

	return status;
}

/* The port lock is not held.  */
static unsigned int sunzilog_tx_empty(struct uart_port *port)
{
	unsigned char status;
	unsigned int ret;

	status = sunzilog_read_channel_status(port);
	if (status & Tx_BUF_EMP)
		ret = TIOCSER_TEMT;
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
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel *channel = ZILOG_CHANNEL_FROM_PORT(port);
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

	/* NOTE: Not subject to 'transmitter active' rule.  */ 
	up->curregs[R5] |= set_bits;
	up->curregs[R5] &= ~clear_bits;
	write_zsreg(channel, R5, up->curregs[R5]);
}

/* The port lock is held and interrupts are disabled.  */
static void sunzilog_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;

	up->flags |= SUNZILOG_FLAG_TX_STOPPED;
}

/* The port lock is held and interrupts are disabled.  */
static void sunzilog_start_tx(struct uart_port *port, unsigned int tty_start)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel *channel = ZILOG_CHANNEL_FROM_PORT(port);
	unsigned char status;

	up->flags |= SUNZILOG_FLAG_TX_ACTIVE;
	up->flags &= ~SUNZILOG_FLAG_TX_STOPPED;

	/* Enable the transmitter.  */
	if (!(up->curregs[R5] & TxENAB)) {
		/* NOTE: Not subject to 'transmitter active' rule.  */ 
		up->curregs[R5] |= TxENAB;
		write_zsreg(channel, R5, up->curregs[R5]);
	}

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

		sbus_writeb(xmit->buf[xmit->tail], &channel->data);
		ZSDELAY();
		ZS_WSYNC(channel);

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_event(&up->port, EVT_WRITE_WAKEUP);
	}
}

/* The port lock is not held.  */
static void sunzilog_stop_rx(struct uart_port *port)
{
	struct uart_sunzilog_port *up = UART_ZILOG(port);
	struct zilog_channel *channel;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	channel = ZILOG_CHANNEL_FROM_PORT(port);

	/* Disable all RX interrupts.  */
	up->curregs[R1] &= ~RxINT_MASK;
	sunzilog_maybe_update_regs(up, channel);

	spin_unlock_irqrestore(&port->lock, flags);
}

/* The port lock is not held.  */
static void sunzilog_enable_ms(struct uart_port *port)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel *channel = ZILOG_CHANNEL_FROM_PORT(port);
	unsigned char new_reg;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	new_reg = up->curregs[R15] | (DCDIE | SYNCIE | CTSIE);
	if (new_reg != up->curregs[R15]) {
		up->curregs[R15] = new_reg;

		/* NOTE: Not subject to 'transmitter active' rule.  */ 
		write_zsreg(channel, R15, up->curregs[R15]);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

/* The port lock is not held.  */
static void sunzilog_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	struct zilog_channel *channel = ZILOG_CHANNEL_FROM_PORT(port);
	unsigned char set_bits, clear_bits, new_reg;
	unsigned long flags;

	set_bits = clear_bits = 0;

	if (break_state)
		set_bits |= SND_BRK;
	else
		clear_bits |= SND_BRK;

	spin_lock_irqsave(&port->lock, flags);

	new_reg = (up->curregs[R5] | set_bits) & ~clear_bits;
	if (new_reg != up->curregs[R5]) {
		up->curregs[R5] = new_reg;

		/* NOTE: Not subject to 'transmitter active' rule.  */ 
		write_zsreg(channel, R5, up->curregs[R5]);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static int sunzilog_startup(struct uart_port *port)
{
	struct uart_sunzilog_port *up = UART_ZILOG(port);
	struct zilog_channel *channel;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	channel = ZILOG_CHANNEL_FROM_PORT(port);
	up->prev_status = sbus_readb(&channel->control);

	/* Enable receiver and transmitter.  */
	up->curregs[R3] |= RxENAB;
	up->curregs[R5] |= TxENAB;

	/* Enable RX and status interrupts.  TX interrupts are enabled
	 * as needed.
	 */
	up->curregs[R1] |= EXT_INT_ENAB | INT_ALL_Rx;
	up->curregs[R9] |= MIE;
	sunzilog_maybe_update_regs(up, channel);

	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static void sunzilog_shutdown(struct uart_port *port)
{
	struct uart_sunzilog_port *up = UART_ZILOG(port);
	struct zilog_channel *channel;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	channel = ZILOG_CHANNEL_FROM_PORT(port);

	/* Disable receiver and transmitter.  */
	up->curregs[R3] &= ~RxENAB;
	up->curregs[R5] &= ~TxENAB;

	/* Disable all interrupts and BRK assertion.  */
	up->curregs[R1] &= ~(EXT_INT_ENAB | TxINT_ENAB | RxINT_MASK);
	up->curregs[R5] &= ~SND_BRK;
	up->curregs[R9] &= ~MIE;
	sunzilog_maybe_update_regs(up, channel);

	spin_unlock_irqrestore(&port->lock, flags);
}

/* Shared by TTY driver and serial console setup.  The port lock is held
 * and local interrupts are disabled.
 */
static void
sunzilog_convert_to_zs(struct uart_sunzilog_port *up, unsigned int cflag,
		       unsigned int iflag, int brg)
{

	/* Don't modify MIE. */
	up->curregs[R9] |= NV;

	up->curregs[R10] = NRZ;
	up->curregs[11] = TCBR | RCBR;

	/* Program BAUD and clock source. */
	up->curregs[4] &= ~XCLK_MASK;
	up->curregs[4] |= X16CLK;
	up->curregs[12] = brg & 0xff;
	up->curregs[13] = (brg >> 8) & 0xff;
	up->curregs[14] = BRSRC | BRENAB;

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

	up->port.read_status_mask = Rx_OVR;
	if (iflag & INPCK)
		up->port.read_status_mask |= CRC_ERR | PAR_ERR;
	if (iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= BRK_ABRT;

	up->port.ignore_status_mask = 0;
	if (iflag & IGNPAR)
		up->port.ignore_status_mask |= CRC_ERR | PAR_ERR;
	if (iflag & IGNBRK) {
		up->port.ignore_status_mask |= BRK_ABRT;
		if (iflag & IGNPAR)
			up->port.ignore_status_mask |= Rx_OVR;
	}

	if ((cflag & CREAD) == 0)
		up->port.ignore_status_mask = 0xff;
}

/* The port lock is not held.  */
static void
sunzilog_change_speed(struct uart_port *port, unsigned int cflag,
		      unsigned int iflag, unsigned int quot)
{
	struct uart_sunzilog_port *up = (struct uart_sunzilog_port *) port;
	unsigned long flags;
	int baud, brg;

	spin_lock_irqsave(&up->port.lock, flags);

	baud = (ZS_CLOCK / (quot * 16));
	brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);

	sunzilog_convert_to_zs(up, cflag, iflag, brg);

	if (UART_ENABLE_MS(&up->port, cflag))
		up->flags |= SUNZILOG_FLAG_MODEM_STATUS;
	else
		up->flags &= ~SUNZILOG_FLAG_MODEM_STATUS;

	up->cflag = cflag;

	sunzilog_maybe_update_regs(up, ZILOG_CHANNEL_FROM_PORT(port));

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
	return 0;
}

/* These do not need to do anything interesting either.  */
static void sunzilog_config_port(struct uart_port *port, int flags)
{
}

/* We do not support letting the user mess with the divisor, IRQ, etc. */
static int sunzilog_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return -EINVAL;
}

static struct uart_ops sunzilog_pops = {
	.tx_empty	=	sunzilog_tx_empty,
	.set_mctrl	=	sunzilog_set_mctrl,
	.get_mctrl	=	sunzilog_get_mctrl,
	.stop_tx	=	sunzilog_stop_tx,
	.start_tx	=	sunzilog_start_tx,
	.stop_rx	=	sunzilog_stop_rx,
	.enable_ms	=	sunzilog_enable_ms,
	.break_ctl	=	sunzilog_break_ctl,
	.startup	=	sunzilog_startup,
	.shutdown	=	sunzilog_shutdown,
	.change_speed	=	sunzilog_change_speed,
	.type		=	sunzilog_type,
	.release_port	=	sunzilog_release_port,
	.request_port	=	sunzilog_request_port,
	.config_port	=	sunzilog_config_port,
	.verify_port	=	sunzilog_verify_port,
};

static struct uart_sunzilog_port *sunzilog_port_table;
static struct zilog_layout **sunzilog_chip_regs;
static int *sunzilog_nodes;

static struct uart_sunzilog_port *sunzilog_irq_chain;
static int zilog_irq = -1;

static struct uart_driver sunzilog_reg = {
	.owner		=	THIS_MODULE,
	.driver_name	=	"ttyS",
#ifdef CONFIG_DEVFS_FS
	.dev_name	=	"ttyS%d",
#else
	.dev_name	=	"ttyS",
#endif
	.major		=	TTY_MAJOR,
	.minor		=	64,
};

static void * __init alloc_one_table(unsigned long size)
{
	void *ret;

	ret = kmalloc(size, GFP_KERNEL);
	if (ret != NULL)
		memset(ret, 0, size);

	return ret;
}

static void __init sunzilog_alloc_tables(void)
{
	sunzilog_port_table = (struct uart_sunzilog_port *)
		alloc_one_table(NUM_CHANNELS * sizeof(struct uart_sunzilog_port));
	sunzilog_chip_regs = (struct zilog_layout **)
		alloc_one_table(NUM_SUNZILOG * sizeof(struct zilog_layout *));
	sunzilog_nodes = (int *)
		alloc_one_table(NUM_SUNZILOG * sizeof(int));

	if (sunzilog_port_table == NULL ||
	    sunzilog_chip_regs == NULL ||
	    sunzilog_nodes == NULL) {
		prom_printf("sunzilog_init: Cannot alloc SunZilog tables.\n");
		prom_halt();
	}
}

#ifdef CONFIG_SPARC64

/* We used to attempt to use the address property of the Zilog device node
 * but that totally is not necessary on sparc64.
 */
static struct zilog_layout * __init get_zs_sun4u(int chip)
{
	unsigned long mapped_addr = 0xdeadbeefUL;
	unsigned int sun4u_ino;
	int busnode, zsnode, seen;

	if (central_bus)
		busnode = central_bus->child->prom_node;
	else
		busnode = prom_searchsiblings(prom_getchild(prom_root_node), "sbus");

	if (busnode == 0 || busnode == -1) {
		prom_printf("SunZilog: Cannot find bus of Zilog %d in get_zs_sun4u.\n",
			    chip);
		prom_halt();
	}

	zsnode = prom_getchild(busnode);
	seen = 0;
	while (zsnode) {
		int slave;

		zsnode = prom_searchsiblings(zsnode, "zs");
		if (zsnode == 0 || zsnode == -1)
			break;
		slave = prom_getintdefault(zsnode, "slave", -1);
		if ((slave == chip) || (seen == chip)) {
			struct sbus_bus *sbus = NULL;
			struct sbus_dev *sdev = NULL;
			int err;

			if (central_bus == NULL) {
				for_each_sbus(sbus) {
					for_each_sbusdev(sdev, sbus) {
						if (sdev->prom_node == zsnode)
							goto found;
					}
				}
			}
		found:
			if (sdev == NULL && central_bus == NULL) {
				prom_printf("SunZilog: sdev&&central == NULL for "
					    "Zilog %d in get_zs_sun4u.\n", chip);
				prom_halt();
			}
			if (central_bus == NULL) {
				mapped_addr =
					sbus_ioremap(&sdev->resource[0], 0,
						     PAGE_SIZE,
						     "Zilog Registers");
			} else {
				struct linux_prom_registers zsregs[1];

				err = prom_getproperty(zsnode, "reg",
						       (char *) &zsregs[0],
						       sizeof(zsregs));
				if (err == -1) {
					prom_printf("SunZilog: Cannot map "
						    "Zilog %d regs on "
						    "central bus.\n", chip);
					prom_halt();
				}
				apply_fhc_ranges(central_bus->child,
						 &zsregs[0], 1);
				apply_central_ranges(central_bus, &zsregs[0], 1);
				mapped_addr =
					(((u64)zsregs[0].which_io)<<32UL) |
					((u64)zsregs[0].phys_addr);
			}

			sunzilog_nodes[chip] = zsnode;
			if (zilog_irq == -1) {
				if (central_bus) {
					unsigned long iclr, imap;

					iclr = central_bus->child->fhc_regs.uregs
						+ FHC_UREGS_ICLR;
					imap = central_bus->child->fhc_regs.uregs
						+ FHC_UREGS_IMAP;
					zilog_irq = build_irq(12, 0, iclr, imap);
				} else {
					err = prom_getproperty(zsnode, "interrupts",
							       (char *) &sun4u_ino,
							       sizeof(sun4u_ino));
					zilog_irq = sbus_build_irq(sbus_root, sun4u_ino);
				}
			}
			break;
		}
		zsnode = prom_getsibling(zsnode);
		seen++;
	}

	if (zsnode == 0 || zsnode == -1) {
		prom_printf("SunZilog: Cannot find Zilog %d in get_zs_sun4u.\n", chip);
		prom_halt();
	}

	return (struct zilog_layout *) mapped_addr;
}
#else /* CONFIG_SPARC64 */
static struct zilog_layout * __init get_zs_sun4cmd(int chip)
{
	struct linux_prom_irqs irq_info[2];
	unsigned long mapped_addr;
	int zsnode, chipid, cpunode;

	if (sparc_cpu_model == sun4d) {
		int bbnode, walk, no;

		zsnode = 0;
		bbnode = 0;
		no = 0;
		for (walk = prom_getchild(prom_root_node);
		     (walk = prom_searchsiblings(walk, "cpu-unit")) != 0;
		     walk = prom_getsibling(walk)) {
			bbnode = prom_getchild(walk);
			if (bbnode &&
			    (bbnode = prom_searchsiblings(bbnode, "bootbus"))) {
				if (no == (chip / 2)) {
					cpunode = walk;
					zsnode = prom_getchild(bbnode);
					chipid = (chip & 1);
					break;
				}
				no++;
			}
		}
		if (!walk) {
			prom_printf("SunZilog: Cannot find the %d'th bootbus on sun4d.\n",
				    (chip / 2));
			prom_halt();
		}
	} else {
		int tmp;

		zsnode = prom_getchild(prom_root_node);
		if ((tmp = prom_searchsiblings(zsnode, "obio"))) {
			zsnode = prom_getchild(tmp);
			if (!zsnode) {
				prom_printf("SunZilog: Child of obio node does "
					    "not exist.\n");
				prom_halt();
			}
		}
		chipid = 0;
	}

	while (zsnode) {
		struct linux_prom_registers zsreg[4];
		struct resource res;

		zsnode = prom_searchsiblings(zsnode, "zs");
		if (zsnode == 0 || zsnode == -1)
			break;

		if (prom_getintdefault(zsnode, "slave", -1) != chipid) {
			zsnode = prom_getsibling(zsnode);
			continue;
		}

		if (sparc_cpu_model == sun4d) {
			/* Sun4d Zilog nodes lack the address property, so just
			 * map it like a normal device.
			 */
			if (prom_getproperty(zsnode, "reg",
					     (char *) zsreg, sizeof(zsreg)) == -1) {
				prom_printf("SunZilog: Cannot map Zilog %d "
					    "regs on sun4c.\n", chip);
				prom_halt();
			}
			prom_apply_generic_ranges(bbnode, cpunode, zsreg, 1);
			res.start = zsreg[0].phys_addr;
			res.end = res.start + (8 - 1);
			res.flags = zsreg[0].which_io | IORESOURCE_IO;
			mapped_addr = sbus_ioremap(&res, 0, 8, "Zilog Serial");
		} else {
			unsigned int vaddr[2];

			if (prom_getproperty(zsnode, "address",
					     (void *) vaddr, sizeof(vaddr))
			    % sizeof(unsigned int)) {
				prom_printf("SunZilog: Cannot get address property for "
					    "Zilog %d.\n", chip);
				prom_halt();
			}
			mapped_addr = (unsigned long) vaddr[0];
		}
		sunzilog_nodes[chip] = zsnode;
		if (prom_getproperty(zsnode, "intr",
				     (char *) irq_info, sizeof(irq_info))
		    % sizeof(struct linux_prom_irqs)) {
			prom_printf("SunZilog: Cannot get IRQ property for "
				    "Zilog %d.\n", chip);
			prom_halt();
		}
		if (zilog_irq == -1) {
			zilog_irq = irq_info[0].pri;
		} else if (zilog_irq != irq_info[0].pri) {
			prom_printf("SunZilog: Inconsistent IRQ layout for Zilog %d.\n",
				    chip);
			promt_halt();
		}
		break;
	}
	if (!zsnode) {
		prom_printf("SunZilog: OBP node for Zilog %d not found.\n", chip);
		prom_halt();
	}

	return (struct zilog_layout *) mapped_addr;
}
#endif /* !(CONFIG_SPARC64) */

/* Get the address of the registers for SunZilog instance CHIP.  */
static struct zilog_layout * __init get_zs(int chip)
{
	if (chip < 0 || chip >= NUM_SUNZILOG) {
		prom_printf("SunZilog: Illegal chip number %d in get_zs.\n", chip);
		prom_halt();
	}

#ifdef CONFIG_SPARC64
	return get_zs_sun4u(chip);
#else

	if (sparc_cpu_model == sun4) {
		struct resource res;

		/* Not probe-able, hard code it. */
		switch (chip) {
		case 0:
			res.start = 0xf1000000;
			break;
		case 1:
			res.start = 0xf0000000;
			break;
		};
		sunzilog_nodes[chip] = 0;
		zilog_irq = 12;
		res.end = (res.start + (8 - 1));
		res.flags = IORESOURCE_IO;
		return sbus_ioremap(&res, 0, 8, "SunZilog");
	}

	return get_zs_sun4cmd(chip);
#endif
}

#define ZS_PUT_CHAR_MAX_DELAY	2000	/* 10 ms */

static void sunzilog_put_char(struct zilog_channel *channel, unsigned char ch)
{
	int loops = ZS_PUT_CHAR_MAX_DELAY;

	/* This is a timed polling loop so do not switch the explicit
	 * udelay with ZSDELAY as that is a NOP on some platforms.  -DaveM
	 */
	do {
		unsigned char val = sbus_readb(&channel->control);
		if (val & Tx_BUF_EMP) {
			ZSDELAY();
			break;
		}
		udelay(5);
	} while (--loops);

	sbus_writeb(ch, &channel->data);
	ZSDELAY();
	ZS_WSYNC(channel);
}

#ifdef CONFIG_SERIO

static spinlock_t sunzilog_serio_lock = SPIN_LOCK_UNLOCKED;

static int sunzilog_serio_write(struct serio *serio, unsigned char ch)
{
	struct uart_sunzilog_port *up = serio->driver;
	unsigned long flags;

	spin_lock_irqsave(&sunzilog_serio_lock, flags);

	sunzilog_put_char(ZILOG_CHANNEL_FROM_PORT(&up->port), ch);

	spin_unlock_irqrestore(&sunzilog_serio_lock, flags);

	return 0;
}

static int sunzilog_serio_open(struct serio *serio)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sunzilog_serio_lock, flags);
	if (serio->private == NULL) {
		serio->private = (void *) -1L;
		ret = 0;
	} else
		ret = -EBUSY;
	spin_unlock_irqrestore(&sunzilog_serio_lock, flags);

	return ret;
}

static void sunzilog_serio_close(struct serio *serio)
{
	unsigned long flags;

	spin_lock_irqsave(&sunzilog_serio_lock, flags);
	serio->private = NULL;
	spin_unlock_irqrestore(&sunzilog_serio_lock, flags);
}

#endif /* CONFIG_SERIO */

static void
sunzilog_console_write(struct console *con, const char *s, unsigned int count)
{
	struct uart_sunzilog_port *up = &sunzilog_port_table[con->index];
	struct zilog_channel *channel = ZILOG_CHANNEL_FROM_PORT(&up->port);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&up->port.lock, flags);
	for (i = 0; i < count; i++, s++) {
		sunzilog_put_char(channel, *s);
		if (*s == 10)
			sunzilog_put_char(channel, 13);
	}
	udelay(2);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static kdev_t sunzilog_console_device(struct console *con)
{
	return mk_kdev(TTY_MAJOR, 64 + con->index);
}

static int __init sunzilog_console_setup(struct console *con, char *options)
{
	struct uart_sunzilog_port *up = &sunzilog_port_table[con->index];
	unsigned long flags;
	int baud, brg;

	printk("Console: ttyS%d (Zilog8530)\n", con->index / 2);

	/* Get firmware console settings.  */
	sunserial_console_termios(con);

	/* Firmware console speed is limited to 150-->38400 baud so
	 * this hackish cflag thing is OK.
	 */
	switch (con->cflag & CBAUD) {
	case B150: baud = 150; break;
	case B300: baud = 300; break;
	case B600: baud = 600; break;
	case B1200: baud = 1200; break;
	case B2400: baud = 2400; break;
	case B4800: baud = 4800; break;
	default: case B9600: baud = 9600; break;
	case B19200: baud = 19200; break;
	case B38400: baud = 38400; break;
	};

	brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);

	/*
	 * Temporary fix.
	 */
	spin_lock_init(&up->port.lock);

	spin_lock_irqsave(&up->port.lock, flags);

	up->curregs[R15] = BRKIE;

	sunzilog_convert_to_zs(up, con->cflag, 0, brg);

	spin_unlock_irqrestore(&up->port.lock, flags);

	sunzilog_set_mctrl(&up->port, TIOCM_DTR | TIOCM_RTS);
	sunzilog_startup(&up->port);

	return 0;
}

static struct console sunzilog_console = {
	.name	=	"ttyS",
	.write	=	sunzilog_console_write,
	.device	=	sunzilog_console_device,
	.setup	=	sunzilog_console_setup,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
};

static int __init sunzilog_console_init(void)
{
	if (con_is_present())
		return 0;

	sunzilog_console.index = serial_console - 1;
	register_console(&sunzilog_console);
	return 0;
}

static void __init sunzilog_prepare(void)
{
	struct uart_sunzilog_port *up;
	struct zilog_layout *rp;
	int channel, chip;

	sunzilog_irq_chain = up = &sunzilog_port_table[0];
	for (channel = 0; channel < NUM_CHANNELS - 1; channel++)
		up[channel].next = &up[channel + 1];
	up[channel].next = NULL;

	for (chip = 0; chip < NUM_SUNZILOG; chip++) {
		if (!sunzilog_chip_regs[chip]) {
			sunzilog_chip_regs[chip] = rp = get_zs(chip);

			up[(chip * 2) + 0].port.membase = (char *) &rp->channelA;
			up[(chip * 2) + 1].port.membase = (char *) &rp->channelB;
		}

		/* Channel A */
		up[(chip * 2) + 0].port.iotype = SERIAL_IO_MEM;
		up[(chip * 2) + 0].port.irq = zilog_irq;
		up[(chip * 2) + 0].port.uartclk = ZS_CLOCK;
		up[(chip * 2) + 0].port.fifosize = 1;
		up[(chip * 2) + 0].port.ops = &sunzilog_pops;
		up[(chip * 2) + 0].port.flags = 0;
		up[(chip * 2) + 0].port.line = (chip * 2) + 0;
		up[(chip * 2) + 0].flags |= SUNZILOG_FLAG_IS_CHANNEL_A;

		/* Channel B */
		up[(chip * 2) + 1].port.iotype = SERIAL_IO_MEM;
		up[(chip * 2) + 1].port.irq = zilog_irq;
		up[(chip * 2) + 1].port.uartclk = ZS_CLOCK;
		up[(chip * 2) + 1].port.fifosize = 1;
		up[(chip * 2) + 1].port.ops = &sunzilog_pops;
		up[(chip * 2) + 1].port.flags = 0;
		up[(chip * 2) + 1].port.line = (chip * 2) + 1;
		up[(chip * 2) + 1].flags |= 0;
	}
}

static void __init sunzilog_init_kbdms(struct uart_sunzilog_port *up, int channel)
{
	int baud, brg;

	if (channel == KEYBOARD_LINE) {
		up->flags |= SUNZILOG_FLAG_CONS_KEYB;
		up->cflag = B1200 | CS8 | CLOCAL | CREAD;
		baud = 1200;
	} else {
		up->flags |= SUNZILOG_FLAG_CONS_MOUSE;
		up->cflag = B4800 | CS8 | CLOCAL | CREAD;
		baud = 4800;
	}
	printk(KERN_INFO "zs%d at 0x%p (irq = %s) is a Zilog8530\n",
	       channel, up->port.membase, __irq_itoa(zilog_irq));

	up->curregs[R15] = BRKIE;
	brg = BPS_TO_BRG(baud, ZS_CLOCK / ZS_CLOCK_DIVISOR);
	sunzilog_convert_to_zs(up, up->cflag, 0, brg);

#ifdef CONFIG_SERIO
	memset(&up->serio, 0, sizeof(up->serio));

	up->serio.driver = up;

	up->serio.type = SERIO_RS232;
	if (channel == KEYBOARD_LINE) {
		up->serio.type |= SERIO_SUNKBD;
		up->serio.name = "zskbd";
	} else {
		up->serio.type |= SERIO_SUN;
		up->serio.name = "zsms";
	}
	up->serio.phys = (channel == KEYBOARD_LINE ?
			  "zs/serio0" : "zs/serio1");

	up->serio.write = sunzilog_serio_write;
	up->serio.open = sunzilog_serio_open;
	up->serio.close = sunzilog_serio_close;

	serio_register_port(&up->serio);
#endif

	spin_unlock(&up->port.lock);
	sunzilog_set_mctrl(&up->port, TIOCM_DTR | TIOCM_RTS);
	sunzilog_startup(&up->port);
	spin_lock(&up->port.lock);
}

static void __init sunzilog_init_hw(void)
{
	int i;

	for (i = 0; i < NUM_CHANNELS; i++) {
		struct uart_sunzilog_port *up = &sunzilog_port_table[i];
		struct zilog_channel *channel = ZILOG_CHANNEL_FROM_PORT(&up->port);
		unsigned long flags;
		int baud, brg;

		spin_lock_irqsave(&up->port.lock, flags);

		if (ZS_IS_CHANNEL_A(up)) {
			write_zsreg(channel, R9, FHWRES);
			ZSDELAY_LONG();
			(void) read_zsreg(channel, R0);
		}

		if (i == KEYBOARD_LINE || i == MOUSE_LINE) {
			sunzilog_init_kbdms(up, i);
		} else if (ZS_IS_CONS(up)) {
			/* sunzilog_console_setup takes care of this */
		} else {
			/* Normal serial TTY. */
			up->parity_mask = 0xff;
			up->curregs[R3] = RxENAB;
			up->curregs[R5] = TxENAB;
			up->curregs[R9] = NV | MIE;
			up->curregs[R10] = NRZ;
			up->curregs[R11] = TCBR | RCBR;
			baud = 9600;
			brg = BPS_TO_BRG(baud, (ZS_CLOCK / ZS_CLOCK_DIVISOR));
			up->curregs[R12] = (brg & 0xff);
			up->curregs[R13] = (brg >> 8) & 0xff;
			up->curregs[R14] = BRSRC | BRENAB;
			sunzilog_maybe_update_regs(up, channel);
		}

		spin_unlock_irqrestore(&up->port.lock, flags);
	}
}

static int __init sunzilog_ports_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: Sun Zilog driver.\n");

	sunzilog_prepare();

	if (request_irq(zilog_irq, sunzilog_interrupt, SA_SHIRQ,
			"SunZilog", sunzilog_irq_chain)) {
		prom_printf("SunZilog: Unable to register zs interrupt handler.\n");
		prom_halt();
	}

	sunzilog_init_hw();

	/* We can only init this once we have probed the Zilogs
	 * in the system.
	 */
	sunzilog_reg.nr = NUM_CHANNELS;

#ifdef CONFIG_SERIAL_CONSOLE
	sunzilog_reg.cons = &sunzilog_console;
#else
	sunzilog_reg.cons = NULL;
#endif

	ret = uart_register_driver(&sunzilog_reg);
	if (ret == 0) {
		int i;

		for (i = 0; i < NUM_CHANNELS; i++) {
			struct uart_sunzilog_port *up = &sunzilog_port_table[i];

			if (ZS_IS_KEYB(up) || ZS_IS_MOUSE(up))
				continue;

			uart_add_one_port(&sunzilog_reg, &up->port);
		}
	}

	return ret;
}

static int __init sunzilog_init(void)
{
	int node;

	/* Sun4 Zilog setup is hard coded, no probing to do.  */
	if (sparc_cpu_model == sun4) {
		NUM_SUNZILOG = 2;
		goto no_probe;
	}

	node = prom_getchild(prom_root_node);
	if (sparc_cpu_model == sun4d) {
		int bbnode;

		NUM_SUNZILOG = 0;
		while (node &&
		       (node = prom_searchsiblings(node, "cpu-unit"))) {
			bbnode = prom_getchild(node);
			if (bbnode && prom_searchsiblings(bbnode, "bootbus"))
				NUM_SUNZILOG += 2;
			node = prom_getsibling(node);
		}
		goto no_probe;
	} else if (sparc_cpu_model == sun4u) {
		int central_node;

		/* Central bus zilogs must be checked for first,
		 * since Enterprise boxes might have SBUSes as well.
		 */
		central_node = prom_finddevice("/central");
		if (central_node != 0 && central_node != -1)
			node = prom_searchsiblings(prom_getchild(central_node), "fhc");
		else
			node = prom_searchsiblings(node, "sbus");
		if (node != 0 && node != -1)
			node = prom_getchild(node);
		if (node == 0 || node == -1)
			return -ENODEV;
	} else {
		node = prom_searchsiblings(node, "obio");
		if (node)
			node = prom_getchild(node);
		NUM_SUNZILOG = 2;
		goto no_probe;
	}

	node = prom_searchsiblings(node, "zs");
	if (!node)
		return -ENODEV;
		
	NUM_SUNZILOG = 2;

no_probe:

	sunzilog_alloc_tables();

	sunzilog_ports_init();
	sunzilog_console_init();

	return 0;
}

static void __exit sunzilog_exit(void)
{
	int i;

	for (i = 0; i < NUM_CHANNELS; i++) {
		struct uart_sunzilog_port *up = &sunzilog_port_table[i];

		if (ZS_IS_KEYB(up) || ZS_IS_MOUSE(up))
			continue;

		uart_remove_one_port(&sunzilog_reg, &up->port);
	}

	uart_unregister_driver(&sunzilog_reg);
}

module_init(sunzilog_init);
module_exit(sunzilog_exit);

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("David S. Miller");
MODULE_DESCRIPTION("Sun Zilog serial port driver");
MODULE_LICENSE("GPL");
