/*
 *  linux/drivers/char/core.c
 *
 *  Driver core for serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
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
 *  $Id: core.c,v 1.91 2002/07/22 15:27:32 rmk Exp $
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/circ_buf.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/pm.h>
#include <linux/serial_core.h>
#include <linux/smp_lock.h>
#include <linux/serial.h> /* for serial_state and serial_icounter_struct */

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#undef	DEBUG
#ifdef DEBUG
#define DPRINTK(x...)	printk(x)
#else
#define DPRINTK(x...)	do { } while (0)
#endif

#ifndef CONFIG_PM
#define pm_access(pm)		do { } while (0)
#define pm_unregister(pm)	do { } while (0)
#endif

/*
 * This is used to lock changes in serial line configuration.
 */
static DECLARE_MUTEX(port_sem);

#define HIGH_BITS_OFFSET	((sizeof(long)-sizeof(int))*8)

static void uart_change_speed(struct uart_info *info, struct termios *old_termios);
static void uart_wait_until_sent(struct tty_struct *tty, int timeout);

/*
 * This routine is used by the interrupt handler to schedule processing in
 * the software interrupt portion of the driver.
 */
void uart_event(struct uart_port *port, int event)
{
	struct uart_info *info = port->info;

	set_bit(0, &info->event);
	tasklet_schedule(&info->tlet);
}

