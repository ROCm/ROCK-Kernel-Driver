/*
 *  linux/arch/x86-64/kernel/time.c
 *
 *  "High Precision Event Timer" based timekeeping.
 *
 *  Copyright (c) 1991,1992,1995  Linus Torvalds
 *  Copyright (c) 1994  Alan Modra
 *  Copyright (c) 1995  Markus Kuhn
 *  Copyright (c) 1996  Ingo Molnar
 *  Copyright (c) 1998  Andrea Arcangeli
 *  Copyright (c) 2002  Vojtech Pavlik
 *  Copyright (c) 2003  Andi Kleen
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/irq.h>
#include <linux/time.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/bcd.h>
#include <asm/vsyscall.h>
#include <asm/timex.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/apic.h>
#endif

u64 jiffies_64 = INITIAL_JIFFIES;

extern int using_apic_timer;

spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;

extern int using_apic_timer;
extern void smp_local_timer_interrupt(struct pt_regs * regs);


unsigned int cpu_khz;					/* TSC clocks / usec, not used here */
unsigned long hpet_period;				/* fsecs / HPET clock */
unsigned long hpet_tick;				/* HPET clocks / interrupt */
int hpet_report_lost_ticks;				/* command line option */

struct hpet_data __hpet __section_hpet;			/* address, quotient, trigger, hz */

volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;
unsigned long __wall_jiffies __section_wall_jiffies = INITIAL_JIFFIES;
struct timespec __xtime __section_xtime;
struct timezone __sys_tz __section_sys_tz;

/*
 * do_gettimeoffset() returns microseconds since last timer interrupt was
 * triggered by hardware. A memory read of HPET is slower than a register read
 * of TSC, but much more reliable. It's also synchronized to the timer
 * interrupt. Note that do_gettimeoffset() may return more than hpet_tick, if a
 * timer interrupt has happened already, but hpet.trigger wasn't updated yet.
 * This is not a problem, because jiffies hasn't updated either. They are bound
 * together by xtime_lock.
         */

inline unsigned int do_gettimeoffset(void)
{
	unsigned long t;
	sync_core();
	rdtscll(t);	
	return (t  - hpet.last_tsc) * (1000000L / HZ) / hpet.ticks + hpet.offset;
}

/*
 * This version of gettimeofday() has microsecond resolution and better than
 * microsecond precision, as we're using at least a 10 MHz (usually 14.31818
 * MHz) HPET timer.
 */

void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq, t;
 	unsigned int sec, usec;

	do {
		seq = read_seqbegin(&xtime_lock);

		sec = xtime.tv_sec;
		usec = xtime.tv_nsec / 1000;

		t = (jiffies - wall_jiffies) * (1000000L / HZ) + do_gettimeoffset();
		usec += t;

	} while (read_seqretry(&xtime_lock, seq));

	tv->tv_sec = sec + usec / 1000000;
	tv->tv_usec = usec % 1000000;
}

/*
 * settimeofday() first undoes the correction that gettimeofday would do
 * on the time, and then saves it. This is ugly, but has been like this for
 * ages already.
 */

void do_settimeofday(struct timeval *tv)
{
	write_seqlock_irq(&xtime_lock);

	tv->tv_usec -= do_gettimeoffset() +
		(jiffies - wall_jiffies) * tick_usec;

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

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be called 500
 * ms after the second nowtime has started, because when nowtime is written
 * into the registers of the CMOS clock, it will jump to the next second
 * precisely 500 ms later. Check the Motorola MC146818A or Dallas DS12887 data
 * sheet for details.
 */

static void set_rtc_mmss(unsigned long nowtime)
{
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char control, freq_select;

/*
 * IRQs are disabled when we're called from the timer interrupt,
 * no need for spin_lock_irqsave()
 */

	spin_lock(&rtc_lock);

/*
 * Tell the clock it's being set and stop it.
 */

	control = CMOS_READ(RTC_CONTROL);
	CMOS_WRITE(control | RTC_SET, RTC_CONTROL);

	freq_select = CMOS_READ(RTC_FREQ_SELECT);
	CMOS_WRITE(freq_select | RTC_DIV_RESET2, RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);
		BCD_TO_BIN(cmos_minutes);

/*
 * since we're only adjusting minutes and seconds, don't interfere with hour
 * overflow. This avoids messing with unknown time zones but requires your RTC
 * not to be off by more than 15 minutes. Since we're calling it only when
 * our clock is externally synchronized using NTP, this shouldn't be a problem.
	 */

	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15) / 30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
			BIN_TO_BCD(real_seconds);
			BIN_TO_BCD(real_minutes);
		CMOS_WRITE(real_seconds, RTC_SECONDS);
		CMOS_WRITE(real_minutes, RTC_MINUTES);
	} else
		printk(KERN_WARNING "time.c: can't update CMOS clock from %d to %d\n",
		       cmos_minutes, real_minutes);

/*
 * The following flags have to be released exactly in this order, otherwise the
 * DS12887 (popular MC146818A clone with integrated battery and quartz) will
 * not reset the oscillator and will not update precisely 500 ms later. You
 * won't find this mentioned in the Dallas Semiconductor data sheets, but who
 * believes data sheets anyway ... -- Markus Kuhn
 */

	CMOS_WRITE(control, RTC_CONTROL);
	CMOS_WRITE(freq_select, RTC_FREQ_SELECT);

	spin_unlock(&rtc_lock);
}

static irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	static unsigned long rtc_update = 0;

