/*
 * LIRC driver for ITE IT8712/IT8705 CIR port
 *
 * Copyright (C) 2001 Hans-Günter Lütke Uphues <hg_lu@web.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * ITE IT8705 and IT8712(not tested) CIR-port support for lirc based
 * via cut and paste from lirc_sir.c (C) 2000 Milan Pikula
 *
 * Attention: Sendmode only tested with debugging logs
 *
 * 2001/02/27 Christoph Bartelmus <lirc@bartelmus.de> :
 *   reimplemented read function
 */


#include <linux/version.h>
#include <linux/module.h>

#include <linux/config.h>


#include <linux/sched.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/serial_reg.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/fcntl.h>

#include <linux/timer.h>

#include <linux/lirc.h>
#include "lirc_dev.h"

#include "lirc_it87.h"

static unsigned long it87_bits_in_byte_out = 0;
static unsigned long it87_send_counter = 0;
static unsigned char it87_RXEN_mask = IT87_CIR_RCR_RXEN;

#define RBUF_LEN 1024
#define WBUF_LEN 1024

#define LIRC_DRIVER_NAME "lirc_it87"

/* timeout for sequences in jiffies (=5/100s) */
/* must be longer than TIME_CONST */
#define IT87_TIMEOUT	(HZ*5/100)

static int io = IT87_CIR_DEFAULT_IOBASE;
static int irq = IT87_CIR_DEFAULT_IRQ;
static unsigned char it87_freq = 38; /* kHz */
/* receiver demodulator default: off */
static unsigned char it87_enable_demodulator = 0;

static spinlock_t timer_lock = SPIN_LOCK_UNLOCKED;
static struct timer_list timerlist;
/* time of last signal change detected */
static struct timeval last_tv = {0, 0};
/* time of last UART data ready interrupt */
static struct timeval last_intr_tv = {0, 0};
static int last_value = 0;

static DECLARE_WAIT_QUEUE_HEAD(lirc_read_queue);

static spinlock_t hardware_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t dev_lock = SPIN_LOCK_UNLOCKED;

static lirc_t rx_buf[RBUF_LEN]; unsigned int rx_tail = 0, rx_head = 0;
static lirc_t tx_buf[WBUF_LEN];

/* SECTION: Prototypes */

/* Communication with user-space */
static int lirc_open(struct inode * inode,
		     struct file * file);
static int lirc_close(struct inode * inode,
		      struct file *file);
static unsigned int lirc_poll(struct file * file,
			      poll_table * wait);
static ssize_t lirc_read(struct file * file,
			 char * buf,
			 size_t count,
			 loff_t * ppos);
static ssize_t lirc_write(struct file * file,
			  const char * buf,
			  size_t n,
			  loff_t * pos);
static int lirc_ioctl(struct inode *node,
		      struct file *filep,
		      unsigned int cmd,
		      unsigned long arg);
static void add_read_queue(int flag,
			   unsigned long val);
static int init_chrdev(void);
static void drop_chrdev(void);
	/* Hardware */
static irqreturn_t it87_interrupt(int irq,
			   void * dev_id,
			   struct pt_regs * regs);
static void send_space(unsigned long len);
static void send_pulse(unsigned long len);
static void init_send(void);
static void terminate_send(unsigned long len);
static int init_hardware(void);
static void drop_hardware(void);
	/* Initialisation */
static int init_port(void);
static void drop_port(void);
int init_module(void);
void cleanup_module(void);


/* SECTION: Communication with user-space */

static int lirc_open(struct inode * inode,
		     struct file * file)
{
	spin_lock(&dev_lock);
#ifdef CONFIG_MODULE_UNLOAD
	if (module_refcount(THIS_MODULE))
	{
		spin_unlock(&dev_lock);
		return -EBUSY;
	}
#endif
	try_module_get(THIS_MODULE);
	spin_unlock(&dev_lock);
	return 0;
}


static int lirc_close(struct inode * inode,
		      struct file *file)
{
	module_put(THIS_MODULE);
	return 0;
}


