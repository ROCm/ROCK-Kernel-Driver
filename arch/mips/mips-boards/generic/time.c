/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Setting up the clock on the MIPS boards.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <asm/mipsregs.h>
#include <asm/ptrace.h>
#include <asm/div64.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>

extern volatile unsigned long wall_jiffies;
static long last_rtc_update = 0;
unsigned long missed_heart_beats = 0;

static unsigned long r4k_offset; /* Amount to increment compare reg each time */
static unsigned long r4k_cur;    /* What counter should be at next timer irq */
extern rwlock_t xtime_lock;

#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)

#if defined(CONFIG_MIPS_ATLAS)
static char display_string[] = "        LINUX ON ATLAS       ";
#endif
#if defined(CONFIG_MIPS_MALTA)
static char display_string[] = "        LINUX ON MALTA       ";
#endif
static unsigned int display_count = 0;
#define MAX_DISPLAY_COUNT (sizeof(display_string) - 8) 

static unsigned int timer_tick_count=0;


static inline void ack_r4ktimer(unsigned long newval)
{
	write_32bit_cp0_register(CP0_COMPARE, newval);
}


/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 *
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you won't notice until after reboot!
 */
static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;

	save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		CMOS_WRITE(real_seconds,RTC_SECONDS);
		CMOS_WRITE(real_minutes,RTC_MINUTES);
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
 		retval = -1;
	}

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

	return retval;
}

/*
 * There are a lot of conceptually broken versions of the MIPS timer interrupt
 * handler floating around.  This one is rather different, but the algorithm
 * is provably more robust.
 */
void mips_timer_interrupt(struct pt_regs *regs)
{
	int irq = 7;

	if (r4k_offset == 0)
		goto null;

	do {
		kstat.irqs[0][irq]++;
		do_timer(regs);

		/* Historical comment/code:
 		 * RTC time of day s updated approx. every 11 
 		 * minutes.  Because of how the numbers work out 
 		 * we need to make absolutely sure we do this update
 		 * within 500ms before the * next second starts, 
 		 * thus the following code.
 		 */
		read_lock(&xtime_lock);
		if ((time_status & STA_UNSYNC) == 0 
		    && xtime.tv_sec > last_rtc_update + 660 
		    && xtime.tv_usec >= 500000 - (tick >> 1) 
		    && xtime.tv_usec <= 500000 + (tick >> 1))
			if (set_rtc_mmss(xtime.tv_sec) == 0)
				last_rtc_update = xtime.tv_sec;
			else
				/* do it again in 60 s */
	    			last_rtc_update = xtime.tv_sec - 600; 
		read_unlock(&xtime_lock);

		if ((timer_tick_count++ % HZ) == 0) {
		    mips_display_message(&display_string[display_count++]);
		    if (display_count == MAX_DISPLAY_COUNT)
		        display_count = 0;
		}

		r4k_cur += r4k_offset;
		ack_r4ktimer(r4k_cur);

	} while (((unsigned long)read_32bit_cp0_register(CP0_COUNT)
	         - r4k_cur) < 0x7fffffff);

	return;

null:
	ack_r4ktimer(0);
}

/* 
 * Figure out the r4k offset, the amount to increment the compare
 * register for each time tick. 
 * Use the RTC to calculate offset.
 */
static unsigned long __init cal_r4koff(void)
{
	unsigned long count;
	unsigned int flags;

	__save_and_cli(flags);

	/* Start counter exactly on falling edge of update flag */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	/* Start r4k counter. */
	write_32bit_cp0_register(CP0_COUNT, 0);

	/* Read counter exactly on falling edge of update flag */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	count = read_32bit_cp0_register(CP0_COUNT);

	/* restore interrupts */
	__restore_flags(flags);

	return (count / HZ);
}

static unsigned long __init get_mips_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned char save_control;

	save_control = CMOS_READ(RTC_CONTROL);

	/* Freeze it. */
	CMOS_WRITE(save_control | RTC_SET, RTC_CONTROL);

	/* Read regs. */
	sec = CMOS_READ(RTC_SECONDS);
	min = CMOS_READ(RTC_MINUTES);
	hour = CMOS_READ(RTC_HOURS);

	if (!(save_control & RTC_24H))
	{
		if ((hour & 0xf) == 0xc)
		        hour &= 0x80;
	        if (hour & 0x80)
		        hour = (hour & 0xf) + 12;     
	}
	day = CMOS_READ(RTC_DAY_OF_MONTH);
	mon = CMOS_READ(RTC_MONTH);
	year = CMOS_READ(RTC_YEAR);

	/* Unfreeze clock. */
	CMOS_WRITE(save_control, RTC_CONTROL);

	if ((year += 1900) < 1970)
	        year += 100;

	return mktime(year, mon, day, hour, min, sec);
}

