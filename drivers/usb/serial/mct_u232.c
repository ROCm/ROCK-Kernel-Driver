/*
 * MCT (Magic Control Technology Corp.) USB RS232 Converter Driver
 *
 *   Copyright (C) 2000 Wolfgang Grandegger (wolfgang@ces.ch)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 * This program is largely derived from the Belkin USB Serial Adapter Driver
 * (see belkin_sa.[ch]). All of the information about the device was acquired
 * by using SniffUSB on Windows98. For technical details see mct_u232.h.
 *
 * William G. Greathouse and Greg Kroah-Hartman provided great help on how to
 * do the reverse engineering and how to write a USB serial device driver.
 *
 * TO BE DONE, TO BE CHECKED:
 *   DTR/RTS signal handling may be incomplete or incorrect. I have mainly
 *   implemented what I have seen with SniffUSB or found in belkin_sa.c.
 *   For further TODOs check also belkin_sa.c.
 *
 * TEST STATUS:
 *   Basic tests have been performed with minicom/zmodem transfers and
 *   modem dialing under Linux 2.4.0-test10 (for me it works fine).
 *
 * 29-Nov-2000 Greg Kroah-Hartman
 *   - Added device id table to fit with 2.4.0-test11 structure.
 *   - took out DEAL_WITH_TWO_INT_IN_ENDPOINTS #define as it's not needed
 *     (lots of things will change if/when the usb-serial core changes to
 *     handle these issues.
 *
 * 27-Nov-2000 Wolfgang Grandegger
 *   A version for kernel 2.4.0-test10 released to the Linux community 
 *   (via linux-usb-devel).
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#ifdef CONFIG_USB_SERIAL_DEBUG
 	#define DEBUG
#else
 	#undef DEBUG
#endif
#include <linux/usb.h>

#include "usb-serial.h"
#include "mct_u232.h"


/*
 * Some not properly written applications do not handle the return code of
 * write() correctly. This can result in character losses. A work-a-round
 * can be compiled in with the following definition. This work-a-round
 * should _NOT_ be part of an 'official' kernel release, of course!
 */
#undef FIX_WRITE_RETURN_CODE_PROBLEM
#ifdef FIX_WRITE_RETURN_CODE_PROBLEM
static int write_blocking = 0; /* disabled by default */
#endif

/*
 * Function prototypes
 */
static int  mct_u232_startup	         (struct usb_serial *serial);
static void mct_u232_shutdown	         (struct usb_serial *serial);
static int  mct_u232_open	         (struct usb_serial_port *port,
					  struct file *filp);
static void mct_u232_close	         (struct usb_serial_port *port,
					  struct file *filp);
#ifdef FIX_WRITE_RETURN_CODE_PROBLEM
static int  mct_u232_write	         (struct usb_serial_port *port,
					  int from_user,
					  const unsigned char *buf,
					  int count);
static void mct_u232_write_bulk_callback (struct urb *urb);
#endif
static void mct_u232_read_int_callback   (struct urb *urb);
static void mct_u232_set_termios         (struct usb_serial_port *port,
					  struct termios * old);
static int  mct_u232_ioctl	         (struct usb_serial_port *port,
					  struct file * file,
					  unsigned int cmd,
					  unsigned long arg);
static void mct_u232_break_ctl	         (struct usb_serial_port *port,
					  int break_state );

/*
 * All of the device info needed for the MCT USB-RS232 converter.
 */
static __devinitdata struct usb_device_id id_table [] = {
	{ idVendor: MCT_U232_VID, idProduct: MCT_U232_PID },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table);


struct usb_serial_device_type mct_u232_device = {
	name:		     "Magic Control Technology USB-RS232",
	id_table:	     id_table,
	needs_interrupt_in:  MUST_HAVE,	 /* 2 interrupt-in endpoints */
	needs_bulk_in:	     MUST_HAVE_NOT,   /* no bulk-in endpoint */
	needs_bulk_out:	     MUST_HAVE,	      /* 1 bulk-out endpoint */
	num_interrupt_in:    2,
	num_bulk_in:	     0,
	num_bulk_out:	     1,
	num_ports:	     1,
	open:		     mct_u232_open,
	close:		     mct_u232_close,
#ifdef FIX_WRITE_RETURN_CODE_PROBLEM
	write:		     mct_u232_write,
	write_bulk_callback: mct_u232_write_bulk_callback,
#endif
	read_int_callback:   mct_u232_read_int_callback,
	ioctl:		     mct_u232_ioctl,
	set_termios:	     mct_u232_set_termios,
	break_ctl:	     mct_u232_break_ctl,
	startup:	     mct_u232_startup,
	shutdown:	     mct_u232_shutdown,
};