static void uart_stop(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	struct uart_port *port = info->port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	port->ops->stop_tx(port, 1);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void __uart_start(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	struct uart_port *port = info->port;

	if (!uart_circ_empty(&info->xmit) && info->xmit.buf &&
	    !tty->stopped && !tty->hw_stopped)
		port->ops->start_tx(port, 1);
}

static void uart_start(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	pm_access(info->state->pm);

	spin_lock_irqsave(&info->port->lock, flags);
	__uart_start(tty);
	spin_unlock_irqrestore(&info->port->lock, flags);
}

static void uart_tasklet_action(unsigned long data)
{
	struct uart_info *info = (struct uart_info *)data;
	struct tty_struct *tty;

	tty = info->tty;
	if (!tty || !test_and_clear_bit(EVT_WRITE_WAKEUP, &info->event))
		return;

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
}

static inline void
uart_update_mctrl(struct uart_port *port, unsigned int set, unsigned int clear)
{
	unsigned long flags;
	unsigned int old;

	spin_lock_irqsave(&port->lock, flags);
	old = port->mctrl;
	port->mctrl = (old & ~clear) | set;
	if (old != port->mctrl)
		port->ops->set_mctrl(port, port->mctrl);
	spin_unlock_irqrestore(&port->lock, flags);
}

#define uart_set_mctrl(port,set)	uart_update_mctrl(port,set,0)
#define uart_clear_mctrl(port,clear)	uart_update_mctrl(port,0,clear)

static inline void uart_update_altspeed(struct uart_info *info)
{
	unsigned int flags = info->port->flags & UPF_SPD_MASK;

	if (flags == UPF_SPD_HI)
		info->tty->alt_speed = 57600;
	if (flags == UPF_SPD_VHI)
		info->tty->alt_speed = 115200;
	if (flags == UPF_SPD_SHI)
		info->tty->alt_speed = 230400;
	if (flags == UPF_SPD_WARP)
		info->tty->alt_speed = 460800;
}

/*
 * Startup the port.  This will be called once per open.  All calls
 * will be serialised by the global port semaphore.
 */
static int uart_startup(struct uart_info *info, int init_hw)
{
	struct uart_port *port = info->port;
	unsigned long page;
	int retval = 0;

	if (info->flags & UIF_INITIALIZED)
		return 0;

	/*
	 * Set the TTY IO error marker - we will only clear this
	 * once we have successfully opened the port.  Also set
	 * up the tty->alt_speed kludge
	 */
	if (info->tty) {
		set_bit(TTY_IO_ERROR, &info->tty->flags);
		uart_update_altspeed(info);
	}

	if (port->type == PORT_UNKNOWN)
		return 0;

	/*
	 * Initialise and allocate the transmit and temporary
	 * buffer.
	 */
	if (!info->xmit.buf) {
		page = get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		info->xmit.buf = (unsigned char *) page;
		info->tmpbuf = info->xmit.buf + UART_XMIT_SIZE;
		init_MUTEX(&info->tmpbuf_sem);
		uart_circ_clear(&info->xmit);
	}

	port->mctrl = 0;

	retval = port->ops->startup(port);
	if (retval == 0) {
		if (init_hw) {
			/*
			 * Initialise the hardware port settings.
			 */
			uart_change_speed(info, NULL);

			/*
			 * Setup the RTS and DTR signals once the
			 * port is open and ready to respond.
			 */
			if (info->tty->termios->c_cflag & CBAUD)
				uart_set_mctrl(port, TIOCM_RTS | TIOCM_DTR);
		}

		info->flags |= UIF_INITIALIZED;

		if (info->tty)
			clear_bit(TTY_IO_ERROR, &info->tty->flags);
	}

	if (retval && capable(CAP_SYS_ADMIN))
		retval = 0;

	return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.  Calls to
 * uart_shutdown are serialised by port_sem.
 */
static void uart_shutdown(struct uart_info *info)
{
	struct uart_port *port = info->port;

	if (!(info->flags & UIF_INITIALIZED))
		return;

	/*
	 * Turn off DTR and RTS early.
	 */
	if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
		uart_clear_mctrl(info->port, TIOCM_DTR | TIOCM_RTS);

	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free
	 * the irq here so the queue might never be woken up.  Note
	 * that we won't end up waiting on delta_msr_wait again since
	 * any outstanding file descriptors should be pointing at
	 * hung_up_tty_fops now.
	 */
	wake_up_interruptible(&info->delta_msr_wait);

	/*
	 * Free the IRQ and disable the port.
	 */
	port->ops->shutdown(port);

	/*
	 * Ensure that the IRQ handler isn't running on another CPU.
	 */
	synchronize_irq(port->irq);

	/*
	 * Free the transmit buffer page.
	 */
	if (info->xmit.buf) {
		free_page((unsigned long)info->xmit.buf);
		info->xmit.buf = NULL;
		info->tmpbuf = NULL;
	}

	/*
	 * kill off our tasklet
	 */
	tasklet_kill(&info->tlet);
	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~UIF_INITIALIZED;
}

static inline
unsigned int uart_calculate_quot(struct uart_info *info, unsigned int baud)
{
	struct uart_port *port = info->port;
	unsigned int quot;

	/* Special case: B0 rate */
	if (baud == 0)
		baud = 9600;

	/* Old HI/VHI/custom speed handling */
	if (baud == 38400 &&
	    ((port->flags & UPF_SPD_MASK) == UPF_SPD_CUST))
		quot = info->state->custom_divisor;
	else
		quot = port->uartclk / (16 * baud);

	return quot;
}

static void
uart_change_speed(struct uart_info *info, struct termios *old_termios)
{
	struct uart_port *port = info->port;
	unsigned int quot, cflag, bits, try;

	/*
	 * If we have no tty, termios, or the port does not exist,
	 * then we can't set the parameters for this port.
	 */
	if (!info->tty || !info->tty->termios || port->type == PORT_UNKNOWN)
		return;

	/*
	 * Set flags based on termios cflag
	 */
	cflag = info->tty->termios->c_cflag;

	/* byte size and parity */
	switch (cflag & CSIZE) {
	case CS5:
		bits = 7;
		break;
	case CS6:
		bits = 8;
		break;
	case CS7:
		bits = 9;
		break;
	default:
		bits = 10;
		break; // CS8
	}

	if (cflag & CSTOPB)
		bits++;
	if (cflag & PARENB)
		bits++;

	for (try = 0; try < 3; try ++) {
		unsigned int baud;

		/* Determine divisor based on baud rate */
		baud = tty_get_baud_rate(info->tty);
		quot = uart_calculate_quot(info, baud);
		if (quot)
			break;

		/*
		 * Oops, the quotient was zero.  Try again with
		 * the old baud rate if possible.
		 */
		info->tty->termios->c_cflag &= ~CBAUD;
		if (old_termios) {
			info->tty->termios->c_cflag |=
				 (old_termios->c_cflag & CBAUD);
			old_termios = NULL;
			continue;
		}

		/*
		 * As a last resort, if the quotient is zero,
		 * default to 9600 bps
		 */
		info->tty->termios->c_cflag |= B9600;
	}

	/*
	 * The total number of bits to be transmitted in the fifo.
	 */
	bits = bits * port->fifosize;

	/*
	 * Figure the timeout to send the above number of bits.
	 * Add .02 seconds of slop
	 */
	port->timeout = (HZ * bits) / (port->uartclk / (16 * quot)) + HZ/50;

	if (cflag & CRTSCTS)
		info->flags |= UIF_CTS_FLOW;
	else
		info->flags &= ~UIF_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~UIF_CHECK_CD;
	else
		info->flags |= UIF_CHECK_CD;

	port->ops->change_speed(port, cflag, info->tty->termios->c_iflag, quot);
}

static inline void
__uart_put_char(struct uart_port *port, struct circ_buf *circ, unsigned char c)
{
	unsigned long flags;

	if (!circ->buf)
		return;

	spin_lock_irqsave(&port->lock, flags);
	if (uart_circ_chars_free(circ) != 0) {
		circ->buf[circ->head] = c;
		circ->head = (circ->head + 1) & (UART_XMIT_SIZE - 1);
	}
	spin_unlock_irqrestore(&port->lock, flags);
}

static inline int
__uart_user_write(struct uart_port *port, struct circ_buf *circ,
		  const unsigned char *buf, int count)
{
	unsigned long flags;
	int c, ret = 0;

	if (down_interruptible(&port->info->tmpbuf_sem))
		return -EINTR;

	while (1) {
		int c1;
		c = CIRC_SPACE_TO_END(circ->head, circ->tail, UART_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;

		c -= copy_from_user(port->info->tmpbuf, buf, c);
		if (!c) {
			if (!ret)
				ret = -EFAULT;
			break;
		}
		spin_lock_irqsave(&port->lock, flags);
		c1 = CIRC_SPACE_TO_END(circ->head, circ->tail, UART_XMIT_SIZE);
		if (c1 < c)
			c = c1;
		memcpy(circ->buf + circ->head, port->info->tmpbuf, c);
		circ->head = (circ->head + c) & (UART_XMIT_SIZE - 1);
		spin_unlock_irqrestore(&port->lock, flags);
		buf += c;
		count -= c;
		ret += c;
	}
	up(&port->info->tmpbuf_sem);

	return ret;
}

static inline int
__uart_kern_write(struct uart_port *port, struct circ_buf *circ,
		  const unsigned char *buf, int count)
{
	unsigned long flags;
	int c, ret = 0;

	spin_lock_irqsave(&port->lock, flags);
	while (1) {
		c = CIRC_SPACE_TO_END(circ->head, circ->tail, UART_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(circ->buf + circ->head, buf, c);
		circ->head = (circ->head + c) & (UART_XMIT_SIZE - 1);
		buf += c;
		count -= c;
		ret += c;
	}
	spin_unlock_irqrestore(&port->lock, flags);

	return ret;
}

static void uart_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct uart_info *info = tty->driver_data;

	if (tty)
		__uart_put_char(info->port, &info->xmit, ch);
}

static void uart_flush_chars(struct tty_struct *tty)
{
	uart_start(tty);
}

static int
uart_write(struct tty_struct *tty, int from_user, const unsigned char * buf,
	   int count)
{
	struct uart_info *info = tty->driver_data;
	int ret;

	if (!tty || !info->xmit.buf)
		return 0;

	if (from_user)
		ret = __uart_user_write(info->port, &info->xmit, buf, count);
	else
		ret = __uart_kern_write(info->port, &info->xmit, buf, count);

	uart_start(tty);
	return ret;
}

static int uart_write_room(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;

	return uart_circ_chars_free(&info->xmit);
}

static int uart_chars_in_buffer(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;

	return uart_circ_chars_pending(&info->xmit);
}

static void uart_flush_buffer(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	DPRINTK("uart_flush_buffer(%d) called\n",
	        MINOR(tty->device) - tty->driver.minor_start);

	spin_lock_irqsave(&info->port->lock, flags);
	uart_circ_clear(&info->xmit);
	spin_unlock_irqrestore(&info->port->lock, flags);
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void uart_send_xchar(struct tty_struct *tty, char ch)
{
	struct uart_info *info = tty->driver_data;
	struct uart_port *port = info->port;
	unsigned long flags;

	if (port->ops->send_xchar)
		port->ops->send_xchar(port, ch);
	else {
		port->x_char = ch;
		if (ch) {
			spin_lock_irqsave(&port->lock, flags);
			port->ops->start_tx(port, 0);
			spin_unlock_irqrestore(&port->lock, flags);
		}
	}
}

static void uart_throttle(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;

	if (I_IXOFF(tty))
		uart_send_xchar(tty, STOP_CHAR(tty));

	if (tty->termios->c_cflag & CRTSCTS)
		uart_clear_mctrl(info->port, TIOCM_RTS);
}

static void uart_unthrottle(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	struct uart_port *port = info->port;

	if (I_IXOFF(tty)) {
		if (port->x_char)
			port->x_char = 0;
		else
			uart_send_xchar(tty, START_CHAR(tty));
	}

	if (tty->termios->c_cflag & CRTSCTS)
		uart_set_mctrl(port, TIOCM_RTS);
}

static int uart_get_info(struct uart_info *info, struct serial_struct *retinfo)
{
	struct uart_state *state = info->state;
	struct uart_port *port = info->port;
	struct serial_struct tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.type	    = port->type;
	tmp.line	    = port->line;
	tmp.port	    = port->iobase;
	if (HIGH_BITS_OFFSET)
		tmp.port_high = port->iobase >> HIGH_BITS_OFFSET;
	tmp.irq		    = port->irq;
	tmp.flags	    = port->flags | info->flags;
	tmp.xmit_fifo_size  = port->fifosize;
	tmp.baud_base	    = port->uartclk / 16;
	tmp.close_delay	    = state->close_delay;
	tmp.closing_wait    = state->closing_wait;
	tmp.custom_divisor  = state->custom_divisor;
	tmp.hub6	    = port->hub6;
	tmp.io_type         = port->iotype;
	tmp.iomem_reg_shift = port->regshift;
	tmp.iomem_base      = (void *)port->mapbase;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int
uart_set_info(struct uart_info *info, struct serial_struct *newinfo)
{
	struct serial_struct new_serial;
	struct uart_state *state = info->state;
	struct uart_port *port = info->port;
	unsigned long new_port;
	unsigned int change_irq, change_port, old_flags;
	unsigned int old_custom_divisor;
	int retval = 0;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	new_port = new_serial.port;
	if (HIGH_BITS_OFFSET)
		new_port += (unsigned long) new_serial.port_high << HIGH_BITS_OFFSET;

	new_serial.irq = irq_cannonicalize(new_serial.irq);

	/*
	 * This semaphore protects state->count.  It is also
	 * very useful to prevent opens.  Also, take the
	 * port configuration semaphore to make sure that a
	 * module insertion/removal doesn't change anything
	 * under us.
	 */
	down(&port_sem);

	change_irq  = new_serial.irq != port->irq;

	/*
	 * Since changing the 'type' of the port changes its resource
	 * allocations, we should treat type changes the same as
	 * IO port changes.
	 */
	change_port = new_port != port->iobase ||
		      (unsigned long)new_serial.iomem_base != port->mapbase ||
		      new_serial.hub6 != port->hub6 ||
		      new_serial.io_type != port->iotype ||
		      new_serial.iomem_reg_shift != port->regshift ||
		      new_serial.type != port->type;

	old_flags = port->flags;
	old_custom_divisor = state->custom_divisor;

	if (!capable(CAP_SYS_ADMIN)) {
		retval = -EPERM;
		if (change_irq || change_port ||
		    (new_serial.baud_base != port->uartclk / 16) ||
		    (new_serial.close_delay != state->close_delay) ||
		    (new_serial.closing_wait != state->closing_wait) ||
		    (new_serial.xmit_fifo_size != port->fifosize) ||
		    (((new_serial.flags ^ old_flags) & ~UPF_USR_MASK) != 0))
			goto exit;
		port->flags = ((port->flags & ~UPF_USR_MASK) |
			       (new_serial.flags & UPF_USR_MASK));
		state->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	/*
	 * Ask the low level driver to verify the settings.
	 */
	if (port->ops->verify_port)
		retval = port->ops->verify_port(port, &new_serial);

	if ((new_serial.irq >= NR_IRQS) || (new_serial.irq < 0) ||
	    (new_serial.baud_base < 9600))
		retval = -EINVAL;

	if (retval)
		goto exit;

	if (change_port || change_irq) {
		retval = -EBUSY;

		/*
		 * Make sure that we are the sole user of this port.
		 */
		if (state->count > 1 || info->blocked_open != 0)
			goto exit;

		/*
		 * We need to shutdown the serial port at the old
		 * port/type/irq combination.
		 */
		uart_shutdown(info);
	}

	if (change_port) {
		unsigned long old_iobase, old_mapbase;
		unsigned int old_type, old_iotype, old_hub6, old_shift;

		old_iobase = port->iobase;
		old_mapbase = port->mapbase;
		old_type = port->type;
		old_hub6 = port->hub6;
		old_iotype = port->iotype;
		old_shift = port->regshift;

		/*
		 * Free and release old regions
		 */
		if (old_type != PORT_UNKNOWN)
			port->ops->release_port(port);

		port->iobase = new_port;
		port->type = new_serial.type;
		port->hub6 = new_serial.hub6;
		port->iotype = new_serial.io_type;
		port->regshift = new_serial.iomem_reg_shift;
		port->mapbase = (unsigned long)new_serial.iomem_base;

		/*
		 * Claim and map the new regions
		 */
		if (port->type != PORT_UNKNOWN)
			retval = port->ops->request_port(port);

		/*
		 * If we fail to request resources for the
		 * new port, try to restore the old settings.
		 */
		if (retval && old_type != PORT_UNKNOWN) {
			port->iobase = old_iobase;
			port->type = old_type;
			port->hub6 = old_hub6;
			port->iotype = old_iotype;
			port->regshift = old_shift;
			port->mapbase = old_mapbase;
			retval = port->ops->request_port(port);
			/*
			 * If we failed to restore the old settings,
			 * we fail like this.
			 */
			if (retval)
				port->type = PORT_UNKNOWN;

			/*
			 * We failed anyway.
			 */
			retval = -EBUSY;
		}
	}

	port->irq              = new_serial.irq;
	port->uartclk          = new_serial.baud_base * 16;
	port->flags            = new_serial.flags & UPF_FLAGS;
	state->custom_divisor  = new_serial.custom_divisor;
	state->close_delay     = new_serial.close_delay * HZ / 100;
	state->closing_wait    = new_serial.closing_wait * HZ / 100;
	port->fifosize         = new_serial.xmit_fifo_size;
	info->tty->low_latency = (port->flags & UPF_LOW_LATENCY) ? 1 : 0;

 check_and_exit:
	retval = 0;
	if (port->type == PORT_UNKNOWN)
		goto exit;
	if (info->flags & UIF_INITIALIZED) {
		if (((old_flags ^ port->flags) & UPF_SPD_MASK) ||
		    old_custom_divisor != state->custom_divisor) {
			uart_update_altspeed(info);
			uart_change_speed(info, NULL);
		}
	} else
		retval = uart_startup(info, 1);
 exit:
	up(&port_sem);
	return retval;
}


/*
 * uart_get_lsr_info - get line status register info
 */
static int uart_get_lsr_info(struct uart_info *info, unsigned int *value)
{
	struct uart_port *port = info->port;
	unsigned int result;

	result = port->ops->tx_empty(port);

	/*
	 * If we're about to load something into the transmit
	 * register, we'll pretend the transmitter isn't empty to
	 * avoid a race condition (depending on when the transmit
	 * interrupt happens).
	 */
	if (info->port->x_char ||
	    ((uart_circ_chars_pending(&info->xmit) > 0) &&
	     !info->tty->stopped && !info->tty->hw_stopped))
		result &= ~TIOCSER_TEMT;
	
	return put_user(result, value);
}

static int uart_get_modem_info(struct uart_port *port, unsigned int *value)
{
	unsigned int result = port->mctrl;

	result |= port->ops->get_mctrl(port);

	return put_user(result, value);
}

static int
uart_set_modem_info(struct uart_port *port, unsigned int cmd,
		    unsigned int *value)
{
	unsigned int arg, set, clear;
	int ret = 0;

	if (get_user(arg, value))
		return -EFAULT;

	set = clear = 0;
	switch (cmd) {
	case TIOCMBIS:
		set = arg;
		break;
	case TIOCMBIC:
		clear = arg;
		break;
	case TIOCMSET:
		set = arg;
		clear = ~arg;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret == 0)
		uart_update_mctrl(port, set, clear);
	return ret;
}

static void uart_break_ctl(struct tty_struct *tty, int break_state)
{
	struct uart_info *info = tty->driver_data;
	struct uart_port *port = info->port;

	BUG_ON(!kernel_locked());

	if (port->type != PORT_UNKNOWN)
		port->ops->break_ctl(port, break_state);
}

static int uart_do_autoconfig(struct uart_info *info)
{
	struct uart_port *port = info->port;
	int flags, ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * Take the 'count' lock.  This prevents count
	 * from incrementing, and hence any extra opens
	 * of the port while we're auto-configging.
	 */
	if (down_interruptible(&port_sem))
		return -ERESTARTSYS;

	ret = -EBUSY;
	if (info->state->count == 1 && info->blocked_open == 0) {
		uart_shutdown(info);

		/*
		 * If we already have a port type configured,
		 * we must release its resources.
		 */
		if (port->type != PORT_UNKNOWN)
			port->ops->release_port(port);

		flags = UART_CONFIG_TYPE;
		if (port->flags & UPF_AUTO_IRQ)
			flags |= UART_CONFIG_IRQ;

		/*
		 * This will claim the ports resources if
		 * a port is found.
		 */
		port->ops->config_port(port, flags);

		ret = uart_startup(info, 1);
	}
	up(&port_sem);
	return ret;
}

static int
uart_wait_modem_status(struct uart_info *info, unsigned long arg)
{
	struct uart_port *port = info->port;
	DECLARE_WAITQUEUE(wait, current);
	struct uart_icount cprev, cnow;
	int ret;

	/*
	 * note the counters on entry
	 */
	spin_lock_irq(&port->lock);
	memcpy(&cprev, &port->icount, sizeof(struct uart_icount));
	spin_unlock_irq(&port->lock);

	/*
	 * Force modem status interrupts on
	 */
	port->ops->enable_ms(port);

	add_wait_queue(&info->delta_msr_wait, &wait);
	for (;;) {
		spin_lock_irq(&port->lock);
		memcpy(&cnow, &port->icount, sizeof(struct uart_icount));
		spin_unlock_irq(&port->lock);

		set_current_state(TASK_INTERRUPTIBLE);

		if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
		    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
		    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
		    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
		    	ret = 0;
		    	break;
		}

		schedule();

		/* see if a signal did it */
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		cprev = cnow;
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&info->delta_msr_wait, &wait);

	return ret;
}

/*
 * Called via sys_ioctl under the BKL.  We can use spin_lock_irq() here.
 */
static int
uart_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd,
	   unsigned long arg)
{
	struct uart_info *info = tty->driver_data;
	struct serial_icounter_struct icount;
	struct uart_icount cnow;
	int ret = -ENOIOCTLCMD;

	BUG_ON(!kernel_locked());

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}

	switch (cmd) {
		case TIOCMGET:
			ret = uart_get_modem_info(info->port,
						  (unsigned int *)arg);
			break;

		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			ret = uart_set_modem_info(info->port, cmd,
						  (unsigned int *)arg);
			break;

		case TIOCGSERIAL:
			ret = uart_get_info(info, (struct serial_struct *)arg);
			break;

		case TIOCSSERIAL:
			ret = uart_set_info(info, (struct serial_struct *)arg);
			break;

		case TIOCSERCONFIG:
			ret = uart_do_autoconfig(info);
			break;

		case TIOCSERGETLSR: /* Get line status register */
			ret = uart_get_lsr_info(info, (unsigned int *)arg);
			break;

		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		case TIOCMIWAIT:
			ret = uart_wait_modem_status(info, arg);
			break;

		/*
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
		case TIOCGICOUNT:
			spin_lock_irq(&info->port->lock);
			memcpy(&cnow, &info->port->icount,
			       sizeof(struct uart_icount));
			spin_unlock_irq(&info->port->lock);

			icount.cts         = cnow.cts;
			icount.dsr         = cnow.dsr;
			icount.rng         = cnow.rng;
			icount.dcd         = cnow.dcd;
			icount.rx          = cnow.rx;
			icount.tx          = cnow.tx;
			icount.frame       = cnow.frame;
			icount.overrun     = cnow.overrun;
			icount.parity      = cnow.parity;
			icount.brk         = cnow.brk;
			icount.buf_overrun = cnow.buf_overrun;

			ret = copy_to_user((void *)arg, &icount, sizeof(icount))
					? -EFAULT : 0;
			break;

		case TIOCSERGWILD: /* obsolete */
		case TIOCSERSWILD: /* obsolete */
			ret = 0;
			break;

		default: {
			struct uart_port *port = info->port;
			if (port->ops->ioctl)
				ret = port->ops->ioctl(port, cmd, arg);
			break;
		}
	}
	return ret;
}

static void uart_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;
	unsigned int cflag = tty->termios->c_cflag;

	BUG_ON(!kernel_locked());

	/*
	 * These are the bits that are used to setup various
	 * flags in the low level driver.
	 */
#define RELEVANT_IFLAG(iflag)	((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	if ((cflag ^ old_termios->c_cflag) == 0 &&
	    RELEVANT_IFLAG(tty->termios->c_iflag ^ old_termios->c_iflag) == 0)
		return;

	uart_change_speed(info, old_termios);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD))
		uart_clear_mctrl(info->port, TIOCM_RTS | TIOCM_DTR);

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		unsigned int mask = TIOCM_DTR;
		if (!(cflag & CRTSCTS) ||
		    !test_bit(TTY_THROTTLED, &tty->flags))
			mask |= TIOCM_RTS;
		uart_set_mctrl(info->port, mask);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) && !(cflag & CRTSCTS)) {
		spin_lock_irqsave(&info->port->lock, flags);
		tty->hw_stopped = 0;
		__uart_start(tty);
		spin_unlock_irqrestore(&info->port->lock, flags);
	}

