/*      $Id: lirc_serial.c,v 5.49 2004/01/12 10:21:12 lirc Exp $      */

/****************************************************************************
 ** lirc_serial.c ***********************************************************
 ****************************************************************************
 *
 * lirc_serial - Device driver that records pulse- and pause-lengths
 *               (space-lengths) between DDCD event on a serial port.
 *
 * Copyright (C) 1996,97 Ralph Metzler <rjkm@thp.uni-koeln.de>
 * Copyright (C) 1998 Trent Piepho <xyzzy@u.washington.edu>
 * Copyright (C) 1998 Ben Pfaff <blp@gnu.org>
 * Copyright (C) 1999 Christoph Bartelmus <lirc@bartelmus.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* Steve's changes to improve transmission fidelity:
     - for systems with the rdtsc instruction and the clock counter, a
       send_pule that times the pulses directly using the counter.
       This means that the CONFIG_LIRC_SERIAL_TRANSMITTER_LATENCY fudge is
       not needed. Measurement shows very stable waveform, even where
       PCI activity slows the access to the UART, which trips up other
       versions.
     - For other system, non-integer-microsecond pulse/space lengths,
       done using fixed point binary. So, much more accurate carrier
       frequency.
     - fine tuned transmitter latency, taking advantage of fractional
       microseconds in previous change
     - Fixed bug in the way transmitter latency was accounted for by
       tuning the pulse lengths down - the send_pulse routine ignored
       this overhead as it timed the overall pulse length - so the
       pulse frequency was right but overall pulse length was too
       long. Fixed by accounting for latency on each pulse/space
       iteration.

   Steve Davies <steve@daviesfam.org>  July 2001

   Flameeyes Patches Contribution
    - Ronald Wahl <ronald.wahl@informatik.tu-chemnitz.de> sent a patch to
      eliminate a deadlock on SMP systems.
    - Florian Steinel <Florian.Steinel@t-online.de> sent a patch to fix irq
      disabling by kernel.
    - Jindrich Makovicka <makovick@kmlinux.fjfi.cvut.cz> sent a patch fixing
      one-shot use of lirc_serial.
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/serial_reg.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/fcntl.h>

#include <linux/lirc.h>
#include "lirc_dev.h"

#if defined(rdtsc)

#define USE_RDTSC
#warning "Note: using rdtsc instruction"
#endif

struct lirc_serial
{
	int type;
	int signal_pin;
	int signal_pin_change;
	int on;
	int off;
	long (*send_pulse)(unsigned long length);
	void (*send_space)(long length);
	int features;
};

#define LIRC_HOMEBREW        0
#define LIRC_IRDEO           1
#define LIRC_IRDEO_REMOTE    2
#define LIRC_ANIMAX          3
#define LIRC_IGOR            4

#ifdef CONFIG_LIRC_SERIAL_IRDEO
int type=LIRC_IRDEO;
#elif defined(CONFIG_LIRC_SERIAL_IRDEO_REMOTE)
int type=LIRC_IRDEO_REMOTE;
#elif defined(CONFIG_LIRC_SERIAL_ANIMAX)
int type=LIRC_ANIMAX;
#elif defined(CONFIG_LIRC_SERIAL_IGOR)
int type=LIRC_IGOR;
#else
int type=LIRC_HOMEBREW;
#endif

#ifdef CONFIG_LIRC_SERIAL_SOFTCARRIER
int softcarrier=1;
#else
int softcarrier=0;
#endif

static int sense = -1;   /* -1 = auto, 0 = active high, 1 = active low */

static int io = CONFIG_LIRC_PORT_SERIAL;

static int irq = CONFIG_LIRC_IRQ_SERIAL;

static int debug = 0;

MODULE_PARM(type, "i");
MODULE_PARM_DESC(type, "Hardware type (0 = home-brew, 1 = IRdeo,"
		 " 2 = IRdeo Remote, 3 = AnimaX");

MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "I/O address base (0x3f8 or 0x2f8)");

MODULE_PARM(irq, "i");
MODULE_PARM_DESC(irq, "Interrupt (4 or 3)");

