/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (c) 2003  Maciej W. Rozycki
 *
 * Common time service routines for MIPS machines. See
 * Documentation/mips/time.README.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/div64.h>
#include <asm/hardirq.h>
#include <asm/sections.h>
#include <asm/time.h>

/* This is for machines which generate the exact clock. */
#define USECS_PER_JIFFY (1000000/HZ)
#define USECS_PER_JIFFY_FRAC ((u32)((1000000ULL << 32) / HZ))

#define TICK_SIZE	(tick_nsec / 1000)

u64 jiffies_64 = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

/*
 * forward reference
 */
extern volatile unsigned long wall_jiffies;

spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;

/*
 * whether we emulate local_timer_interrupts for SMP machines.
 */
int emulate_local_timer_interrupt;

/*
 * By default we provide the null RTC ops
 */
static unsigned long null_rtc_get_time(void)
{
	return mktime(2000, 1, 1, 0, 0, 0);
}

static int null_rtc_set_time(unsigned long sec)
{
	return 0;
}

unsigned long (*rtc_get_time)(void) = null_rtc_get_time;
int (*rtc_set_time)(unsigned long) = null_rtc_set_time;
int (*rtc_set_mmss)(unsigned long);


/*
 * This version of gettimeofday has microsecond resolution and better than
 * microsecond precision on fast machines with cycle counter.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin(&xtime_lock);
		usec = do_gettimeoffset();
		{
			unsigned long lost = jiffies - wall_jiffies;
			if (lost)
				usec += lost * (1000000 / HZ);
		}
		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry(&xtime_lock, seq));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	nsec -= do_gettimeoffset() * NSEC_PER_USEC;
	nsec -= (jiffies - wall_jiffies) * TICK_NSEC;

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_sequnlock_irq(&xtime_lock);

	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

/*
 * Gettimeoffset routines.  These routines returns the time duration
 * since last timer interrupt in usecs.
 *
 * If the exact CPU counter frequency is known, use fixed_rate_gettimeoffset.
 * Otherwise use calibrate_gettimeoffset()
 *
 * If the CPU does not have counter register all, you can either supply
 * your own gettimeoffset() routine, or use null_gettimeoffset() routines,
 * which gives the same resolution as HZ.
 */


/* usecs per counter cycle, shifted to left by 32 bits */
static unsigned int sll32_usecs_per_cycle;

/* how many counter cycles in a jiffy */
static unsigned long cycles_per_jiffy;

/* Cycle counter value at the previous timer interrupt.. */
static unsigned int timerhi, timerlo;

/* expirelo is the count value for next CPU timer interrupt */
static unsigned int expirelo;

/* last time when xtime and rtc are sync'ed up */
static long last_rtc_update;

/* the function pointer to one of the gettimeoffset funcs*/
unsigned long (*do_gettimeoffset)(void) = null_gettimeoffset;

unsigned long null_gettimeoffset(void)
{
	return 0;
}

unsigned long fixed_rate_gettimeoffset(void)
{
	u32 count;
	unsigned long res;

	/* Get last timer tick in absolute kernel time */
	count = read_c0_count();

	/* .. relative to previous jiffy (32 bits is enough) */
	count -= timerlo;

	__asm__("multu	%1,%2"
		: "=h" (res)
		: "r" (count), "r" (sll32_usecs_per_cycle)
		: "lo", "accum");

	/*
	 * Due to possible jiffies inconsistencies, we need to check
	 * the result so that we'll get a timer that is monotonic.
	 */
	if (res >= USECS_PER_JIFFY)
		res = USECS_PER_JIFFY - 1;

	return res;
}

/*
 * Cached "1/(clocks per usec) * 2^32" value.
 * It has to be recalculated once each jiffy.
 */
static unsigned long cached_quotient;

/* Last jiffy when calibrate_divXX_gettimeoffset() was called. */
static unsigned long last_jiffies;


/*
 * This is copied from dec/time.c:do_ioasic_gettimeoffset() by Maciej.
 */
unsigned long calibrate_div32_gettimeoffset(void)
{
	u32 count;
	unsigned long res, tmp;
	unsigned long quotient;

	tmp = jiffies;

	quotient = cached_quotient;

	if (last_jiffies != tmp) {
		last_jiffies = tmp;
		if (last_jiffies != 0) {
			unsigned long r0;
			do_div64_32(r0, timerhi, timerlo, tmp);
			do_div64_32(quotient, USECS_PER_JIFFY,
				    USECS_PER_JIFFY_FRAC, r0);
			cached_quotient = quotient;
		}
	}

	/* Get last timer tick in absolute kernel time */
	count = read_c0_count();

	/* .. relative to previous jiffy (32 bits is enough) */
	count -= timerlo;

	__asm__("multu  %1,%2"
		: "=h" (res)
		: "r" (count), "r" (quotient)
		: "lo", "accum");

	/*
	 * Due to possible jiffies inconsistencies, we need to check
	 * the result so that we'll get a timer that is monotonic.
	 */
	if (res >= USECS_PER_JIFFY)
		res = USECS_PER_JIFFY - 1;

	return res;
}

