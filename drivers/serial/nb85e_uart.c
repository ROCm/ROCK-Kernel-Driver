/*
 * drivers/serial/nb85e_uart.c -- Serial I/O using V850E/NB85E on-chip UART
 *
 *  Copyright (C) 2001,02,03  NEC Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/nb85e_uart.h>
#include <asm/nb85e_utils.h>

/* Initial UART state.  This may be overridden by machine-dependent headers. */
#ifndef NB85E_UART_INIT_BAUD
#define NB85E_UART_INIT_BAUD	115200
#endif
#ifndef NB85E_UART_INIT_CFLAGS
#define NB85E_UART_INIT_CFLAGS	(B115200 | CS8 | CREAD)
#endif

/* XXX This should be in a header file.  */
#define NB85E_UART_BRGC_MIN	8

/* A string used for prefixing printed descriptions; since the same UART
   macro is actually used on other chips than the V850E/NB85E.  This must
   be a constant string.  */
#ifndef NB85E_UART_CHIP_NAME
#define NB85E_UART_CHIP_NAME "V850E/NB85E"
#endif


/* Helper functions for doing baud-rate/frequency calculations.  */

/* Calculate the minimum value for CKSR on this processor.  */
static inline unsigned cksr_min (void)
{
	int min = 0;
	unsigned freq = NB85E_UART_BASE_FREQ;
	while (freq > NB85E_UART_CKSR_MAX_FREQ) {
		freq >>= 1;
		min++;
	}
	return min;
}

/* Minimum baud rate possible.  */
#define min_baud() \
   ((NB85E_UART_BASE_FREQ >> NB85E_UART_CKSR_MAX) / (2 * 255) + 1)

/* Maximum baud rate possible.  The error is quite high at max, though.  */
#define max_baud() \
   ((NB85E_UART_BASE_FREQ >> cksr_min()) / (2 * NB85E_UART_BRGC_MIN))


/* Low-level UART functions.  */

/* These masks define which control bits affect TX/RX modes, respectively.  */
#define RX_BITS \
  (NB85E_UART_ASIM_PS_MASK | NB85E_UART_ASIM_CL_8 | NB85E_UART_ASIM_ISRM)
#define TX_BITS \
  (NB85E_UART_ASIM_PS_MASK | NB85E_UART_ASIM_CL_8 | NB85E_UART_ASIM_SL_2)

/* The UART require various delays after writing control registers.  */
static inline void nb85e_uart_delay (unsigned cycles)
{
	/* The loop takes 2 insns, so loop CYCLES / 2 times.  */
	register unsigned count = cycles >> 1;
	while (--count != 0)
		/* nothing */;
}

/* Configure and turn on uart channel CHAN, using the termios `control
   modes' bits in CFLAGS, and a baud-rate of BAUD.  */