void __init time_init(void)
{
        unsigned int est_freq, flags;

        /* Set Data mode - binary. */ 
        CMOS_WRITE(CMOS_READ(RTC_CONTROL) | RTC_DM_BINARY, RTC_CONTROL);

	printk("calculating r4koff... ");
	r4k_offset = cal_r4koff();
	printk("%08lx(%d)\n", r4k_offset, (int) r4k_offset);

	est_freq = 2*r4k_offset*HZ;	
	est_freq += 5000;    /* round */
	est_freq -= est_freq%10000;
	printk("CPU frequency %d.%02d MHz\n", est_freq/1000000, 
	       (est_freq%1000000)*100/1000000);
	r4k_cur = (read_32bit_cp0_register(CP0_COUNT) + r4k_offset);

	write_32bit_cp0_register(CP0_COMPARE, r4k_cur);
	change_cp0_status(ST0_IM, ALLINTS);

	/* Read time from the RTC chipset. */
	write_lock_irqsave (&xtime_lock, flags);
	xtime.tv_sec = get_mips_time();
	xtime.tv_usec = 0;
	write_unlock_irqrestore(&xtime_lock, flags);
}

/* This is for machines which generate the exact clock. */
#define USECS_PER_JIFFY (1000000/HZ)
#define USECS_PER_JIFFY_FRAC ((1000000 << 32) / HZ & 0xffffffff)

/* Cycle counter value at the previous timer interrupt.. */

static unsigned int timerhi = 0, timerlo = 0;

/*
 * FIXME: Does playing with the RP bit in c0_status interfere with this code?
 */
static unsigned long do_fast_gettimeoffset(void)
{
	u32 count;
	unsigned long res, tmp;

	/* Last jiffy when do_fast_gettimeoffset() was called. */
	static unsigned long last_jiffies=0;
	unsigned long quotient;

	/*
	 * Cached "1/(clocks per usec)*2^32" value.
	 * It has to be recalculated once each jiffy.
	 */
	static unsigned long cached_quotient=0;

	tmp = jiffies;

	quotient = cached_quotient;

	if (tmp && last_jiffies != tmp) {
		last_jiffies = tmp;
#ifdef CONFIG_CPU_MIPS32
		if (last_jiffies != 0) {
			unsigned long r0;
			do_div64_32(r0, timerhi, timerlo, tmp);
			do_div64_32(quotient, USECS_PER_JIFFY,
				    USECS_PER_JIFFY_FRAC, r0);
			cached_quotient = quotient;
		}
#else
		__asm__(".set\tnoreorder\n\t"
			".set\tnoat\n\t"
			".set\tmips3\n\t"
			"lwu\t%0,%2\n\t"
			"dsll32\t$1,%1,0\n\t"
			"or\t$1,$1,%0\n\t"
			"ddivu\t$0,$1,%3\n\t"
			"mflo\t$1\n\t"
			"dsll32\t%0,%4,0\n\t"
			"nop\n\t"
			"ddivu\t$0,%0,$1\n\t"
			"mflo\t%0\n\t"
			".set\tmips0\n\t"
			".set\tat\n\t"
			".set\treorder"
			:"=&r" (quotient)
			:"r" (timerhi),
			 "m" (timerlo),
			 "r" (tmp),
			 "r" (USECS_PER_JIFFY)
			:"$1");
		cached_quotient = quotient;
#endif
	}

	/* Get last timer tick in absolute kernel time */
	count = read_32bit_cp0_register(CP0_COUNT);

	/* .. relative to previous jiffy (32 bits is enough) */
	count -= timerlo;

	__asm__("multu\t%1,%2\n\t"
		"mfhi\t%0"
		:"=r" (res)
		:"r" (count),
		 "r" (quotient));

	/*
 	 * Due to possible jiffies inconsistencies, we need to check 
	 * the result so that we'll get a timer that is monotonic.
	 */
	if (res >= USECS_PER_JIFFY)
		res = USECS_PER_JIFFY-1;

	return res;
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned int flags;

	read_lock_irqsave (&xtime_lock, flags);
	*tv = xtime;
	tv->tv_usec += do_fast_gettimeoffset();

	/*
	 * xtime is atomically updated in timer_bh. jiffies - wall_jiffies
	 * is nonzero if the timer bottom half hasnt executed yet.
	 */
	if (jiffies - wall_jiffies)
		tv->tv_usec += USECS_PER_JIFFY;

	read_unlock_irqrestore (&xtime_lock, flags);

	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
}

void do_settimeofday(struct timeval *tv)
{
	write_lock_irq (&xtime_lock);

	/* This is revolting. We need to set the xtime.tv_usec correctly.
	 * However, the value in this location is is value at the last tick.
	 * Discover what correction gettimeofday would have done, and then
	 * undo it!
	 */
	tv->tv_usec -= do_fast_gettimeoffset();

	if (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;

	write_unlock_irq (&xtime_lock);
}
