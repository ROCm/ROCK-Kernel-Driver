/*      $Id: lirc_parallel.c,v 5.18 2002/11/19 20:22:07 ranty Exp $      */

/****************************************************************************
 ** lirc_parallel.c *********************************************************
 ****************************************************************************
 *
 * lirc_parallel - device driver for infra-red signal receiving and
 *                 transmitting unit built by the author
 *
 * Copyright (C) 1998 Christoph Bartelmus <lirc@bartelmus.de>
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

/***********************************************************************
 *************************       Includes        ***********************
 ***********************************************************************/

#include <linux/version.h>

#include <linux/config.h>

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/signal.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/div64.h>

#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/parport.h>

#include <linux/lirc.h>
#include "lirc_dev.h"

#include "lirc_parallel.h"

#define LIRC_DRIVER_NAME "lirc_parallel"

/***********************************************************************
 *************************   Globale Variablen   ***********************
 ***********************************************************************/

unsigned int irq = CONFIG_LIRC_IRQ_PARALLEL;
unsigned int io = CONFIG_LIRC_PORT_PARALLEL;
#ifdef CONFIG_LIRC_TIMER
unsigned int timer = 0;
unsigned int default_timer = CONFIG_LIRC_TIMER;
#endif

#define WBUF_SIZE (256)
#define RBUF_SIZE (256) /* this must be a power of 2 larger than 1 */

static lirc_t wbuf[WBUF_SIZE];
static lirc_t rbuf[RBUF_SIZE];

DECLARE_WAIT_QUEUE_HEAD(lirc_wait);

unsigned int rptr=0,wptr=0;
unsigned int lost_irqs=0;
int is_open=0;

struct parport *pport;
struct pardevice *ppdevice;
int is_claimed=0;

int pf(void *handle);
void kf(void *handle);

/***********************************************************************
 *************************   Interne Funktionen  ***********************
 ***********************************************************************/

unsigned int __inline__ in(int offset)
{
	switch(offset)
	{
	case LIRC_LP_BASE:
		return(parport_read_data(pport));
	case LIRC_LP_STATUS:
		return(parport_read_status(pport));
	case LIRC_LP_CONTROL:
		return(parport_read_control(pport));
	}
	return(0); /* make compiler happy */
}

void __inline__ out(int offset, int value)
{
	switch(offset)
	{
	case LIRC_LP_BASE:
		parport_write_data(pport,value);
		break;
	case LIRC_LP_CONTROL:
		parport_write_control(pport,value);
		break;
	case LIRC_LP_STATUS:
		printk(KERN_INFO "%s: attempt to write to status register\n",
		       LIRC_DRIVER_NAME);
		break;
	}
}

unsigned int __inline__ lirc_get_timer(void)
{
	return(in(LIRC_PORT_TIMER)&LIRC_PORT_TIMER_BIT);
}

unsigned int __inline__  lirc_get_signal(void)
{
	return(in(LIRC_PORT_SIGNAL)&LIRC_PORT_SIGNAL_BIT);
}

void __inline__ lirc_on(void)
{
	out(LIRC_PORT_DATA,LIRC_PORT_DATA_BIT);
}

void __inline__ lirc_off(void)
{
	out(LIRC_PORT_DATA,0);
}

unsigned int init_CONFIG_LIRC_TIMER(void)
{
	struct timeval tv,now;
	unsigned int level,newlevel,timeelapsed,newtimer;
	int count=0;

	do_gettimeofday(&tv);
	tv.tv_sec++;                     /* wait max. 1 sec. */
	level=lirc_get_timer();
	do
	{
		newlevel=lirc_get_timer();
		if(level==0 && newlevel!=0) count++;
		level=newlevel;
		do_gettimeofday(&now);
	}
	while(count<1000 && (now.tv_sec<tv.tv_sec
			     || (now.tv_sec==tv.tv_sec
				 && now.tv_usec<tv.tv_usec)));

	timeelapsed=((now.tv_sec+1-tv.tv_sec)*1000000
		     +(now.tv_usec-tv.tv_usec));
	if(count>=1000 && timeelapsed>0)
	{
		if(default_timer==0)                    /* autodetect timer */
		{
			newtimer=(1000000*count)/timeelapsed;
			printk(KERN_INFO "%s: %u Hz timer detected\n",
			       LIRC_DRIVER_NAME,newtimer);
			return(newtimer);
		}
		else
		{
			newtimer=(1000000*count)/timeelapsed;
			if(abs(newtimer-default_timer)>
			   default_timer/10) /* bad timer */
			{
				printk(KERN_NOTICE "%s: bad timer: %u Hz\n",
				       LIRC_DRIVER_NAME,newtimer);
				printk(KERN_NOTICE "%s: using default timer: "
				       "%u Hz\n",
				       LIRC_DRIVER_NAME,default_timer);
				return(default_timer);
			}
			else
			{
				printk(KERN_INFO "%s: %u Hz timer detected\n",
				       LIRC_DRIVER_NAME,newtimer);
				return(newtimer); /* use detected value */
			}
		}
	}
	else
	{
		printk(KERN_NOTICE "%s: no timer detected\n",LIRC_DRIVER_NAME);
		return(0);
	}
}