void nb85e_uart_configure (unsigned chan, unsigned cflags, unsigned baud)
{
	int flags;
	unsigned new_config = 0; /* What we'll write to the control reg. */
	unsigned new_clk_divlog2; /* New baud-rate generate clock divider. */
	unsigned new_brgen_count; /* New counter max for baud-rate generator.*/
	/* These are the current values corresponding to the above.  */
	unsigned old_config, old_clk_divlog2, old_brgen_count;

	/* Calculate new baud-rate generator config values.  */

	/* Calculate the log2 clock divider and baud-rate counter values
	   (note that the UART divides the resulting clock by 2, so
	   multiply BAUD by 2 here to compensate).  */
	calc_counter_params (NB85E_UART_BASE_FREQ, baud * 2,
			     cksr_min(), NB85E_UART_CKSR_MAX, 8/*bits*/,
			     &new_clk_divlog2, &new_brgen_count);

	/* Figure out new configuration of control register.  */
	if (cflags & CSTOPB)
		/* Number of stop bits, 1 or 2.  */
		new_config |= NB85E_UART_ASIM_SL_2;
	if ((cflags & CSIZE) == CS8)
		/* Number of data bits, 7 or 8.  */
		new_config |= NB85E_UART_ASIM_CL_8;
	if (! (cflags & PARENB))
		/* No parity check/generation.  */
		new_config |= NB85E_UART_ASIM_PS_NONE;
	else if (cflags & PARODD)
		/* Odd parity check/generation.  */
		new_config |= NB85E_UART_ASIM_PS_ODD;
	else
		/* Even parity check/generation.  */
		new_config |= NB85E_UART_ASIM_PS_EVEN;
	if (cflags & CREAD)
		/* Reading enabled.  */
		new_config |= NB85E_UART_ASIM_RXE;

	new_config |= NB85E_UART_ASIM_TXE; /* Writing is always enabled.  */
	new_config |= NB85E_UART_ASIM_CAE;
	new_config |= NB85E_UART_ASIM_ISRM; /* Errors generate a read-irq.  */

	/* Disable interrupts while we're twiddling the hardware.  */
	local_irq_save (flags);

#ifdef NB85E_UART_PRE_CONFIGURE
	NB85E_UART_PRE_CONFIGURE (chan, cflags, baud);
#endif

	old_config = NB85E_UART_ASIM (chan);
	old_clk_divlog2 = NB85E_UART_CKSR (chan);
	old_brgen_count = NB85E_UART_BRGC (chan);

	if (new_clk_divlog2 != old_clk_divlog2
	    || new_brgen_count != old_brgen_count)
	{
		/* The baud rate has changed.  First, disable the UART.  */
		NB85E_UART_ASIM (chan) = 0;
		old_config = 0;
		/* Reprogram the baud-rate generator.  */
		NB85E_UART_CKSR (chan) = new_clk_divlog2;
		NB85E_UART_BRGC (chan) = new_brgen_count;
	}

	if (! (old_config & NB85E_UART_ASIM_CAE)) {
		/* If we are enabling the uart for the first time, start
		   by turning on the enable bit, which must be done
		   before turning on any other bits.  */
		NB85E_UART_ASIM (chan) = NB85E_UART_ASIM_CAE;
		/* Enabling the uart also resets it.  */
		old_config = NB85E_UART_ASIM_CAE;
	}

	if (new_config != old_config) {
		/* Which of the TXE/RXE bits we'll temporarily turn off
		   before changing other control bits.  */
		unsigned temp_disable = 0;
		/* Which of the TXE/RXE bits will be enabled.  */
		unsigned enable = 0;
		unsigned changed_bits = new_config ^ old_config;

		/* Which of RX/TX will be enabled in the new configuration.  */
		if (new_config & RX_BITS)
			enable |= (new_config & NB85E_UART_ASIM_RXE);
		if (new_config & TX_BITS)
			enable |= (new_config & NB85E_UART_ASIM_TXE);

		/* Figure out which of RX/TX needs to be disabled; note
		   that this will only happen if they're not already
		   disabled.  */
		if (changed_bits & RX_BITS)
			temp_disable |= (old_config & NB85E_UART_ASIM_RXE);
		if (changed_bits & TX_BITS)
			temp_disable |= (old_config & NB85E_UART_ASIM_TXE);

		/* We have to turn off RX and/or TX mode before changing
		   any associated control bits.  */
		if (temp_disable)
			NB85E_UART_ASIM (chan) = old_config & ~temp_disable;

		/* Write the new control bits, while RX/TX are disabled. */ 
		if (changed_bits & ~enable)
			NB85E_UART_ASIM (chan) = new_config & ~enable;

		/* The UART may not be reset properly unless we
		   wait at least 2 `basic-clocks' until turning
		   on the TXE/RXE bits again.  A `basic clock'
		   is the clock used by the baud-rate generator, i.e.,
		   the cpu clock divided by the 2^new_clk_divlog2.  */
		nb85e_uart_delay (1 << (new_clk_divlog2 + 1));

		/* Write the final version, with enable bits turned on.  */
		NB85E_UART_ASIM (chan) = new_config;
	}

	local_irq_restore (flags);
}


/*  Low-level console. */

#ifdef CONFIG_V850E_NB85E_UART_CONSOLE