/*
 * Here we are in the timer irq handler. We have irqs locally disabled (so we
 * don't need spin_lock_irqsave()) but we don't know if the timer_bh is running
 * on the other CPU, so we need a lock. We also need to lock the vsyscall
 * variables, because both do_timer() and us change them -arca+vojtech
	 */

	write_seqlock(&xtime_lock);

	{
		unsigned long t;

		sync_core();
		rdtscll(t);
		hpet.offset = (t  - hpet.last_tsc) * (1000000L / HZ) / hpet.ticks + hpet.offset - 1000000L / HZ;
		if (hpet.offset >= 1000000L / HZ)
			hpet.offset = 0;
		hpet.ticks = min_t(long, max_t(long, (t  - hpet.last_tsc) * (1000000L / HZ) / (1000000L / HZ - hpet.offset),
				cpu_khz * 1000/HZ * 15 / 16), cpu_khz * 1000/HZ * 16 / 15); 
		hpet.last_tsc = t;
	}

/*
 * Do the timer stuff.
 */

	do_timer(regs);

/*
 * In the SMP case we use the local APIC timer interrupt to do the profiling,
 * except when we simulate SMP mode on a uniprocessor system, in that case we
 * have to call the local interrupt handler.
 */

#ifndef CONFIG_X86_LOCAL_APIC
	x86_do_profile(regs);
#else
	if (!using_apic_timer)
		smp_local_timer_interrupt(regs);
#endif

/*
 * If we have an externally synchronized Linux clock, then update CMOS clock
 * accordingly every ~11 minutes. set_rtc_mmss() will be called in the jiffy
 * closest to exactly 500 ms before the next second. If the update fails, we
 * don'tcare, as it'll be updated on the next turn, and the problem (time way
 * off) isn't likely to go away much sooner anyway.
 */

	if ((~time_status & STA_UNSYNC) && xtime.tv_sec > rtc_update &&
		abs(xtime.tv_nsec - 500000000) <= tick_nsec / 2) {
		set_rtc_mmss(xtime.tv_sec);
		rtc_update = xtime.tv_sec + 660;
	}
 
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

unsigned long get_cmos_time(void)
{
	unsigned int timeout, year, mon, day, hour, min, sec;
	unsigned char last, this;

/*
 * The Linux interpretation of the CMOS clock register contents: When the
 * Update-In-Progress (UIP) flag goes from 1 to 0, the RTC registers show the
 * second which has precisely just started. Waiting for this can take up to 1
 * second, we timeout approximately after 2.4 seconds on a machine with
 * standard 8.3 MHz ISA bus.
 */

	spin_lock(&rtc_lock); 

	timeout = 1000000;
	last = this = 0;

	while (timeout && last && !this) {
		last = this;
		this = CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP;
		timeout--;
	}

/*
 * Here we are safe to assume the registers won't change for a whole second, so
 * we just go ahead and read them.
	 */

		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);

	spin_unlock(&rtc_lock);

/*
 * We know that x86-64 always uses BCD format, no need to check the config
 * register.
 */

	    BCD_TO_BIN(sec);
	    BCD_TO_BIN(min);
	    BCD_TO_BIN(hour);
	    BCD_TO_BIN(day);
	    BCD_TO_BIN(mon);
	    BCD_TO_BIN(year);

/*
 * This will work up to Dec 31, 2069.
 */

	if ((year += 1900) < 1970)
		year += 100;

	return mktime(year, mon, day, hour, min, sec);
}

/*
 * calibrate_tsc() calibrates the processor TSC in a very simple way, comparing
 * it to the HPET timer of known frequency.
 */

#define TICK_COUNT 100000000

/*
 * pit_calibrate_tsc() uses the speaker output (channel 2) of
 * the PIT. This is better than using the timer interrupt output,
 * because we can read the value of the speaker with just one inb(),
 * where we need three i/o operations for the interrupt channel.
 * We count how many ticks the TSC does in 50 ms.
 */

static unsigned int __init pit_calibrate_tsc(void)
{
	unsigned long start, end;
	unsigned long flags;

	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	local_irq_save(flags);
	local_irq_disable();

	outb(0xb0, 0x43);
	outb((1193182 / (1000 / 50)) & 0xff, 0x42);
	outb((1193182 / (1000 / 50)) >> 8, 0x42);
	rdtscll(start);
	sync_core();
	while ((inb(0x61) & 0x20) == 0);
	sync_core();
	rdtscll(end);


	local_irq_restore(flags);
	
	return (end - start) / 50;
}

void __init pit_init(void)
{
	outb_p(0x34, 0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff, 0x40);	/* LSB */
	outb_p(LATCH >> 8, 0x40);	/* MSB */
}

int __init time_setup(char *str)
{
	hpet_report_lost_ticks = 1;
	return 1;
}

static struct irqaction irq0 = { timer_interrupt, SA_INTERRUPT, 0, "timer", NULL, NULL};

extern void __init config_acpi_tables(void);

void __init time_init(void)
{
	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = 0;

	pit_init();
	printk(KERN_INFO "time.c: Using 1.1931816 MHz PIT timer.\n");
	cpu_khz = pit_calibrate_tsc();
	printk(KERN_INFO "time.c: Detected %d.%03d MHz processor.\n",
		cpu_khz / 1000, cpu_khz % 1000);
	hpet.ticks = cpu_khz * (1000 / HZ);
	rdtscll(hpet.last_tsc);
	setup_irq(0, &irq0);
}

__setup("report_lost_ticks", time_setup);
