/*
 * linux/drivers/serial/pmac_zilog.c
 * 
 * Driver for PowerMac Z85c30 based ESCC cell found in the
 * "macio" ASICs of various PowerMac models
 * 
 * Copyright (C) 2003 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 * Derived from drivers/macintosh/macserial.c by Paul Mackerras
 * and drivers/serial/sunzilog.c by David S. Miller
 *
 * Hrm... actually, I ripped most of sunzilog (Thanks David !) and
 * adapted special tweaks needed for us. I don't think it's worth
 * merging back those though. The DMA code still has to get in
 * and once done, I expect that driver to remain fairly stable in
 * the long term, unless we change the driver model again...
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
 * TODO:   - Add DMA support
 *         - Defer port shutdown to a few seconds after close
 *         - maybe put something right into up->clk_divisor
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/tty.h>

#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/slab.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/bitops.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/kgdb.h>
#include <asm/dbdma.h>
#include <asm/macio.h>

#include <linux/serial.h>
#include <linux/serial_core.h>

#include "pmac_zilog.h"


/* Not yet implemented */
#undef HAS_DBDMA

static char version[] __initdata = "pmac_zilog.c 0.5a (Benjamin Herrenschmidt <benh@kernel.crashing.org>)";
MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Driver for the PowerMac serial ports.");
MODULE_LICENSE("GPL");

#define PWRDBG(fmt, arg...)	printk(KERN_DEBUG fmt , ## arg)


/*
 * For the sake of early serial console, we can do a pre-probe
 * (optional) of the ports at rather early boot time.
 */
static struct uart_pmac_port	pmz_ports[MAX_ZS_PORTS];
static int			pmz_ports_count;


/* 
 * Load all registers to reprogram the port
 * This function must only be called when the TX is not busy.  The UART
 * port lock must be held and local interrupts disabled.
 */
static void pmz_load_zsregs(struct uart_pmac_port *up, u8 *regs)
{
	int i;

	/* Let pending transmits finish.  */
	for (i = 0; i < 1000; i++) {
		unsigned char stat = read_zsreg(up, R1);
		if (stat & ALL_SNT)
			break;
		udelay(100);
	}

	ZS_CLEARERR(up);
	zssync(up);
	ZS_CLEARFIFO(up);
	zssync(up);
	ZS_CLEARERR(up);

	/* Disable all interrupts.  */
	write_zsreg(up, R1,
		    regs[R1] & ~(RxINT_MASK | TxINT_ENAB | EXT_INT_ENAB));

	/* Set parity, sync config, stop bits, and clock divisor.  */
	write_zsreg(up, R4, regs[R4]);

	/* Set misc. TX/RX control bits.  */
	write_zsreg(up, R10, regs[R10]);

	/* Set TX/RX controls sans the enable bits.  */
	write_zsreg(up, R3, regs[R3] & ~RxENABLE);
	write_zsreg(up, R5, regs[R5] & ~TxENABLE);

	/* Synchronous mode config.  */
	write_zsreg(up, R6, regs[R6]);
	write_zsreg(up, R7, regs[R7]);

	/* Disable baud generator.  */
	write_zsreg(up, R14, regs[R14] & ~BRENAB);

	/* Clock mode control.  */
	write_zsreg(up, R11, regs[R11]);

	/* Lower and upper byte of baud rate generator divisor.  */
	write_zsreg(up, R12, regs[R12]);
	write_zsreg(up, R13, regs[R13]);
	
	/* Now rewrite R14, with BRENAB (if set).  */
	write_zsreg(up, R14, regs[R14]);

	/* External status interrupt control.  */
	write_zsreg(up, R15, regs[R15]);

	/* Reset external status interrupts.  */
	write_zsreg(up, R0, RES_EXT_INT);
	write_zsreg(up, R0, RES_EXT_INT);

	/* Rewrite R3/R5, this time without enables masked.  */
	write_zsreg(up, R3, regs[R3]);
	write_zsreg(up, R5, regs[R5]);

	/* Rewrite R1, this time without IRQ enabled masked.  */
	write_zsreg(up, R1, regs[R1]);

	/* Enable interrupts */
	write_zsreg(up, R9, regs[R9]);
}

/* 
 * We do like sunzilog to avoid disrupting pending Tx
 * Reprogram the Zilog channel HW registers with the copies found in the
 * software state struct.  If the transmitter is busy, we defer this update
 * until the next TX complete interrupt.  Else, we do it right now.
 *
 * The UART port lock must be held and local interrupts disabled.
 */
static void pmz_maybe_update_regs(struct uart_pmac_port *up)
{
#if 1
       	if (!ZS_REGS_HELD(up)) {
		if (ZS_TX_ACTIVE(up)) {
			up->flags |= PMACZILOG_FLAG_REGS_HELD;
		} else {
			pr_debug("pmz: maybe_update_regs: updating\n");
			pmz_load_zsregs(up, up->curregs);
		}
	}
#else
       	pr_debug("pmz: maybe_update_regs: updating\n");
	 pmz_load_zsregs(up, up->curregs);
#endif
}