MODULE_PARM(sense, "i");
MODULE_PARM_DESC(sense, "Override autodetection of IR receiver circuit"
		 " (0 = active high, 1 = active low )");

MODULE_PARM(softcarrier, "i");
MODULE_PARM_DESC(softcarrier, "Software carrier (0 = off, 1 = on)");

MODULE_PARM(debug,"i");

#define dprintk           if (debug) printk

#define LOGHEAD           "lirc_serial: "

/* forward declarations */
long send_pulse_irdeo(unsigned long length);
long send_pulse_homebrew(unsigned long length);
void send_space_irdeo(long length);
void send_space_homebrew(long length);

struct lirc_serial hardware[]=
{
	/* home-brew receiver/transmitter */
	{
		LIRC_HOMEBREW,
		UART_MSR_DCD,
		UART_MSR_DDCD,
		UART_MCR_RTS|UART_MCR_OUT2|UART_MCR_DTR,
		UART_MCR_RTS|UART_MCR_OUT2,
		send_pulse_homebrew,
		send_space_homebrew,
		(
#ifdef CONFIG_LIRC_SERIAL_TRANSMITTER
		 LIRC_CAN_SET_SEND_DUTY_CYCLE|
		 LIRC_CAN_SET_SEND_CARRIER|
		 LIRC_CAN_SEND_PULSE|
#endif
		 LIRC_CAN_REC_MODE2)
	},

	/* IRdeo classic */
	{
		LIRC_IRDEO,
		UART_MSR_DSR,
		UART_MSR_DDSR,
		UART_MCR_OUT2,
		UART_MCR_RTS|UART_MCR_DTR|UART_MCR_OUT2,
		send_pulse_irdeo,
		send_space_irdeo,
		(LIRC_CAN_SET_SEND_DUTY_CYCLE|
		 LIRC_CAN_SEND_PULSE|
		 LIRC_CAN_REC_MODE2)
	},

	/* IRdeo remote */
	{
		LIRC_IRDEO_REMOTE,
		UART_MSR_DSR,
		UART_MSR_DDSR,
		UART_MCR_RTS|UART_MCR_DTR|UART_MCR_OUT2,
		UART_MCR_RTS|UART_MCR_DTR|UART_MCR_OUT2,
		send_pulse_irdeo,
		send_space_irdeo,
		(LIRC_CAN_SET_SEND_DUTY_CYCLE|
		 LIRC_CAN_SEND_PULSE|
		 LIRC_CAN_REC_MODE2)
	},

	/* AnimaX */
	{
		LIRC_ANIMAX,
		UART_MSR_DCD,
		UART_MSR_DDCD,
		0,
		UART_MCR_RTS|UART_MCR_DTR|UART_MCR_OUT2,
		NULL,
		NULL,
		LIRC_CAN_REC_MODE2
	},

	/* home-brew receiver/transmitter (Igor Cesko's variation) */
	{
		LIRC_HOMEBREW,
		UART_MSR_DSR,
		UART_MSR_DDSR,
		UART_MCR_RTS|UART_MCR_OUT2|UART_MCR_DTR,
		UART_MCR_RTS|UART_MCR_OUT2,
		send_pulse_homebrew,
		send_space_homebrew,
		(
#ifdef CONFIG_LIRC_SERIAL_TRANSMITTER
		 LIRC_CAN_SET_SEND_DUTY_CYCLE|
		 LIRC_CAN_SET_SEND_CARRIER|
		 LIRC_CAN_SEND_PULSE|
#endif
		 LIRC_CAN_REC_MODE2)
	}

};

#define LIRC_DRIVER_NAME "lirc_serial"

#define RS_ISR_PASS_LIMIT 256

/* A long pulse code from a remote might take upto 300 bytes.  The
   daemon should read the bytes as soon as they are generated, so take
   the number of keys you think you can push before the daemon runs
   and multiply by 300.  The driver will warn you if you overrun this
   buffer.  If you have a slow computer or non-busmastering IDE disks,
   maybe you will need to increase this.  */