int lirc_claim(void)
{
	if(parport_claim(ppdevice)!=0)
	{
		printk(KERN_WARNING "%s: could not claim port\n",
		       LIRC_DRIVER_NAME);
		printk(KERN_WARNING "%s: waiting for port becoming available"
		       "\n",LIRC_DRIVER_NAME);
		if(parport_claim_or_block(ppdevice)<0)
		{
			printk(KERN_NOTICE "%s: could not claim port, giving"
			       " up\n",LIRC_DRIVER_NAME);
			return(0);
		}
	}
	out(LIRC_LP_CONTROL,LP_PSELECP|LP_PINITP);
	is_claimed=1;
	return(1);
}

/***********************************************************************
 *************************   interrupt handler  ************************
 ***********************************************************************/

static inline void rbuf_write(lirc_t signal)
{
	unsigned int nwptr;

	nwptr=(wptr+1) & (RBUF_SIZE-1);
	if(nwptr==rptr) /* no new signals will be accepted */
	{
		lost_irqs++;
		printk(KERN_NOTICE "%s: buffer overrun\n",LIRC_DRIVER_NAME);
		return;
	}
	rbuf[wptr]=signal;
	wptr=nwptr;
}

void irq_handler(int i,void *blah,struct pt_regs * regs)
{
	struct timeval tv;
	static struct timeval lasttv;
	static int init=0;
	long signal;
	lirc_t data;
	unsigned int level,newlevel;
	unsigned int timeout;

#ifdef CONFIG_MODULE_UNLOAD
	if(!module_refcount(THIS_MODULE))
		return;
#endif

	if(!is_claimed)
	{
		return;
	}

	/* disable interrupt */
	/*
	  disable_irq(irq);
	  out(LIRC_PORT_IRQ,in(LIRC_PORT_IRQ)&(~LP_PINTEN));
	*/
	if(in(1)&LP_PSELECD)
	{
		return;
	}

#ifdef CONFIG_LIRC_TIMER
	if(init)
	{
		do_gettimeofday(&tv);

	        signal=tv.tv_sec-lasttv.tv_sec;
		if(signal>15)
		{
			data=PULSE_MASK;  /* really long time */
		}
		else
		{
			data=(lirc_t) (signal*1000000+
				       tv.tv_usec-lasttv.tv_usec+
				       LIRC_SFH506_DELAY);
		};

		rbuf_write(data); /* space */
	}
	else
	{
		if(timer==0) /* wake up; we'll lose this signal
				but it will be garbage if the device
				is turned on anyway
			      */
		{
			timer=init_CONFIG_LIRC_TIMER();
			/* enable_irq(irq); */
			return;
		}
		init=1;
	}

	timeout=timer/10;           /* timeout after 1/10 sec. */
	signal=1;
	level=lirc_get_timer();
	do{
		newlevel=lirc_get_timer();
		if(level==0 && newlevel!=0) signal++;
		level=newlevel;

		/* giving up */
		if(signal>timeout || (in(1)&LP_PSELECD))
		{
			signal=0;
			printk(KERN_NOTICE "%s: timeout\n",LIRC_DRIVER_NAME);
			break;
		}
	}
	while(lirc_get_signal());
	if(signal!=0)
	{
		/* ajust value to usecs */
		signal=(long) (((unsigned long long) signal)*1000000)/timer;

		if(signal>LIRC_SFH506_DELAY)
		{
			data=signal-LIRC_SFH506_DELAY;
		}
		else
		{
			data=1;
		}
		rbuf_write(PULSE_BIT|data); /* pulse */
	}
	do_gettimeofday(&lasttv);
#else
	/* add your code here */
#endif

	wake_up_interruptible(&lirc_wait);

	/* enable interrupt */
	/*
	  enable_irq(irq);
	  out(LIRC_PORT_IRQ,in(LIRC_PORT_IRQ)|LP_PINTEN);
	*/
}

/***********************************************************************
 **************************   file_operations   ************************
 ***********************************************************************/

static loff_t lirc_lseek(struct file *filep,loff_t offset,int orig)
{
	return(-ESPIPE);
}

