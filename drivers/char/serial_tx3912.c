/*
 *  drivers/char/serial_tx3912.c
 *
 *  Copyright (C) 1999 Harald Koerfgen
 *  Copyright (C) 2000 Jim Pick <jim@jimpick.com>
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *  
 *  Serial driver for TMPR3912/05 and PR31700 processors
 */
#include <linux/init.h>
#include <linux/config.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/ptrace.h>
#include <linux/console.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/wbflush.h>
#include <asm/tx3912.h>
#include "serial_tx3912.h"

/*
 * Forward declarations for serial routines
 */
static void rs_disable_tx_interrupts (void * ptr);
static void rs_enable_tx_interrupts (void * ptr); 
static void rs_disable_rx_interrupts (void * ptr); 
static void rs_enable_rx_interrupts (void * ptr); 
static int rs_get_CD (void * ptr); 
static void rs_shutdown_port (void * ptr); 
static int rs_set_real_termios (void *ptr);
static int rs_chars_in_buffer (void * ptr); 

/*
 * Used by generic serial driver to access hardware
 */
static struct real_driver rs_real_driver = { 
	.disable_tx_interrupts = rs_disable_tx_interrupts, 
	.enable_tx_interrupts  = rs_enable_tx_interrupts, 
	.disable_rx_interrupts = rs_disable_rx_interrupts, 
	.enable_rx_interrupts  = rs_enable_rx_interrupts, 
	.get_CD                = rs_get_CD, 
	.shutdown_port         = rs_shutdown_port,  
	.set_real_termios      = rs_set_real_termios,  
	.chars_in_buffer       = rs_chars_in_buffer, 
}; 

/*
 * Structures and such for TTY sessions and usage counts
 */
static struct tty_driver *rs_driver;
struct rs_port *rs_ports;
int rs_initialized = 0;

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * Note: rs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * rs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 * 
 * gcc -S -DKERNEL -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer serial.c
 *
 * and look at the resulting assemble code in serial.s.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */
static inline void receive_char_pio(struct rs_port *port)
{
	struct tty_struct *tty = port->gs.tty;
	unsigned char ch;
	int counter = 2048;

	/* While there are characters, get them ... */
	while (counter>0) {
		if (!(inl(port->base + TX3912_UART_CTRL1) & UART_RX_HOLD_FULL))
			break;
		ch = inb(port->base + TX3912_UART_DATA);
		if (tty->flip.count < TTY_FLIPBUF_SIZE) {
			*tty->flip.char_buf_ptr++ = ch;
			*tty->flip.flag_buf_ptr++ = 0;
			tty->flip.count++;
		}
		udelay(1); /* Allow things to happen - it take a while */
		counter--;
	}
	if (!counter)
		printk( "Ugh, looped in receive_char_pio!\n" );

	tty_flip_buffer_push(tty);

#if 0
	/* Now handle error conditions */
	if (*status & (INTTYPE(UART_RXOVERRUN_INT) |
			INTTYPE(UART_FRAMEERR_INT) |
			INTTYPE(UART_PARITYERR_INT) |
			INTTYPE(UART_BREAK_INT))) {

		/*
		 * Now check to see if character should be
		 * ignored, and mask off conditions which
		 * should be ignored.
	       	 */
		if (*status & port->ignore_status_mask) {
			goto ignore_char;
		}
		*status &= port->read_status_mask;
		
		if (*status & INTTYPE(UART_BREAK_INT)) {
			rs_dprintk(TX3912_UART_DEBUG_INTERRUPTS, "handling break....");
			*tty->flip.flag_buf_ptr = TTY_BREAK;
		}
		else if (*status & INTTYPE(UART_PARITYERR_INT)) {
			*tty->flip.flag_buf_ptr = TTY_PARITY;
		}
		else if (*status & INTTYPE(UART_FRAMEERR_INT)) {
			*tty->flip.flag_buf_ptr = TTY_FRAME;
		}
		if (*status & INTTYPE(UART_RXOVERRUN_INT)) {
			/*
			 * Overrun is special, since it's
			 * reported immediately, and doesn't
			 * affect the current character
			 */
			if (tty->flip.count < TTY_FLIPBUF_SIZE) {
				tty->flip.count++;
				tty->flip.flag_buf_ptr++;
				tty->flip.char_buf_ptr++;
				*tty->flip.flag_buf_ptr = TTY_OVERRUN;
			}
		}
	}

	tty->flip.flag_buf_ptr++;
	tty->flip.char_buf_ptr++;
	tty->flip.count++;

ignore_char:
	tty_flip_buffer_push(tty);
#endif
}