struct mct_u232_private {
	unsigned long	     control_state; /* Modem Line Setting (TIOCM) */
	unsigned char        last_lcr;      /* Line Control Register */
	unsigned char	     last_lsr;      /* Line Status Register */
	unsigned char	     last_msr;      /* Modem Status Register */
};

/*
 * Handle vendor specific USB requests
 */

#define WDR_TIMEOUT (HZ * 5 ) /* default urb timeout */

static int mct_u232_set_baud_rate(struct usb_serial *serial, int value)
{
	unsigned int divisor;
        int rc;
	divisor = MCT_U232_BAUD_RATE(value);
        rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
                             MCT_U232_SET_BAUD_RATE_REQUEST,
			     MCT_U232_SET_REQUEST_TYPE,
                             0, 0, &divisor, MCT_U232_SET_BAUD_RATE_SIZE,
			     WDR_TIMEOUT);
	if (rc < 0)
		err("Set BAUD RATE %d failed (error = %d)", value, rc);
	dbg("set_baud_rate: 0x%x", divisor);
        return rc;
} /* mct_u232_set_baud_rate */

static int mct_u232_set_line_ctrl(struct usb_serial *serial, unsigned char lcr)
{
        int rc;
        rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
                             MCT_U232_SET_LINE_CTRL_REQUEST,
			     MCT_U232_SET_REQUEST_TYPE,
                             0, 0, &lcr, MCT_U232_SET_LINE_CTRL_SIZE,
			     WDR_TIMEOUT);
	if (rc < 0)
		err("Set LINE CTRL 0x%x failed (error = %d)", lcr, rc);
	dbg("set_line_ctrl: 0x%x", lcr);
        return rc;
} /* mct_u232_set_line_ctrl */

static int mct_u232_set_modem_ctrl(struct usb_serial *serial,
				   unsigned long control_state)
{
        int rc;
	unsigned char mcr = MCT_U232_MCR_NONE;

	if (control_state & TIOCM_DTR)
		mcr |= MCT_U232_MCR_DTR;
	if (control_state & TIOCM_RTS)
		mcr |= MCT_U232_MCR_RTS;

        rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
                             MCT_U232_SET_MODEM_CTRL_REQUEST,
			     MCT_U232_SET_REQUEST_TYPE,
                             0, 0, &mcr, MCT_U232_SET_MODEM_CTRL_SIZE,
			     WDR_TIMEOUT);
	if (rc < 0)
		err("Set MODEM CTRL 0x%x failed (error = %d)", mcr, rc);
	dbg("set_modem_ctrl: state=0x%lx ==> mcr=0x%x", control_state, mcr);

        return rc;
} /* mct_u232_set_modem_ctrl */

static int mct_u232_get_modem_stat(struct usb_serial *serial, unsigned char *msr)
{
        int rc;
        rc = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
                             MCT_U232_GET_MODEM_STAT_REQUEST,
			     MCT_U232_GET_REQUEST_TYPE,
                             0, 0, msr, MCT_U232_GET_MODEM_STAT_SIZE,
			     WDR_TIMEOUT);
	if (rc < 0) {
		err("Get MODEM STATus failed (error = %d)", rc);
		*msr = 0;
	}
	dbg("get_modem_stat: 0x%x", *msr);
        return rc;
} /* mct_u232_get_modem_stat */

static void mct_u232_msr_to_state(unsigned long *control_state, unsigned char msr)
{
 	/* Translate Control Line states */
	if (msr & MCT_U232_MSR_DSR)
		*control_state |=  TIOCM_DSR;
	else
		*control_state &= ~TIOCM_DSR;
	if (msr & MCT_U232_MSR_CTS)
		*control_state |=  TIOCM_CTS;
	else
		*control_state &= ~TIOCM_CTS;
	if (msr & MCT_U232_MSR_RI)
		*control_state |=  TIOCM_RI;
	else
		*control_state &= ~TIOCM_RI;
	if (msr & MCT_U232_MSR_CD)
		*control_state |=  TIOCM_CD;
	else
		*control_state &= ~TIOCM_CD;
 	dbg("msr_to_state: msr=0x%x ==> state=0x%lx", msr, *control_state);
} /* mct_u232_msr_to_state */

