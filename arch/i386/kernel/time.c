/*
 *  linux/arch/i386/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file contains the PC-specific time handling details:
 * reading the RTC at bootup, etc..
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 * 1996-05-03    Ingo Molnar
 *      fixed time warps in do_[slow|fast]_gettimeoffset()
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 * 1998-09-05    (Various)
 *	More robust do_fast_gettimeoffset() algorithm implemented
 *	(works with APM, Cyrix 6x86MX and Centaur C6),
 *	monotonic gettimeofday() with fast_get_timeoffset(),
 *	drift-proof precision TSC calibration on boot
 *	(C. Scott Ananian <cananian@alumni.princeton.edu>, Andrew D.
 *	Balsa <andrebalsa@altern.org>, Philip Gladstone <philip@raptor.com>;
 *	ported from 2.0.35 Jumbo-9 by Michael Krause <m.krause@tu-harburg.de>).
 * 1998-12-16    Andrea Arcangeli
 *	Fixed Jumbo-9 code in 2.1.131: do_gettimeofday was missing 1 jiffy
 *	because was not accounting lost_ticks.
 * 1998-12-24 Copyright (C) 1998  Andrea Arcangeli
 *	Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *	serialize accesses to xtime/lost_ticks).
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/bcd.h>

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/msr.h>
#include <asm/delay.h>
#include <asm/mpspec.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/timer.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>
#include <linux/config.h>

#include <asm/arch_hooks.h>

extern spinlock_t i8259A_lock;
int pit_latch_buggy;              /* extern */

#include "do_timer.h"

u64 jiffies_64 = INITIAL_JIFFIES;

unsigned long cpu_khz;	/* Detected as we calibrate the TSC */

extern unsigned long wall_jiffies;

spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;

spinlock_t i8253_lock = SPIN_LOCK_UNLOCKED;
EXPORT_SYMBOL(i8253_lock);

extern struct timer_opts timer_none;
struct timer_opts* timer = &timer_none;

/*
 * This version of gettimeofday has microsecond resolution
 * and better than microsecond precision on fast x86 machines with TSC.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin(&xtime_lock);

		usec = timer->get_offset();
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

void do_settimeofday(struct timeval *tv)
{
	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	tv->tv_usec -= timer->get_offset();
	tv->tv_usec -= (jiffies - wall_jiffies) * (1000000 / HZ);

	while (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime.tv_sec = tv->tv_sec;
	xtime.tv_nsec = (tv->tv_usec * 1000);
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
}

/* monotonic_clock(): returns # of nanoseconds passed since time_init()
 *		Note: This function is required to return accurate
 *		time even in the absence of multiple timer ticks.
 */
unsigned long long monotonic_clock(void)
{
	return timer->monotonic_clock();
}
EXPORT_SYMBOL(monotonic_clock);


/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 *
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you'll only notice that after reboot!
 */
static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;

	/* gets recalled with irq locally disabled */
	spin_lock(&rtc_lock);
	save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
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
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			BIN_TO_BCD(real_seconds);
			BIN_TO_BCD(real_minutes);
		}
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
	spin_unlock(&rtc_lock);

	return retval;
}

/* last time the cmos clock got updated */
static long last_rtc_update;

int timer_ack;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void do_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
#ifdef CONFIG_X86_IO_APIC
	if (timer_ack) {
		/*
		 * Subtle, when I/O APICs are used we have to ack timer IRQ
		 * manually to reset the IRR bit for do_slow_gettimeoffset().
		 * This will also deassert NMI lines for the watchdog if run
		 * on an 82489DX-based system.
		 */
		spin_lock(&i8259A_lock);
		outb(0x0c, 0x20);
		/* Ack the IRQ; AEOI will end it automatically. */
		inb(0x20);
		spin_unlock(&i8259A_lock);
	}
#endif

	do_timer_interrupt_hook(regs);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}
	    
#ifdef CONFIG_MCA
	if( MCA_bus ) {
		/* The PS/2 uses level-triggered interrupts.  You can't
		turn them off, nor would you want to (any attempt to
		enable edge-triggered interrupts usually gets intercepted by a
		special hardware circuit).  Hence we have to acknowledge
		the timer interrupt.  Through some incredibly stupid
		design idea, the reset for IRQ 0 is done by setting the
		high bit of the PPI port B (0x61).  Note that some PS/2s,
		notably the 55SX, work fine if this is removed.  */

		irq = inb_p( 0x61 );	/* read the current state */
		outb_p( irq|0x80, 0x61 );	/* reset the IRQ */
	}
#endif
}

/*
 * Lost tick detection and compensation
 */
static inline void detect_lost_tick(void)
{
	/* read time since last interrupt */
	unsigned long delta = timer->get_offset();
	static unsigned long dbg_print;
	
	/* check if delta is greater then two ticks */
	if(delta >= 2*(1000000/HZ)){

		/*
		 * only print debug info first 5 times
		 */
		/*
		 * AKPM: disable this for now; it's nice, but irritating.
		 */
		if (0 && dbg_print < 5) {
			printk(KERN_WARNING "\nWarning! Detected %lu "
				"micro-second gap between interrupts.\n",
				delta);
			printk(KERN_WARNING "  Compensating for %lu lost "
				"ticks.\n",
				delta/(1000000/HZ)-1);
			dump_stack();
			dbg_print++;
		}
		/* calculate number of missed ticks */
		delta = delta/(1000000/HZ)-1;
		jiffies += delta;
	}
		
}

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);

	detect_lost_tick();
	timer->mark_offset();
 
	do_timer_interrupt(irq, NULL, regs);

	write_sequnlock(&xtime_lock);

}

/* not static: needed by APM */
unsigned long get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	int i;

	spin_lock(&rtc_lock);
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
	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	  {
	    BCD_TO_BIN(sec);
	    BCD_TO_BIN(min);
	    BCD_TO_BIN(hour);
	    BCD_TO_BIN(day);
	    BCD_TO_BIN(mon);
	    BCD_TO_BIN(year);
	  }
	spin_unlock(&rtc_lock);
	if ((year += 1900) < 1970)
		year += 100;
	return mktime(year, mon, day, hour, min, sec);
}

/* XXX this driverfs stuff should probably go elsewhere later -john */
static struct sys_device device_i8253 = {
	.name		= "rtc",
	.id		= 0,
	.dev	= {
		.name	= "i8253 Real Time Clock",
	},
};

static int time_init_device(void)
{
	return sys_device_register(&device_i8253);
}

device_initcall(time_init_device);


void __init time_init(void)
{
	
	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = 0;


	timer = select_timer();
	time_init_hook();
}