#if 0
	/*
	 * No need to wake up processes in open wait, since they
	 * sample the CLOCAL flag once, and don't recheck it.
	 * XXX  It's not clear whether the current behavior is correct
	 * or not.  Hence, this may change.....
	 */
	if (!(old_termios->c_cflag & CLOCAL) &&
	    (tty->termios->c_cflag & CLOCAL))
		wake_up_interruptible(&info->open_wait);
#endif
}

/*
 * In 2.4.5, calls to this will be serialized via the BKL in
 *  linux/drivers/char/tty_io.c:tty_release()
 *  linux/drivers/char/tty_io.c:do_tty_handup()
 */
static void uart_close(struct tty_struct *tty, struct file *filp)
{
	struct uart_driver *drv = (struct uart_driver *)tty->driver.driver_state;
	struct uart_info *info = tty->driver_data;
	struct uart_port *port = info->port;
	struct uart_state *state;
	unsigned long flags;

	BUG_ON(!kernel_locked());

	if (!info)
		return;

	state = info->state;

	DPRINTK("uart_close() called\n");

	/*
	 * This is safe, as long as the BKL exists in
	 * do_tty_hangup(), and we're protected by the BKL.
	 */
	if (tty_hung_up_p(filp))
		goto done;

	spin_lock_irqsave(&info->port->lock, flags);
	if ((tty->count == 1) && (state->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  state->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("uart_close: bad serial port count; tty->count is 1, "
		       "state->count is %d\n", state->count);
		state->count = 1;
	}
	if (--state->count < 0) {
		printk("rs_close: bad serial port count for %s%d: %d\n",
		       tty->driver.name, info->port->line, state->count);
		state->count = 0;
	}
	if (state->count) {
		spin_unlock_irqrestore(&info->port->lock, flags);
		goto done;
	}

	/*
	 * The UIF_CLOSING flag protects us against further opens
	 * of this port.
	 */
	info->flags |= UIF_CLOSING;
	spin_unlock_irqrestore(&info->port->lock, flags);

	/*
	 * Now we wait for the transmit buffer to clear; and we notify
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->state->closing_wait != USF_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->state->closing_wait);

	/*
	 * At this point, we stop accepting input.  To do this, we
	 * disable the receive line status interrupts.
	 */
	if (info->flags & UIF_INITIALIZED) {
		spin_lock_irqsave(&port->lock, flags);
		port->ops->stop_rx(port);
		spin_unlock_irqrestore(&port->lock, flags);
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		uart_wait_until_sent(tty, port->timeout);
	}
	down(&port_sem);
	uart_shutdown(info);
	up(&port_sem);
	uart_flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = NULL;
	if (info->blocked_open) {
		if (info->state->close_delay) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(info->state->close_delay);
			set_current_state(TASK_RUNNING);
		}
	} else {
#ifdef CONFIG_PM
		/*
		 * Put device into D3 state.
		 */
		pm_send(info->state->pm, PM_SUSPEND, (void *)3);
#else
		if (port->ops->pm)
			port->ops->pm(port, 3, 0);
#endif
	}

	/*
	 * Wake up anyone trying to open this port.
	 */
	info->flags &= ~(UIF_NORMAL_ACTIVE|UIF_CLOSING);
	wake_up_interruptible(&info->open_wait);

 done:
	if (drv->owner)
		__MOD_DEC_USE_COUNT(drv->owner);
}