static unsigned int lirc_poll(struct file * file,
			      poll_table * wait)
{
	poll_wait(file, &lirc_read_queue, wait);
	if (rx_head != rx_tail)
		return POLLIN | POLLRDNORM;
	return 0;
}


static ssize_t lirc_read(struct file * file,
			 char * buf,
			 size_t count,
			 loff_t * ppos)
{
	int n=0;
	int retval=0;

	while(n<count)
	{
		if(file->f_flags & O_NONBLOCK &&
		   rx_head==rx_tail)
		{
			retval = -EAGAIN;
			break;
		}
		retval=wait_event_interruptible(lirc_read_queue,
						rx_head!=rx_tail);
		if(retval)
		{
			break;
		}

		retval=verify_area(VERIFY_WRITE,(void *) buf+n,
				   sizeof(lirc_t));
		if (retval)
		{
			return retval;
		}
		copy_to_user((void *) buf+n,(void *) (rx_buf+rx_head),
			     sizeof(lirc_t));
		rx_head=(rx_head+1)&(RBUF_LEN-1);
		n+=sizeof(lirc_t);
	}
	if(n)
	{
		return n;
	}
	return retval;
}


static ssize_t lirc_write(struct file * file,
			  const char * buf,
			  size_t n,
			  loff_t * pos)
{
	int i;
	int retval;

        if(n%sizeof(lirc_t) || (n/sizeof(lirc_t)) > WBUF_LEN)
		return(-EINVAL);
	retval = verify_area(VERIFY_READ, buf, n);
	if (retval)
		return retval;
	copy_from_user(tx_buf, buf, n);
	i = 0;
	n/=sizeof(lirc_t);
	init_send();
	while (1) {
		if (i >= n)
			break;
		if (tx_buf[i])
			send_pulse(tx_buf[i]);
		i++;
		if (i >= n)
			break;
		if (tx_buf[i])
			send_space(tx_buf[i]);
		i++;
	}
	terminate_send(tx_buf[i-1]);
	return n;
}


static int lirc_ioctl(struct inode *node,
		      struct file *filep,
		      unsigned int cmd,
		      unsigned long arg)
{
	int retval = 0;
	unsigned long value = 0;
	unsigned int ivalue;

	if (cmd == LIRC_GET_FEATURES)
		value = LIRC_CAN_SEND_PULSE |
			LIRC_CAN_SET_SEND_CARRIER |
			LIRC_CAN_REC_MODE2;
	else if (cmd == LIRC_GET_SEND_MODE)
		value = LIRC_MODE_PULSE;
	else if (cmd == LIRC_GET_REC_MODE)
		value = LIRC_MODE_MODE2;

	switch (cmd) {
	case LIRC_GET_FEATURES:
	case LIRC_GET_SEND_MODE:
	case LIRC_GET_REC_MODE:
		retval = put_user(value, (unsigned long *) arg);
		break;

	case LIRC_SET_SEND_MODE:
	case LIRC_SET_REC_MODE:
		retval = get_user(value, (unsigned long *) arg);
		break;

	case LIRC_SET_SEND_CARRIER:
		retval=get_user(ivalue,(unsigned int *) arg);
		if(retval) return(retval);
		ivalue /= 1000;
		if (ivalue > IT87_CIR_FREQ_MAX ||
		    ivalue < IT87_CIR_FREQ_MIN) return(-EINVAL);

		it87_freq = ivalue;
		{
			unsigned long hw_flags;

			spin_lock_irqsave(&hardware_lock, hw_flags);
			outb(((inb(io + IT87_CIR_TCR2) & IT87_CIR_TCR2_TXMPW) |
			      (it87_freq - IT87_CIR_FREQ_MIN) << 3),
			     io + IT87_CIR_TCR2);
			spin_unlock_irqrestore(&hardware_lock, hw_flags);
#ifdef DEBUG
			printk(KERN_DEBUG LIRC_DRIVER_NAME
			       " demodulation frequency: %d kHz\n", it87_freq);
#endif
		}

		break;

	default:
		retval = -ENOIOCTLCMD;
	}

	if (retval)
		return retval;

	if (cmd == LIRC_SET_REC_MODE) {
		if (value != LIRC_MODE_MODE2)
			retval = -ENOSYS;
	} else if (cmd == LIRC_SET_SEND_MODE) {
		if (value != LIRC_MODE_PULSE)
			retval = -ENOSYS;
	}
	return retval;
}

