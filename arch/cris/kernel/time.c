/* $Id: time.c,v 1.9 2003/07/04 08:27:52 starvik Exp $
 *
 *  linux/arch/cris/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Copyright (C) 1999, 2000, 2001 Axis Communications AB
 *
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 * 1996-05-03    Ingo Molnar
 *      fixed time warps in do_[slow|fast]_gettimeoffset()
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 *
 * Linux/CRIS specific code:
 *
 * Authors:    Bjorn Wesen
 *             Johan Adolfsson  
 *
 */

#include <asm/rtc.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/bcd.h>
#include <linux/timex.h>

u64 jiffies_64 = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

int have_rtc;  /* used to remember if we have an RTC or not */;

#define TICK_SIZE tick

extern unsigned long wall_jiffies;

extern unsigned long do_slow_gettimeoffset(void);
static unsigned long (*do_gettimeoffset)(void) = do_slow_gettimeoffset;

/*
 * This version of gettimeofday has near microsecond resolution.
 *
 * Note: Division is quite slow on CRIS and do_gettimeofday is called
 *       rather often. Maybe we should do some kind of approximation here
 *       (a naive approximation would be to divide by 1024).
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	signed long usec, sec;
	local_irq_save(flags);
	local_irq_disable();
	usec = do_gettimeoffset();
	{
		unsigned long lost = jiffies - wall_jiffies;
		if (lost)
			usec += lost * (1000000 / HZ);
	}
	sec = xtime.tv_sec;
	usec += xtime.tv_nsec / 1000;
	local_irq_restore(flags);

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
	unsigned long flags;

        if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	local_irq_save(flags);
	local_irq_disable();

	/* This is revolting. We need to set the xtime.tv_usec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
	tv->tv_nsec -= do_gettimeoffset() * 1000;
        tv->tv_nsec -= (jiffies - wall_jiffies) * TICK_NSEC;

	while (tv->tv_nsec < 0) {
		tv->tv_nsec += NSEC_PER_SEC;
		tv->tv_sec--;
	}
	xtime.tv_sec = tv->tv_sec;
	xtime.tv_nsec = tv->tv_nsec;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_state = TIME_ERROR;	/* p. 24, (a) */
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	local_irq_restore(flags);
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);


/*
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you'll only notice that after reboot!
 */

int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;

	printk("set_rtc_mmss(%lu)\n", nowtime);

	if(!have_rtc)
		return 0;

	cmos_minutes = CMOS_READ(RTC_MINUTES);
	BCD_TO_BIN(cmos_minutes);

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
		BIN_TO_BCD(real_seconds);
		BIN_TO_BCD(real_minutes);
		CMOS_WRITE(real_seconds,RTC_SECONDS);
		CMOS_WRITE(real_minutes,RTC_MINUTES);
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	return retval;
}

/* grab the time from the RTC chip */

unsigned long
get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;

	sec = CMOS_READ(RTC_SECONDS);
	min = CMOS_READ(RTC_MINUTES);
	hour = CMOS_READ(RTC_HOURS);
	day = CMOS_READ(RTC_DAY_OF_MONTH);
	mon = CMOS_READ(RTC_MONTH);
	year = CMOS_READ(RTC_YEAR);

	printk("rtc: sec 0x%x min 0x%x hour 0x%x day 0x%x mon 0x%x year 0x%x\n", 
	       sec, min, hour, day, mon, year);

	BCD_TO_BIN(sec);
	BCD_TO_BIN(min);
	BCD_TO_BIN(hour);
	BCD_TO_BIN(day);
	BCD_TO_BIN(mon);
	BCD_TO_BIN(year);

	if ((year += 1900) < 1970)
		year += 100;

	return mktime(year, mon, day, hour, min, sec);
}

/* update xtime from the CMOS settings. used when /dev/rtc gets a SET_TIME.
 * TODO: this doesn't reset the fancy NTP phase stuff as do_settimeofday does.
 */

void
update_xtime_from_cmos(void)
{
	if(have_rtc) {
		xtime.tv_sec = get_cmos_time();
		xtime.tv_nsec = 0;
	}
}