static inline void transmit_char_pio(struct rs_port *port)
{
	/* While I'm able to transmit ... */
	for (;;) {
		if (!(inl(port->base + TX3912_UART_CTRL1) & UART_TX_EMPTY))
			break;
		else if (port->x_char) {
			outb(port->x_char, port->base + TX3912_UART_DATA);
			port->icount.tx++;
			port->x_char = 0;
		}
		else if (port->gs.xmit_cnt <= 0 || port->gs.tty->stopped ||
		    port->gs.tty->hw_stopped) {
			break;
		}
		else {
			outb(port->gs.xmit_buf[port->gs.xmit_tail++],
				port->base + TX3912_UART_DATA);
			port->icount.tx++;
			port->gs.xmit_tail &= SERIAL_XMIT_SIZE-1;
			if (--port->gs.xmit_cnt <= 0) {
				break;
			}
		}
		udelay(10); /* Allow things to happen - it take a while */
	}

	if (port->gs.xmit_cnt <= 0 || port->gs.tty->stopped ||
	     port->gs.tty->hw_stopped) {
		rs_disable_tx_interrupts(port);
	}
	
        if (port->gs.xmit_cnt <= port->gs.wakeup_chars) {
                if ((port->gs.tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
                    port->gs.tty->ldisc.write_wakeup)
                        (port->gs.tty->ldisc.write_wakeup)(port->gs.tty);
                rs_dprintk (TX3912_UART_DEBUG_TRANSMIT, "Waking up.... ldisc (%d)....\n",
                            port->gs.wakeup_chars); 
                wake_up_interruptible(&port->gs.tty->write_wait);
       	}	
}



static inline void check_modem_status(struct rs_port *port)
{
        /* We don't have a carrier detect line - but just respond
           like we had one anyways so that open() becomes unblocked */
	wake_up_interruptible(&port->gs.open_wait);
}

int count = 0;

/*
 * This is the serial driver's interrupt routine (inlined, because
 * there are two different versions of this, one for each serial port,
 * differing only by the bits used in interrupt status 2 register)
 */

static inline void rs_rx_interrupt(int irq, void *dev_id,
				  struct pt_regs * regs, int intshift)
{
	struct rs_port * port;
	unsigned long int2status;
	unsigned long flags;
	unsigned long ints;

	save_and_cli(flags);

	port = (struct rs_port *)dev_id;
	rs_dprintk (TX3912_UART_DEBUG_INTERRUPTS, "rs_interrupt (port %p, shift %d)...", port, intshift);

	/* Get the interrrupts we have enabled */
	int2status = IntStatus2 & IntEnable2;

	/* Get interrupts in easy to use form */
	ints = int2status >> intshift;

	/* Clear any interrupts we might be about to handle */
	IntClear2 = int2status & (
		(INTTYPE(UART_RXOVERRUN_INT) |
		 INTTYPE(UART_FRAMEERR_INT) |
		 INTTYPE(UART_BREAK_INT) |
		 INTTYPE(UART_PARITYERR_INT) |
		 INTTYPE(UART_RX_INT)) << intshift);

	if (!port || !port->gs.tty) {
		restore_flags(flags);
		return;
	}

	/* RX Receiver Holding Register Overrun */
	if (ints & INTTYPE(UART_RXOVERRUN_INT)) {
		rs_dprintk (TX3912_UART_DEBUG_INTERRUPTS, "overrun");
		port->icount.overrun++;
	}

	/* RX Frame Error */
	if (ints & INTTYPE(UART_FRAMEERR_INT)) {
		rs_dprintk (TX3912_UART_DEBUG_INTERRUPTS, "frame error");
		port->icount.frame++;
	}

	/* Break signal received */
	if (ints & INTTYPE(UART_BREAK_INT)) {
		rs_dprintk (TX3912_UART_DEBUG_INTERRUPTS, "break");
		port->icount.brk++;
      	}

	/* RX Parity Error */
	if (ints & INTTYPE(UART_PARITYERR_INT)) {
		rs_dprintk (TX3912_UART_DEBUG_INTERRUPTS, "parity error");
		port->icount.parity++;
	}

	/* Receive byte (non-DMA) */
	if (ints & INTTYPE(UART_RX_INT)) {
		receive_char_pio(port);
	}

	restore_flags(flags);

	rs_dprintk (TX3912_UART_DEBUG_INTERRUPTS, "end.\n");
}

static inline void rs_tx_interrupt(int irq, void *dev_id,
				  struct pt_regs * regs, int intshift)
{
	struct rs_port * port;
	unsigned long int2status;
	unsigned long flags;
	unsigned long ints;

	save_and_cli(flags);

	port = (struct rs_port *)dev_id;
	rs_dprintk (TX3912_UART_DEBUG_INTERRUPTS, "rs_interrupt (port %p, shift %d)...", port, intshift);

	/* Get the interrrupts we have enabled */
	int2status = IntStatus2 & IntEnable2;

	if (!port || !port->gs.tty) {
		restore_flags(flags);
		return;
	}

	/* Get interrupts in easy to use form */
	ints = int2status >> intshift;

	/* Clear any interrupts we might be about to handle */
	IntClear2 = int2status & (
		(INTTYPE(UART_TX_INT) |
		 INTTYPE(UART_EMPTY_INT) |
		 INTTYPE(UART_TXOVERRUN_INT)) << intshift);

	/* TX holding register empty, so transmit byte (non-DMA) */
	if (ints & (INTTYPE(UART_TX_INT) | INTTYPE(UART_EMPTY_INT))) {
		transmit_char_pio(port);
	}

	/* TX Transmit Holding Register Overrun (shouldn't happen) */
	if (ints & INTTYPE(UART_TXOVERRUN_INT)) {
		printk ( "rs: TX overrun\n");
	}

	/*
	check_modem_status();
	*/

	restore_flags(flags);

	rs_dprintk (TX3912_UART_DEBUG_INTERRUPTS, "end.\n");
}

static void rs_rx_interrupt_uarta(int irq, void *dev_id,
					 struct pt_regs * regs)
{
	rs_rx_interrupt(irq, dev_id, regs, UARTA_SHIFT);
}

static void rs_tx_interrupt_uarta(int irq, void *dev_id,
					 struct pt_regs * regs)
{
	rs_tx_interrupt(irq, dev_id, regs, UARTA_SHIFT);
}

/*
 ***********************************************************************
 *                Here are the routines that actually                  *
 *              interface with the generic_serial driver               *
 ***********************************************************************
 */
static void rs_disable_tx_interrupts (void * ptr) 
{
	struct rs_port *port = ptr; 
	unsigned long flags;

	save_and_cli(flags);
        port->gs.flags &= ~GS_TX_INTEN;

	IntEnable2 &= ~((INTTYPE(UART_TX_INT) |
			INTTYPE(UART_EMPTY_INT) |
			INTTYPE(UART_TXOVERRUN_INT)) << port->intshift);

	IntClear2 = (INTTYPE(UART_TX_INT) |
			INTTYPE(UART_EMPTY_INT) |
			INTTYPE(UART_TXOVERRUN_INT)) << port->intshift;

	restore_flags(flags);
}

static void rs_enable_tx_interrupts (void * ptr) 
{
	struct rs_port *port = ptr; 
	unsigned long flags;

	save_and_cli(flags);

	IntClear2 = (INTTYPE(UART_TX_INT) |
			INTTYPE(UART_EMPTY_INT) |
			INTTYPE(UART_TXOVERRUN_INT)) << port->intshift;

	IntEnable2 |= (INTTYPE(UART_TX_INT) |
			INTTYPE(UART_EMPTY_INT) |
			INTTYPE(UART_TXOVERRUN_INT)) << port->intshift;

	/* Send a char to start TX interrupts happening */
	transmit_char_pio(port);

	restore_flags(flags);
}

static void rs_disable_rx_interrupts (void * ptr) 
{
	struct rs_port *port = ptr;
	unsigned long flags;

	save_and_cli(flags);

	IntEnable2 &= ~((INTTYPE(UART_RX_INT) |
			 INTTYPE(UART_RXOVERRUN_INT) |
			 INTTYPE(UART_FRAMEERR_INT) |
			 INTTYPE(UART_BREAK_INT) |
			 INTTYPE(UART_PARITYERR_INT)) << port->intshift);

	IntClear2 = (INTTYPE(UART_RX_INT) |
			 INTTYPE(UART_RXOVERRUN_INT) |
			 INTTYPE(UART_FRAMEERR_INT) |
			 INTTYPE(UART_BREAK_INT) |
			 INTTYPE(UART_PARITYERR_INT)) << port->intshift;

	restore_flags(flags);
}

static void rs_enable_rx_interrupts (void * ptr) 
{
	struct rs_port *port = ptr;
	unsigned long flags;

	save_and_cli(flags);

	IntEnable2 |= (INTTYPE(UART_RX_INT) |
			 INTTYPE(UART_RXOVERRUN_INT) |
			 INTTYPE(UART_FRAMEERR_INT) |
			 INTTYPE(UART_BREAK_INT) |
			 INTTYPE(UART_PARITYERR_INT)) << port->intshift;

	/* Empty the input buffer - apparently this is *vital* */
	while (inl(port->base + TX3912_UART_CTRL1) & UART_RX_HOLD_FULL) { 
		inb(port->base + TX3912_UART_DATA);
	}

	IntClear2 = (INTTYPE(UART_RX_INT) |
			 INTTYPE(UART_RXOVERRUN_INT) |
			 INTTYPE(UART_FRAMEERR_INT) |
			 INTTYPE(UART_BREAK_INT) |
			 INTTYPE(UART_PARITYERR_INT)) << port->intshift;

	restore_flags(flags);
}


static int rs_get_CD (void * ptr) 
{
	/* No Carried Detect in Hardware - just return true */
	func_exit();
	return (1);
}

static void rs_shutdown_port (void * ptr) 
{
	struct rs_port *port = ptr; 

	func_enter();

	port->gs.flags &= ~GS_ACTIVE;

	func_exit();
}

static int rs_set_real_termios (void *ptr)
{
	struct rs_port *port = ptr;
	int t;

	switch (port->gs.baud) {
		/* Save some typing work... */
#define e(x) case x:t= TX3912_UART_CTRL2_B ## x ; break
		e(300);e(600);e(1200);e(2400);e(4800);e(9600);
		e(19200);e(38400);e(57600);e(76800);e(115200);e(230400);
	case 0      :t = -1;
		break;
	default:
		/* Can I return "invalid"? */
		t = TX3912_UART_CTRL2_B9600;
		printk (KERN_INFO "rs: unsupported baud rate: %d.\n", port->gs.baud);
		break;
	}
#undef e
	if (t >= 0) {
		/* Jim: Set Hardware Baud rate - there is some good
		   code in drivers/char/serial.c */

	  	/* Program hardware for parity, data bits, stop bits (note: these are hardcoded to 8N1 */
		UartA_Ctrl1 &= 0xf000000f;
		UartA_Ctrl1 &= ~(UART_DIS_TXD | SER_SEVEN_BIT | SER_EVEN_PARITY | SER_TWO_STOP);

#define CFLAG port->gs.tty->termios->c_cflag
		if (C_PARENB(port->gs.tty)) {
			if (!C_PARODD(port->gs.tty))
				UartA_Ctrl1 |= SER_EVEN_PARITY;
			else
				UartA_Ctrl1 |= SER_ODD_PARITY;
		}
		if ((CFLAG & CSIZE)==CS6)
			printk(KERN_ERR "6 bits not supported\n");
		if ((CFLAG & CSIZE)==CS5)
			printk(KERN_ERR "5 bits not supported\n");
		if ((CFLAG & CSIZE)==CS7)
			UartA_Ctrl1 |= SER_SEVEN_BIT;
		if (C_CSTOPB(port->gs.tty))
			UartA_Ctrl1 |= SER_TWO_STOP;

		outl(t, port->base + TX3912_UART_CTRL2);
		outl(0, port->base + TX3912_UART_DMA_CTRL1);
		outl(0, port->base + TX3912_UART_DMA_CTRL2);
        	UartA_Ctrl1 |= TX3912_UART_CTRL1_UARTON;

        /* wait until UARTA is stable */
        while (~UartA_Ctrl1 & TX3912_UART_CTRL1_UARTON);
	}

	func_exit ();
        return 0;
}

static int rs_chars_in_buffer (void * ptr) 
{
	struct rs_port *port = ptr;
	int scratch;

	scratch = inl(port->base + TX3912_UART_CTRL1);

	return ((scratch & UART_TX_EMPTY) ? 0 : 1);
}

/* ********************************************************************** *
 *                Here are the routines that actually                     *
 *               interface with the rest of the system                    *
 * ********************************************************************** */
static int rs_open  (struct tty_struct * tty, struct file * filp)
{
	struct rs_port *port;
	int retval, line;

	func_enter();

	if (!rs_initialized) {
		return -EIO;
	}

	line = tty->index;
	rs_dprintk (TX3912_UART_DEBUG_OPEN, "%d: opening line %d. tty=%p ctty=%p)\n", 
	            (int) current->pid, line, tty, current->tty);

	if ((line < 0) || (line >= TX3912_UART_NPORTS))
		return -ENODEV;

	/* Pre-initialized already */
	port = & rs_ports[line];

	rs_dprintk (TX3912_UART_DEBUG_OPEN, "port = %p\n", port);

	tty->driver_data = port;
	port->gs.tty = tty;
	port->gs.count++;

	rs_dprintk (TX3912_UART_DEBUG_OPEN, "starting port\n");

	/*
	 * Start up serial port
	 */
	retval = gs_init_port(&port->gs);
	rs_dprintk (TX3912_UART_DEBUG_OPEN, "done gs_init\n");
	if (retval) {
		port->gs.count--;
		return retval;
	}

	port->gs.flags |= GS_ACTIVE;

	rs_dprintk (TX3912_UART_DEBUG_OPEN, "before inc_use_count (count=%d.\n", 
	            port->gs.count);
	rs_dprintk (TX3912_UART_DEBUG_OPEN, "after inc_use_count\n");

	/* Jim: Initialize port hardware here */

	/* Enable high-priority interrupts for UARTA */
	IntEnable6 |= INT6_UARTARXINT; 
	rs_enable_rx_interrupts(&rs_ports[0]); 

	retval = gs_block_til_ready(&port->gs, filp);
	rs_dprintk (TX3912_UART_DEBUG_OPEN, "Block til ready returned %d. Count=%d\n", 
	            retval, port->gs.count);

	if (retval) {
		port->gs.count--;
		return retval;
	}
	/* tty->low_latency = 1; */

	func_exit();

	/* Jim */
/*	cli(); */

	return 0;

}


static int rs_ioctl (struct tty_struct * tty, struct file * filp, 
                     unsigned int cmd, unsigned long arg)
{
	int rc;
	struct rs_port *port = tty->driver_data;
	int ival;

	rc = 0;
	switch (cmd) {
	case TIOCGSOFTCAR:
		rc = put_user(((tty->termios->c_cflag & CLOCAL) ? 1 : 0),
		              (unsigned int *) arg);
		break;
	case TIOCSSOFTCAR:
		if ((rc = get_user(ival, (unsigned int *) arg)) == 0) {
			tty->termios->c_cflag =
				(tty->termios->c_cflag & ~CLOCAL) |
				(ival ? CLOCAL : 0);
		}
		break;
	case TIOCGSERIAL:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		                      sizeof(struct serial_struct))) == 0)
			rc = gs_getserial(&port->gs, (struct serial_struct *) arg);
		break;
	case TIOCSSERIAL:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		                      sizeof(struct serial_struct))) == 0)
			rc = gs_setserial(&port->gs, (struct serial_struct *) arg);
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	/* func_exit(); */
	return rc;
}