static void add_read_queue(int flag,
			   unsigned long val)
{
	unsigned int new_rx_tail;
	lirc_t newval;

#ifdef DEBUG_SIGNAL
	printk(KERN_DEBUG LIRC_DRIVER_NAME
	       ": add flag %d with val %lu\n",
	       flag,val);
#endif

	newval = val & PULSE_MASK;

	/* statistically pulses are ~TIME_CONST/2 too long: we could
	   maybe make this more exactly but this is good enough */
	if(flag) /* pulse */ {
		if(newval>TIME_CONST/2) {
			newval-=TIME_CONST/2;
		}
		else /* should not ever happen */ {
			newval=1;
		}
		newval|=PULSE_BIT;
	}
	else {
		newval+=TIME_CONST/2;
	}
	new_rx_tail = (rx_tail + 1) & (RBUF_LEN - 1);
	if (new_rx_tail == rx_head) {
#ifdef DEBUG
		printk(KERN_WARNING LIRC_DRIVER_NAME ": Buffer overrun.\n");
#endif
		return;
	}
	rx_buf[rx_tail] = newval;
	rx_tail = new_rx_tail;
	wake_up_interruptible(&lirc_read_queue);
}


static struct file_operations lirc_fops = {
	read:    lirc_read,
	write:   lirc_write,
	poll:    lirc_poll,
	ioctl:   lirc_ioctl,
	open:    lirc_open,
	release: lirc_close,
};

static int set_use_inc(void* data)
{
#if WE_DONT_USE_LOCAL_OPEN_CLOSE
       try_module_get(THIS_MODULE);
#endif
       return 0;
}

static void set_use_dec(void* data)
{
#if WE_DONT_USE_LOCAL_OPEN_CLOSE
       module_put(THIS_MODULE);
#endif
}
static struct lirc_plugin plugin = {
       name:           LIRC_DRIVER_NAME,
       minor:          -1,
       code_length:    1,
       sample_rate:    0,
       data:           NULL,
       add_to_buf:     NULL,
       get_queue:      NULL,
       set_use_inc:    set_use_inc,
       set_use_dec:    set_use_dec,
       fops:           &lirc_fops,
};


int init_chrdev(void)
{
	plugin.minor = lirc_register_plugin(&plugin);

	if (plugin.minor < 0) {
		printk(KERN_ERR LIRC_DRIVER_NAME ": init_chrdev() failed.\n");
		return -EIO;
	}
	return 0;
}


static void drop_chrdev(void)
{
	lirc_unregister_plugin(plugin.minor);
}

/* SECTION: Hardware */
static long delta(struct timeval * tv1,
		  struct timeval * tv2)
{
	unsigned long deltv;

	deltv = tv2->tv_sec - tv1->tv_sec;
	if (deltv > 15)
		deltv = 0xFFFFFF;
	else
		deltv = deltv*1000000 +
			tv2->tv_usec -
			tv1->tv_usec;
	return deltv;
}


static void it87_timeout(unsigned long data)
{
	/* if last received signal was a pulse, but receiving stopped
	   within the 9 bit frame, we need to finish this pulse and
	   simulate a signal change to from pulse to space. Otherwise
	   upper layers will receive two sequences next time. */

	unsigned long flags;
	unsigned long pulse_end;

	/* avoid interference with interrupt */
 	spin_lock_irqsave(&timer_lock, flags);
	if (last_value) {
		/* determine 'virtual' pulse end: */
	 	pulse_end = delta(&last_tv, &last_intr_tv);
#ifdef DEBUG_SIGNAL
		printk(KERN_DEBUG LIRC_DRIVER_NAME
		       ": timeout add %d for %lu usec\n",
		       last_value,
		       pulse_end);
#endif
		add_read_queue(last_value,
			       pulse_end);
		last_value = 0;
		last_tv=last_intr_tv;
	}
	spin_unlock_irqrestore(&timer_lock, flags);
}