static void nb85e_uart_cons_write (struct console *co,
				   const char *s, unsigned count)
{
	if (count > 0) {
		unsigned chan = co->index;
		unsigned irq = IRQ_INTST (chan);
		int irq_was_enabled, irq_was_pending, flags;

		/* We don't want to get `transmission completed' (INTST)
		   interrupts, since we're busy-waiting, so we disable
		   them while sending (we don't disable interrupts
		   entirely because sending over a serial line is really
		   slow).  We save the status of INTST and restore it
		   when we're done so that using printk doesn't
		   interfere with normal serial transmission (other than
		   interleaving the output, of course!).  This should
		   work correctly even if this function is interrupted
		   and the interrupt printks something.  */

		/* Disable interrupts while fiddling with INTST.  */
		local_irq_save (flags);
		/* Get current INTST status.  */
		irq_was_enabled = nb85e_intc_irq_enabled (irq);
		irq_was_pending = nb85e_intc_irq_pending (irq);
		/* Disable INTST if necessary.  */
		if (irq_was_enabled)
			nb85e_intc_disable_irq (irq);
		/* Turn interrupts back on.  */
		local_irq_restore (flags);

		/* Send characters.  */
		while (count > 0) {
			int ch = *s++;

			if (ch == '\n') {
				/* We don't have the benefit of a tty
				   driver, so translate NL into CR LF.  */
				nb85e_uart_wait_for_xmit_ok (chan);
				nb85e_uart_putc (chan, '\r');
			}

			nb85e_uart_wait_for_xmit_ok (chan);
			nb85e_uart_putc (chan, ch);

			count--;
		}

		/* Restore saved INTST status.  */
		if (irq_was_enabled) {
			/* Wait for the last character we sent to be
			   completely transmitted (as we'll get an INTST
			   interrupt at that point).  */
			nb85e_uart_wait_for_xmit_done (chan);
			/* Clear pending interrupts received due
			   to our transmission, unless there was already
			   one pending, in which case we want the
			   handler to be called.  */
			if (! irq_was_pending)
				nb85e_intc_clear_pending_irq (irq);
			/* ... and then turn back on handling.  */
			nb85e_intc_enable_irq (irq);
		}
	}
}

extern struct uart_driver nb85e_uart_driver;
static struct console nb85e_uart_cons =
{
    .name	= "ttyS",
    .write	= nb85e_uart_cons_write,
    .device	= uart_console_device,
    .flags	= CON_PRINTBUFFER,
    .cflag	= NB85E_UART_INIT_CFLAGS,
    .index	= -1,
    .data	= &nb85e_uart_driver,
};

void nb85e_uart_cons_init (unsigned chan)
{
	nb85e_uart_configure (chan, NB85E_UART_INIT_CFLAGS,
			      NB85E_UART_INIT_BAUD);
	nb85e_uart_cons.index = chan;
	register_console (&nb85e_uart_cons);
	printk ("Console: %s on-chip UART channel %d\n",
		NB85E_UART_CHIP_NAME, chan);
}

#define NB85E_UART_CONSOLE &nb85e_uart_cons

#else /* !CONFIG_V850E_NB85E_UART_CONSOLE */
#define NB85E_UART_CONSOLE 0
#endif /* CONFIG_V850E_NB85E_UART_CONSOLE */

/* TX/RX interrupt handlers.  */

static void nb85e_uart_stop_tx (struct uart_port *port, unsigned tty_stop);

void nb85e_uart_tx (struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;
	int stopped = uart_tx_stopped (port);

	if (nb85e_uart_xmit_ok (port->line)) {
		int tx_ch;

		if (port->x_char) {
			tx_ch = port->x_char;
			port->x_char = 0;
		} else if (!uart_circ_empty (xmit) && !stopped) {
			tx_ch = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		} else
			goto no_xmit;

		nb85e_uart_putc (port->line, tx_ch);
		port->icount.tx++;

		if (uart_circ_chars_pending (xmit) < WAKEUP_CHARS)
			uart_write_wakeup (port);
	}

 no_xmit:
	if (uart_circ_empty (xmit) || stopped)
		nb85e_uart_stop_tx (port, stopped);
}

static void nb85e_uart_tx_irq (int irq, void *data, struct pt_regs *regs)
{
	struct uart_port *port = data;
	nb85e_uart_tx (port);
}

static void nb85e_uart_rx_irq (int irq, void *data, struct pt_regs *regs)
{
	struct uart_port *port = data;
	unsigned ch_stat = TTY_NORMAL;
	unsigned ch = NB85E_UART_RXB (port->line);
	unsigned err = NB85E_UART_ASIS (port->line);

	if (err) {
		if (err & NB85E_UART_ASIS_OVE) {
			ch_stat = TTY_OVERRUN;
			port->icount.overrun++;
		} else if (err & NB85E_UART_ASIS_FE) {
			ch_stat = TTY_FRAME;
			port->icount.frame++;
		} else if (err & NB85E_UART_ASIS_PE) {
			ch_stat = TTY_PARITY;
			port->icount.parity++;
		}
	}

	port->icount.rx++;

	tty_insert_flip_char (port->info->tty, ch, ch_stat);
	tty_schedule_flip (port->info->tty);
}