/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void rs_send_xchar(struct tty_struct * tty, char ch)
{
	struct rs_port *port = (struct rs_port *)tty->driver_data;
	func_enter ();
	
	port->x_char = ch;
	if (ch) {
		/* Make sure transmit interrupts are on */
		rs_enable_tx_interrupts(tty);
	}

	func_exit();
}


/*
 * ------------------------------------------------------------
 * rs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_throttle(struct tty_struct * tty)
{
#ifdef TX3912_UART_DEBUG_THROTTLE
	char	buf[64];
	
	printk("throttle %s: %d....\n", tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	func_enter ();
	
	if (I_IXOFF(tty))
		rs_send_xchar(tty, STOP_CHAR(tty));

	func_exit ();
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct rs_port *port = (struct rs_port *)tty->driver_data;
#ifdef TX3912_UART_DEBUG_THROTTLE
	char	buf[64];
	
	printk("unthrottle %s: %d....\n", tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	func_enter();
	
	if (I_IXOFF(tty)) {
		if (port->x_char)
			port->x_char = 0;
		else
			rs_send_xchar(tty, START_CHAR(tty));
	}

	func_exit();
}





/* ********************************************************************** *
 *                    Here are the initialization routines.               *
 * ********************************************************************** */

void * ckmalloc (int size)
{
        void *p;

        p = kmalloc(size, GFP_KERNEL);
        if (p) 
                memset(p, 0, size);
        return p;
}