/* This MUST be a power of two!  It has to be larger than 1 as well. */

#define RBUF_LEN 256
#define WBUF_LEN 256

static struct timeval lasttv = {0, 0};

static spinlock_t lirc_lock = SPIN_LOCK_UNLOCKED;

static struct lirc_buffer rbuf;

static lirc_t wbuf[WBUF_LEN];

unsigned int freq = 38000;
unsigned int duty_cycle = 50;

/* Initialized in init_timing_params() */
unsigned long period = 0;
unsigned long pulse_width = 0;
unsigned long space_width = 0;

#if defined(__i386__)
/*
  From:
  Linux I/O port programming mini-HOWTO
  Author: Riku Saikkonen <Riku.Saikkonen@hut.fi>
  v, 28 December 1997

  [...]
  Actually, a port I/O instruction on most ports in the 0-0x3ff range
  takes almost exactly 1 microsecond, so if you're, for example, using
  the parallel port directly, just do additional inb()s from that port
  to delay.
  [...]
*/
/* transmitter latency 1.5625us 0x1.90 - this figure arrived at from
 * comment above plus trimming to match actual measured frequency.
 * This will be sensitive to cpu speed, though hopefully most of the 1.5us
 * is spent in the uart access.  Still - for reference test machine was a
 * 1.13GHz Athlon system - Steve
 */

/* changed from 400 to 450 as this works better on slower machines;
   faster machines will use the rdtsc code anyway */

#define CONFIG_LIRC_SERIAL_TRANSMITTER_LATENCY 450

#else

/* does anybody have information on other platforms ? */
/* 256 = 1<<8 */
#define CONFIG_LIRC_SERIAL_TRANSMITTER_LATENCY 256

#endif  /* __i386__ */

static inline unsigned int sinp(int offset)
{
	return inb(io + offset);
}

static inline void soutp(int offset, int value)
{
	outb(value, io + offset);
}

static inline void on(void)
{
	soutp(UART_MCR,hardware[type].on);
}

static inline void off(void)
{
	soutp(UART_MCR,hardware[type].off);
}

#ifndef MAX_UDELAY_MS
#define MAX_UDELAY_US 5000
#else
#define MAX_UDELAY_US (MAX_UDELAY_MS*1000)
#endif

static inline void safe_udelay(unsigned long usecs)
{
	while(usecs>MAX_UDELAY_US)
	{
		udelay(MAX_UDELAY_US);
		usecs-=MAX_UDELAY_US;
	}
	udelay(usecs);
}

#ifdef USE_RDTSC
/* This is an overflow/precision juggle, complicated in that we can't
   do long long divide in the kernel */

/* When we use the rdtsc instruction to measure clocks, we keep the
 * pulse and space widths as clock cycles.  As this is CPU speed
 * dependent, the widths must be calculated in init_port and ioctl
 * time
 */

/* So send_pulse can quickly convert microseconds to clocks */
unsigned long conv_us_to_clocks = 0;