unsigned long calibrate_div64_gettimeoffset(void)
{
	u32 count;
	unsigned long res, tmp;
	unsigned long quotient;

	tmp = jiffies;

	quotient = cached_quotient;

	if (tmp && last_jiffies != tmp) {
		last_jiffies = tmp;
		__asm__(".set	push\n\t"
			".set	noreorder\n\t"
			".set	noat\n\t"
			".set	mips3\n\t"
			"lwu	%0,%2\n\t"
			"dsll32	$1,%1,0\n\t"
			"or	$1,$1,%0\n\t"
			"ddivu	$0,$1,%3\n\t"
			"mflo	$1\n\t"
			"dsll32	%0,%4,0\n\t"
			"nop\n\t"
			"ddivu	$0,%0,$1\n\t"
			"mflo	%0\n\t"
			".set	pop"
			: "=&r" (quotient)
			: "r" (timerhi), "m" (timerlo),
			  "r" (tmp), "r" (USECS_PER_JIFFY));
		cached_quotient = quotient;
	}

	/* Get last timer tick in absolute kernel time */
	count = read_c0_count();

	/* .. relative to previous jiffy (32 bits is enough) */
	count -= timerlo;

	__asm__("multu	%1,%2"
		: "=h" (res)
		: "r" (count), "r" (quotient)
		: "lo", "accum");

	/*
	 * Due to possible jiffies inconsistencies, we need to check
	 * the result so that we'll get a timer that is monotonic.
	 */
	if (res >= USECS_PER_JIFFY)
		res = USECS_PER_JIFFY - 1;

	return res;
}


/*
 * local_timer_interrupt() does profiling and process accounting
 * on a per-CPU basis.
 *
 * In UP mode, it is invoked from the (global) timer_interrupt.
 *
 * In SMP mode, it might invoked by per-CPU timer interrupt, or
 * a broadcasted inter-processor interrupt which itself is triggered
 * by the global timer interrupt.
 */
void local_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	if (!user_mode(regs)) {
		if (prof_buffer && current->pid) {
			unsigned long pc = regs->cp0_epc;

			pc -= (unsigned long) _stext;
			pc >>= prof_shift;
			/*
			 * Dont ignore out-of-bounds pc values silently,
			 * put them into the last histogram slot, so if
			 * present, they will show up as a sharp peak.
			 */
			if (pc > prof_len - 1)
				pc = prof_len - 1;
			atomic_inc((atomic_t *)&prof_buffer[pc]);
		}
	}

#ifdef CONFIG_SMP
	/* in UP mode, update_process_times() is invoked by do_timer() */
	update_process_times(user_mode(regs));
#endif
}

/*
 * high-level timer interrupt service routines.  This function
 * is set as irqaction->handler and is invoked through do_IRQ.
 */
irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	if (cpu_has_counter) {
		unsigned int count;

		/* ack timer interrupt, and try to set next interrupt */
		expirelo += cycles_per_jiffy;
		write_c0_compare(expirelo);
		count = read_c0_count();

		/* check to see if we have missed any timer interrupts */
		if ((count - expirelo) < 0x7fffffff) {
			/* missed_timer_count++; */
			expirelo = count + cycles_per_jiffy;
			write_c0_compare(expirelo);
		}

		/* Update timerhi/timerlo for intra-jiffy calibration. */
		timerhi += count < timerlo;	/* Wrap around */
		timerlo = count;
	}

	/*
	 * call the generic timer interrupt handling
	 */
	do_timer(regs);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. rtc_set_time() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	write_seqlock(&xtime_lock);
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (rtc_set_mmss(xtime.tv_sec) == 0) {
			last_rtc_update = xtime.tv_sec;
		} else {
			/* do it again in 60 s */
			last_rtc_update = xtime.tv_sec - 600;
		}
	}
	write_sequnlock(&xtime_lock);

	/*
	 * If jiffies has overflowed in this timer_interrupt we must
	 * update the timer[hi]/[lo] to make fast gettimeoffset funcs
	 * quotient calc still valid. -arca
	 */
	if (!jiffies) {
		timerhi = timerlo = 0;
	}

#if !defined(CONFIG_SMP)
	/*
	 * In UP mode, we call local_timer_interrupt() to do profiling
	 * and process accouting.
	 *
	 * In SMP mode, local_timer_interrupt() is invoked by appropriate
	 * low-level local timer interrupt handler.
	 */
	local_timer_interrupt(irq, dev_id, regs);

#else	/* CONFIG_SMP */

	if (emulate_local_timer_interrupt) {
		/*
		 * this is the place where we send out inter-process
		 * interrupts and let each CPU do its own profiling
		 * and process accouting.
		 *
		 * Obviously we need to call local_timer_interrupt() for
		 * the current CPU too.
		 */
		panic("Not implemented yet!!!");
	}
#endif	/* CONFIG_SMP */

	return IRQ_HANDLED;
}