static irqreturn_t it87_interrupt(int irq,
			   void * dev_id,
			   struct pt_regs * regs)
{
	unsigned char data;
	struct timeval curr_tv;
	static unsigned long deltv;
	unsigned long deltintrtv;
	unsigned long flags, hw_flags;
	int iir, lsr;
	int fifo = 0;

	iir = inb(io + IT87_CIR_IIR);

	switch (iir & IT87_CIR_IIR_IID) {
	case 0x4:
	case 0x6:
		lsr = inb(io + IT87_CIR_RSR) & (IT87_CIR_RSR_RXFTO |
						    IT87_CIR_RSR_RXFBC);
		fifo = lsr & IT87_CIR_RSR_RXFBC;
#ifdef DEBUG_SIGNAL
		printk(KERN_DEBUG LIRC_DRIVER_NAME
		       "iir: 0x%x fifo: 0x%x\n", iir, lsr);
#endif

		/* avoid interference with timer */
		spin_lock_irqsave(&timer_lock, flags);
		spin_lock_irqsave(&hardware_lock, hw_flags);
		do {
			del_timer(&timerlist);
			data = inb(io + IT87_CIR_DR);
#ifdef DEBUG_SIGNAL
			printk(KERN_DEBUG LIRC_DRIVER_NAME
			       ": data=%.2x\n",
			       data);
#endif
			do_gettimeofday(&curr_tv);
			deltv = delta(&last_tv, &curr_tv);
			deltintrtv = delta(&last_intr_tv, &curr_tv);
#ifdef DEBUG_SIGNAL
			printk(KERN_DEBUG LIRC_DRIVER_NAME
			       ": t %lu , d %d\n",
			       deltintrtv,
			       (int)data);
#endif
			/* if nothing came in last 2 cycles,
			   it was gap */
			if (deltintrtv > TIME_CONST * 2) {
				if (last_value) {
#ifdef DEBUG_SIGNAL
					printk(KERN_DEBUG LIRC_DRIVER_NAME ": GAP\n");
#endif
					/* simulate signal change */
					add_read_queue(last_value,
						       deltv-
						       deltintrtv);
					last_value = 0;
					last_tv.tv_sec = last_intr_tv.tv_sec;
					last_tv.tv_usec = last_intr_tv.tv_usec;
					deltv = deltintrtv;
				}
			}
			data = 1;
			if (data ^ last_value) {
				/* deltintrtv > 2*TIME_CONST,
				   remember ? */
				/* the other case is timeout */
				add_read_queue(last_value,
					       deltv-TIME_CONST);
				last_value = data;
				last_tv = curr_tv;
				if(last_tv.tv_usec>=TIME_CONST) {
					last_tv.tv_usec-=TIME_CONST;
				}
				else {
					last_tv.tv_sec--;
					last_tv.tv_usec+=1000000-
						TIME_CONST;
				}
			}
			last_intr_tv = curr_tv;
			if (data) {
				/* start timer for end of sequence detection */
				timerlist.expires = jiffies + IT87_TIMEOUT;
				add_timer(&timerlist);
			}
			outb((inb(io + IT87_CIR_RCR) & ~IT87_CIR_RCR_RXEN) |
			     IT87_CIR_RCR_RXACT,
			     io + IT87_CIR_RCR);
			if (it87_RXEN_mask) {
				outb(inb(io + IT87_CIR_RCR) | IT87_CIR_RCR_RXEN,
				     io + IT87_CIR_RCR);
			}
			fifo--;
		}
		while (fifo != 0);
		spin_unlock_irqrestore(&hardware_lock, hw_flags);
		spin_unlock_irqrestore(&timer_lock, flags);
		break;

	default:
		/* not our irq */
#ifdef DEBUG_SIGNAL
		printk(KERN_DEBUG LIRC_DRIVER_NAME
		       "unknown IRQ (shouldn't happen) !!\n");
#endif
		break;
	}
	return IRQ_HANDLED; //FIXME true status should be returned (include/linux/interrupt.h)
}