static inline int init_timing_params(unsigned int new_duty_cycle,
		unsigned int new_freq)
{
	unsigned long long loops_per_sec,work;

	duty_cycle=new_duty_cycle;
	freq=new_freq;

	loops_per_sec=current_cpu_data.loops_per_jiffy;
	loops_per_sec*=HZ;

	/* How many clocks in a microsecond?, avoiding long long divide */
	work=loops_per_sec;
	work*=4295;  /* 4295 = 2^32 / 1e6 */
	conv_us_to_clocks=(work>>32);

	/* Carrier period in clocks, approach good up to 32GHz clock,
           gets carrier frequency within 8Hz */
	period=loops_per_sec>>3;
	period/=(freq>>3);

	/* Derive pulse and space from the period */

	pulse_width = period*duty_cycle/100;
	space_width = period - pulse_width;
	dprintk(LOGHEAD
	       ": in init_timing_params, freq=%d, duty_cycle=%d, "
	       "clk/jiffy=%ld, pulse=%ld, space=%ld, conv_us_to_clocks=%ld\n",
	       freq, duty_cycle, current_cpu_data.loops_per_jiffy,
	       pulse_width, space_width, conv_us_to_clocks);
	return 0;
}
#else /* ! USE_RDTSC */
static inline int init_timing_params(unsigned int new_duty_cycle,
		unsigned int new_freq)
{
/* period, pulse/space width are kept with 8 binary places -
 * IE multiplied by 256. */
	if(256*1000000L/new_freq*new_duty_cycle/100<=
	   CONFIG_LIRC_SERIAL_TRANSMITTER_LATENCY) return(-EINVAL);
	if(256*1000000L/new_freq*(100-new_duty_cycle)/100<=
	   CONFIG_LIRC_SERIAL_TRANSMITTER_LATENCY) return(-EINVAL);
	duty_cycle=new_duty_cycle;
	freq=new_freq;
	period=256*1000000L/freq;
	pulse_width=period*duty_cycle/100;
	space_width=period-pulse_width;
	dprintk(LOGHEAD
	       ": in init_timing_params, freq=%d pulse=%ld, "
	       "space=%ld\n", freq, pulse_width, space_width);
	return 0;
}
#endif /* USE_RDTSC */


/* return value: space length delta */

long send_pulse_irdeo(unsigned long length)
{
	long rawbits;
	int i;
	unsigned char output;
	unsigned char chunk,shifted;

	/* how many bits have to be sent ? */
	rawbits=length*1152/10000;
	if(duty_cycle>50) chunk=3;
	else chunk=1;
	for(i=0,output=0x7f;rawbits>0;rawbits-=3)
	{
		shifted=chunk<<(i*3);
		shifted>>=1;
		output&=(~shifted);
		i++;
		if(i==3)
		{
			soutp(UART_TX,output);
			while(!(sinp(UART_LSR) & UART_LSR_THRE));
			output=0x7f;
			i=0;
		}
	}
	if(i!=0)
	{
		soutp(UART_TX,output);
		while(!(sinp(UART_LSR) & UART_LSR_TEMT));
	}

	if(i==0)
	{
		return((-rawbits)*10000/1152);
	}
	else
	{
		return((3-i)*3*10000/1152+(-rawbits)*10000/1152);
	}
}

#ifdef USE_RDTSC
/* Version that uses Pentium rdtsc instruction to measure clocks */

/* This version does sub-microsecond timing using rdtsc instruction,
 * and does away with the fudged CONFIG_LIRC_SERIAL_TRANSMITTER_LATENCY
 * Implicitly i586 architecture...  - Steve
 */

static inline long send_pulse_homebrew_softcarrier(unsigned long length)
{
	int flag;
	unsigned long target, start, now;

	/* Get going quick as we can */
	rdtscl(start);on();
	/* Convert length from microseconds to clocks */
	length*=conv_us_to_clocks;
	/* And loop till time is up - flipping at right intervals */
	now=start;
	target=pulse_width;
	flag=1;
	while((now-start)<length)
	{
		/* Delay till flip time */
		do
		{
			rdtscl(now);
		}
		while ((now-start)<target);
		/* flip */
		if(flag)
		{
			rdtscl(now);off();
			target+=space_width;
		}
		else
		{
			rdtscl(now);on();
			target+=pulse_width;
		}
		flag=!flag;
	}
	rdtscl(now);
	return(((now-start)-length)/conv_us_to_clocks);
}
#else /* ! USE_RDTSC */
/* Version using udelay() */

/* here we use fixed point arithmetic, with 8
   fractional bits.  that gets us within 0.1% or so of the right average
   frequency, albeit with some jitter in pulse length - Steve */

/* To match 8 fractional bits used for pulse/space length */