/*
 * Driver's tty interface functions
 */

static int mct_u232_startup (struct usb_serial *serial)
{
	struct mct_u232_private *priv;

	/* allocate the private data structure */
	serial->port->private = kmalloc(sizeof(struct mct_u232_private),
					GFP_KERNEL);
	if (!serial->port->private)
		return (-1); /* error */
	priv = (struct mct_u232_private *)serial->port->private;
	/* set initial values for control structures */
	priv->control_state = 0;
	priv->last_lsr = 0;
	priv->last_msr = 0;

	init_waitqueue_head(&serial->port->write_wait);
	
	return (0);
} /* mct_u232_startup */


static void mct_u232_shutdown (struct usb_serial *serial)
{
	int i;
	
	dbg (__FUNCTION__);

	/* stop reads and writes on all ports */
	for (i=0; i < serial->num_ports; ++i) {
		while (serial->port[i].open_count > 0) {
			mct_u232_close (&serial->port[i], NULL);
		}
		/* My special items, the standard routines free my urbs */
		if (serial->port->private)
			kfree(serial->port->private);
	}
} /* mct_u232_shutdown */

static int  mct_u232_open (struct usb_serial_port *port, struct file *filp)
{
	unsigned long flags;
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = (struct mct_u232_private *)port->private;

	dbg(__FUNCTION__" port %d", port->number);

	spin_lock_irqsave (&port->port_lock, flags);
	
	++port->open_count;
	MOD_INC_USE_COUNT;

	if (!port->active) {
		port->active = 1;

		/* Do a defined restart: the normal serial device seems to 
		 * always turn on DTR and RTS here, so do the same. I'm not
		 * sure if this is really necessary. But it should not harm
		 * either.
		 */
		if (port->tty->termios->c_cflag & CBAUD)
			priv->control_state = TIOCM_DTR | TIOCM_RTS;
		else
			priv->control_state = 0;
		mct_u232_set_modem_ctrl(serial, priv->control_state);
		
		priv->last_lcr = (MCT_U232_DATA_BITS_8 | 
				  MCT_U232_PARITY_NONE |
				  MCT_U232_STOP_BITS_1);
		mct_u232_set_line_ctrl(serial, priv->last_lcr);

		/* Read modem status and update control state */
		mct_u232_get_modem_stat(serial, &priv->last_msr);
		mct_u232_msr_to_state(&priv->control_state, priv->last_msr);

		{
			/* Puh, that's dirty */
			struct usb_serial_port *rport;	
			rport = &serial->port[1];
			rport->tty = port->tty;
			rport->private = port->private;
			port->read_urb = rport->interrupt_in_urb;
		}

		port->read_urb->dev = port->serial->dev;
		if (usb_submit_urb(port->read_urb))
			err("usb_submit_urb(read bulk) failed");

		port->interrupt_in_urb->dev = port->serial->dev;
		if (usb_submit_urb(port->interrupt_in_urb))
			err(" usb_submit_urb(read int) failed");

	}

	spin_unlock_irqrestore (&port->port_lock, flags);
	
	return 0;
} /* mct_u232_open */


static void mct_u232_close (struct usb_serial_port *port, struct file *filp)
{
	unsigned long flags;

	dbg(__FUNCTION__" port %d", port->number);

	spin_lock_irqsave (&port->port_lock, flags);

	--port->open_count;
	MOD_DEC_USE_COUNT;

	if (port->open_count <= 0) {
		/* shutdown our bulk reads and writes */
		usb_unlink_urb (port->write_urb);
		usb_unlink_urb (port->read_urb);
		/* wgg - do I need this? I think so. */
		usb_unlink_urb (port->interrupt_in_urb);
		port->active = 0;
	}
	
	spin_unlock_irqrestore (&port->port_lock, flags);

} /* mct_u232_close */


#ifdef FIX_WRITE_RETURN_CODE_PROBLEM
/* The generic routines work fine otherwise */