static int rs_init_portstructs(void)
{
	struct rs_port *port;
	int i;

	/* Debugging */
	func_enter();

	rs_ports          = ckmalloc(TX3912_UART_NPORTS * sizeof (struct rs_port));
	if (!rs_ports)
		return -ENOMEM;

	port = rs_ports;
	for (i=0; i < TX3912_UART_NPORTS;i++) {
		rs_dprintk (TX3912_UART_DEBUG_INIT, "initing port %d\n", i);
		port->gs.magic = SERIAL_MAGIC;
		port->gs.close_delay = HZ/2;
		port->gs.closing_wait = 30 * HZ;
		port->gs.rd = &rs_real_driver;
#ifdef NEW_WRITE_LOCKING
		port->gs.port_write_sem = MUTEX;
#endif
#ifdef DECLARE_WAITQUEUE
		init_waitqueue_head(&port->gs.open_wait);
		init_waitqueue_head(&port->gs.close_wait);
#endif
		port->base = (i == 0) ? TX3912_UARTA_BASE : TX3912_UARTB_BASE;
		port->intshift = (i == 0) ? UARTA_SHIFT : UARTB_SHIFT;
		rs_dprintk (TX3912_UART_DEBUG_INIT, "base 0x%08lx intshift %d\n",
			    port->base, port->intshift);
		port++;
	}

	func_exit();
	return 0;
}