static void uart_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct uart_info *info = tty->driver_data;
	struct uart_port *port = info->port;
	unsigned long char_time, expire;

	BUG_ON(!kernel_locked());

	if (port->type == PORT_UNKNOWN || port->fifosize == 0)
		return;

	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 *
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = (port->timeout - HZ/50) / port->fifosize;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout && timeout < char_time)
		char_time = timeout;

	/*
	 * If the transmitter hasn't cleared in twice the approximate
	 * amount of time to send the entire FIFO, it probably won't
	 * ever clear.  This assumes the UART isn't doing flow
	 * control, which is currently the case.  Hence, if it ever
	 * takes longer than port->timeout, this is probably due to a
	 * UART bug of some kind.  So, we clamp the timeout parameter at
	 * 2*port->timeout.
	 */
	if (timeout == 0 || timeout > 2 * port->timeout)
		timeout = 2 * port->timeout;

	expire = jiffies + timeout;

	DPRINTK("uart_wait_until_sent(%d), jiffies=%lu, expire=%lu...\n",
	        port->line, jiffies, expire);

	/*
	 * Check whether the transmitter is empty every 'char_time'.
	 * 'timeout' / 'expire' give us the maximum amount of time
	 * we wait.
	 */
	while (!port->ops->tx_empty(port)) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(char_time);
		if (signal_pending(current))
			break;
		if (time_after(jiffies, expire))
			break;
	}
	set_current_state(TASK_RUNNING); /* might not be needed */
}

/*
 * This is called with the BKL held in
 *  linux/drivers/char/tty_io.c:do_tty_hangup()
 * We're called from the eventd thread, so we can sleep for
 * a _short_ time only.
 */
static void uart_hangup(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	struct uart_state *state = info->state;

	BUG_ON(!kernel_locked());

	uart_flush_buffer(tty);
	down(&port_sem);
	if (info->flags & UIF_CLOSING) {
		up(&port_sem);
		return;
	}
	uart_shutdown(info);
	info->event = 0;
	state->count = 0;
	info->flags &= ~UIF_NORMAL_ACTIVE;
	info->tty = NULL;
	up(&port_sem);
	wake_up_interruptible(&info->open_wait);
}