static int mct_u232_write (struct usb_serial_port *port, int from_user,
			   const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	unsigned long flags;
	int result, bytes_sent, size;

	dbg(__FUNCTION__ " - port %d", port->number);

	if (count == 0) {
		dbg(__FUNCTION__ " - write request of 0 bytes");
		return (0);
	}

	/* only do something if we have a bulk out endpoint */
	if (!serial->num_bulk_out)
		return(0);;
	
	/* another write is still pending? */
	if (port->write_urb->status == -EINPROGRESS) {
		dbg (__FUNCTION__ " - already writing");
		return (0);
	}
		
	bytes_sent = 0;
	while (count > 0) {
		
		spin_lock_irqsave (&port->port_lock, flags);
		
		size = (count > port->bulk_out_size) ? port->bulk_out_size : count;
		
		usb_serial_debug_data (__FILE__, __FUNCTION__, size, buf);
		
		if (from_user) {
			copy_from_user(port->write_urb->transfer_buffer, buf, size);
		}
		else {
			memcpy (port->write_urb->transfer_buffer, buf, size);
		}
		
		/* set up our urb */
		FILL_BULK_URB(port->write_urb, serial->dev,
			      usb_sndbulkpipe(serial->dev,
					      port->bulk_out_endpointAddress),
			      port->write_urb->transfer_buffer, size,
			      ((serial->type->write_bulk_callback) ?
			       serial->type->write_bulk_callback :
			       mct_u232_write_bulk_callback),
			      port);
		
		/* send the data out the bulk port */
		result = usb_submit_urb(port->write_urb);
		if (result) {
			err(__FUNCTION__
			    " - failed submitting write urb, error %d", result);
			spin_unlock_irqrestore (&port->port_lock, flags);
			return bytes_sent;
		}

		spin_unlock_irqrestore (&port->port_lock, flags);

		bytes_sent += size;
		if (write_blocking)
			interruptible_sleep_on(&port->write_wait);
		else
			break;

		buf += size;
		count -= size;
	}
	
	return bytes_sent;
} /* mct_u232_write */

static void mct_u232_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = port->serial;
       	struct tty_struct *tty = port->tty;

	dbg(__FUNCTION__ " - port %d", port->number);
	
	if (!serial) {
		dbg(__FUNCTION__ " - bad serial pointer, exiting");
		return;
	}

	if (urb->status) {
		dbg(__FUNCTION__ " - nonzero write bulk status received: %d",
		    urb->status);
		return;
	}

	if (write_blocking) {
		wake_up_interruptible(&port->write_wait);
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
		
	} else {
		/* from generic_write_bulk_callback */
		queue_task(&port->tqueue, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}

	return;
} /* mct_u232_write_bulk_callback */
#endif

static void mct_u232_read_int_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct mct_u232_private *priv = (struct mct_u232_private *)port->private;
	struct usb_serial *serial = port->serial;
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;

        dbg(__FUNCTION__ " - port %d", port->number);

	/* The urb might have been killed. */
        if (urb->status) {
                dbg(__FUNCTION__ " - nonzero read bulk status received: %d",
		    urb->status);
                return;
        }
	if (!serial) {
		dbg(__FUNCTION__ " - bad serial pointer, exiting");
		return;
	}
	
	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, data);

	/*
	 * Work-a-round: handle the 'usual' bulk-in pipe here
	 */
	if (urb->transfer_buffer_length > 2) {
		int i;
		tty = port->tty;
		if (urb->actual_length) {
			for (i = 0; i < urb->actual_length ; ++i) {
				tty_insert_flip_char(tty, data[i], 0);
			}
			tty_flip_buffer_push(tty);
		}
		/* INT urbs are automatically re-submitted */
		return;
	}
	
	/*
	 * The interrupt-in pipe signals exceptional conditions (modem line
	 * signal changes and errors). data[0] holds MSR, data[1] holds LSR.
	 */
	priv->last_msr = data[MCT_U232_MSR_INDEX];
	
	/* Record Control Line states */
	mct_u232_msr_to_state(&priv->control_state, priv->last_msr);