static struct tty_operations rs_ops = {
	.open	= rs_open,
	.close = gs_close,
	.write = gs_write,
	.put_char = gs_put_char, 
	.flush_chars = gs_flush_chars,
	.write_room = gs_write_room,
	.chars_in_buffer = gs_chars_in_buffer,
	.flush_buffer = gs_flush_buffer,
	.ioctl = rs_ioctl,
	.throttle = rs_throttle,
	.unthrottle = rs_unthrottle,
	.set_termios = gs_set_termios,
	.stop = gs_stop,
	.start = gs_start,
	.hangup = gs_hangup,
};

static int rs_init_drivers(void)
{
	int error;

	func_enter();

	rs_driver = alloc_tty_driver(TX3912_UART_NPORTS);
	if (!rs_driver)
		return -ENOMEM;
	rs_driver->owner = THIS_MODULE;
	rs_driver->driver_name = "serial";
	rs_driver->name = "ttyS";
	rs_driver->major = TTY_MAJOR;
	rs_driver->minor_start = 64;
	rs_driver->type = TTY_DRIVER_TYPE_SERIAL;
	rs_driver->subtype = SERIAL_TYPE_NORMAL;
	rs_driver->init_termios = tty_std_termios;
	rs_driver->init_termios.c_cflag =
		B115200 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_set_operations(rs_driver, &rs_ops);
	if ((error = tty_register_driver(rs_driver))) {
		printk(KERN_ERR "Couldn't register serial driver, error = %d\n",
		       error);
		put_tty_driver(rs_driver);
		return 1;
	}
	return 0;
}


