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
#include <asm/pgtable.h>
#include <asm/vsyscall.h>
#include <asm/timex.h>
#include <asm/proto.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/apic.h>
#endif

u64 jiffies_64 = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

extern int using_apic_timer;

spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;
spinlock_t i8253_lock = SPIN_LOCK_UNLOCKED;

#undef HPET_HACK_ENABLE_DANGEROUS


unsigned int cpu_khz;					/* TSC clocks / usec, not used here */
unsigned long hpet_period;				/* fsecs / HPET clock */
unsigned long hpet_tick;				/* HPET clocks / interrupt */
unsigned long vxtime_hz = 1193182;
int report_lost_ticks;				/* command line option */
unsigned long long monotonic_base;

struct vxtime_data __vxtime __section_vxtime;	/* for vsyscalls */

volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;
unsigned long __wall_jiffies __section_wall_jiffies = INITIAL_JIFFIES;
struct timespec __xtime __section_xtime;
struct timezone __sys_tz __section_sys_tz;

static inline void rdtscll_sync(unsigned long *tsc)
{
#ifdef CONFIG_SMP
	sync_core();
#endif
	rdtscll(*tsc);
}

/*
 * do_gettimeoffset() returns microseconds since last timer interrupt was
 * triggered by hardware. A memory read of HPET is slower than a register read
 * of TSC, but much more reliable. It's also synchronized to the timer
 * interrupt. Note that do_gettimeoffset() may return more than hpet_tick, if a
 * timer interrupt has happened already, but vxtime.trigger wasn't updated yet.
 * This is not a problem, because jiffies hasn't updated either. They are bound
 * together by xtime_lock.
         */

static inline unsigned int do_gettimeoffset_tsc(void)
{
	unsigned long t;
	unsigned long x;
	rdtscll_sync(&t);
	if (t < vxtime.last_tsc) t = vxtime.last_tsc; /* hack */
	x = ((t - vxtime.last_tsc) * vxtime.tsc_quot) >> 32;
	return x;
}

static inline unsigned int do_gettimeoffset_hpet(void)
{
	return ((hpet_readl(HPET_COUNTER) - vxtime.last) * vxtime.quot) >> 32;
}

unsigned int (*do_gettimeoffset)(void) = do_gettimeoffset_tsc;

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

		/*
		 * If time_adjust is negative then NTP is slowing the clock
		 * so make sure not to go into next possible interval.
		 * Better to lose some accuracy than have time go backwards..
		 */
		if (unlikely(time_adjust < 0) && usec > tickadj)
			usec = tickadj;

		t = (jiffies - wall_jiffies) * (1000000L / HZ) +
			do_gettimeoffset();
		usec += t;

	} while (read_seqretry(&xtime_lock, seq));

	tv->tv_sec = sec + usec / 1000000;
	tv->tv_usec = usec % 1000000;
}

EXPORT_SYMBOL(do_gettimeofday);

/*
 * settimeofday() first undoes the correction that gettimeofday would do
 * on the time, and then saves it. This is ugly, but has been like this for
 * ages already.
 */

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);

	nsec -= do_gettimeoffset() * 1000 +
		(jiffies - wall_jiffies) * (NSEC_PER_SEC/HZ);

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;

	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

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
		printk(KERN_WARNING "time.c: can't update CMOS clock "
		       "from %d to %d\n", cmos_minutes, real_minutes);

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


/* monotonic_clock(): returns # of nanoseconds passed since time_init()
 *		Note: This function is required to return accurate
 *		time even in the absence of multiple timer ticks.
 */
unsigned long long monotonic_clock(void)
{
	unsigned long seq;
 	u32 last_offset, this_offset, offset;
	unsigned long long base;

	if (vxtime.mode == VXTIME_HPET) {
		do {
			seq = read_seqbegin(&xtime_lock);

			last_offset = vxtime.last;
			base = monotonic_base;
			this_offset = hpet_readl(HPET_T0_CMP) - hpet_tick;

		} while (read_seqretry(&xtime_lock, seq));
		offset = (this_offset - last_offset);
		offset *=(NSEC_PER_SEC/HZ)/hpet_tick;
		return base + offset;
	}else{
		do {
			seq = read_seqbegin(&xtime_lock);

			last_offset = vxtime.last_tsc;
			base = monotonic_base;
		} while (read_seqretry(&xtime_lock, seq));
		sync_core();
		rdtscll(this_offset);
		offset = (this_offset - last_offset)*1000/cpu_khz; 
		return base + offset;
	}


}
EXPORT_SYMBOL(monotonic_clock);


static irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	static unsigned long rtc_update = 0;
	unsigned long tsc, lost = 0;
	int delay, offset = 0;

/*
 * Here we are in the timer irq handler. We have irqs locally disabled (so we
 * don't need spin_lock_irqsave()) but we don't know if the timer_bh is running
 * on the other CPU, so we need a lock. We also need to lock the vsyscall
 * variables, because both do_timer() and us change them -arca+vojtech
	 */

	write_seqlock(&xtime_lock);

	if (vxtime.hpet_address) {
		offset = hpet_readl(HPET_T0_CMP) - hpet_tick;
		delay = hpet_readl(HPET_COUNTER) - offset;
	} else {
		spin_lock(&i8253_lock);
		outb_p(0x00, 0x43);
		delay = inb_p(0x40);
		delay |= inb(0x40) << 8;
		spin_unlock(&i8253_lock);
		delay = LATCH - 1 - delay;
	}

	rdtscll_sync(&tsc);

	if (vxtime.mode == VXTIME_HPET) {
		if (offset - vxtime.last > hpet_tick) {
			lost = (offset - vxtime.last) / hpet_tick - 1;
		}

		monotonic_base += 
			(offset - vxtime.last)*(NSEC_PER_SEC/HZ) / hpet_tick;

		vxtime.last = offset;
	} else {
		offset = (((tsc - vxtime.last_tsc) *
			   vxtime.tsc_quot) >> 32) - (USEC_PER_SEC / HZ);

		if (offset < 0)
			offset = 0;

		if (offset > (USEC_PER_SEC / HZ)) {
			lost = offset / (USEC_PER_SEC / HZ);
			offset %= (USEC_PER_SEC / HZ);
		}

		monotonic_base += (tsc - vxtime.last_tsc)*1000000/cpu_khz ;

		vxtime.last_tsc = tsc - vxtime.quot * delay / vxtime.tsc_quot;

		if ((((tsc - vxtime.last_tsc) *
		      vxtime.tsc_quot) >> 32) < offset)
			vxtime.last_tsc = tsc -
				(((long) offset << 32) / vxtime.tsc_quot) - 1;
	}

	if (lost) {
		if (report_lost_ticks)
			printk(KERN_WARNING "time.c: Lost %ld timer "
			       "tick(s)! (rip %016lx)\n",
			       (offset - vxtime.last) / hpet_tick - 1,
			       regs->rip);
		jiffies += lost;
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
 * don't care, as it'll be updated on the next turn, and the problem (time way
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

/* RED-PEN: calculation is done in 32bits with multiply for performance
   and could overflow, it may be better (but slower)to use an 64bit division. */
unsigned long long sched_clock(void)
{
	unsigned long a = 0;

#if 0
	/* Don't do a HPET read here. Using TSC always is much faster
	   and HPET may not be mapped yet when the scheduler first runs.
           Disadvantage is a small drift between CPUs in some configurations,
	   but that should be tolerable. */
	if (__vxtime.mode == VXTIME_HPET)
		return (hpet_readl(HPET_COUNTER) * vxtime.quot) >> 32;
#endif

	/* Could do CPU core sync here. Opteron can execute rdtsc speculatively,
	   which means it is not completely exact and may not be monotonous between
	   CPUs. But the errors should be too small to matter for scheduling
	   purposes. */

	rdtscll(a);
	return (a * vxtime.tsc_quot) >> 32;
}

unsigned long get_cmos_time(void)
{
	unsigned int timeout, year, mon, day, hour, min, sec;
	unsigned char last, this;
	unsigned long flags;

/*
 * The Linux interpretation of the CMOS clock register contents: When the
 * Update-In-Progress (UIP) flag goes from 1 to 0, the RTC registers show the
 * second which has precisely just started. Waiting for this can take up to 1
 * second, we timeout approximately after 2.4 seconds on a machine with
 * standard 8.3 MHz ISA bus.
 */

	spin_lock_irqsave(&rtc_lock, flags);

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

	spin_unlock_irqrestore(&rtc_lock, flags);

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

#ifdef CONFIG_CPU_FREQ

/* Frequency scaling support. Adjust the TSC based timer when the cpu frequency
   changes.
   
   RED-PEN: On SMP we assume all CPUs run with the same frequency.  It's
   not that important because current Opteron setups do not support
   scaling on SMP anyroads.

   Should fix up last_tsc too. Currently gettimeofday in the
   first tick after the change will be slightly wrong. */

static unsigned int  ref_freq = 0;
static unsigned long loops_per_jiffy_ref = 0;

static unsigned long cpu_khz_ref = 0;

static int time_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				 void *data)
{
        struct cpufreq_freqs *freq = data;
	unsigned long *lpj;

#ifdef CONFIG_SMP
	lpj = &cpu_data[freq->cpu].loops_per_jiffy;
#else
	lpj = &boot_cpu_data.loops_per_jiffy;
#endif

	if (!ref_freq) {
		ref_freq = freq->old;
		loops_per_jiffy_ref = *lpj;
		cpu_khz_ref = cpu_khz;
	}
        if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
            (val == CPUFREQ_POSTCHANGE && freq->old > freq->new)) {
                *lpj =
		cpufreq_scale(loops_per_jiffy_ref, ref_freq, freq->new);

		cpu_khz = cpufreq_scale(cpu_khz_ref, ref_freq, freq->new);
		vxtime.tsc_quot = (1000L << 32) / cpu_khz;
	}
	
	return 0;
}
 
static struct notifier_block time_cpufreq_notifier_block = {
         .notifier_call  = time_cpufreq_notifier
};
#endif

/*
 * calibrate_tsc() calibrates the processor TSC in a very simple way, comparing
 * it to the HPET timer of known frequency.
 */

#define TICK_COUNT 100000000

static unsigned int __init hpet_calibrate_tsc(void)
{
	int tsc_start, hpet_start;
	int tsc_now, hpet_now;
	unsigned long flags;

	local_irq_save(flags);
	local_irq_disable();

	hpet_start = hpet_readl(HPET_COUNTER);
	rdtscl(tsc_start);

	do {
		local_irq_disable();
		hpet_now = hpet_readl(HPET_COUNTER);
		sync_core();
		rdtscl(tsc_now);
		local_irq_restore(flags);
	} while ((tsc_now - tsc_start) < TICK_COUNT &&
		 (hpet_now - hpet_start) < TICK_COUNT);

	return (tsc_now - tsc_start) * 1000000000L
		/ ((hpet_now - hpet_start) * hpet_period / 1000);
}


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

	spin_lock_irqsave(&i8253_lock, flags);

	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	outb(0xb0, 0x43);
	outb((1193182 / (1000 / 50)) & 0xff, 0x42);
	outb((1193182 / (1000 / 50)) >> 8, 0x42);
	rdtscll(start);
	sync_core();
	while ((inb(0x61) & 0x20) == 0);
	sync_core();
	rdtscll(end);

	spin_unlock_irqrestore(&i8253_lock, flags);
	
	return (end - start) / 50;
}