static ssize_t lirc_read(struct file *filep,char *buf,size_t n,loff_t *ppos)
{
	int result;
	int count=0;
	DECLARE_WAITQUEUE(wait, current);

	if(n%sizeof(lirc_t)) return(-EINVAL);

	result=verify_area(VERIFY_WRITE,buf,n);
	if(result) return(result);

	add_wait_queue(&lirc_wait,&wait);
	current->state=TASK_INTERRUPTIBLE;
	while(count<n)
	{
		if(rptr!=wptr)
		{
			copy_to_user(buf+count,(char *) &rbuf[rptr],
				     sizeof(lirc_t));
			rptr=(rptr+1)&(RBUF_SIZE-1);
			count+=sizeof(lirc_t);
		}
		else
		{
			if(filep->f_flags & O_NONBLOCK)
			{
				result=-EAGAIN;
				break;
			}
			if (signal_pending(current))
			{
				result=-ERESTARTSYS;
				break;
			}
			schedule();
			current->state=TASK_INTERRUPTIBLE;
		}
	}
	remove_wait_queue(&lirc_wait,&wait);
	current->state=TASK_RUNNING;
	return(count ? count:result);
}

static ssize_t lirc_write(struct file *filep,const char *buf,size_t n,
			  loff_t *ppos)
{
	int result,count;
	unsigned int i;
	unsigned int level,newlevel;
	unsigned long flags;
	lirc_t counttimer;

	if(!is_claimed)
	{
		return(-EBUSY);
	}
	if(n%sizeof(lirc_t)) return(-EINVAL);
	result=verify_area(VERIFY_READ,buf,n);
	if(result) return(result);

	count=n/sizeof(lirc_t);

	if(count>WBUF_SIZE || count%2==0) return(-EINVAL);

	copy_from_user(wbuf,buf,n);

#ifdef CONFIG_LIRC_TIMER
	if(timer==0) /* try again if device is ready */
	{
		timer=init_CONFIG_LIRC_TIMER();
		if(timer==0) return(-EIO);
	}

	/* ajust values from usecs */
	for(i=0;i<count;i++)
	{
		uint64_t n = (uint64_t)wbuf[i] * timer;
		do_div(n, 1000000ul);
		wbuf[i] = (lirc_t) n;
	}

	local_save_flags(flags);local_irq_disable();
	i=0;
	while(i<count)
	{
		level=lirc_get_timer();
		counttimer=0;
		lirc_on();
		do
		{
			newlevel=lirc_get_timer();
			if(level==0 && newlevel!=0) counttimer++;
			level=newlevel;
			if(in(1)&LP_PSELECD)
			{
				lirc_off();
				local_irq_restore(flags); /* sti(); */
				return(-EIO);
			}
		}
		while(counttimer<wbuf[i]);i++;

		lirc_off();
		if(i==count) break;
		counttimer=0;
		do
		{
			newlevel=lirc_get_timer();
			if(level==0 && newlevel!=0) counttimer++;
			level=newlevel;
			if(in(1)&LP_PSELECD)
			{
				local_irq_restore(flags); /* sti(); */
				return(-EIO);
			}
		}
		while(counttimer<wbuf[i]);i++;
	}
	local_irq_restore(flags); /* sti(); */
#else
	/*
	   place code that handles write
	   without extarnal timer here
	*/
#endif
	return(n);
}

static unsigned int lirc_poll(struct file *file, poll_table * wait)
{
	poll_wait(file, &lirc_wait,wait);
	if (rptr!=wptr)
		return(POLLIN|POLLRDNORM);
	return(0);
}

static int lirc_ioctl(struct inode *node,struct file *filep,unsigned int cmd,
		      unsigned long arg)
{
        int result;
	unsigned long features=LIRC_CAN_SEND_PULSE|LIRC_CAN_REC_MODE2,mode;

	switch(cmd)
	{
	case LIRC_GET_FEATURES:
		result=put_user(features,(unsigned long *) arg);
		if(result) return(result);
		break;
	case LIRC_GET_SEND_MODE:
		result=put_user(LIRC_MODE_PULSE,(unsigned long *) arg);
		if(result) return(result);
		break;
	case LIRC_GET_REC_MODE:
		result=put_user(LIRC_MODE_MODE2,(unsigned long *) arg);
		if(result) return(result);
		break;
	case LIRC_SET_SEND_MODE:
		result=get_user(mode,(unsigned long *) arg);
		if(result) return(result);
		if(mode!=LIRC_MODE_PULSE) return(-EINVAL);
		break;
	case LIRC_SET_REC_MODE:
		result=get_user(mode,(unsigned long *) arg);
		if(result) return(result);
		if(mode!=LIRC_MODE_MODE2) return(-ENOSYS);
		break;
	default:
		return(-ENOIOCTLCMD);
	}
	return(0);
}