static void send_it87(unsigned long len,
		      unsigned long stime,
		      unsigned char send_byte,
		      unsigned int count_bits)
{
        long count = len / stime;
	long time_left = 0;
	static unsigned char byte_out = 0;

#ifdef DEBUG_SIGNAL
	printk(KERN_DEBUG LIRC_DRIVER_NAME
	       "send_it87: len=%ld, sb=%d\n",
	       len,
	       send_byte);
#endif
	time_left = (long)len - (long)count * (long)stime;
	count += ((2 * time_left) / stime);
	while (count) {
		long i=0;
		for (i=0; i<count_bits; i++) {
			byte_out = (byte_out << 1) | (send_byte & 1);
			it87_bits_in_byte_out++;
		}
		if (it87_bits_in_byte_out == 8) {
#ifdef DEBUG_SIGNAL
			printk(KERN_DEBUG LIRC_DRIVER_NAME
			       "out=0x%x, tsr_txfbc: 0x%x\n",
			       byte_out,
			       inb(io + IT87_CIR_TSR) &
			       IT87_CIR_TSR_TXFBC);
#endif
			while ((inb(io + IT87_CIR_TSR) &
				IT87_CIR_TSR_TXFBC) >= IT87_CIR_FIFO_SIZE);
			{
				unsigned long hw_flags;

				spin_lock_irqsave(&hardware_lock, hw_flags);
				outb(byte_out, io + IT87_CIR_DR);
				spin_unlock_irqrestore(&hardware_lock, hw_flags);
			}
			it87_bits_in_byte_out = 0;
			it87_send_counter++;
			byte_out = 0;
		}
		count--;
	}
}


/*
maybe: exchange space and pulse because
it8705 only modulates 0-bits
*/


static void send_space(unsigned long len)
{
	send_it87(len,
		  TIME_CONST,
		  IT87_CIR_SPACE,
		  IT87_CIR_BAUDRATE_DIVISOR);
}

static void send_pulse(unsigned long len)
{
	send_it87(len,
		  TIME_CONST,
		  IT87_CIR_PULSE,
		  IT87_CIR_BAUDRATE_DIVISOR);
}


static void init_send()
{
	unsigned long flags;

	spin_lock_irqsave(&hardware_lock, flags);
	/* RXEN=0: receiver disable */
	it87_RXEN_mask = 0;
	outb(inb(io + IT87_CIR_RCR) & ~IT87_CIR_RCR_RXEN,
	     io + IT87_CIR_RCR);
	spin_unlock_irqrestore(&hardware_lock, flags);
	it87_bits_in_byte_out = 0;
	it87_send_counter = 0;
}


static void terminate_send(unsigned long len)
{
	unsigned long flags;
	unsigned long last = 0;

	last = it87_send_counter;
	/* make sure all necessary data has been sent */
	while (last == it87_send_counter)
		send_space(len);
	/* wait until all data sent */
	while ((inb(io + IT87_CIR_TSR) & IT87_CIR_TSR_TXFBC) != 0);
	/* then reenable receiver */
	spin_lock_irqsave(&hardware_lock, flags);
	it87_RXEN_mask = IT87_CIR_RCR_RXEN;
	outb(inb(io + IT87_CIR_RCR) | IT87_CIR_RCR_RXEN,
	     io + IT87_CIR_RCR);
	spin_unlock_irqrestore(&hardware_lock, flags);
}


static int init_hardware(void)
{
	unsigned long flags;
	unsigned char it87_rcr = 0;

	spin_lock_irqsave(&hardware_lock, flags);
	/* init cir-port */
	/* enable r/w-access to Baudrate-Register */
	outb(IT87_CIR_IER_BR, io + IT87_CIR_IER);
	outb(IT87_CIR_BAUDRATE_DIVISOR % 0x100, io+IT87_CIR_BDLR);
	outb(IT87_CIR_BAUDRATE_DIVISOR / 0x100, io+IT87_CIR_BDHR);
	/* Baudrate Register off, define IRQs: Input only */
	outb(IT87_CIR_IER_IEC | IT87_CIR_IER_RDAIE, io + IT87_CIR_IER);
	/* RX: HCFS=0, RXDCR = 001b (35,6..40,3 kHz), RXEN=1 */
	it87_rcr = (IT87_CIR_RCR_RXEN & it87_RXEN_mask) | 0x1;
	if (it87_enable_demodulator)
		it87_rcr |= IT87_CIR_RCR_RXEND;
	outb(it87_rcr, io + IT87_CIR_RCR);
	/* TX: 38kHz, 13,3us (pulse-width */
	outb(((it87_freq - IT87_CIR_FREQ_MIN) << 3) | 0x06,
	     io + IT87_CIR_TCR2);
	spin_unlock_irqrestore(&hardware_lock, flags);
	return 0;
}