static void __init tx3912_rs_init(void)
{
	int rc;


	func_enter();
	rs_dprintk (TX3912_UART_DEBUG_INIT, "Initing serial module... (rs_debug=%d)\n", rs_debug);

	rc = rs_init_portstructs ();
	rs_init_drivers ();
	if (request_irq(2, rs_tx_interrupt_uarta, SA_SHIRQ | SA_INTERRUPT,
			"serial", &rs_ports[0])) {
		printk(KERN_ERR "rs: Cannot allocate irq for UARTA.\n");
		rc = 0;
	}
	if (request_irq(3, rs_rx_interrupt_uarta, SA_SHIRQ | SA_INTERRUPT,
			"serial", &rs_ports[0])) {
		printk(KERN_ERR "rs: Cannot allocate irq for UARTA.\n");
		rc = 0;
	}

	IntEnable6 |= INT6_UARTARXINT; 
	rs_enable_rx_interrupts(&rs_ports[0]); 

#ifndef CONFIG_SERIAL_TX3912_CONSOLE
{
	unsigned int scratch = 0;

	/* Setup master clock for UART */
	scratch = inl(TX3912_CLK_CTRL_BASE);
	scratch &= ~TX3912_CLK_CTRL_SIBMCLKDIV_MASK;
	scratch |= ((0x2 << TX3912_CLK_CTRL_SIBMCLKDIV_SHIFT) &
				TX3912_CLK_CTRL_SIBMCLKDIV_MASK)
			| TX3912_CLK_CTRL_SIBMCLKDIR
			| TX3912_CLK_CTRL_ENSIBMCLK
			| TX3912_CLK_CTRL_CSERSEL;
	outl(scratch, TX3912_CLK_CTRL_BASE);

	/* Configure UARTA clock */
	scratch = inl(TX3912_CLK_CTRL_BASE);
	scratch |= ((0x3 << TX3912_CLK_CTRL_CSERDIV_SHIFT) &
				TX3912_CLK_CTRL_CSERDIV_MASK)
			| TX3912_CLK_CTRL_ENCSERCLK
			| TX3912_CLK_CTRL_ENUARTACLK;
	outl(scratch, TX3912_CLK_CTRL_BASE);
		
	/* Setup UARTA for 115200,8N1 */
	outl(0, TX3912_UARTA_BASE + TX3912_UART_CTRL1);
	outl(TX3912_UART_CTRL2_B115200, TX3912_UARTA_BASE + TX3912_UART_CTRL2);
	outl(0, TX3912_UARTA_BASE + TX3912_UART_DMA_CTRL1);
	outl(0, TX3912_UARTA_BASE + TX3912_UART_DMA_CTRL2);

	/* Enable UARTA */
	outl(TX3912_UART_CTRL1_ENUART, TX3912_UARTA_BASE + TX3912_UART_CTRL1);
	while (~inl(TX3912_UARTA_BASE + TX3912_UART_CTRL1) &
		TX3912_UART_CTRL1_UARTON);
}
#endif

	/* Note: I didn't do anything to enable the second UART */
	if (rc >= 0) 
		rs_initialized++;

	func_exit();
}
module_init(tx3912_rs_init);