asmlinkage void ll_timer_interrupt(int irq, struct pt_regs *regs)
{
	irq_enter();
	kstat_this_cpu.irqs[irq]++;

	/* we keep interrupt disabled all the time */
	timer_interrupt(irq, NULL, regs);

	irq_exit();
}

asmlinkage void ll_local_timer_interrupt(int irq, struct pt_regs *regs)
{
	irq_enter();
	kstat_this_cpu.irqs[irq]++;

	/* we keep interrupt disabled all the time */
	local_timer_interrupt(irq, NULL, regs);

	irq_exit();
}

/*
 * time_init() - it does the following things.
 *
 * 1) board_time_init() -
 * 	a) (optional) set up RTC routines,
 *      b) (optional) calibrate and set the mips_counter_frequency
 *	    (only needed if you intended to use fixed_rate_gettimeoffset
 *	     or use cpu counter as timer interrupt source)
 * 2) setup xtime based on rtc_get_time().
 * 3) choose a appropriate gettimeoffset routine.
 * 4) calculate a couple of cached variables for later usage
 * 5) board_timer_setup() -
 *	a) (optional) over-write any choices made above by time_init().
 *	b) machine specific code should setup the timer irqaction.
 *	c) enable the timer interrupt
 */

void (*board_time_init)(void);
void (*board_timer_setup)(struct irqaction *irq);

unsigned int mips_counter_frequency;

static struct irqaction timer_irqaction = {
	.handler = timer_interrupt,
	.flags = SA_INTERRUPT,
	.name = "timer",
};

void __init time_init(void)
{
	if (board_time_init)
		board_time_init();

	if (!rtc_set_mmss)
		rtc_set_mmss = rtc_set_time;

	xtime.tv_sec = rtc_get_time();
	xtime.tv_nsec = 0;

	set_normalized_timespec(&wall_to_monotonic,
	                        -xtime.tv_sec, -xtime.tv_nsec);

	/* choose appropriate gettimeoffset routine */
	if (!cpu_has_counter) {
		/* no cpu counter - sorry */
		do_gettimeoffset = null_gettimeoffset;
	} else if (mips_counter_frequency != 0) {
		/* we have cpu counter and know counter frequency! */
		do_gettimeoffset = fixed_rate_gettimeoffset;
	} else if ((current_cpu_data.isa_level == MIPS_CPU_ISA_M32) ||
		   (current_cpu_data.isa_level == MIPS_CPU_ISA_I) ||
		   (current_cpu_data.isa_level == MIPS_CPU_ISA_II) ) {
		/* we need to calibrate the counter but we don't have
		 * 64-bit division. */
		do_gettimeoffset = calibrate_div32_gettimeoffset;
	} else {
		/* we need to calibrate the counter but we *do* have
		 * 64-bit division. */
		do_gettimeoffset = calibrate_div64_gettimeoffset;
	}

	/* caclulate cache parameters */
	if (mips_counter_frequency) {
		cycles_per_jiffy = mips_counter_frequency / HZ;

		/* sll32_usecs_per_cycle = 10^6 * 2^32 / mips_counter_freq */
		/* any better way to do this? */
		sll32_usecs_per_cycle = mips_counter_frequency / 100000;
		sll32_usecs_per_cycle = 0xffffffff / sll32_usecs_per_cycle;
		sll32_usecs_per_cycle *= 10;

		/*
		 * For those using cpu counter as timer,  this sets up the
		 * first interrupt
		 */
		write_c0_compare(cycles_per_jiffy);
		write_c0_count(0);
		expirelo = cycles_per_jiffy;
	}

	/*
	 * Call board specific timer interrupt setup.
	 *
	 * this pointer must be setup in machine setup routine.
	 *
	 * Even if the machine choose to use low-level timer interrupt,
	 * it still needs to setup the timer_irqaction.
	 * In that case, it might be better to set timer_irqaction.handler
	 * to be NULL function so that we are sure the high-level code
	 * is not invoked accidentally.
	 */
	board_timer_setup(&timer_irqaction);
}

#define FEBRUARY		2
#define STARTOFTIME		1970
#define SECDAY			86400L
#define SECYR			(SECDAY * 365)
#define leapyear(y)		((!((y) % 4) && ((y) % 100)) || !((y) % 400))
#define days_in_year(y)		(leapyear(y) ? 366 : 365)
#define days_in_month(m)	(month_days[(m) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

void to_tm(unsigned long tim, struct rtc_time *tm)
{
	long hms, day, gday;
	int i;

	gday = day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i - 1;		/* tm_mon starts from 0 to 11 */

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;

	/*
	 * Determine the day of week
	 */
	tm->tm_wday = (gday + 4) % 7;	/* 1970/1/1 was Thursday */
}

EXPORT_SYMBOL(rtc_lock);
EXPORT_SYMBOL(to_tm);
EXPORT_SYMBOL(rtc_set_time);
EXPORT_SYMBOL(rtc_get_time);