/* Control functions for the serial framework.  */

static void nb85e_uart_nop (struct uart_port *port) { }
static int nb85e_uart_success (struct uart_port *port) { return 0; }

static unsigned nb85e_uart_tx_empty (struct uart_port *port)
{
	return TIOCSER_TEMT;	/* Can't detect.  */
}

static void nb85e_uart_set_mctrl (struct uart_port *port, unsigned mctrl)
{
#ifdef NB85E_UART_SET_RTS
	NB85E_UART_SET_RTS (port->line, (mctrl & TIOCM_RTS));
#endif
}

static unsigned nb85e_uart_get_mctrl (struct uart_port *port)
{
	/* We don't support DCD or DSR, so consider them permanently active. */
	int mctrl = TIOCM_CAR | TIOCM_DSR;

	/* We may support CTS.  */
#ifdef NB85E_UART_CTS
	mctrl |= NB85E_UART_CTS(port->line) ? TIOCM_CTS : 0;
#else
	mctrl |= TIOCM_CTS;
#endif

	return mctrl;
}

static void nb85e_uart_start_tx (struct uart_port *port, unsigned tty_start)
{
	nb85e_intc_disable_irq (IRQ_INTST (port->line));
	nb85e_uart_tx (port);
	nb85e_intc_enable_irq (IRQ_INTST (port->line));
}

static void nb85e_uart_stop_tx (struct uart_port *port, unsigned tty_stop)
{
	nb85e_intc_disable_irq (IRQ_INTST (port->line));
}

static void nb85e_uart_start_rx (struct uart_port *port)
{
	nb85e_intc_enable_irq (IRQ_INTSR (port->line));
}

static void nb85e_uart_stop_rx (struct uart_port *port)
{
	nb85e_intc_disable_irq (IRQ_INTSR (port->line));
}

static void nb85e_uart_break_ctl (struct uart_port *port, int break_ctl)
{
	/* Umm, do this later.  */
}

static int nb85e_uart_startup (struct uart_port *port)
{
	int err;

	/* Alloc RX irq.  */
	err = request_irq (IRQ_INTSR (port->line), nb85e_uart_rx_irq,
			   SA_INTERRUPT, "nb85e_uart", port);
	if (err)
		return err;

	/* Alloc TX irq.  */
	err = request_irq (IRQ_INTST (port->line), nb85e_uart_tx_irq,
			   SA_INTERRUPT, "nb85e_uart", port);
	if (err) {
		free_irq (IRQ_INTSR (port->line), port);
		return err;
	}

	nb85e_uart_start_rx (port);

	return 0;
}

static void nb85e_uart_shutdown (struct uart_port *port)
{
	/* Disable port interrupts.  */
	free_irq (IRQ_INTST (port->line), port);
	free_irq (IRQ_INTSR (port->line), port);

	/* Turn off xmit/recv enable bits.  */
	NB85E_UART_ASIM (port->line)
		&= ~(NB85E_UART_ASIM_TXE | NB85E_UART_ASIM_RXE);
	/* Then reset the channel.  */
	NB85E_UART_ASIM (port->line) = 0;
}

static void
nb85e_uart_set_termios (struct uart_port *port, struct termios *termios,
		        struct termios *old)
{
	unsigned cflags = termios->c_cflag;

	/* Restrict flags to legal values.  */
	if ((cflags & CSIZE) != CS7 && (cflags & CSIZE) != CS8)
		/* The new value of CSIZE is invalid, use the old value.  */
		cflags = (cflags & ~CSIZE)
			| (old ? (old->c_cflag & CSIZE) : CS8);

	termios->c_cflag = cflags;

	nb85e_uart_configure (port->line, cflags,
			      uart_get_baud_rate (port, termios, old,
						  min_baud(), max_baud()));
}

static const char *nb85e_uart_type (struct uart_port *port)
{
	return port->type == PORT_NB85E_UART ? "nb85e_uart" : 0;
}

static void nb85e_uart_config_port (struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_NB85E_UART;
}