#if 0
	/* Not yet handled. See belin_sa.c for further information */
	/* Now to report any errors */
	priv->last_lsr = data[MCT_U232_LSR_INDEX];
	/*
	 * fill in the flip buffer here, but I do not know the relation
	 * to the current/next receive buffer or characters.  I need
	 * to look in to this before committing any code.
	 */
	if (priv->last_lsr & MCT_U232_LSR_ERR) {
		tty = port->tty;
		/* Overrun Error */
		if (priv->last_lsr & MCT_U232_LSR_OE) {
		}
		/* Parity Error */
		if (priv->last_lsr & MCT_U232_LSR_PE) {
		}
		/* Framing Error */
		if (priv->last_lsr & MCT_U232_LSR_FE) {
		}
		/* Break Indicator */
		if (priv->last_lsr & MCT_U232_LSR_BI) {
		}
	}
#endif

	/* INT urbs are automatically re-submitted */
} /* mct_u232_read_int_callback */


static void mct_u232_set_termios (struct usb_serial_port *port,
				  struct termios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = (struct mct_u232_private *)port->private;
	unsigned int iflag = port->tty->termios->c_iflag;
	unsigned int old_iflag = old_termios->c_iflag;
	unsigned int cflag = port->tty->termios->c_cflag;
	unsigned int old_cflag = old_termios->c_cflag;
	
	/*
	 * Update baud rate
	 */
	if( (cflag & CBAUD) != (old_cflag & CBAUD) ) {
	        /* reassert DTR and (maybe) RTS on transition from B0 */
		if( (old_cflag & CBAUD) == B0 ) {
			dbg(__FUNCTION__ ": baud was B0");
			priv->control_state |= TIOCM_DTR;
			/* don't set RTS if using hardware flow control */
			if (!(old_cflag & CRTSCTS)) {
				priv->control_state |= TIOCM_RTS;
			}
			mct_u232_set_modem_ctrl(serial, priv->control_state);
		}
		
		switch(cflag & CBAUD) {
		case B0: /* handled below */
			break;
		case B300: mct_u232_set_baud_rate(serial, 300);
			break;
		case B600: mct_u232_set_baud_rate(serial, 600);
			break;
		case B1200: mct_u232_set_baud_rate(serial, 1200);
			break;
		case B2400: mct_u232_set_baud_rate(serial, 2400);
			break;
		case B4800: mct_u232_set_baud_rate(serial, 4800);
			break;
		case B9600: mct_u232_set_baud_rate(serial, 9600);
			break;
		case B19200: mct_u232_set_baud_rate(serial, 19200);
			break;
		case B38400: mct_u232_set_baud_rate(serial, 38400);
			break;
		case B57600: mct_u232_set_baud_rate(serial, 57600);
			break;
		case B115200: mct_u232_set_baud_rate(serial, 115200);
			break;
		default: err("MCT USB-RS232 converter: unsupported baudrate request, using default of 9600");
			mct_u232_set_baud_rate(serial, 9600); break;
		}
		if ((cflag & CBAUD) == B0 ) {
			dbg(__FUNCTION__ ": baud is B0");
			/* Drop RTS and DTR */
			priv->control_state &= ~(TIOCM_DTR | TIOCM_RTS);
        		mct_u232_set_modem_ctrl(serial, priv->control_state);
		}
	}

	/*
	 * Update line control register (LCR)
	 */
	if ((cflag & (PARENB|PARODD)) != (old_cflag & (PARENB|PARODD))
	    || (cflag & CSIZE) != (old_cflag & CSIZE)
	    || (cflag & CSTOPB) != (old_cflag & CSTOPB) ) {
		

		priv->last_lcr = 0;

		/* set the parity */
		if (cflag & PARENB)
			priv->last_lcr |= (cflag & PARODD) ?
				MCT_U232_PARITY_ODD : MCT_U232_PARITY_EVEN;
		else
			priv->last_lcr |= MCT_U232_PARITY_NONE;

		/* set the number of data bits */
		switch (cflag & CSIZE) {
		case CS5:
			priv->last_lcr |= MCT_U232_DATA_BITS_5; break;
		case CS6:
			priv->last_lcr |= MCT_U232_DATA_BITS_6; break;
		case CS7:
			priv->last_lcr |= MCT_U232_DATA_BITS_7; break;
		case CS8:
			priv->last_lcr |= MCT_U232_DATA_BITS_8; break;
		default:
			err("CSIZE was not CS5-CS8, using default of 8");
			priv->last_lcr |= MCT_U232_DATA_BITS_8;
			break;
		}

		/* set the number of stop bits */
		priv->last_lcr |= (cflag & CSTOPB) ?
			MCT_U232_STOP_BITS_2 : MCT_U232_STOP_BITS_1;

		mct_u232_set_line_ctrl(serial, priv->last_lcr);
	}
	
	/*
	 * Set flow control: well, I do not really now how to handle DTR/RTS.
	 * Just do what we have seen with SniffUSB on Win98.
	 */
	if( (iflag & IXOFF) != (old_iflag & IXOFF)
	    || (iflag & IXON) != (old_iflag & IXON)
	    ||  (cflag & CRTSCTS) != (old_cflag & CRTSCTS) ) {
		
		/* Drop DTR/RTS if no flow control otherwise assert */
		if ((iflag & IXOFF) || (iflag & IXON) || (cflag & CRTSCTS) )
			priv->control_state |= TIOCM_DTR | TIOCM_RTS;
		else
			priv->control_state &= ~(TIOCM_DTR | TIOCM_RTS);
		mct_u232_set_modem_ctrl(serial, priv->control_state);
	}
} /* mct_u232_set_termios */


