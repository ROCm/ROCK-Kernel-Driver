/*
 *  linux/arch/arm/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Modifications for ARM (C) 1994-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the ARM-specific time handling details:
 *  reading the RTC at bootup, etc...
 *
 *  1994-07-02  Alan Modra
 *              fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 *  1998-12-20  Updated NTP code according to technical memorandum Jan '96
 *              "A Kernel Model for Precision Timekeeping" by Dave Mills
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/profile.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>

u64 jiffies_64 = INITIAL_JIFFIES;

extern unsigned long wall_jiffies;

/* this needs a better home */
spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;

#ifdef CONFIG_SA1100_RTC_MODULE
EXPORT_SYMBOL(rtc_lock);
#endif

/* change this if you have some constant time drift */
#define USECS_PER_JIFFY	(1000000/HZ)

static int dummy_set_rtc(void)
{
	return 0;
}

/*
 * hook for setting the RTC's idea of the current time.
 */
int (*set_rtc)(void) = dummy_set_rtc;

static unsigned long dummy_gettimeoffset(void)
{
	return 0;
}

/*
 * hook for getting the time offset.  Note that it is
 * always called with interrupts disabled.
 */
unsigned long (*gettimeoffset)(void) = dummy_gettimeoffset;

/*
 * Handle kernel profile stuff...
 */
static inline void do_profile(struct pt_regs *regs)
{
	if (!user_mode(regs) &&
	    prof_buffer &&
	    current->pid) {
		unsigned long pc = instruction_pointer(regs);
		extern int _stext;

		pc -= (unsigned long)&_stext;

		pc >>= prof_shift;

		if (pc >= prof_len)
			pc = prof_len - 1;

		prof_buffer[pc] += 1;
	}
}

static unsigned long next_rtc_update;

/*
 * If we have an externally synchronized linux clock, then update
 * CMOS clock accordingly every ~11 minutes.  set_rtc() has to be
 * called as close as possible to 500 ms before the new second
 * starts.
 */
static inline void do_set_rtc(void)
{
	if (time_status & STA_UNSYNC || set_rtc == NULL)
		return;

	if (next_rtc_update &&
	    time_before(xtime.tv_sec, next_rtc_update))
		return;

	if (xtime.tv_nsec < 500000000 - ((unsigned) tick_nsec >> 1) &&
	    xtime.tv_nsec >= 500000000 + ((unsigned) tick_nsec >> 1))
		return;

	if (set_rtc())
		/*
		 * rtc update failed.  Try again in 60s
		 */
		next_rtc_update = xtime.tv_sec + 60;
	else
		next_rtc_update = xtime.tv_sec + 660;
}

#ifdef CONFIG_LEDS

static void dummy_leds_event(led_event_t evt)
{
}

void (*leds_event)(led_event_t) = dummy_leds_event;

EXPORT_SYMBOL(leds_event);
#endif

#ifdef CONFIG_LEDS_TIMER
static void do_leds(void)
{
	static unsigned int count = 50;

	if (--count == 0) {
		count = 50;
		leds_event(led_timer);
	}
}
#else
#define do_leds()
#endif

void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec, lost;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = gettimeoffset();

		lost = jiffies - wall_jiffies;
		if (lost)
			usec += lost * USECS_PER_JIFFY;

		sec = xtime.tv_sec;
		usec += xtime.tv_nsec / 1000;
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	/* usec may have gone up a lot: be safe */
	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

void do_settimeofday(struct timeval *tv)
{
	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * done, and then undo it!
	 */
	tv->tv_usec -= gettimeoffset();
	tv->tv_usec -= (jiffies - wall_jiffies) * USECS_PER_JIFFY;

	while (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime.tv_sec = tv->tv_sec;
	xtime.tv_nsec = tv->tv_usec * 1000;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_sequnlock_irq(&xtime_lock);
}

static struct irqaction timer_irq = {
	.name	= "timer",
	.flags	= SA_INTERRUPT,
};

/*
 * Include architecture specific code
 */
#include <asm/arch/time.h>