static void drop_hardware(void)
{
	unsigned long flags;

	spin_lock_irqsave(&hardware_lock, flags);
	disable_irq(irq);
	/* receiver disable */
	it87_RXEN_mask = 0;
	outb(0x1, io + IT87_CIR_RCR);
	/* turn off irqs */
	outb(0, io + IT87_CIR_IER);
	/* fifo clear */
        outb(IT87_CIR_TCR1_FIFOCLR, io+IT87_CIR_TCR1);
        /* reset */
        outb(IT87_CIR_IER_RESET, io+IT87_CIR_IER);
	enable_irq(irq);
	spin_unlock_irqrestore(&hardware_lock, flags);
}


static unsigned char it87_read(unsigned char port)
{
	outb(port, IT87_ADRPORT);
	return inb(IT87_DATAPORT);
}


static void it87_write(unsigned char port,
		       unsigned char data)
{
	outb(port, IT87_ADRPORT);
	outb(data, IT87_DATAPORT);
}


/* SECTION: Initialisation */

static int init_port(void)
{
	int retval = 0;

	unsigned char init_bytes[4] = {IT87_INIT};
	unsigned char it87_chipid = 0;
	unsigned char ldn = 0;
	unsigned int  it87_io = 0;
	unsigned int  it87_irq = 0;

	/* Enter MB PnP Mode */
	outb(init_bytes[0], IT87_ADRPORT);
	outb(init_bytes[1], IT87_ADRPORT);
	outb(init_bytes[2], IT87_ADRPORT);
	outb(init_bytes[3], IT87_ADRPORT);

	/* 8712 or 8705 ? */
	it87_chipid = it87_read(IT87_CHIP_ID1);
	if (it87_chipid != 0x87) {
		retval = -ENXIO;
		return retval;
	}
	it87_chipid = it87_read(IT87_CHIP_ID2);
	if ((it87_chipid != 0x12) && (it87_chipid != 0x05)) {
		printk(KERN_INFO LIRC_DRIVER_NAME
		       ": no IT8705/12 found, exiting..\n");
		retval = -ENXIO;
		return retval;
	}
	printk(KERN_INFO LIRC_DRIVER_NAME
	       ": found IT87%.2x.\n",
	       it87_chipid);

	/* get I/O-Port and IRQ */
	if (it87_chipid == 0x12)
		ldn = IT8712_CIR_LDN;
	else
		ldn = IT8705_CIR_LDN;
	it87_write(IT87_LDN, ldn);

	it87_io = it87_read(IT87_CIR_BASE_MSB) * 256 +
		it87_read(IT87_CIR_BASE_LSB);
	if (it87_io == 0) {
		if (io == 0)
			io = IT87_CIR_DEFAULT_IOBASE;
		printk(KERN_INFO LIRC_DRIVER_NAME
		       ": set default io 0x%x\n",
		       io);
		it87_write(IT87_CIR_BASE_MSB, io / 0x100);
		it87_write(IT87_CIR_BASE_LSB, io % 0x100);
	}
	else
		io = it87_io;

	it87_irq = it87_read(IT87_CIR_IRQ);
	if (it87_irq == 0) {
		if (irq == 0)
			irq = IT87_CIR_DEFAULT_IRQ;
		printk(KERN_INFO LIRC_DRIVER_NAME
		       ": set default irq 0x%x\n",
		       irq);
		it87_write(IT87_CIR_IRQ, irq);
	}
	else
		irq = it87_irq;

	{
		unsigned long hw_flags;

		spin_lock_irqsave(&hardware_lock, hw_flags);
		/* reset */
		outb(IT87_CIR_IER_RESET, io+IT87_CIR_IER);
		/* fifo clear */
		outb(IT87_CIR_TCR1_FIFOCLR |
		     /*	     IT87_CIR_TCR1_ILE | */
		     IT87_CIR_TCR1_TXRLE |
		     IT87_CIR_TCR1_TXENDF, io+IT87_CIR_TCR1);
		spin_unlock_irqrestore(&hardware_lock, hw_flags);
	}

	/* get I/O port access and IRQ line */
	retval = request_region(io, 8, LIRC_DRIVER_NAME);

	if (!retval) {
		printk(KERN_ERR LIRC_DRIVER_NAME
		       ": Unable to reserve IO region for LIRC IT87. Port 0x%.4x already in use.\n",
		       io);
		/* Leaving MB PnP Mode */
		it87_write(IT87_CFGCTRL, 0x2);
		return retval;
	}

	/* activate CIR-Device */
	it87_write(IT87_CIR_ACT, 0x1);

	/* Leaving MB PnP Mode */
	it87_write(IT87_CFGCTRL, 0x2);

	retval = request_irq(irq, it87_interrupt, 0 /*SA_INTERRUPT*/,
			     LIRC_DRIVER_NAME, NULL);
	if (retval < 0) {
		printk(KERN_ERR LIRC_DRIVER_NAME
		       ": IRQ %d already in use.\n",
		       irq);
		return retval;
	}

	printk(KERN_INFO LIRC_DRIVER_NAME
	       ": I/O port 0x%.4x, IRQ %d.\n",
	       io,
	       irq);

	request_region(io, 8, LIRC_DRIVER_NAME);
	init_timer(&timerlist);
	timerlist.function = it87_timeout;
	timerlist.data = 0xabadcafe;

	return 0;
}