/*
 * Begin serial console routines
 */
#ifdef CONFIG_SERIAL_TX3912_CONSOLE

void serial_outc(unsigned char c)
{
	int i;
	unsigned long int2;
	#define BUSY_WAIT 10000

	/*
	 * Turn UARTA interrupts off
	 */
	int2 = IntEnable2;
	IntEnable2 &=
		~(INT2_UARTATXINT | INT2_UARTATXOVERRUN | INT2_UARTAEMPTY);

	/*
	 * The UART_TX_EMPTY bit in UartA_Ctrl1 seems
	 * not to be very reliable :-(
	 *
	 * Wait for the Tx register to become empty
	 */
	for (i = 0; !(IntStatus2 & INT2_UARTATXINT) && (i < BUSY_WAIT); i++);

	IntClear2 = INT2_UARTATXINT | INT2_UARTATXOVERRUN | INT2_UARTAEMPTY;
	UartA_Data = c;
	for (i = 0; !(IntStatus2 & INT2_UARTATXINT) && (i < BUSY_WAIT); i++);
	IntClear2 = INT2_UARTATXINT | INT2_UARTATXOVERRUN | INT2_UARTAEMPTY;

	IntEnable2 = int2;
}

static void serial_console_write(struct console *co, const char *s,
		unsigned count)
{
    	unsigned int i;

	for (i = 0; i < count; i++) {
		if (*s == '\n')
			serial_outc('\r');
		serial_outc(*s++);
    	}
}