static int hpet_init(void)
{
	unsigned int cfg, id;

	if (!vxtime.hpet_address)
		return -1;
	set_fixmap_nocache(FIX_HPET_BASE, vxtime.hpet_address);

/*
 * Read the period, compute tick and quotient.
 */

	id = hpet_readl(HPET_ID);

	if (!(id & HPET_ID_VENDOR) || !(id & HPET_ID_NUMBER) ||
	    !(id & HPET_ID_LEGSUP))
		return -1;

	hpet_period = hpet_readl(HPET_PERIOD);
	if (hpet_period < 100000 || hpet_period > 100000000)
		return -1;

	hpet_tick = (1000000000L * (USEC_PER_SEC / HZ) + hpet_period / 2) /
		hpet_period;

/*
 * Stop the timers and reset the main counter.
 */

	cfg = hpet_readl(HPET_CFG);
	cfg &= ~(HPET_CFG_ENABLE | HPET_CFG_LEGACY);
	hpet_writel(cfg, HPET_CFG);
	hpet_writel(0, HPET_COUNTER);
	hpet_writel(0, HPET_COUNTER + 4);

/*
 * Set up timer 0, as periodic with first interrupt to happen at hpet_tick,
 * and period also hpet_tick.
 */

	hpet_writel(HPET_T0_ENABLE | HPET_T0_PERIODIC | HPET_T0_SETVAL |
		    HPET_T0_32BIT, HPET_T0_CFG);
	hpet_writel(hpet_tick, HPET_T0_CMP);
	hpet_writel(hpet_tick, HPET_T0_CMP);

/*
 * Go!
 */

	cfg |= HPET_CFG_ENABLE | HPET_CFG_LEGACY;
	hpet_writel(cfg, HPET_CFG);

	return 0;
}