static inline long send_pulse_homebrew_softcarrier(unsigned long length)
{
	int flag;
	unsigned long actual, target, d;
	length<<=8;

	actual=target=0; flag=0;
	while(actual<length)
	{
		if(flag)
		{
			off();
			target+=space_width;
		}
		else
		{
			on();
			target+=pulse_width;
		}
		d=(target-actual-CONFIG_LIRC_SERIAL_TRANSMITTER_LATENCY+128)>>8;
		/* Note - we've checked in ioctl that the pulse/space
		   widths are big enough so that d is > 0 */
		udelay(d);
		actual+=(d<<8)+CONFIG_LIRC_SERIAL_TRANSMITTER_LATENCY;
		flag=!flag;
	}
	return((actual-length)>>8);
}
#endif /* USE_RDTSC */

long send_pulse_homebrew(unsigned long length)
{
	if(length<=0) return 0;
	if(softcarrier)
	{
		return send_pulse_homebrew_softcarrier(length);
	}
	else
	{
		on();
		safe_udelay(length);
		return(0);
	}
}

void send_space_irdeo(long length)
{
	if(length<=0) return;
	safe_udelay(length);
}

void send_space_homebrew(long length)
{
        off();
	if(length<=0) return;
	safe_udelay(length);
}

static void inline rbwrite(lirc_t l)
{
	if(lirc_buffer_full(&rbuf))    /* no new signals will be accepted */
	{
		dprintk(LOGHEAD ": Buffer overrun\n");
		return;
	}
	_lirc_buffer_write_1(&rbuf, (void *)&l);
}

static void inline frbwrite(lirc_t l)
{
	/* simple noise filter */
	static lirc_t pulse=0L,space=0L;
	static unsigned int ptr=0;

	if(ptr>0 && (l&PULSE_BIT))
	{
		pulse+=l&PULSE_MASK;
		if(pulse>250)
		{
			rbwrite(space);
			rbwrite(pulse|PULSE_BIT);
			ptr=0;
			pulse=0;
		}
		return;
	}
	if(!(l&PULSE_BIT))
	{
		if(ptr==0)
		{
			if(l>20000)
			{
				space=l;
				ptr++;
				return;
			}
		}
		else
		{
			if(l>20000)
			{
				space+=pulse;
				if(space>PULSE_MASK) space=PULSE_MASK;
				space+=l;
				if(space>PULSE_MASK) space=PULSE_MASK;
				pulse=0;
				return;
			}
			rbwrite(space);
			rbwrite(pulse|PULSE_BIT);
			ptr=0;
			pulse=0;
		}
	}
	rbwrite(l);
}

irqreturn_t irq_handler(int i, void *blah, struct pt_regs *regs)
{
	struct timeval tv;
	int status,counter,dcd;
	long deltv;
	lirc_t data;

	counter=0;
	do{
		counter++;
		status=sinp(UART_MSR);
		if(counter>RS_ISR_PASS_LIMIT)
		{
			printk(KERN_WARNING LIRC_DRIVER_NAME ": AIEEEE: "
			       "We're caught!\n");
			break;
		}
		if((status&hardware[type].signal_pin_change) && sense!=-1)
		{
			/* get current time */
			do_gettimeofday(&tv);

			/* New mode, written by Trent Piepho
			   <xyzzy@u.washington.edu>. */

			/* The old format was not very portable.
			   We now use the type lirc_t to pass pulses
			   and spaces to user space.

			   If PULSE_BIT is set a pulse has been
			   received, otherwise a space has been
			   received.  The driver needs to know if your
			   receiver is active high or active low, or
			   the space/pulse sense could be
			   inverted. The bits denoted by PULSE_MASK are
			   the length in microseconds. Lengths greater
			   than or equal to 16 seconds are clamped to
			   PULSE_MASK.  All other bits are unused.
			   This is a much simpler interface for user
			   programs, as well as eliminating "out of
			   phase" errors with space/pulse
			   autodetection. */

			/* calculate time since last interrupt in
			   microseconds */
			dcd=(status & hardware[type].signal_pin) ? 1:0;

			deltv=tv.tv_sec-lasttv.tv_sec;
			if(deltv>15)
			{
				dprintk(LOGHEAD
				       ": AIEEEE: %d %d %lx %lx %lx %lx\n",
				       dcd,sense,
				       tv.tv_sec,lasttv.tv_sec,
				       tv.tv_usec,lasttv.tv_usec);
				data=PULSE_MASK; /* really long time */
				if(!(dcd^sense)) /* sanity check */
				{
				        /* detecting pulse while this
					   MUST be a space! */
				        sense=sense ? 0:1;
				}
			}
			else
			{
				data=(lirc_t) (deltv*1000000+
					       tv.tv_usec-
					       lasttv.tv_usec);
			};
			if(tv.tv_sec<lasttv.tv_sec ||
			   (tv.tv_sec==lasttv.tv_sec &&
			    tv.tv_usec<lasttv.tv_usec))
			{
				printk(KERN_WARNING LIRC_DRIVER_NAME
				       ": AIEEEE: your clock just jumped "
				       "backwards\n");
				printk(KERN_WARNING LIRC_DRIVER_NAME
				       ": %d %d %lx %lx %lx %lx\n",
				       dcd,sense,
				       tv.tv_sec,lasttv.tv_sec,
				       tv.tv_usec,lasttv.tv_usec);
				data=PULSE_MASK;
			}
			frbwrite(dcd^sense ? data : (data|PULSE_BIT));
			lasttv=tv;
			wake_up_interruptible(&rbuf.wait_poll);
		}
	} while(!(sinp(UART_IIR) & UART_IIR_NO_INT)); /* still pending ? */

	return IRQ_HANDLED;
}

