/*
 *  linux/arch/arm/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Modifications for ARM (C) 1994, 1995, 1996,1997 Russell King
 *  Copyright (C) 1999 SuSE GmbH, (Philipp Rumpf, prumpf@tux.org)
 *
 * 1994-07-02  Alan Modra
 *             fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1998-12-20  Updated NTP code according to technical memorandum Jan '96
 *             "A Kernel Model for Precision Timekeeping" by Dave Mills
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/param.h>
#include <asm/pdc.h>
#include <asm/led.h>

#include <linux/timex.h>

extern rwlock_t xtime_lock;

static int timer_value;
static int timer_delta;
static struct pdc_tod tod_data __attribute__((aligned(8)));

void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int old;
	int lost = 0;
	int cr16;
	
	old = timer_value;

	cr16 = mfctl(16);
	while((timer_value - cr16) < (timer_delta / 2)) {
		timer_value += timer_delta;
		lost++;
	}

	mtctl(timer_value ,16);

	do_timer(regs);
    
	led_interrupt_func();
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	
	read_lock_irqsave(&xtime_lock, flags);
	tv->tv_sec = xtime.tv_sec;
	tv->tv_usec = xtime.tv_usec;
	read_unlock_irqrestore(&xtime_lock, flags);

}

void do_settimeofday(struct timeval *tv)
{
	write_lock_irq(&xtime_lock);
	xtime.tv_sec = tv->tv_sec;
	xtime.tv_usec = tv->tv_usec;
	write_unlock_irq(&xtime_lock);
}

void __init time_init(void)
{
	timer_delta = (100 * PAGE0->mem_10msec) / HZ;

	/* make the first timer interrupt go off in one second */
	timer_value = mfctl(16) + (HZ * timer_delta);
	mtctl(timer_value, 16);


	if(pdc_tod_read(&tod_data) == 0) {
		xtime.tv_sec = tod_data.tod_sec;
		xtime.tv_usec = tod_data.tod_usec;
	} else {
		printk(KERN_ERR "Error reading tod clock\n");
	        xtime.tv_sec = 0;
		xtime.tv_usec = 0;
	}

}