void __init pit_init(void)
{
	unsigned long flags;

	spin_lock_irqsave(&i8253_lock, flags);
	outb_p(0x34, 0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff, 0x40);	/* LSB */
	outb_p(LATCH >> 8, 0x40);	/* MSB */
	spin_unlock_irqrestore(&i8253_lock, flags);
}

int __init time_setup(char *str)
{
	report_lost_ticks = 1;
	return 1;
}

static struct irqaction irq0 = {
	timer_interrupt, SA_INTERRUPT, 0, "timer", NULL, NULL
};

extern void __init config_acpi_tables(void);

void __init time_init(void)
{
	char *timename;

#ifdef HPET_HACK_ENABLE_DANGEROUS
        if (!vxtime.hpet_address) {
		printk(KERN_WARNING "time.c: WARNING: Enabling HPET base "
		       "manually!\n");
                outl(0x800038a0, 0xcf8);
                outl(0xff000001, 0xcfc);
                outl(0x800038a0, 0xcf8);
                hpet_address = inl(0xcfc) & 0xfffffffe;
		printk(KERN_WARNING "time.c: WARNING: Enabled HPET "
		       "at %#lx.\n", hpet_address);
        }
#endif

	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = 0;

	set_normalized_timespec(&wall_to_monotonic,
	                        -xtime.tv_sec, -xtime.tv_nsec);

	if (!hpet_init()) {
                vxtime_hz = (1000000000000000L + hpet_period / 2) /
			hpet_period;
		cpu_khz = hpet_calibrate_tsc();
		timename = "HPET";
	} else {
	pit_init();
	cpu_khz = pit_calibrate_tsc();
		timename = "PIT";
	}

	printk(KERN_INFO "time.c: Using %ld.%06ld MHz %s timer.\n",
	       vxtime_hz / 1000000, vxtime_hz % 1000000, timename);
	printk(KERN_INFO "time.c: Detected %d.%03d MHz processor.\n",
		cpu_khz / 1000, cpu_khz % 1000);
	vxtime.mode = VXTIME_TSC;
	vxtime.quot = (1000000L << 32) / vxtime_hz;
	vxtime.tsc_quot = (1000L << 32) / cpu_khz;
	vxtime.hz = vxtime_hz;
	rdtscll_sync(&vxtime.last_tsc);
	setup_irq(0, &irq0);

#ifdef CONFIG_CPU_FREQ
	cpufreq_register_notifier(&time_cpufreq_notifier_block, 
				  CPUFREQ_TRANSITION_NOTIFIER);
#endif
}

void __init time_init_smp(void)
{
	char *timetype;

	if (vxtime.hpet_address) {
		timetype = "HPET";
		vxtime.last = hpet_readl(HPET_T0_CMP) - hpet_tick;
		vxtime.mode = VXTIME_HPET;
		do_gettimeoffset = do_gettimeoffset_hpet;
	} else {
		timetype = "PIT/TSC";
		vxtime.mode = VXTIME_TSC;
	}
	printk(KERN_INFO "time.c: Using %s based timekeeping.\n", timetype);
}

__setup("report_lost_ticks", time_setup);