static void pmz_receive_chars(struct uart_pmac_port *up, struct pt_regs *regs)
{
	struct tty_struct *tty = up->port.info->tty;	/* XXX info==NULL? */

	while (1) {
		unsigned char ch, r1;

		if (unlikely(tty->flip.count >= TTY_FLIPBUF_SIZE)) {
			tty->flip.work.func((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE)
				/* XXX Ignores SysRq when we need it most. Fix. */
				return;	
		}

		r1 = read_zsreg(up, R1);
		if (r1 & (PAR_ERR | Rx_OVR | CRC_ERR)) {
			write_zsreg(up, R0, ERR_RES);
			zssync(up);
		}

		ch = read_zsreg(up, R0);

		/* This funny hack depends upon BRK_ABRT not interfering
		 * with the other bits we care about in R1.
		 */
		if (ch & BRK_ABRT)
			r1 |= BRK_ABRT;

		ch = read_zsdata(up);
		ch &= up->parity_mask;

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
		ch = read_zsreg(up, R0);
		if (!(ch & Rx_CH_AV))
			break;
	}

	tty_flip_buffer_push(tty);
}

static void pmz_status_handle(struct uart_pmac_port *up, struct pt_regs *regs)
{
	unsigned char status;

	status = read_zsreg(up, R0);
	write_zsreg(up, R0, RES_EXT_INT);
	zssync(up);

	if (ZS_WANTS_MODEM_STATUS(up)) {
		if (status & SYNC_HUNT)
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

static void pmz_transmit_chars(struct uart_pmac_port *up)
{
	struct circ_buf *xmit;

	if (ZS_IS_CONS(up)) {
		unsigned char status = read_zsreg(up, R0);

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

	up->flags &= ~PMACZILOG_FLAG_TX_ACTIVE;

	if (ZS_REGS_HELD(up)) {
		pmz_load_zsregs(up, up->curregs);
		up->flags &= ~PMACZILOG_FLAG_REGS_HELD;
	}

	if (ZS_TX_STOPPED(up)) {
		up->flags &= ~PMACZILOG_FLAG_TX_STOPPED;
		goto ack_tx_int;
	}

	if (up->port.x_char) {
		up->flags |= PMACZILOG_FLAG_TX_ACTIVE;
		write_zsdata(up, up->port.x_char);
		zssync(up);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}

	if (up->port.info == NULL)
		goto ack_tx_int;
	xmit = &up->port.info->xmit;
	if (uart_circ_empty(xmit)) {
		uart_write_wakeup(&up->port);
		goto ack_tx_int;
	}
	if (uart_tx_stopped(&up->port))
		goto ack_tx_int;

	up->flags |= PMACZILOG_FLAG_TX_ACTIVE;
	write_zsdata(up, xmit->buf[xmit->tail]);
	zssync(up);

	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	up->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	return;

ack_tx_int:
	write_zsreg(up, R0, RES_Tx_P);
	zssync(up);
}

/* Hrm... we register that twice, fixme later.... */
static irqreturn_t pmz_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_pmac_port *up = dev_id;
	struct uart_pmac_port *up_a;
	struct uart_pmac_port *up_b;
	int rc = IRQ_NONE;
	u8 r3;

	up_a = ZS_IS_CHANNEL_A(up) ? up : up->mate;
	up_b = up_a->mate;
       
       	spin_lock(&up_a->port.lock);
	r3 = read_zsreg(up, R3);
	pr_debug("pmz_irq: %x\n", r3);

       	/* Channel A */
       	if (r3 & (CHAEXT | CHATxIP | CHARxIP)) {
		write_zsreg(up_a, R0, RES_H_IUS);
		zssync(up_a);		
		pr_debug("pmz: irq channel A: %x\n", r3);
		if (r3 & CHARxIP)
			pmz_receive_chars(up_a, regs);
       		if (r3 & CHAEXT)
       			pmz_status_handle(up_a, regs);
       		if (r3 & CHATxIP)
       			pmz_transmit_chars(up_a);
	        rc = IRQ_HANDLED;
       	}
       	spin_unlock(&up_a->port.lock);
	
       	spin_lock(&up_b->port.lock);
	if (r3 & (CHBEXT | CHBTxIP | CHBRxIP)) {
		write_zsreg(up_b, R0, RES_H_IUS);
		zssync(up_b);
		pr_debug("pmz: irq channel B: %x\n", r3);
       	       	if (r3 & CHBRxIP)
       			pmz_receive_chars(up_b, regs);
       		if (r3 & CHBEXT)
       			pmz_status_handle(up_b, regs);
       		if (r3 & CHBTxIP)
       			pmz_transmit_chars(up_b);
	       	rc = IRQ_HANDLED;
       	}
       	spin_unlock(&up_b->port.lock);


	return rc;
}

/*
 * Peek the status register, lock not held by caller
 */
static inline u8 pmz_peek_status(struct uart_pmac_port *up)
{
	unsigned long flags;
	u8 status;
	
	spin_lock_irqsave(&up->port.lock, flags);
	status = read_zsreg(up, R0);
	spin_unlock_irqrestore(&up->port.lock, flags);

	return status;
}

/* 
 * Check if transmitter is empty
 * The port lock is not held.
 */
static unsigned int pmz_tx_empty(struct uart_port *port)
{
	unsigned char status;

	status = pmz_peek_status(to_pmz(port));
	if (status & Tx_BUF_EMP)
		return TIOCSER_TEMT;
	return 0;
}

/* 
 * Set Modem Control (RTS & DTR) bits
 * The port lock is held and interrupts are disabled.
 * Note: Shall we really filter out RTS on external ports or
 * should that be dealt at higher level only ?
 */
static void pmz_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_pmac_port *up = to_pmz(port);
	unsigned char set_bits, clear_bits;

        /* Do nothing for irda for now... */
	if (ZS_IS_IRDA(up))
		return;

	set_bits = clear_bits = 0;

	if (ZS_IS_INTMODEM(up)) {
		if (mctrl & TIOCM_RTS)
			set_bits |= RTS;
		else
			clear_bits |= RTS;
	}
	if (mctrl & TIOCM_DTR)
		set_bits |= DTR;
	else
		clear_bits |= DTR;

	/* NOTE: Not subject to 'transmitter active' rule.  */ 
	up->curregs[R5] |= set_bits;
	up->curregs[R5] &= ~clear_bits;
	write_zsreg(up, R5, up->curregs[R5]);
	zssync(up);
}

/* 
 * Get Modem Control bits (only the input ones, the core will
 * or that with a cached value of the control ones)
 * The port lock is not held.
 */
static unsigned int pmz_get_mctrl(struct uart_port *port)
{
	unsigned char status;
	unsigned int ret;

	status = pmz_peek_status(to_pmz(port));

	ret = 0;
	if (status & DCD)
		ret |= TIOCM_CAR;
	if (status & SYNC_HUNT)
		ret |= TIOCM_DSR;
	if (status & CTS)
		ret |= TIOCM_CTS;

	return ret;
}

/* 
 * Stop TX side. Dealt like sunzilog at next Tx interrupt,
 * though for DMA, we will have to do a bit more. What is
 * the meaning of the tty_stop bit ? XXX
 * The port lock is held and interrupts are disabled.
 */
static void pmz_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	to_pmz(port)->flags |= PMACZILOG_FLAG_TX_STOPPED;
}

/* 
 * Kick the Tx side.
 * The port lock is held and interrupts are disabled.
 */
static void pmz_start_tx(struct uart_port *port, unsigned int tty_start)
{
	struct uart_pmac_port *up = to_pmz(port);
	unsigned char status;

	pr_debug("pmz: start_tx()\n");

	up->flags |= PMACZILOG_FLAG_TX_ACTIVE;
	up->flags &= ~PMACZILOG_FLAG_TX_STOPPED;

	status = read_zsreg(up, R0);

	/* TX busy?  Just wait for the TX done interrupt.  */
	if (!(status & Tx_BUF_EMP))
		return;

	/* Send the first character to jump-start the TX done
	 * IRQ sending engine.
	 */
	if (port->x_char) {
		write_zsdata(up, port->x_char);
		zssync(up);
		port->icount.tx++;
		port->x_char = 0;
	} else {
		struct circ_buf *xmit = &port->info->xmit;

		write_zsdata(up, xmit->buf[xmit->tail]);
		zssync(up);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_write_wakeup(&up->port);
	}
	pr_debug("pmz: start_tx() done.\n");
}

/* 
 * Stop Rx side, basically disable emitting of
 * Rx interrupts on the port
 * The port lock is held.
 */
static void pmz_stop_rx(struct uart_port *port)
{
	struct uart_pmac_port *up = to_pmz(port);

	if (ZS_IS_CONS(up))
		return;

	pr_debug("pmz: stop_rx()()\n");

	/* Disable all RX interrupts.  */
	up->curregs[R1] &= ~RxINT_MASK;
	pmz_maybe_update_regs(up);

	pr_debug("pmz: stop_rx() done.\n");
}

/* 
 * Enable modem status change interrupts
 * The port lock is not held.
 */
static void pmz_enable_ms(struct uart_port *port)
{
	struct uart_pmac_port *up = to_pmz(port);
	unsigned char new_reg;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	new_reg = up->curregs[R15] | (DCDIE | SYNCIE | CTSIE);
	if (new_reg != up->curregs[R15]) {
		up->curregs[R15] = new_reg;

		/* NOTE: Not subject to 'transmitter active' rule.  */ 
		write_zsreg(up, R15, up->curregs[R15]);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

/* 
 * Control break state emission
 * The port lock is not held.
 */
static void pmz_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_pmac_port *up = to_pmz(port);
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
		write_zsreg(up, R5, up->curregs[R5]);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

/*
 * Turn power on or off to the SCC and associated stuff
 * (port drivers, modem, IR port, etc.)
 * Returns the number of milliseconds we should wait before
 * trying to use the port.
 */
static int pmz_set_scc_power(struct uart_pmac_port *up, int state)
{
	int delay = 0;

	if (state) {
		pmac_call_feature(
			PMAC_FTR_SCC_ENABLE, up->node, up->port_type, 1);
		if (ZS_IS_INTMODEM(up)) {
			pmac_call_feature(
				PMAC_FTR_MODEM_ENABLE, up->node, 0, 1);
			delay = 2500;	/* wait for 2.5s before using */
		} else if (ZS_IS_IRDA(up))
			mdelay(50);	/* Do better here once the problems
			                 * with blocking have been ironed out
			                 */
	} else {
		/* TODO: Make that depend on a timer, don't power down
		 * immediately
		 */
		if (ZS_IS_INTMODEM(up)) {
			pmac_call_feature(
				PMAC_FTR_MODEM_ENABLE, up->node, 0, 0);
		}
		pmac_call_feature(
			PMAC_FTR_SCC_ENABLE, up->node, up->port_type, 0);
	}
	return delay;
}

/*
 * FixZeroBug....Works around a bug in the SCC receving channel.
 * Taken from Darwin code, 15 Sept. 2000  -DanM
 *
 * The following sequence prevents a problem that is seen with O'Hare ASICs
 * (most versions -- also with some Heathrow and Hydra ASICs) where a zero
 * at the input to the receiver becomes 'stuck' and locks up the receiver.
 * This problem can occur as a result of a zero bit at the receiver input
 * coincident with any of the following events:
 *
 *	The SCC is initialized (hardware or software).
 *	A framing error is detected.
 *	The clocking option changes from synchronous or X1 asynchronous
 *		clocking to X16, X32, or X64 asynchronous clocking.
 *	The decoding mode is changed among NRZ, NRZI, FM0, or FM1.
 *
 * This workaround attempts to recover from the lockup condition by placing
 * the SCC in synchronous loopback mode with a fast clock before programming
 * any of the asynchronous modes.
 */
static void pmz_fix_zero_bug_scc(struct uart_pmac_port *up)
{
	write_zsreg(up, 9, ZS_IS_CHANNEL_A(up) ? CHRA : CHRB);
	zssync(up);
	udelay(10);
	write_zsreg(up, 9, (ZS_IS_CHANNEL_A(up) ? CHRA : CHRB) | NV);
	zssync(up);

	write_zsreg(up, 4, (X1CLK | EXTSYNC));

	/* I think this is wrong....but, I just copying code....
	*/
	write_zsreg(up, 3, (8 & ~RxENABLE));

	write_zsreg(up, 5, (8 & ~TxENABLE));
	write_zsreg(up, 9, NV);	/* Didn't we already do this? */
	write_zsreg(up, 11, (RCBR | TCBR));
	write_zsreg(up, 12, 0);
	write_zsreg(up, 13, 0);
	write_zsreg(up, 14, (LOOPBAK | SSBR));
	write_zsreg(up, 14, (LOOPBAK | SSBR | BRENAB));
	write_zsreg(up, 3, (8 | RxENABLE));
	write_zsreg(up, 0, RES_EXT_INT);
	write_zsreg(up, 0, RES_EXT_INT);	/* to kill some time */

	/* The channel should be OK now, but it is probably receiving
	 * loopback garbage.
	 * Switch to asynchronous mode, disable the receiver,
	 * and discard everything in the receive buffer.
	 */
	write_zsreg(up, 9, NV);
	write_zsreg(up, 4, PAR_ENAB);
	write_zsreg(up, 3, (8 & ~RxENABLE));

	while (read_zsreg(up, 0) & Rx_CH_AV) {
		(void)read_zsreg(up, 8);
		write_zsreg(up, 0, RES_EXT_INT);
		write_zsreg(up, 0, ERR_RES);
	}
}

/*
 * Real startup routine, powers up the hardware and sets up
 * the SCC. Returns a delay in ms where you need to wait before
 * actually using the port, this is typically the internal modem
 * powerup delay. This routine expect the lock to be taken.
 */
static int __pmz_startup(struct uart_pmac_port *up)
{
	int pwr_delay = 0;

	memset(&up->curregs, 0, sizeof(up->curregs));

	/* Power up the SCC & underlying hardware (modem/irda) */
	pwr_delay = pmz_set_scc_power(up, 1);

	/* Nice buggy HW ... */
	pmz_fix_zero_bug_scc(up);

	/* Reset the chip */
	write_zsreg(up, 9, ZS_IS_CHANNEL_A(up) ? CHRA : CHRB);
	zssync(up);
	udelay(10);
	write_zsreg(up, 9, 0);
	zssync(up);

	/* Clear the interrupt registers */
	write_zsreg(up, R1, 0);
	write_zsreg(up, R0, ERR_RES);
	write_zsreg(up, R0, ERR_RES);
	write_zsreg(up, R0, RES_H_IUS);
	write_zsreg(up, R0, RES_H_IUS);

	/* Remember status for DCD/CTS changes */
	up->prev_status = read_zsreg(up, R0);

	/* Enable receiver and transmitter.  */
	up->curregs[R3] |= RxENABLE;
	up->curregs[R5] |= TxENABLE | RTS | DTR;

	/* Master interrupt enable */
	up->curregs[R9] |= NV | MIE;

	up->curregs[R1] |= EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB;
	//	pmz_maybe_update_regs(up);

	return pwr_delay;
}

/*
 * This is the "normal" startup routine, using the above one
 * wrapped with the lock and doing a schedule delay
 */
static int pmz_startup(struct uart_port *port)
{
	struct uart_pmac_port *up = to_pmz(port);
	unsigned long flags;
	int pwr_delay = 0;

	pr_debug("pmz: startup()\n");

	/* A console is never powered down */
	if (!ZS_IS_CONS(up)) {
		spin_lock_irqsave(&port->lock, flags);
		pwr_delay = __pmz_startup(up);
		spin_unlock_irqrestore(&port->lock, flags);
	}
	
	if (request_irq(up->port.irq, pmz_interrupt, SA_SHIRQ, "PowerMac Zilog", up)) {
		printk(KERN_ERR "Unable to register zs interrupt handler.\n");
		pmz_set_scc_power(up, 0);
		return -ENXIO;
	}

	/* Right now, we deal with delay by blocking here, I'll be
	 * smarter later on
	 */
	if (pwr_delay != 0) {
		pr_debug("pmz: delaying %d ms\n", pwr_delay);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((pwr_delay * HZ)/1000);
	}

	pr_debug("pmz: startup() done.\n");

	return 0;
}

static void pmz_shutdown(struct uart_port *port)
{
	struct uart_pmac_port *up = to_pmz(port);
	unsigned long flags;

	pr_debug("pmz: shutdown()\n");

	/* Release interrupt handler */
       	free_irq(up->port.irq, up);

	if (ZS_IS_CONS(up))
		return;

	spin_lock_irqsave(&port->lock, flags);

	/* Disable receiver and transmitter.  */
	up->curregs[R3] &= ~RxENABLE;
	up->curregs[R5] &= ~TxENABLE;

	/* Disable all interrupts and BRK assertion.  */
	up->curregs[R1] &= ~(EXT_INT_ENAB | TxINT_ENAB | RxINT_MASK);
	up->curregs[R5] &= ~SND_BRK;
	pmz_maybe_update_regs(up);

	/* Shut the chip down */
	pmz_set_scc_power(up, 0);

	spin_unlock_irqrestore(&port->lock, flags);

	pr_debug("pmz: shutdown() done.\n");
}

/* Shared by TTY driver and serial console setup.  The port lock is held
 * and local interrupts are disabled.
 */
static void
pmz_convert_to_zs(struct uart_pmac_port *up, unsigned int cflag,
		       unsigned int iflag, int baud)
{
	int brg;

	switch (baud) {
	case ZS_CLOCK/16:	/* 230400 */
		up->curregs[R4] = X16CLK;
		up->curregs[R11] = 0;
		break;
	case ZS_CLOCK/32:	/* 115200 */
	        up->curregs[R4] = X32CLK;
		up->curregs[R11] = 0;
		break;
	default:
		up->curregs[R4] = X16CLK;
		up->curregs[R11] = TCBR | RCBR;
		brg = BPS_TO_BRG(baud, ZS_CLOCK / 16);
		up->curregs[R12] = (brg & 255);
		up->curregs[R13] = ((brg >> 8) & 255);
		up->curregs[R14] = BRENAB;
	}

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
	up->curregs[4] &= ~(SB_MASK);
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

static void pmz_irda_rts_pulses(struct uart_pmac_port *up, int w)
{
	udelay(w);
	write_zsreg(up, 5, Tx8 | TxENABLE);
	zssync(up);
	udelay(2);
	write_zsreg(up, 5, Tx8 | TxENABLE | RTS);
	zssync(up);
	udelay(8);
	write_zsreg(up, 5, Tx8 | TxENABLE);
	zssync(up);
	udelay(4);
	write_zsreg(up, 5, Tx8 | TxENABLE | RTS);
	zssync(up);
}

/*
 * Set the irda codec on the imac to the specified baud rate.
 */
static void pmz_irda_setup(struct uart_pmac_port *up, int cflags)
{
	int code, speed, t;

	speed = cflags & CBAUD;
	if (speed < B2400 || speed > B115200)
		return;
	code = 0x4d + B115200 - speed;

	/* disable serial interrupts and receive DMA */
	write_zsreg(up, 1, up->curregs[1] & ~0x9f);

	/* wait for transmitter to drain */
	t = 10000;
	while ((read_zsreg(up, R0) & Tx_BUF_EMP) == 0
	       || (read_zsreg(up, R1) & ALL_SNT) == 0) {
		if (--t <= 0) {
			printk(KERN_ERR "transmitter didn't drain\n");
			return;
		}
		udelay(10);
	}
	udelay(100);

	/* set to 8 bits, no parity, 19200 baud, RTS on, DTR off */
	write_zsreg(up, R4, X16CLK | SB1);
	write_zsreg(up, R11, TCBR | RCBR);
	t = BPS_TO_BRG(19200, ZS_CLOCK/16);
	write_zsreg(up, R12, t);
	write_zsreg(up, R13, t >> 8);
	write_zsreg(up, R14, BRENAB);
	write_zsreg(up, R3, Rx8 | RxENABLE);
	write_zsreg(up, R5, Tx8 | TxENABLE | RTS);
	zssync(up);

	/* set TxD low for ~104us and pulse RTS */
	udelay(1000);
	write_zsdata(up, 0xfe);
	pmz_irda_rts_pulses(up, 150);
	pmz_irda_rts_pulses(up, 180);
	pmz_irda_rts_pulses(up, 50);
	udelay(100);

	/* assert DTR, wait 30ms, talk to the chip */
	write_zsreg(up, R5, Tx8 | TxENABLE | RTS | DTR);
	zssync(up);
	mdelay(30);
	while (read_zsreg(up, R0) & Rx_CH_AV)
		read_zsdata(up);

	write_zsdata(up, 1);
	t = 1000;
	while ((read_zsreg(up, R0) & Rx_CH_AV) == 0) {
		if (--t <= 0) {
			printk(KERN_ERR "irda_setup timed out on 1st byte\n");
			goto out;
		}
		udelay(10);
	}
	t = read_zsdata(up);
	if (t != 4)
		printk(KERN_ERR "irda_setup 1st byte = %x\n", t);

	write_zsdata(up, code);
	t = 1000;
	while ((read_zsreg(up, R0) & Rx_CH_AV) == 0) {
		if (--t <= 0) {
			printk(KERN_ERR "irda_setup timed out on 2nd byte\n");
			goto out;
		}
		udelay(10);
	}
	t = read_zsdata(up);
	if (t != code)
		printk(KERN_ERR "irda_setup 2nd byte = %x (%x)\n", t, code);

	/* Drop DTR again and do some more RTS pulses */
 out:
	udelay(100);
	write_zsreg(up, R5, Tx8 | TxENABLE | RTS);
	pmz_irda_rts_pulses(up, 80);

	/* We should be right to go now.  We assume that load_zsregs
	   will get called soon to load up the correct baud rate etc. */
	up->curregs[R5] = (up->curregs[R5] | RTS) & ~DTR;
}

/* The port lock is not held.  */
static void
pmz_set_termios(struct uart_port *port, struct termios *termios,
		     struct termios *old)
{
	struct uart_pmac_port *up = to_pmz(port);
	unsigned long flags;
	int baud;

	pr_debug("pmz: set_termios()\n");

	baud = uart_get_baud_rate(port, termios, old, 1200, 230400);

	spin_lock_irqsave(&up->port.lock, flags);

	pmz_convert_to_zs(up, termios->c_cflag, termios->c_iflag, baud);

	if (UART_ENABLE_MS(&up->port, termios->c_cflag))
		up->flags |= PMACZILOG_FLAG_MODEM_STATUS;
	else
		up->flags &= ~PMACZILOG_FLAG_MODEM_STATUS;

	/* set the irda codec to the right rate */
	if (ZS_IS_IRDA(up))
		pmz_irda_setup(up, termios->c_cflag);

	/* Load registers to the chip */
	pmz_maybe_update_regs(up);

	spin_unlock_irqrestore(&up->port.lock, flags);

	pr_debug("pmz: set_termios() done.\n");
}

static const char *pmz_type(struct uart_port *port)
{
	return "PowerMac Zilog";
}

/* We do not request/release mappings of the registers here, this
 * happens at early serial probe time.
 */
static void pmz_release_port(struct uart_port *port)
{
}

static int pmz_request_port(struct uart_port *port)
{
	return 0;
}

/* These do not need to do anything interesting either.  */
static void pmz_config_port(struct uart_port *port, int flags)
{
}

/* We do not support letting the user mess with the divisor, IRQ, etc. */
static int pmz_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return -EINVAL;
}

static struct uart_ops pmz_pops = {
	.tx_empty	=	pmz_tx_empty,
	.set_mctrl	=	pmz_set_mctrl,
	.get_mctrl	=	pmz_get_mctrl,
	.stop_tx	=	pmz_stop_tx,
	.start_tx	=	pmz_start_tx,
	.stop_rx	=	pmz_stop_rx,
	.enable_ms	=	pmz_enable_ms,
	.break_ctl	=	pmz_break_ctl,
	.startup	=	pmz_startup,
	.shutdown	=	pmz_shutdown,
	.set_termios	=	pmz_set_termios,
	.type		=	pmz_type,
	.release_port	=	pmz_release_port,
	.request_port	=	pmz_request_port,
	.config_port	=	pmz_config_port,
	.verify_port	=	pmz_verify_port,
};

/*
 * Setup one port structure after probing, HW is down at this point,
 * Unlike sunzilog, we don't need to pre-init the spinlock as we don't
 * register our console before uart_add_one_port() is called
 */
static int __init pmz_setup_port(struct uart_pmac_port *up, int early)
{
	struct device_node *np = up->node;
	char *conn;
	struct slot_names_prop {
		int	count;
		char	name[1];
	} *slots;
	int len;

	/*
	 * Request & map chip registers
	 */
	if (!early && request_OF_resource(np, 0, NULL) == NULL) {
		printk("pmac_zilog: failed to request resources for %s\n",
			np->full_name);
		return -EBUSY;
	}
	up->port.mapbase = np->addrs[0].address;
	up->port.membase = ioremap(up->port.mapbase, 0x1000);
      
	up->control_reg = (volatile u8 *)up->port.membase;
	up->data_reg = up->control_reg + 0x10;
	
	/*
	 * Request & map DBDMA registers
	 */
#ifdef HAS_DBDMA
	if (np->n_addrs >= 3 && np->n_intrs >= 3)
		up->flags |= PMACZILOG_FLAG_HAS_DMA;
#endif	
	if (ZS_HAS_DMA(up)) {
		if (!early && request_OF_resource(np, np->n_addrs - 2, " (tx dma)") == NULL) {
			printk(KERN_ERR "pmac_zilog: can't request TX DMA resource !\n");
			up->flags &= ~PMACZILOG_FLAG_HAS_DMA;
			goto no_dma;
		}
		if (!early && request_OF_resource(np, np->n_addrs - 1, " (rx dma)") == NULL) {
			release_OF_resource(np, np->n_addrs - 2);
			printk(KERN_ERR "pmac_zilog: can't request RX DMA resource !\n");
			up->flags &= ~PMACZILOG_FLAG_HAS_DMA;
			goto no_dma;
		}
		up->tx_dma_regs = (volatile struct dbdma_regs *)
			ioremap(np->addrs[np->n_addrs - 2].address, 0x1000);
		up->rx_dma_regs = (volatile struct dbdma_regs *)
			ioremap(np->addrs[np->n_addrs - 1].address, 0x1000);
		up->tx_dma_irq = np->intrs[1].line;
		up->rx_dma_irq = np->intrs[2].line;
	}
no_dma:
	if (!early)
		up->flags |= PMACZILOG_FLAG_RSRC_REQUESTED;

	/*
	 * Detect port type
	 */
	if (device_is_compatible(np, "cobalt"))
		up->flags |= PMACZILOG_FLAG_IS_INTMODEM;
	conn = get_property(np, "AAPL,connector", &len);
	if (conn && (strcmp(conn, "infrared") == 0))
		up->flags |= PMACZILOG_FLAG_IS_IRDA;
	up->port_type = PMAC_SCC_ASYNC;
	/* 1999 Powerbook G3 has slot-names property instead */
	slots = (struct slot_names_prop *)get_property(np, "slot-names", &len);
	if (slots && slots->count > 0) {
		if (strcmp(slots->name, "IrDA") == 0)
			up->flags |= PMACZILOG_FLAG_IS_IRDA;
		else if (strcmp(slots->name, "Modem") == 0)
			up->flags |= PMACZILOG_FLAG_IS_INTMODEM;
	}
	if (ZS_IS_IRDA(up))
		up->port_type = PMAC_SCC_IRDA;
	if (ZS_IS_INTMODEM(up)) {
		struct device_node* i2c_modem = find_devices("i2c-modem");
		if (i2c_modem) {
			char* mid = get_property(i2c_modem, "modem-id", NULL);
			if (mid) switch(*mid) {
			case 0x04 :
			case 0x05 :
			case 0x07 :
			case 0x08 :
			case 0x0b :
			case 0x0c :
				up->port_type = PMAC_SCC_I2S1;
			}
			printk(KERN_INFO "pmac_zilog: i2c-modem detected, id: %d\n",
				mid ? (*mid) : 0);
		} else {
			printk(KERN_INFO "pmac_zilog: serial modem detected\n");
		}
	}

	/*
	 * Init remaining bits of "port" structure
	 */
	up->port.iotype = SERIAL_IO_MEM;
	up->port.irq = np->intrs[0].line;
	up->port.uartclk = ZS_CLOCK;
	up->port.fifosize = 1;
	up->port.ops = &pmz_pops;
	up->port.type = PORT_PMAC_ZILOG;
	up->port.flags = 0;

	return 0;
}

/*
 * Get rid of a port on module removal
 */
static void pmz_dispose_port(struct uart_pmac_port *up)
{
	struct device_node *np;

	if (up->flags & PMACZILOG_FLAG_RSRC_REQUESTED) {
		release_OF_resource(up->node, 0);
		if (ZS_HAS_DMA(up)) {
			release_OF_resource(up->node, up->node->n_addrs - 2);
			release_OF_resource(up->node, up->node->n_addrs - 1);
		}
	}
	iounmap((void *)up->control_reg);
	np = up->node;
	up->node = NULL;
	of_node_put(np);
}

/*
 * Called upon match with an escc node in the devive-tree.
 */
static int pmz_attach(struct macio_dev *mdev, const struct of_match *match)
{
	int i;
	
	/* Iterate the pmz_ports array to find a matching entry
	 */
	for (i = 0; i < MAX_ZS_PORTS; i++)
		if (pmz_ports[i].node == mdev->ofdev.node) {
			pmz_ports[i].dev = mdev;
			dev_set_drvdata(&mdev->ofdev.dev, &pmz_ports[i]);
			return 0;
		}
	return -ENODEV;
}

/*
 * That one should not be called, macio isn't really a hotswap device,
 * we don't expect one of those serial ports to go away...
 */
static int pmz_detach(struct macio_dev *mdev)
{
	struct uart_pmac_port	*port = dev_get_drvdata(&mdev->ofdev.dev);
	
	if (!port)
		return -ENODEV;

	dev_set_drvdata(&mdev->ofdev.dev, NULL);
	port->dev = NULL;
	
	return 0;
}

/*
 * Probe all ports in the system and build the ports array, we register
 * with the serial layer at this point, the macio-type probing is only
 * used later to "attach" to the sysfs tree so we get power management
 * events
 */
static int __init pmz_probe(int early)
{
	struct device_node	*node_p, *node_a, *node_b, *np;
	int			count = 0;
	int			rc;

	/*
	 * Find all escc chips in the system
	 */
	node_p = of_find_node_by_name(NULL, "escc");
	while (node_p) {
		/*
		 * First get channel A/B node pointers
		 * 
		 * TODO: Add routines with proper locking to do that...
		 */
		node_a = node_b = NULL;
		for (np = NULL; (np = of_get_next_child(node_p, np)) != NULL;) {
			if (strncmp(np->name, "ch-a", 4) == 0)
				node_a = of_node_get(np);
			else if (strncmp(np->name, "ch-b", 4) == 0)
				node_b = of_node_get(np);
		}
		if (!node_a || !node_b) {
			of_node_put(node_a);
			of_node_put(node_b);
			printk(KERN_ERR "pmac_zilog: missing node %c for escc %s\n",
				(!node_a) ? 'a' : 'b', node_p->full_name);
			goto next;
		}

		/*
		 * Fill basic fields in the port structures
		 */
		pmz_ports[count].mate		= &pmz_ports[count+1];
		pmz_ports[count+1].mate		= &pmz_ports[count];
		pmz_ports[count].flags		= PMACZILOG_FLAG_IS_CHANNEL_A;
		pmz_ports[count].node		= node_a;
		pmz_ports[count+1].node		= node_b;
		pmz_ports[count].port.line	= count;
		pmz_ports[count+1].port.line   	= count+1;

		/*
		 * Setup the ports for real
		 */
		rc = pmz_setup_port(&pmz_ports[count], early);
		if (rc == 0)
			rc = pmz_setup_port(&pmz_ports[count+1], early);
		if (rc != 0) {
			of_node_put(node_a);
			of_node_put(node_b);
			memset(&pmz_ports[count], 0, sizeof(struct uart_pmac_port));
			memset(&pmz_ports[count+1], 0, sizeof(struct uart_pmac_port));
			goto next;
		}
		count += 2;
next:
		node_p = of_find_node_by_name(node_p, "escc");
	}
	pmz_ports_count = count;

	return 0;
}

static struct uart_driver pmz_uart_reg = {
	.owner		=	THIS_MODULE,
	.driver_name	=	"ttyS",
	.devfs_name	=	"tts/",
	.dev_name	=	"ttyS",
	.major		=	TTY_MAJOR,
};

#ifdef CONFIG_SERIAL_PMACZILOG_CONSOLE

static void pmz_console_write(struct console *con, const char *s, unsigned int count);
static int __init pmz_console_setup(struct console *co, char *options);

static struct console pmz_console = {
	.name	=	"ttyS",
	.write	=	pmz_console_write,
	.device	=	uart_console_device,
	.setup	=	pmz_console_setup,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
	.data   =	&pmz_uart_reg,
};

#define PMACZILOG_CONSOLE	&pmz_console
#else /* CONFIG_SERIAL_PMACZILOG_CONSOLE */
#define PMACZILOG_CONSOLE	(NULL)
#endif /* CONFIG_SERIAL_PMACZILOG_CONSOLE */

/*
 * Register the driver, console driver and ports with the serial
 * core
 */
static int __init pmz_register(void)
{
	int i, rc;
	
	pmz_uart_reg.nr = pmz_ports_count;
	pmz_uart_reg.cons = PMACZILOG_CONSOLE;
	pmz_uart_reg.minor = 64;

	/*
	 * Register this driver with the serial core
	 */
	rc = uart_register_driver(&pmz_uart_reg);
	if (rc != 0)
		return rc;

	/*
	 * Register each port with the serial core
	 */
	for (i = 0; i < pmz_ports_count; i++) {
		struct uart_pmac_port *uport = &pmz_ports[i];
		if (uport->node != NULL)
			uart_add_one_port(&pmz_uart_reg, &uport->port);
	}

	return 0;
}

static struct of_match pmz_match[] = 
{
	{
	.name 		= "ch-a",
	.type		= OF_ANY_MATCH,
	.compatible	= OF_ANY_MATCH
	},
	{
	.name 		= "ch-b",
	.type		= OF_ANY_MATCH,
	.compatible	= OF_ANY_MATCH
	},
	{},
};

static struct macio_driver pmz_driver = 
{
	.name 		= "pmac_zilog",
	.match_table	= pmz_match,
	.probe		= pmz_attach,
	.remove		= pmz_detach,
//	.suspend	= pmz_suspend, *** NYI
//	.resume		= pmz_resume,  *** NYI
};

static void pmz_fixup_resources(void)
{
	int i;
       	for (i=0; i<pmz_ports_count; i++) {
       		struct uart_pmac_port *up = &pmz_ports[i];

		if (up->node == NULL)
			continue;
       		if (up->flags & PMACZILOG_FLAG_RSRC_REQUESTED)
			continue;
		if (request_OF_resource(up->node, 0, NULL) == NULL)
			printk(KERN_WARNING "%s: Failed to do late IO resource request, port still active\n",
			       up->node->name);
		up->flags |= PMACZILOG_FLAG_RSRC_REQUESTED;
		if (!ZS_HAS_DMA(up))
			continue;
		if (request_OF_resource(up->node, up->node->n_addrs - 2, NULL) == NULL)
			printk(KERN_WARNING "%s: Failed to do late DMA resource request, port still active\n",
			       up->node->name);
		if (request_OF_resource(up->node, up->node->n_addrs - 1, NULL) == NULL)
			printk(KERN_WARNING "%s: Failed to do late DMA resource request, port still active\n",
			       up->node->name);
       	}

}

static int __init init_pmz(void)
{
	printk(KERN_DEBUG "%s\n", version);

	/*
	 * If we had serial console, then we didn't request
	 * resources yet. We fix that up now
	 */
	if (pmz_ports_count > 0)
		pmz_fixup_resources();

	/* 
	 * First, we need to do a direct OF-based probe pass. We
	 * do that because we want serial console up before the
	 * macio stuffs calls us back, and since that makes it
	 * easier to pass the proper number of channels to
	 * uart_register_driver()
	 */
	if (pmz_ports_count == 0)
		pmz_probe(0);

	/*
	 * Bail early if no port found
	 */
	if (pmz_ports_count == 0)
		return -ENODEV;

	/*
	 * Now we register with the serial layer
	 */
	pmz_register();
	
	/*
	 * Then we register the macio driver itself
	 */
	return macio_register_driver(&pmz_driver);
}

static void __exit exit_pmz(void)
{
	int i;

	/* Get rid of macio-driver (detach from macio) */
	macio_unregister_driver(&pmz_driver);

	/* Unregister UART driver */
	uart_unregister_driver(&pmz_uart_reg);

	for (i = 0; i < pmz_ports_count; i++) {
		struct uart_pmac_port *uport = &pmz_ports[i];
		if (uport->node != NULL) {
			uart_remove_one_port(&pmz_uart_reg, &uport->port);
			pmz_dispose_port(uport);
		}
	}
}

#ifdef CONFIG_SERIAL_PMACZILOG_CONSOLE

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 */
static void pmz_console_write(struct console *con, const char *s, unsigned int count)
{
	struct uart_pmac_port *up = &pmz_ports[con->index];
	unsigned long flags;
	int i;

	spin_lock_irqsave(&up->port.lock, flags);

	/* Turn of interrupts and enable the transmitter. */
	write_zsreg(up, R1, up->curregs[1] & ~TxINT_ENAB);
	write_zsreg(up, R5, up->curregs[5] | TxENABLE | RTS | DTR);

	for (i = 0; i < count; i++) {
		/* Wait for the transmit buffer to empty. */
		while ((read_zsreg(up, R0) & Tx_BUF_EMP) == 0)
			udelay(5);
		write_zsdata(up, s[i]);
		if (s[i] == 10) {
			while ((read_zsreg(up, R0) & Tx_BUF_EMP) == 0)
				udelay(5);
			write_zsdata(up, R13);
		}
	}

	/* Restore the values in the registers. */
	write_zsreg(up, R1, up->curregs[1]);
	/* Don't disable the transmitter. */

	spin_unlock_irqrestore(&up->port.lock, flags);
}

/*
 * Setup the serial console
 */
static int __init pmz_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	unsigned long pwr_delay;

	/*
	 * XServe's default to 57600 bps
	 */
	if (machine_is_compatible("RackMac1,1")
	 || machine_is_compatible("RackMac1,2"))
	 	baud = 57600;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= pmz_ports_count)
		co->index = 0;
	port = &pmz_ports[co->index].port;

	/*
	 * Mark port as beeing a console
	 */
	port->flags |= PMACZILOG_FLAG_IS_CONS;

	/*
	 * Temporary fix for uart layer who didn't setup the spinlock yet
	 */
	spin_lock_init(&port->lock);

	/*
	 * Enable the hardware
	 */
	pwr_delay = __pmz_startup(&pmz_ports[co->index]);
	if (pwr_delay)
		mdelay(pwr_delay);
	
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static int __init pmz_console_init(void)
{
	/* Probe ports */
	pmz_probe(1);

	/* TODO: Autoprobe console based on OF */
	/* pmz_console.index = i; */
	register_console(&pmz_console);

	return 0;

}
console_initcall(pmz_console_init);
#endif /* CONFIG_SERIAL_PMACZILOG_CONSOLE */

module_init(init_pmz);
module_exit(exit_pmz);