static struct tty_driver *serial_console_device(struct console *c, int *index)
{
	*index = c->index;
	return rs_driver;
}

static __init int serial_console_setup(struct console *co, char *options)
{
	unsigned int scratch = 0;

	/* Setup master clock for UART */
	scratch = inl(TX3912_CLK_CTRL_BASE);
	scratch &= ~TX3912_CLK_CTRL_SIBMCLKDIV_MASK;
	scratch |= ((0x2 << TX3912_CLK_CTRL_SIBMCLKDIV_SHIFT) &
				TX3912_CLK_CTRL_SIBMCLKDIV_MASK)
			| TX3912_CLK_CTRL_SIBMCLKDIR
			| TX3912_CLK_CTRL_ENSIBMCLK
			| TX3912_CLK_CTRL_CSERSEL;
	outl(scratch, TX3912_CLK_CTRL_BASE);

	/* Configure UARTA clock */
	scratch = inl(TX3912_CLK_CTRL_BASE);
	scratch |= ((0x3 << TX3912_CLK_CTRL_CSERDIV_SHIFT) &
				TX3912_CLK_CTRL_CSERDIV_MASK)
			| TX3912_CLK_CTRL_ENCSERCLK
			| TX3912_CLK_CTRL_ENUARTACLK;
	outl(scratch, TX3912_CLK_CTRL_BASE);
		
	/* Setup UARTA for 115200,8N1 */
	outl(0, TX3912_UARTA_BASE + TX3912_UART_CTRL1);
	outl(TX3912_UART_CTRL2_B115200, TX3912_UARTA_BASE + TX3912_UART_CTRL2);
	outl(0, TX3912_UARTA_BASE + TX3912_UART_DMA_CTRL1);
	outl(0, TX3912_UARTA_BASE + TX3912_UART_DMA_CTRL2);

	/* Enable UARTA */
	outl(TX3912_UART_CTRL1_ENUART, TX3912_UARTA_BASE + TX3912_UART_CTRL1);
	while (~inl(TX3912_UARTA_BASE + TX3912_UART_CTRL1) &
		TX3912_UART_CTRL1_UARTON);

	return 0;
}

static struct console sercons = {
	.name     = "ttyS",
	.write    = serial_console_write,
	.device   = serial_console_device,
	.setup    = serial_console_setup,
	.flags    = CON_PRINTBUFFER,
	.index    = -1
};

static int __init tx3912_console_init(void)
{
	register_console(&sercons);
	return 0;
}
console_initcall(tx3912_console_init);

#endif