static int
nb85e_uart_verify_port (struct uart_port *port, struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_NB85E_UART)
		return -EINVAL;
	if (ser->irq != IRQ_INTST (port->line))
		return -EINVAL;
	return 0;
}

static struct uart_ops nb85e_uart_ops = {
	.tx_empty	= nb85e_uart_tx_empty,
	.get_mctrl	= nb85e_uart_get_mctrl,
	.set_mctrl	= nb85e_uart_set_mctrl,
	.start_tx	= nb85e_uart_start_tx,
	.stop_tx	= nb85e_uart_stop_tx,
	.stop_rx	= nb85e_uart_stop_rx,
	.enable_ms	= nb85e_uart_nop,
	.break_ctl	= nb85e_uart_break_ctl,
	.startup	= nb85e_uart_startup,
	.shutdown	= nb85e_uart_shutdown,
	.set_termios	= nb85e_uart_set_termios,
	.type		= nb85e_uart_type,
	.release_port	= nb85e_uart_nop,
	.request_port	= nb85e_uart_success,
	.config_port	= nb85e_uart_config_port,
	.verify_port	= nb85e_uart_verify_port,
};

/* Initialization and cleanup.  */

static struct uart_driver nb85e_uart_driver = {
	.owner			= THIS_MODULE,
	.driver_name		= "nb85e_uart",
#ifdef CONFIG_DEVFS_FS
	.dev_name		= "tts/",
#else
	.dev_name		= "ttyS",
#endif
	.major			= TTY_MAJOR,
	.minor			= NB85E_UART_MINOR_BASE,
	.nr			= NB85E_UART_NUM_CHANNELS,
	.cons			= NB85E_UART_CONSOLE,
};


static struct uart_port nb85e_uart_ports[NB85E_UART_NUM_CHANNELS];

static int __init nb85e_uart_init (void)
{
	int rval;

	printk (KERN_INFO "%s on-chip UART\n", NB85E_UART_CHIP_NAME);

	rval = uart_register_driver (&nb85e_uart_driver);
	if (rval == 0) {
		unsigned chan;

		for (chan = 0; chan < NB85E_UART_NUM_CHANNELS; chan++) {
			struct uart_port *port = &nb85e_uart_ports[chan];
			
			memset (port, 0, sizeof *port);

			port->ops = &nb85e_uart_ops;
			port->line = chan;
			port->iotype = SERIAL_IO_MEM;
			port->flags = UPF_BOOT_AUTOCONF;

			/* We actually use multiple IRQs, but the serial
			   framework seems to mainly use this for
			   informational purposes anyway.  Here we use the TX
			   irq.  */
			port->irq = IRQ_INTST (chan);

			/* The serial framework doesn't really use these
			   membase/mapbase fields for anything useful, but
			   it requires that they be something non-zero to
			   consider the port `valid', and also uses them
			   for informational purposes.  */
			port->membase = (void *)NB85E_UART_BASE_ADDR (chan);
			port->mapbase = NB85E_UART_BASE_ADDR (chan);

			/* The framework insists on knowing the uart's master
			   clock freq, though it doesn't seem to do anything
			   useful for us with it.  We must make it at least
			   higher than (the maximum baud rate * 16), otherwise
			   the framework will puke during its internal
			   calculations, and force the baud rate to be 9600.
			   To be accurate though, just repeat the calculation
			   we use when actually setting the speed.

			   The `* 8' means `* 16 / 2':  16 to account for for
			   the serial framework's built-in bias, and 2 because
			   there's an additional / 2 in the hardware.  */
			port->uartclk =
				(NB85E_UART_BASE_FREQ >> cksr_min()) * 8;

			uart_add_one_port (&nb85e_uart_driver, port);
		}
	}

	return rval;
}

static void __exit nb85e_uart_exit (void)
{
	unsigned chan;

	for (chan = 0; chan < NB85E_UART_NUM_CHANNELS; chan++)
		uart_remove_one_port (&nb85e_uart_driver,
				      &nb85e_uart_ports[chan]);

	uart_unregister_driver (&nb85e_uart_driver);
}

module_init (nb85e_uart_init);
module_exit (nb85e_uart_exit);

MODULE_AUTHOR ("Miles Bader");
MODULE_DESCRIPTION ("NEC " NB85E_UART_CHIP_NAME " on-chip UART");
MODULE_LICENSE ("GPL");