static void mct_u232_break_ctl( struct usb_serial_port *port, int break_state )
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = (struct mct_u232_private *)port->private;
	unsigned char lcr = priv->last_lcr;

	dbg (__FUNCTION__ "state=%d", break_state);

	if (break_state)
		lcr |= MCT_U232_SET_BREAK;

	mct_u232_set_line_ctrl(serial, lcr);
} /* mct_u232_break_ctl */


static int mct_u232_ioctl (struct usb_serial_port *port, struct file * file,
			   unsigned int cmd, unsigned long arg)
{
	struct usb_serial *serial = port->serial;
	struct mct_u232_private *priv = (struct mct_u232_private *)port->private;
	int ret, mask;
	
	dbg (__FUNCTION__ "cmd=0x%x", cmd);

	/* Based on code from acm.c and others */
	switch (cmd) {
	case TIOCMGET:
		return put_user(priv->control_state, (unsigned long *) arg);
		break;

	case TIOCMSET: /* Turns on and off the lines as specified by the mask */
	case TIOCMBIS: /* turns on (Sets) the lines as specified by the mask */
	case TIOCMBIC: /* turns off (Clears) the lines as specified by the mask */
		if ((ret = get_user(mask, (unsigned long *) arg))) return ret;

		if ((cmd == TIOCMSET) || (mask & TIOCM_RTS)) {
			/* RTS needs set */
			if( ((cmd == TIOCMSET) && (mask & TIOCM_RTS)) ||
			    (cmd == TIOCMBIS) )
				priv->control_state |=  TIOCM_RTS;
			else
				priv->control_state &= ~TIOCM_RTS;
		}

		if ((cmd == TIOCMSET) || (mask & TIOCM_DTR)) {
			/* DTR needs set */
			if( ((cmd == TIOCMSET) && (mask & TIOCM_DTR)) ||
			    (cmd == TIOCMBIS) )
				priv->control_state |=  TIOCM_DTR;
			else
				priv->control_state &= ~TIOCM_DTR;
		}
		mct_u232_set_modem_ctrl(serial, priv->control_state);
		break;
					
	case TIOCMIWAIT:
		/* wait for any of the 4 modem inputs (DCD,RI,DSR,CTS)*/
		/* TODO */
		return( 0 );

	case TIOCGICOUNT:
		/* return count of modemline transitions */
		/* TODO */
		return 0;

	default:
		dbg(__FUNCTION__ ": arg not supported - 0x%04x",cmd);
		return(-ENOIOCTLCMD);
		break;
	}
	return 0;
} /* mct_u232_ioctl */


static int __init mct_u232_init (void)
{
	usb_serial_register (&mct_u232_device);
	
	return 0;
}


static void __exit mct_u232_exit (void)
{
	usb_serial_deregister (&mct_u232_device);
}


module_init (mct_u232_init);
module_exit(mct_u232_exit);

MODULE_AUTHOR("Wolfgang Grandegger <wolfgang@ces.ch>");
MODULE_DESCRIPTION("Magic Control Technology USB-RS232 converter driver");

#ifdef FIX_WRITE_RETURN_CODE_PROBLEM
MODULE_PARM(write_blocking, "i");
MODULE_PARM_DESC(write_blocking, 
		 "The write function will block to write out all data");
#endif