/*
 * Copy across the serial console cflag setting into the termios settings
 * for the initial open of the port.  This allows continuity between the
 * kernel settings, and the settings init adopts when it opens the port
 * for the first time.
 */
static void uart_update_termios(struct uart_info *info)
{
	struct tty_struct *tty = info->tty;

#ifdef CONFIG_SERIAL_CORE_CONSOLE
	struct console *c = info->port->cons;

	if (c && c->cflag && c->index == info->port->line) {
		tty->termios->c_cflag = c->cflag;
		c->cflag = 0;
	}
#endif

	/*
	 * If the device failed to grab its irq resources,
	 * or some other error occurred, don't try to talk
	 * to the port hardware.
	 */
	if (!(tty->flags & (1 << TTY_IO_ERROR))) {
		/*
		 * Make termios settings take effect.
		 */
		uart_change_speed(info, NULL);

		/*
		 * And finally enable the RTS and DTR signals.
		 */
		if (tty->termios->c_cflag & CBAUD)
			uart_set_mctrl(info->port, TIOCM_DTR | TIOCM_RTS);
	}
}

static int
uart_block_til_ready(struct file *filp, struct uart_info *info)
{
	DECLARE_WAITQUEUE(wait, current);
	struct uart_state *state = info->state;
	struct uart_port *port = info->port;

	info->blocked_open++;
	state->count--;

	add_wait_queue(&info->open_wait, &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		/*
		 * If we have been hung up, tell userspace/restart open.
		 */
		if (tty_hung_up_p(filp))
			break;

		/*
		 * If the device is in the middle of being closed, block
		 * until it's done.  We will need to re-initialise the
		 * port.  Hmm, is it legal to block a non-blocking open?
		 */
		if (info->flags & UIF_CLOSING)
			goto wait;

		/*
		 * If the port has been closed, tell userspace/restart open.
		 */
		if (!(info->flags & UIF_INITIALIZED))
			break;

		/*
		 * If non-blocking mode is set, or CLOCAL mode is set,
		 * we don't want to wait for the modem status lines to
		 * indicate that the port is ready.
		 *
		 * Also, if the port is not enabled/configured, we want
		 * to allow the open to succeed here.  Note that we will
		 * have set TTY_IO_ERROR for a non-existant port.
		 */
		if ((filp->f_flags & O_NONBLOCK) ||
	            (info->tty->termios->c_cflag & CLOCAL) ||
		    (info->tty->flags & (1 << TTY_IO_ERROR))) {
			break;
		}

		/*
		 * Set DTR to allow modem to know we're waiting.  Do
		 * not set RTS here - we want to make sure we catch
		 * the data from the modem.
		 */
		if (info->tty->termios->c_cflag & CBAUD)
			uart_set_mctrl(info->port, TIOCM_DTR);

		/*
		 * and wait for the carrier to indicate that the
		 * modem is ready for us.
		 */
		if (port->ops->get_mctrl(port) & TIOCM_CAR)
			break;

	 wait:
		schedule();

		if (signal_pending(current))
			break;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);

	state->count++;
	info->blocked_open--;

	if (signal_pending(current))
		return -ERESTARTSYS;

	if (info->tty->flags & (1 << TTY_IO_ERROR))
		return 0;

	if (tty_hung_up_p(filp) || !(info->flags & UIF_INITIALIZED))
		return (port->flags & UPF_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS;

	return 0;
}

static struct uart_info *uart_get(struct uart_driver *drv, int line)
{
	struct uart_state *state = drv->state + line;
	struct uart_info *info = NULL;

	down(&port_sem);
	if (!state->port)
		goto out;

	state->count++;
	info = state->info;

	if (!info) {
		info = kmalloc(sizeof(struct uart_info), GFP_KERNEL);
		if (info) {
			memset(info, 0, sizeof(struct uart_info));
			init_waitqueue_head(&info->open_wait);
			init_waitqueue_head(&info->delta_msr_wait);

			/*
			 * Link the info into the other structures.
			 */
			info->port  = state->port;
			info->state = state;
			state->port->info = info;

			tasklet_init(&info->tlet, uart_tasklet_action,
				     (unsigned long)info);
			state->info = info;
		} else
			state->count--;
	}

 out:
	up(&port_sem);
	return info;
}

/*
 * In 2.4.5, calls to uart_open are serialised by the BKL in
 *   linux/fs/devices.c:chrdev_open()
 * Note that if this fails, then uart_close() _will_ be called.
 *
 * In time, we want to scrap the "opening nonpresent ports"
 * behaviour and implement an alternative way for setserial
 * to set base addresses/ports/types.  This will allow us to
 * get rid of a certain amount of extra tests.
 */
static int uart_open(struct tty_struct *tty, struct file *filp)
{
	struct uart_driver *drv = (struct uart_driver *)tty->driver.driver_state;
	struct uart_info *info;
	int retval, line = minor(tty->device) - tty->driver.minor_start;

	BUG_ON(!kernel_locked());

	DPRINTK("uart_open(%d) called\n", line);

	/*
	 * tty->driver.num won't change, so we won't fail here with
	 * tty->driver_data set to something non-NULL (and therefore
	 * we won't get caught by uart_close()).
	 */
	retval = -ENODEV;
	if (line >= tty->driver.num)
		goto fail;

	/*
	 * If we fail to increment the module use count, we can't have
	 * any other users of this tty (since this implies that the module
	 * is about to be unloaded).  Therefore, it is safe to set
	 * tty->driver_data to be NULL, so uart_close() doesn't bite us.
	 */
	if (!try_inc_mod_count(drv->owner)) {
		tty->driver_data = NULL;
		goto fail;
	}

	/*
	 * FIXME: This one isn't fun.  We can't guarantee that the tty isn't
	 * already in open, nor can we guarantee the state of tty->driver_data
	 */
	info = uart_get(drv, line);
	retval = -ENOMEM;
	if (!info) {
		if (tty->driver_data)
			goto fail;
		else
			goto out;
	}

	/*
	 * Once we set tty->driver_data here, we are guaranteed that
	 * uart_close() will decrement the driver module use count.
	 * Any failures from here onwards should not touch the count.
	 */
	tty->driver_data = info;
	info->tty = tty;
	info->tty->low_latency = (info->port->flags & UPF_LOW_LATENCY) ? 1 : 0;

	/*
	 * If the port is in the middle of closing, bail out now.
	 */
	if (tty_hung_up_p(filp) || (info->flags & UIF_CLOSING)) {
	    	wait_event_interruptible(info->open_wait,
					 !(info->flags & UIF_CLOSING));
		retval = (info->port->flags & UPF_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS;
		goto fail;
	}

	/*
	 * Make sure the device is in D0 state.
	 */
	if (info->state->count == 1) {
#ifdef CONFIG_PM
		pm_send(info->state->pm, PM_RESUME, (void *)0);
#else
		struct uart_port *port = info->port;
		if (port->ops->pm)
			port->ops->pm(port, 0, 3);
#endif
	}

	/*
	 * Start up the serial port.  We have this semaphore here to
	 * prevent uart_startup or uart_shutdown being re-entered if
	 * we sleep while requesting an IRQ.
	 */
	down(&port_sem);
	retval = uart_startup(info, 0);
	up(&port_sem);
	if (retval)
		goto fail;

	/*
	 * Wait until the port is ready.
	 */
	retval = uart_block_til_ready(filp, info);

	/*
	 * If this is the first open to succeed, adjust things to suit.
	 */
	if (retval == 0 && !(info->flags & UIF_NORMAL_ACTIVE)) {
		info->flags |= UIF_NORMAL_ACTIVE;

		uart_update_termios(info);
	}

	return retval;

 out:
	if (drv->owner)
		__MOD_DEC_USE_COUNT(drv->owner);
 fail:
	return retval;
}

#ifdef CONFIG_PROC_FS

static const char *uart_type(struct uart_port *port)
{
	const char *str = NULL;

	if (port->ops->type)
		str = port->ops->type(port);

	if (!str)
		str = "unknown";

	return str;
}

static int uart_line_info(char *buf, struct uart_driver *drv, int i)
{
	struct uart_state *state = drv->state + i;
	struct uart_port *port = state->port;
	char stat_buf[32];
	unsigned int status;
	int ret;

	if (!port)
		return 0;

	ret = sprintf(buf, "%d: uart:%s port:%08X irq:%d",
			port->line, uart_type(port),
			port->iobase, port->irq);

	if (port->type == PORT_UNKNOWN) {
		strcat(buf, "\n");
		return ret + 1;
	}

	status = port->ops->get_mctrl(port);

	ret += sprintf(buf + ret, " tx:%d rx:%d",
			port->icount.tx, port->icount.rx);
	if (port->icount.frame)
		ret += sprintf(buf + ret, " fe:%d",
			port->icount.frame);
	if (port->icount.parity)
		ret += sprintf(buf + ret, " pe:%d",
			port->icount.parity);
	if (port->icount.brk)
		ret += sprintf(buf + ret, " brk:%d",
			port->icount.brk);
	if (port->icount.overrun)
		ret += sprintf(buf + ret, " oe:%d",
			port->icount.overrun);

#define INFOBIT(bit,str) \
	if (port->mctrl & (bit)) \
		strncat(stat_buf, (str), sizeof(stat_buf) - \
			strlen(stat_buf) - 2)
#define STATBIT(bit,str) \
	if (status & (bit)) \
		strncat(stat_buf, (str), sizeof(stat_buf) - \
		       strlen(stat_buf) - 2)

	stat_buf[0] = '\0';
	stat_buf[1] = '\0';
	INFOBIT(TIOCM_RTS, "|RTS");
	STATBIT(TIOCM_CTS, "|CTS");
	INFOBIT(TIOCM_DTR, "|DTR");
	STATBIT(TIOCM_DSR, "|DSR");
	STATBIT(TIOCM_CAR, "|CD");
	STATBIT(TIOCM_RNG, "|RI");
	if (stat_buf[0])
		stat_buf[0] = ' ';
	strcat(stat_buf, "\n");

	ret += sprintf(buf + ret, stat_buf);
	return ret;
}

static int uart_read_proc(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	struct tty_driver *ttydrv = data;
	struct uart_driver *drv = ttydrv->driver_state;
	int i, len = 0, l;
	off_t begin = 0;

	len += sprintf(page, "serinfo:1.0 driver%s%s revision:%s\n",
			"", "", "");
	for (i = 0; i < drv->nr && len < PAGE_SIZE - 96; i++) {
		l = uart_line_info(page + len, drv, i);
		len += l;
		if (len + begin > off + count)
			goto done;
		if (len + begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
 done:
	if (off >= len + begin)
		return 0;
	*start = page + (off - begin);
	return (count < begin + len - off) ? count : (begin + len - off);
}
#endif

#ifdef CONFIG_SERIAL_CORE_CONSOLE
/*
 *	Check whether an invalid uart number has been specified, and
 *	if so, search for the first available port that does have
 *	console support.
 */
struct uart_port * __init
uart_get_console(struct uart_port *ports, int nr, struct console *co)
{
	int idx = co->index;

	if (idx < 0 || idx >= nr || (ports[idx].iobase == 0 &&
				     ports[idx].membase == NULL))
		for (idx = 0; idx < nr; idx++)
			if (ports[idx].iobase != 0 ||
			    ports[idx].membase != NULL)
				break;

	co->index = idx;

	return ports + idx;
}

/**
 *	uart_parse_options - Parse serial port baud/parity/bits/flow contro.
 *	@options: pointer to option string
 *	@baud: pointer to an 'int' variable for the baud rate.
 *	@parity: pointer to an 'int' variable for the parity.
 *	@bits: pointer to an 'int' variable for the number of data bits.
 *	@flow: pointer to an 'int' variable for the flow control character.
 *
 *	uart_parse_options decodes a string containing the serial console
 *	options.  The format of the string is <baud><parity><bits><flow>,
 *	eg: 115200n8r
 */
void __init
uart_parse_options(char *options, int *baud, int *parity, int *bits, int *flow)
{
	char *s = options;

	*baud = simple_strtoul(s, NULL, 10);
	while (*s >= '0' && *s <= '9')
		s++;
	if (*s)
		*parity = *s++;
	if (*s)
		*bits = *s++ - '0';
	if (*s)
		*flow = *s;
}

struct baud_rates {
	unsigned int rate;
	unsigned int cflag;
};

static struct baud_rates baud_rates[] = {
	{ 921600, B921600 },
	{ 460800, B460800 },
	{ 230400, B230400 },
	{ 115200, B115200 },
	{  57600, B57600  },
	{  38400, B38400  },
	{  19200, B19200  },
	{   9600, B9600   },
	{   4800, B4800   },
	{   2400, B2400   },
	{   1200, B1200   },
	{      0, B38400  }
};

/**
 *	uart_set_options - setup the serial console parameters
 *	@port: pointer to the serial ports uart_port structure
 *	@co: console pointer
 *	@baud: baud rate
 *	@parity: parity character - 'n' (none), 'o' (odd), 'e' (even)
 *	@bits: number of data bits
 *	@flow: flow control character - 'r' (rts)
 */
int __init
uart_set_options(struct uart_port *port, struct console *co,
		 int baud, int parity, int bits, int flow)
{
	unsigned int cflag = CREAD | HUPCL | CLOCAL;
	unsigned int quot;
	int i;

	/*
	 * Construct a cflag setting.
	 */
	for (i = 0; baud_rates[i].rate; i++)
		if (baud_rates[i].rate <= baud)
			break;

	cflag |= baud_rates[i].cflag;

	if (bits == 7)
		cflag |= CS7;
	else
		cflag |= CS8;

	switch (parity) {
	case 'o': case 'O':
		cflag |= PARODD;
		/*fall through*/
	case 'e': case 'E':
		cflag |= PARENB;
		break;
	}

	if (flow == 'r')
		cflag |= CRTSCTS;

	co->cflag = cflag;
	quot = (port->uartclk / (16 * baud));
	port->ops->change_speed(port, cflag, 0, quot);

	return 0;
}

extern void ambauart_console_init(void);
extern void anakin_console_init(void);
extern void clps711xuart_console_init(void);
extern void rs285_console_init(void);
extern void sa1100_rs_console_init(void);
extern void serial8250_console_init(void);
extern void uart00_console_init(void);

/*
 * Central "initialise all serial consoles" container.  Needs to be killed.
 */
void __init uart_console_init(void)
{
#ifdef CONFIG_SERIAL_AMBA_CONSOLE
	ambauart_console_init();
#endif
#ifdef CONFIG_SERIAL_ANAKIN_CONSOLE
	anakin_console_init();
#endif
#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE
	clps711xuart_console_init();
#endif
#ifdef CONFIG_SERIAL_21285_CONSOLE
	rs285_console_init();
#endif
#ifdef CONFIG_SERIAL_SA1100_CONSOLE
	sa1100_rs_console_init();
#endif
#ifdef CONFIG_SERIAL_8250_CONSOLE
	serial8250_console_init();
#endif
#ifdef CONFIG_SERIAL_UART00_CONSOLE
	uart00_console_init();
#endif
}
#endif /* CONFIG_SERIAL_CORE_CONSOLE */

#ifdef CONFIG_PM
/*
 *  Serial port power management.
 *
 * This is pretty coarse at the moment - either all on or all off.  We
 * should probably some day do finer power management here some day.
 *
 * We don't actually save any state; the serial driver already has the
 * state held internally to re-setup the port when we come out of D3.
 */
static int uart_pm_set_state(struct uart_state *state, int pm_state, int oldstate)
{
	struct uart_port *port;
	struct uart_ops *ops;
	int running = state->info &&
		      state->info->flags & UIF_INITIALIZED;

	down(&port_sem);

	if (!state->port || state->port->type == PORT_UNKNOWN) {
		up(&port_sem);
		return 0;
	}

	port = state->port;
	ops = port->ops;

	DPRINTK("pm: %08x: %d -> %d, %srunning\n",
		port->iobase, dev->state, pm_state, running ? "" : "not ");

	if (pm_state == 0) {
		if (ops->pm)
			ops->pm(port, pm_state, oldstate);
		if (running) {
			/*
			 * The port lock isn't taken here -
			 * the port isn't initialised.
			 */
			ops->set_mctrl(port, 0);
			ops->startup(port);
			uart_change_speed(state->info, NULL);
			spin_lock_irq(&port->lock);
			ops->set_mctrl(port, port->mctrl);
			ops->start_tx(port, 0);
			spin_unlock_irq(&port->lock);
		}

		/*
		 * Re-enable the console device after suspending.
		 */
		if (port->cons && port->cons->index == port->line)
			port->cons->flags |= CON_ENABLED;
	} else if (pm_state == 1) {
		if (ops->pm)
			ops->pm(port, pm_state, oldstate);
	} else {
		/*
		 * Disable the console device before suspending.
		 */
		if (port->cons && port->cons->index == port->line)
			port->cons->flags &= ~CON_ENABLED;

		if (running) {
			spin_lock_irq(&port->lock);
			ops->stop_tx(port, 0);
			ops->set_mctrl(port, 0);
			ops->stop_rx(port);
			spin_unlock_irq(&port->lock);
			ops->shutdown(port);
		}
		if (ops->pm)
			ops->pm(port, pm_state, oldstate);
	}
	up(&port_sem);

	return 0;
}

/*
 *  Wakeup support.
 */
static int uart_pm_set_wakeup(struct uart_state *state, int data)
{
	int err = 0;

	if (state->port->ops->set_wake)
		err = state->port->ops->set_wake(state->port, data);

	return err;
}

static int uart_pm(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	struct uart_state *state = dev->data;
	int err = 0;

	switch (rqst) {
	case PM_SUSPEND:
	case PM_RESUME:
		err = uart_pm_set_state(state, (int)data, dev->state);
		break;

	case PM_SET_WAKEUP:
		err = uart_pm_set_wakeup(state, (int)data);
		break;
	}
	return err;
}
#endif

static inline void
uart_report_port(struct uart_driver *drv, struct uart_port *port)
{
	printk("%s%d at ", drv->dev_name, port->line);
	switch (port->iotype) {
	case UPIO_PORT:
		printk("I/O 0x%x", port->iobase);
		break;
	case UPIO_HUB6:
		printk("I/O 0x%x offset 0x%x", port->iobase, port->hub6);
		break;
	case UPIO_MEM:
		printk("MMIO 0x%lx", port->mapbase);
		break;
	}
	printk(" (irq = %d) is a %s\n", port->irq, uart_type(port));
}

static void
__uart_register_port(struct uart_driver *drv, struct uart_state *state,
		     struct uart_port *port)
{
	unsigned int flags;

	state->port = port;

	spin_lock_init(&port->lock);
	port->type = PORT_UNKNOWN;
	port->cons = drv->cons;
	port->info = state->info;

	/*
	 * If there isn't a port here, don't do anything further.
	 */
	if (!port->iobase && !port->mapbase)
		return;

	/*
	 * Now do the auto configuration stuff.  Note that config_port
	 * is expected to claim the resources and map the port for us.
	 */
	flags = UART_CONFIG_TYPE;
	if (port->flags & UPF_AUTO_IRQ)
		flags |= UART_CONFIG_IRQ;
	if (port->flags & UPF_BOOT_AUTOCONF)
		port->ops->config_port(port, flags);

	/*
	 * Register the port whether it's detected or not.  This allows
	 * setserial to be used to alter this ports parameters.
	 */
	tty_register_devfs(drv->tty_driver, 0, drv->minor + port->line);

	if (port->type != PORT_UNKNOWN) {
		unsigned long flags;

		uart_report_port(drv, port);

		/*
		 * Ensure that the modem control lines are de-activated.
		 * We probably don't need a spinlock around this, but
		 */
		spin_lock_irqsave(&port->lock, flags);
		port->ops->set_mctrl(port, 0);
		spin_unlock_irqrestore(&port->lock, flags);

#ifdef CONFIG_PM
		/*
		 * Power down all ports by default, except the
		 * console if we have one.  We need to drop the
		 * port semaphore here.
		 */
		if (state->pm && (!drv->cons || port->line != drv->cons->index)) {
			up(&port_sem);
			pm_send(state->pm, PM_SUSPEND, (void *)3);
			down(&port_sem);
		}
#endif
	}
}

/*
 *	Hangup the port.  This must be done outside the port_sem
 *	since uart_hangup() grabs this same semaphore.  Grr.
 */
static void
__uart_hangup_port(struct uart_driver *drv, struct uart_state *state)
{
	struct uart_info *info = state->info;

	if (info && info->tty)
		tty_vhangup(info->tty);
}

/*
 * This reverses the affects of __uart_register_port.
 */
static void
__uart_unregister_port(struct uart_driver *drv, struct uart_state *state)
{
	struct uart_port *port = state->port;
	struct uart_info *info = state->info;

	state->info = NULL;

	/*
	 * Remove the devices from devfs
	 */
	tty_unregister_devfs(drv->tty_driver, drv->minor + port->line);

	/*
	 * Free the port IO and memory resources, if any.
	 */
	if (port->type != PORT_UNKNOWN)
		port->ops->release_port(port);

	/*
	 * Indicate that there isn't a port here anymore.
	 */
	port->type = PORT_UNKNOWN;

	/*
	 * Kill the tasklet, and free resources.
	 */
	if (info) {
		tasklet_kill(&info->tlet);
		kfree(info);
	}
}

/**
 *	uart_register_driver - register a driver with the uart core layer
 *	@drv: low level driver structure
 *
 *	Register a uart driver with the core driver.  We in turn register
 *	with the tty layer, and initialise the core driver per-port state.
 *
 *	We have a proc file in /proc/tty/driver which is named after the
 *	normal driver.
 *
 *	drv->port should be NULL, and the per-port structures should be
 *	registered using uart_add_one_port after this call has succeeded.
 */
int uart_register_driver(struct uart_driver *drv)
{
	struct tty_driver *normal = NULL;
	struct termios **termios = NULL;
	int i, retval;

	BUG_ON(drv->state);

	/*
	 * Maybe we should be using a slab cache for this, especially if
	 * we have a large number of ports to handle.  Note that we also
	 * allocate space for an integer for reference counting.
	 */
	drv->state = kmalloc(sizeof(struct uart_state) * drv->nr +
			     sizeof(int), GFP_KERNEL);
	retval = -ENOMEM;
	if (!drv->state)
		goto out;

	memset(drv->state, 0, sizeof(struct uart_state) * drv->nr +
			sizeof(int));

	termios = kmalloc(sizeof(struct termios *) * drv->nr * 2 +
			  sizeof(struct tty_struct *) * drv->nr, GFP_KERNEL);
	if (!termios)
		goto out;

	memset(termios, 0, sizeof(struct termios *) * drv->nr * 2 +
			   sizeof(struct tty_struct *) * drv->nr);

	normal  = kmalloc(sizeof(struct tty_driver), GFP_KERNEL);
	if (!normal)
		goto out;

	memset(normal, 0, sizeof(struct tty_driver));

	drv->tty_driver = normal;

	normal->magic		= TTY_DRIVER_MAGIC;
	normal->driver_name	= drv->driver_name;
	normal->name		= drv->dev_name;
	normal->major		= drv->major;
	normal->minor_start	= drv->minor;
	normal->num		= drv->nr;
	normal->type		= TTY_DRIVER_TYPE_SERIAL;
	normal->subtype		= SERIAL_TYPE_NORMAL;
	normal->init_termios	= tty_std_termios;
	normal->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	normal->flags		= TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	normal->refcount	= (int *)(drv->state + drv->nr);
	normal->termios		= termios;
	normal->termios_locked	= termios + drv->nr;
	normal->table		= (struct tty_struct **)(termios + drv->nr * 2);
	normal->driver_state    = drv;

	normal->open		= uart_open;
	normal->close		= uart_close;
	normal->write		= uart_write;
	normal->put_char	= uart_put_char;
	normal->flush_chars	= uart_flush_chars;
	normal->write_room	= uart_write_room;
	normal->chars_in_buffer	= uart_chars_in_buffer;
	normal->flush_buffer	= uart_flush_buffer;
	normal->ioctl		= uart_ioctl;
	normal->throttle	= uart_throttle;
	normal->unthrottle	= uart_unthrottle;
	normal->send_xchar	= uart_send_xchar;
	normal->set_termios	= uart_set_termios;
	normal->stop		= uart_stop;
	normal->start		= uart_start;
	normal->hangup		= uart_hangup;
	normal->break_ctl	= uart_break_ctl;
	normal->wait_until_sent	= uart_wait_until_sent;
#ifdef CONFIG_PROC_FS
	normal->read_proc	= uart_read_proc;
#endif

	/*
	 * Initialise the UART state(s).
	 */
	for (i = 0; i < drv->nr; i++) {
		struct uart_state *state = drv->state + i;

		state->close_delay     = 5 * HZ / 10;
		state->closing_wait    = 30 * HZ;
#ifdef CONFIG_PM
		state->pm = pm_register(PM_SYS_DEV, PM_SYS_COM, uart_pm);
		if (state->pm)
			state->pm->data = state;
#endif
	}

	retval = tty_register_driver(normal);
 out:
	if (retval < 0) {
#ifdef CONFIG_PM
		for (i = 0; i < drv->nr; i++)
			pm_unregister(drv->state[i].pm);
#endif
		kfree(normal);
		kfree(drv->state);
		kfree(termios);
	}
	return retval;
}

/**
 *	uart_unregister_driver - remove a driver from the uart core layer
 *	@drv: low level driver structure
 *
 *	Remove all references to a driver from the core driver.  The low
 *	level driver must have removed all its ports via the
 *	uart_remove_one_port() if it registered them with uart_add_one_port().
 *	(ie, drv->port == NULL)
 */
void uart_unregister_driver(struct uart_driver *drv)
{
	int i;

	for (i = 0; i < drv->nr; i++)
		pm_unregister(drv->state[i].pm);

	tty_unregister_driver(drv->tty_driver);

	kfree(drv->state);
	kfree(drv->tty_driver->termios);
	kfree(drv->tty_driver);
}

/**
 *	uart_add_one_port - attach a driver-defined port structure
 *	@drv: pointer to the uart low level driver structure for this port
 *	@port: uart port structure to use for this port.
 *
 *	This allows the driver to register its own uart_port structure
 *	with the core driver.  The main purpose is to allow the low
 *	level uart drivers to expand uart_port, rather than having yet
 *	more levels of structures.
 */
int uart_add_one_port(struct uart_driver *drv, struct uart_port *port)
{
	struct uart_state *state;

	BUG_ON(in_interrupt());

	if (port->line >= drv->nr)
		return -EINVAL;

	state = drv->state + port->line;

	down(&port_sem);
	__uart_register_port(drv, state, port);
	up(&port_sem);

	return 0;
}

/**
 *	uart_remove_one_port - detach a driver defined port structure
 *	@drv: pointer to the uart low level driver structure for this port
 *	@port: uart port structure for this port
 *
 *	This unhooks (and hangs up) the specified port structure from the
 *	core driver.  No further calls will be made to the low-level code
 *	for this port.
 */
int uart_remove_one_port(struct uart_driver *drv, struct uart_port *port)
{
	struct uart_state *state = drv->state + port->line;

	BUG_ON(in_interrupt());

	if (state->port != port)
		printk(KERN_ALERT "Removing wrong port: %p != %p\n",
			state->port, port);

	__uart_hangup_port(drv, state);

	down(&port_sem);
	__uart_unregister_port(drv, state);
	state->port = NULL;
	up(&port_sem);

	return 0;
}

/*
 *	Are the two ports equivalent?
 */
static int uart_match_port(struct uart_port *port1, struct uart_port *port2)
{
	if (port1->iotype != port2->iotype)
		return 0;

	switch (port1->iotype) {
	case UPIO_PORT:
		return (port1->iobase == port2->iobase);
	case UPIO_HUB6:
		return (port1->iobase == port2->iobase) &&
		       (port1->hub6   == port2->hub6);
	case UPIO_MEM:
		return (port1->membase == port2->membase);
	}
	return 0;
}

/*
 *	Try to find an unused uart_state slot for a port.
 */
static struct uart_state *
uart_find_match_or_unused(struct uart_driver *drv, struct uart_port *port)
{
	int i;

	/*
	 * First, find a port entry which matches.  Note: if we do
	 * find a matching entry, and it has a non-zero use count,
	 * then we can't register the port.
	 */
	for (i = 0; i < drv->nr; i++)
		if (uart_match_port(drv->state[i].port, port))
			return &drv->state[i];

	/*
	 * We didn't find a matching entry, so look for the first
	 * free entry.  We look for one which hasn't been previously
	 * used (indicated by zero iobase).
	 */
	for (i = 0; i < drv->nr; i++)
		if (drv->state[i].port->type == PORT_UNKNOWN &&
		    drv->state[i].port->iobase == 0 &&
		    drv->state[i].count == 0)
			return &drv->state[i];

	/*
	 * That also failed.  Last resort is to find any currently
	 * entry which doesn't have a real port associated with it.
	 */
	for (i = 0; i < drv->nr; i++)
		if (drv->state[i].port->type == PORT_UNKNOWN &&
		    drv->state[i].count == 0)
			return &drv->state[i];

	return NULL;
}

/**
 *	uart_register_port: register uart settings with a port
 *	@drv: pointer to the uart low level driver structure for this port
 *	@port: uart port structure describing the port
 *
 *	Register UART settings with the specified low level driver.  Detect
 *	the type of the port if UPF_BOOT_AUTOCONF is set, and detect the
 *	IRQ if UPF_AUTO_IRQ is set.
 *
 *	We try to pick the same port for the same IO base address, so that
 *	when a modem is plugged in, unplugged and plugged back in, it gets
 *	allocated the same port.
 *
 *	Returns negative error, or positive line number.
 */
int uart_register_port(struct uart_driver *drv, struct uart_port *port)
{
	struct uart_state *state;
	int ret;

	down(&port_sem);

	state = uart_find_match_or_unused(drv, port);

	if (state) {
		/*
		 * Ok, we've found a line that we can use.
		 *
		 * If we find a port that matches this one, and it appears
		 * to be in-use (even if it doesn't have a type) we shouldn't
		 * alter it underneath itself - the port may be open and
		 * trying to do useful work.
		 */
		if (state->count != 0 ||
		    (state->info && state->info->blocked_open != 0)) {
			ret = -EBUSY;
			goto out;
		}

		state->port->iobase   = port->iobase;
		state->port->membase  = port->membase;
		state->port->irq      = port->irq;
		state->port->uartclk  = port->uartclk;
		state->port->fifosize = port->fifosize;
		state->port->regshift = port->regshift;
		state->port->iotype   = port->iotype;
		state->port->flags    = port->flags;
		state->port->line     = state - drv->state;

		__uart_register_port(drv, state, state->port);

		ret = state->port->line;
	} else
		ret = -ENOSPC;
 out:
	up(&port_sem);
	return ret;
}

/**
 *	uart_unregister_port - de-allocate a port
 *	@drv: pointer to the uart low level driver structure for this port
 *	@line: line index previously returned from uart_register_port()
 *
 *	Hang up the specified line associated with the low level driver,
 *	and mark the port as unused.
 */
void uart_unregister_port(struct uart_driver *drv, int line)
{
	struct uart_state *state;

	if (line < 0 || line >= drv->nr) {
		printk(KERN_ERR "Attempt to unregister %s%d\n",
			drv->dev_name, line);
		return;
	}

	state = drv->state + line;

	__uart_hangup_port(drv, state);

	down(&port_sem);
	__uart_unregister_port(drv, state);
	up(&port_sem);
}

EXPORT_SYMBOL(uart_event);
EXPORT_SYMBOL(uart_register_driver);
EXPORT_SYMBOL(uart_unregister_driver);
EXPORT_SYMBOL(uart_register_port);
EXPORT_SYMBOL(uart_unregister_port);
EXPORT_SYMBOL(uart_add_one_port);
EXPORT_SYMBOL(uart_remove_one_port);

MODULE_DESCRIPTION("Serial driver core");
MODULE_LICENSE("GPL");