static DECLARE_WAIT_QUEUE_HEAD(power_supply_queue);
static spinlock_t lirc_lock;

static int init_port(void)
{
	unsigned long flags;

	/* Reserve io region. */
	if(!request_region(io, 8, LIRC_DRIVER_NAME))
	{
		printk(KERN_ERR  LIRC_DRIVER_NAME
		       ": port %04x already in use\n", io);
		printk(KERN_WARNING LIRC_DRIVER_NAME
		       ": use 'setserial /dev/ttySX uart none'\n");
		printk(KERN_WARNING LIRC_DRIVER_NAME
		       ": or compile the serial port driver as module and\n");
		printk(KERN_WARNING LIRC_DRIVER_NAME
		       ": make sure this module is loaded first\n");
		return(-EBUSY);
	}

	lock_kernel();

	spin_lock_irqsave(&lirc_lock, flags);

	/* Set DLAB 0. */
	soutp(UART_LCR, sinp(UART_LCR) & (~UART_LCR_DLAB));

	/* First of all, disable all interrupts */
	soutp(UART_IER, sinp(UART_IER)&
	      (~(UART_IER_MSI|UART_IER_RLSI|UART_IER_THRI|UART_IER_RDI)));

	/* Clear registers. */
	sinp(UART_LSR);
	sinp(UART_RX);
	sinp(UART_IIR);
	sinp(UART_MSR);

	/* Set line for power source */
	soutp(UART_MCR, hardware[type].off);

	/* Clear registers again to be sure. */
	sinp(UART_LSR);
	sinp(UART_RX);
	sinp(UART_IIR);
	sinp(UART_MSR);

	switch(hardware[type].type)
	{
	case LIRC_IRDEO:
	case LIRC_IRDEO_REMOTE:
		/* setup port to 7N1 @ 115200 Baud */
		/* 7N1+start = 9 bits at 115200 ~ 3 bits at 38kHz */

		/* Set DLAB 1. */
		soutp(UART_LCR, sinp(UART_LCR) | UART_LCR_DLAB);
		/* Set divisor to 1 => 115200 Baud */
		soutp(UART_DLM,0);
		soutp(UART_DLL,1);
		/* Set DLAB 0 +  7N1 */
		soutp(UART_LCR,UART_LCR_WLEN7);
		/* THR interrupt already disabled at this point */
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&lirc_lock, flags);

	/* Initialize pulse/space widths */
	init_timing_params(duty_cycle, freq);

	/* If pin is high, then this must be an active low receiver. */
	if(sense==-1)
	{
		/* wait 1 sec for the power supply */

		sleep_on_timeout(&power_supply_queue,HZ);

		sense=(sinp(UART_MSR) & hardware[type].signal_pin) ? 1:0;
		printk(KERN_INFO  LIRC_DRIVER_NAME  ": auto-detected active "
		       "%s receiver\n",sense ? "low":"high");
	}
	else
	{
		printk(KERN_INFO  LIRC_DRIVER_NAME  ": Manually using active "
		       "%s receiver\n",sense ? "low":"high");
	};

	unlock_kernel();

	return 0;
}

