/*
 * IP32 timer calibration
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Keith M Wesolowski
 */
#include <linux/bcd.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/timex.h>

#include <asm/mipsregs.h>
#include <asm/param.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/ip32_ints.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/io.h>
#include <asm/irq.h>

extern volatile unsigned long wall_jiffies;

u32 cc_interval;

/* Cycle counter value at the previous timer interrupt.. */
static unsigned int timerhi, timerlo;

/* An arbitrary time; this can be decreased if reliability looks good */
#define WAIT_MS 10
#define PER_MHZ (1000000 / 2 / HZ)
/*
 * Change this if you have some constant time drift
 */
#define USECS_PER_JIFFY (1000000/HZ)


static irqreturn_t cc_timer_interrupt(int irq, void *dev_id, struct pt_regs * regs);

void __init ip32_timer_setup (struct irqaction *irq)
{
	u64 crime_time;
	u32 cc_tick;


	write_c0_count(0);
	irq->handler = cc_timer_interrupt;

	printk("Calibrating system timer... ");

	crime_time = crime_read_64(CRIME_TIME) & CRIME_TIME_MASK;
	cc_tick = read_c0_count();

	while ((crime_read_64 (CRIME_TIME) & CRIME_TIME_MASK) - crime_time
		< WAIT_MS * 1000000 / CRIME_NS_PER_TICK)
		;
	cc_tick = read_c0_count() - cc_tick;
	cc_interval = cc_tick / HZ * (1000 / WAIT_MS);
	/*
	 * The round-off seems unnecessary; in testing, the error of the
	 * above procedure is < 100 ticks, which means it gets filtered
	 * out by the HZ adjustment.
	 */
	cc_interval = (cc_interval / PER_MHZ) * PER_MHZ;

	printk("%d MHz CPU detected\n", (int) (cc_interval / PER_MHZ));

	setup_irq (CLOCK_IRQ, irq);
#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)
	/* Set ourselves up for future interrupts */
	write_c0_compare(read_c0_count() + cc_interval);
        change_c0_status(ST0_IM, ALLINTS);
	local_irq_enable();
}

static irqreturn_t cc_timer_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	u32 count;

	/*
	 * The cycle counter is only 32 bit which is good for about
	 * a minute at current count rates of upto 150MHz or so.
	 */
	count = read_c0_count();
	timerhi += (count < timerlo);	/* Wrap around */
	timerlo = count;

	write_c0_compare((u32) (count + cc_interval));
	kstat_this_cpu.irqs[irq]++;
	do_timer(regs);

	if (!jiffies) {
		/*
		 * If jiffies has overflowed in this timer_interrupt we must
		 * update the timer[hi]/[lo] to make do_fast_gettimeoffset()
		 * quotient calc still valid. -arca
		 */
		timerhi = timerlo = 0;
	}
	return IRQ_HANDLED;
}

void __init ip32_time_init(void)
{
	unsigned int epoch = 0, year, mon, day, hour, min, sec;
	int i;

	/* The Linux interpretation of the CMOS clock register contents:
	 * When the Update-In-Progress (UIP) flag goes from 1 to 0, the
	 * RTC registers show the second which has precisely just started.
	 * Let's hope other operating systems interpret the RTC the same way.
	 */
	/* read RTC exactly on falling edge of update flag */
	for (i = 0 ; i < 1000000 ; i++)	/* may take up to 1 second... */
		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP)
			break;
	for (i = 0 ; i < 1000000 ; i++)	/* must try at least 2.228 ms */
		if (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP))
			break;
	do { /* Isn't this overkill ? UIP above should guarantee consistency */
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);
	} while (sec != CMOS_READ(RTC_SECONDS));
	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		sec = BCD2BIN(sec);
		min = BCD2BIN(min);
		hour = BCD2BIN(hour);
		day = BCD2BIN(day);
		mon = BCD2BIN(mon);
		year = BCD2BIN(year);
	}

	/* Attempt to guess the epoch.  This is the same heuristic as in
	 * rtc.c so no stupid things will happen to timekeeping.  Who knows,
	 * maybe Ultrix also uses 1952 as epoch ...
	 */
	if (year > 10 && year < 44)
		epoch = 1980;
	else if (year < 96)
		epoch = 1952;
	year += epoch;

	write_seqlock_irq(&xtime_lock);
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_nsec = 0;
	write_sequnlock_irq(&xtime_lock);
}