static void drop_port(void)
{
/*
        unsigned char init_bytes[4] = {IT87_INIT};

        / * Enter MB PnP Mode * /
        outb(init_bytes[0], IT87_ADRPORT);
        outb(init_bytes[1], IT87_ADRPORT);
        outb(init_bytes[2], IT87_ADRPORT);
        outb(init_bytes[3], IT87_ADRPORT);

        / * deactivate CIR-Device * /
        it87_write(IT87_CIR_ACT, 0x0);

        / * Leaving MB PnP Mode * /
        it87_write(IT87_CFGCTRL, 0x2);
*/

	del_timer_sync(&timerlist);
	free_irq(irq, NULL);
	release_region(io, 8);
}


int init_lirc_it87(void)
{
	int retval;

	init_waitqueue_head(&lirc_read_queue);
	retval = init_port();
	if (retval < 0)
		return retval;
	init_hardware();
	printk(KERN_INFO LIRC_DRIVER_NAME
	       ": Installed.\n");
	return 0;
}


MODULE_AUTHOR("Hans-Günter Lütke Uphues");
MODULE_DESCRIPTION("LIRC driver for ITE IT8712/IT8705 CIR port");
MODULE_PARM(io, "i");
MODULE_PARM_DESC(io,
		 "I/O base address (default: 0x310)");
MODULE_PARM(irq, "i");
MODULE_PARM_DESC(irq,
		 "Interrupt (1,3-12) (default: 7)");
MODULE_PARM(it87_enable_demodulator, "i");
MODULE_PARM_DESC(it87_enable_demodulator,
		 "Receiver demodulator enable/disable (1/0), default: 0");
MODULE_LICENSE("GPL");

static int __init lirc_it87_init(void)
{
	int retval;

	retval=init_chrdev();
	if(retval < 0)
		return retval;
	retval = init_lirc_it87();
	if (retval) {
		drop_chrdev();
		return retval;
	}
	return 0;
}


static void __exit lirc_it87_exit(void)
{
	drop_hardware();
	drop_chrdev();
	drop_port();
	printk(KERN_INFO LIRC_DRIVER_NAME ": Uninstalled.\n");
}

module_init(lirc_it87_init);
module_exit(lirc_it87_exit);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