static int set_use_inc(void* data)
{
	int result;
	unsigned long flags;

	spin_lock(&lirc_lock);
#ifdef CONFIG_MODULE_UNLOAD
	if(module_refcount(THIS_MODULE))
	{
		spin_unlock(&lirc_lock);
		return -EBUSY;
	}
#endif

	/* initialize timestamp */
	do_gettimeofday(&lasttv);

	result=request_irq(irq,irq_handler,SA_INTERRUPT,LIRC_DRIVER_NAME,NULL);
	switch(result)
	{
	case -EBUSY:
		printk(KERN_ERR LIRC_DRIVER_NAME ": IRQ %d busy\n", irq);
		spin_unlock(&lirc_lock);
		return -EBUSY;
	case -EINVAL:
		printk(KERN_ERR LIRC_DRIVER_NAME
		       ": Bad irq number or handler\n");
		spin_unlock(&lirc_lock);
		return -EINVAL;
	default:
		dprintk(LOGHEAD
		       ": Interrupt %d, port %04x obtained\n", irq, io);
		break;
	};

	local_irq_save(flags);

	/* Set DLAB 0. */
	soutp(UART_LCR, sinp(UART_LCR) & (~UART_LCR_DLAB));

	soutp(UART_IER, sinp(UART_IER)|UART_IER_MSI);

	local_irq_restore(flags);

	try_module_get(THIS_MODULE);
	spin_unlock(&lirc_lock);
	return 0;
}

static void set_use_dec(void* data)
{	unsigned long flags;

	spin_lock_irqsave(&lirc_lock, flags);

	/* Set DLAB 0. */
	soutp(UART_LCR, sinp(UART_LCR) & (~UART_LCR_DLAB));

	/* First of all, disable all interrupts */
	soutp(UART_IER, sinp(UART_IER)&
	      (~(UART_IER_MSI|UART_IER_RLSI|UART_IER_THRI|UART_IER_RDI)));
	spin_unlock_irqrestore(&lirc_lock, flags);

	free_irq(irq, NULL);
	dprintk(LOGHEAD ": freed IRQ %d\n", irq);

	module_put(THIS_MODULE);
}

static ssize_t lirc_write(struct file *file, const char *buf,
			 size_t n, loff_t * ppos)
{
	int retval,i,count;
	unsigned long flags;
	long delta=0;

	if(!(hardware[type].features&LIRC_CAN_SEND_PULSE))
	{
		return(-EBADF);
	}

	if(n%sizeof(lirc_t)) return(-EINVAL);
	retval=verify_area(VERIFY_READ,buf,n);
	if(retval) return(retval);
	count=n/sizeof(lirc_t);
	if(count>WBUF_LEN || count%2==0) return(-EINVAL);
	copy_from_user(wbuf,buf,n);
	spin_lock_irqsave(&lirc_lock, flags);
	if(hardware[type].type==LIRC_IRDEO)
	{
		/* DTR, RTS down */
		on();
	}
	for(i=0;i<count;i++)
	{
		if(i%2) hardware[type].send_space(wbuf[i]-delta);
		else delta=hardware[type].send_pulse(wbuf[i]);
	}
	off();
	spin_unlock_irqrestore(&lirc_lock, flags);
	return(n);
}