static int lirc_open(struct inode* node,struct file* filep)
{
#ifdef CONFIG_MODULE_UNLOAD
	if(module_refcount(THIS_MODULE))
	{
		return(-EBUSY);
	}
#endif
	if(!lirc_claim())
	{
		return(-EBUSY);
	}
	pport->ops->enable_irq(pport);

	/* init read ptr */
	rptr=wptr=0;
	lost_irqs=0;

	try_module_get(THIS_MODULE);
	is_open=1;
	return(0);
}

static int lirc_close(struct inode* node,struct file* filep)
{
	if(is_claimed)
	{
		is_claimed=0;
		parport_release(ppdevice);
	}
	is_open=0;
	module_put(THIS_MODULE);
	return(0);
}

static struct file_operations lirc_fops =
{
	llseek:  lirc_lseek,
	read:    lirc_read,
	write:   lirc_write,
	poll:    lirc_poll,
	ioctl:   lirc_ioctl,
	open:    lirc_open,
	release: lirc_close
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

static int __init lirc_parallel_init (void)
{
#ifndef LIRC26
	pport=parport_enumerate();
	while(pport!=NULL)
	{
		if(pport->base==io)
		{
			break;
		}
		pport=pport->next;
	}
#else
	pport=parport_find_base(io);
#endif
	if(pport==NULL)
	{
		printk(KERN_NOTICE "%s: no port at %x found\n",
		       LIRC_DRIVER_NAME,io);
		return(-ENXIO);
	}
	ppdevice=parport_register_device(pport,LIRC_DRIVER_NAME,
					 pf,kf,irq_handler,0,NULL);
	if(ppdevice==NULL)
	{
		printk(KERN_NOTICE "%s: parport_register_device() failed\n",
		       LIRC_DRIVER_NAME);
		return(-ENXIO);
	}
	if(parport_claim(ppdevice)!=0)
		goto skip_init;
	is_claimed=1;
	out(LIRC_LP_CONTROL,LP_PSELECP|LP_PINITP);

#ifdef CONFIG_LIRC_TIMER
#       ifdef DEBUG
	out(LIRC_PORT_DATA,LIRC_PORT_DATA_BIT);
#       endif

	timer=init_CONFIG_LIRC_TIMER();

#       if 0 	/* continue even if device is offline */
	if(timer==0)
	{
		is_claimed=0;
		parport_release(pport);
		parport_unregister_device(ppdevice);
		return(-EIO);
	}

#       endif
#       ifdef DEBUG
	out(LIRC_PORT_DATA,0);
#       endif
#endif

	is_claimed=0;
	parport_release(ppdevice);
 skip_init:
	if ((plugin.minor = lirc_register_plugin(&plugin)) < 0)
	{
		printk(KERN_NOTICE "%s: register_chrdev() failed\n",LIRC_DRIVER_NAME);
		parport_unregister_device(ppdevice);
		return(-EIO);
	}
	printk(KERN_INFO "%s: installed using port 0x%04x irq %d\n",LIRC_DRIVER_NAME,io,irq);
	return(0);
}

static void __exit lirc_parallel_exit(void)
{
#ifdef CONFIG_MODULE_UNLOAD
	if(module_refcount(THIS_MODULE)) return;
#endif
	parport_unregister_device(ppdevice);
	lirc_unregister_plugin(plugin.minor);
}

static struct timer_list poll_timer;
static void poll_state(unsigned long ignored);

static void poll_state(unsigned long ignored)
{
	printk(KERN_NOTICE "%s: time\n",
	       LIRC_DRIVER_NAME);
	del_timer(&poll_timer);
	if(is_claimed)
		return;
	kf(NULL);
	if(!is_claimed)
	{
		printk(KERN_NOTICE "%s: could not claim port, giving up\n",
		       LIRC_DRIVER_NAME);
		init_timer(&poll_timer);
		poll_timer.expires=jiffies+HZ;
		poll_timer.data=(unsigned long) current;
		poll_timer.function=poll_state;
		add_timer(&poll_timer);
	}
}

int pf(void *handle)
{
	pport->ops->disable_irq(pport);
	is_claimed=0;
	return(0);
}


void kf(void *handle)
{
	if(!is_open)
		return;
	if(!lirc_claim())
		return;
	pport->ops->enable_irq(pport);
	/* this is a bit annoying when you actually print...*/
	/*
	printk(KERN_INFO "%s: reclaimed port\n",LIRC_DRIVER_NAME);
	*/
}

MODULE_AUTHOR("Christoph Bartelmus");
MODULE_DESCRIPTION("Infrared receiver driver for parallel ports.");
MODULE_LICENSE("GPL");

MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "I/O address base (0x3bc, 0x378 or 0x278)");

MODULE_PARM(irq, "i");
MODULE_PARM_DESC(irq, "Interrupt (7 or 5)");

module_init(lirc_parallel_init);
module_exit(lirc_parallel_exit);