static int lirc_ioctl(struct inode *node,struct file *filep,unsigned int cmd,
		      unsigned long arg)
{
        int result;
	unsigned long value;
	unsigned int ivalue;

	switch(cmd)
	{
	case LIRC_GET_SEND_MODE:
		if(!(hardware[type].features&LIRC_CAN_SEND_MASK))
		{
			return(-ENOIOCTLCMD);
		}

		result=put_user(LIRC_SEND2MODE
				(hardware[type].features&LIRC_CAN_SEND_MASK),
				(unsigned long *) arg);
		if(result) return(result);
		break;

	case LIRC_SET_SEND_MODE:
		if(!(hardware[type].features&LIRC_CAN_SEND_MASK))
		{
			return(-ENOIOCTLCMD);
		}

		result=get_user(value,(unsigned long *) arg);
		if(result) return(result);
		/* only LIRC_MODE_PULSE supported */
		if(value!=LIRC_MODE_PULSE) return(-ENOSYS);
		break;

	case LIRC_GET_LENGTH:
		return(-ENOSYS);
		break;

	case LIRC_SET_SEND_DUTY_CYCLE:
		dprintk(LOGHEAD ": SET_SEND_DUTY_CYCLE\n");
		if(!(hardware[type].features&LIRC_CAN_SET_SEND_DUTY_CYCLE))
		{
			return(-ENOIOCTLCMD);
		}

		result=get_user(ivalue,(unsigned int *) arg);
		if(result) return(result);
		if(ivalue<=0 || ivalue>100) return(-EINVAL);
		return init_timing_params(ivalue, freq);
		break;

	case LIRC_SET_SEND_CARRIER:
		dprintk(LOGHEAD ": SET_SEND_CARRIER\n");
		if(!(hardware[type].features&LIRC_CAN_SET_SEND_CARRIER))
		{
			return(-ENOIOCTLCMD);
		}

		result=get_user(ivalue,(unsigned int *) arg);
		if(result) return(result);
		if(ivalue>500000 || ivalue<20000) return(-EINVAL);
		return init_timing_params(duty_cycle, ivalue);
		break;

	default:
		return(-ENOIOCTLCMD);
	}
	return(0);
}

static struct file_operations lirc_fops =
{
	write:   lirc_write,
};

static struct lirc_plugin plugin = {
	name:		LIRC_DRIVER_NAME,
	minor:		-1,
	code_length:	1,
	sample_rate:	0,
	data:		NULL,
	add_to_buf:	NULL,
	get_queue:	NULL,
	rbuf:		&rbuf,
	set_use_inc:	set_use_inc,
	set_use_dec:	set_use_dec,
	ioctl:		lirc_ioctl,
	fops:		&lirc_fops,
};

MODULE_AUTHOR("Ralph Metzler, Trent Piepho, Ben Pfaff, Christoph Bartelmus");
MODULE_DESCRIPTION("Infra-red receiver driver for serial ports.");
MODULE_LICENSE("GPL");

static int __init lirc_serial_init(void)
{
	int result;

	switch(type)
	{
	case LIRC_HOMEBREW:
	case LIRC_IRDEO:
	case LIRC_IRDEO_REMOTE:
	case LIRC_ANIMAX:
	case LIRC_IGOR:
		break;
	default:
		return(-EINVAL);
	}
	if(!softcarrier && hardware[type].type==LIRC_HOMEBREW)
	{
		hardware[type].features&=~(LIRC_CAN_SET_SEND_DUTY_CYCLE|
					   LIRC_CAN_SET_SEND_CARRIER);
	}
	if ((result = init_port()) < 0)
		return result;
	plugin.features = hardware[type].features;
	lirc_buffer_init(&rbuf, sizeof(lirc_t), RBUF_LEN);
	if ((plugin.minor = lirc_register_plugin(&plugin)) < 0) {
		printk(KERN_ERR  LIRC_DRIVER_NAME
		       ": register_chrdev failed!\n");
		release_region(io, 8);
		return -EIO;
	}
	return 0;
}

static void __exit lirc_serial_exit(void)
{
	release_region(io, 8);
	lirc_buffer_free(&rbuf);
	lirc_unregister_plugin(plugin.minor);
	dprintk(LOGHEAD ": cleaned up module\n");
}

module_init(lirc_serial_init);
module_exit(lirc_serial_exit);
