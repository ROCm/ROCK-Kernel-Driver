/*
 * linux/arch/ia64/kernel/time.c
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger <davidm@hpl.hp.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 * Copyright (C) 1999-2000 VA Linux Systems
 * Copyright (C) 1999-2000 Walt Drummond <drummond@valinux.com>
 */
#include <linux/config.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/profile.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/efi.h>
#include <linux/profile.h>
#include <linux/timex.h>

#include <asm/machvec.h>
#include <asm/delay.h>
#include <asm/hw_irq.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/sections.h>
#include <asm/system.h>

extern unsigned long wall_jiffies;

u64 jiffies_64 = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

#define TIME_KEEPER_ID	0	/* smp_processor_id() of time-keeper */

#ifdef CONFIG_IA64_DEBUG_IRQ

unsigned long last_cli_ip;
EXPORT_SYMBOL(last_cli_ip);

#endif

unsigned long long
sched_clock (void)
{
	unsigned long offset = ia64_get_itc();

	return (offset * local_cpu_data->nsec_per_cyc) >> IA64_NSEC_PER_CYC_SHIFT;
}

static void
itc_reset (void)
{
}

/*
 * Adjust for the fact that xtime has been advanced by delta_nsec (may be negative and/or
 * larger than NSEC_PER_SEC.
 */
static void
itc_update (long delta_nsec)
{
}

/*
 * Return the number of nano-seconds that elapsed since the last
 * update to jiffy.  It is quite possible that the timer interrupt
 * will interrupt this and result in a race for any of jiffies,
 * wall_jiffies or itm_next.  Thus, the xtime_lock must be at least
 * read synchronised when calling this routine (see do_gettimeofday()
 * below for an example).
 */
unsigned long
itc_get_offset (void)
{
	unsigned long elapsed_cycles, lost = jiffies - wall_jiffies;
	unsigned long now = ia64_get_itc(), last_tick;

	last_tick = (cpu_data(TIME_KEEPER_ID)->itm_next
		     - (lost + 1)*cpu_data(TIME_KEEPER_ID)->itm_delta);

	elapsed_cycles = now - last_tick;
	return (elapsed_cycles*local_cpu_data->nsec_per_cyc) >> IA64_NSEC_PER_CYC_SHIFT;
}

static struct time_interpolator itc_interpolator = {
	.get_offset =	itc_get_offset,
	.update =	itc_update,
	.reset =	itc_reset
};

int
do_settimeofday (struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	{
		/*
		 * This is revolting. We need to set "xtime" correctly. However, the value
		 * in this location is the value at the most recent update of wall time.
		 * Discover what correction gettimeofday would have done, and then undo
		 * it!
		 */
		nsec -= time_interpolator_get_offset();

		wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
		wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

		set_normalized_timespec(&xtime, sec, nsec);
		set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

		time_adjust = 0;		/* stop active adjtime() */
		time_status |= STA_UNSYNC;
		time_maxerror = NTP_PHASE_LIMIT;
		time_esterror = NTP_PHASE_LIMIT;
		time_interpolator_reset();
	}
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

void
do_gettimeofday (struct timeval *tv)
{
	unsigned long seq, nsec, usec, sec, old, offset;

	while (1) {
		seq = read_seqbegin(&xtime_lock);
		{
			old = last_nsec_offset;
			offset = time_interpolator_get_offset();
			sec = xtime.tv_sec;
			nsec = xtime.tv_nsec;
		}
		if (unlikely(read_seqretry(&xtime_lock, seq)))
			continue;
		/*
		 * Ensure that for any pair of causally ordered gettimeofday() calls, time
		 * never goes backwards (even when ITC on different CPUs are not perfectly
		 * synchronized).  (A pair of concurrent calls to gettimeofday() is by
		 * definition non-causal and hence it makes no sense to talk about
		 * time-continuity for such calls.)
		 *
		 * Doing this in a lock-free and race-free manner is tricky.  Here is why
		 * it works (most of the time): read_seqretry() just succeeded, which
		 * implies we calculated a consistent (valid) value for "offset".  If the
		 * cmpxchg() below succeeds, we further know that last_nsec_offset still
		 * has the same value as at the beginning of the loop, so there was
		 * presumably no timer-tick or other updates to last_nsec_offset in the
		 * meantime.  This isn't 100% true though: there _is_ a possibility of a
		 * timer-tick occurring right right after read_seqretry() and then getting
		 * zero or more other readers which will set last_nsec_offset to the same
		 * value as the one we read at the beginning of the loop.  If this
		 * happens, we'll end up returning a slightly newer time than we ought to
		 * (the jump forward is at most "offset" nano-seconds).  There is no
		 * danger of causing time to go backwards, though, so we are safe in that
		 * sense.  We could make the probability of this unlucky case occurring
		 * arbitrarily small by encoding a version number in last_nsec_offset, but
		 * even without versioning, the probability of this unlucky case should be
		 * so small that we won't worry about it.
		 */
		if (offset <= old) {
			offset = old;
			break;
		} else if (likely(cmpxchg(&last_nsec_offset, old, offset) == old))
			break;

		/* someone else beat us to updating last_nsec_offset; try again */
	}

	usec = (nsec + offset) / 1000;

	while (unlikely(usec >= USEC_PER_SEC)) {
		usec -= USEC_PER_SEC;
		++sec;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

/*
 * The profiling function is SMP safe. (nothing can mess
 * around with "current", and the profiling counters are
 * updated with atomic operations). This is especially
 * useful with a profiling multiplier != 1
 */
static inline void
ia64_do_profile (struct pt_regs * regs)
{
	unsigned long ip, slot;
	extern cpumask_t prof_cpu_mask;

	profile_hook(regs);

	if (user_mode(regs))
		return;

	if (!prof_buffer)
		return;

	ip = instruction_pointer(regs);
	/* Conserve space in histogram by encoding slot bits in address
	 * bits 2 and 3 rather than bits 0 and 1.
	 */
	slot = ip & 3;
	ip = (ip & ~3UL) + 4*slot;

	/*
	 * Only measure the CPUs specified by /proc/irq/prof_cpu_mask.
	 * (default is all CPUs.)
	 */
	if (!cpu_isset(smp_processor_id(), prof_cpu_mask))
		return;

	ip -= (unsigned long) &_stext;
	ip >>= prof_shift;
	/*
	 * Don't ignore out-of-bounds IP values silently,
	 * put them into the last histogram slot, so if
	 * present, they will show up as a sharp peak.
	 */
	if (ip > prof_len-1)
		ip = prof_len-1;
	atomic_inc((atomic_t *)&prof_buffer[ip]);
}

static irqreturn_t
timer_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long new_itm;

	platform_timer_interrupt(irq, dev_id, regs);

	new_itm = local_cpu_data->itm_next;

	if (!time_after(ia64_get_itc(), new_itm))
		printk(KERN_ERR "Oops: timer tick before it's due (itc=%lx,itm=%lx)\n",
		       ia64_get_itc(), new_itm);

	ia64_do_profile(regs);

	while (1) {
#ifdef CONFIG_SMP
		/*
		 * For UP, this is done in do_timer().  Weird, but
		 * fixing that would require updates to all
		 * platforms.
		 */
		update_process_times(user_mode(regs));
#endif
		new_itm += local_cpu_data->itm_delta;

		if (smp_processor_id() == TIME_KEEPER_ID) {
			/*
			 * Here we are in the timer irq handler. We have irqs locally
			 * disabled, but we don't know if the timer_bh is running on
			 * another CPU. We need to avoid to SMP race by acquiring the
			 * xtime_lock.
			 */
			write_seqlock(&xtime_lock);
			do_timer(regs);
			local_cpu_data->itm_next = new_itm;
			write_sequnlock(&xtime_lock);
		} else
			local_cpu_data->itm_next = new_itm;

		if (time_after(new_itm, ia64_get_itc()))
			break;
	}

	do {
		/*
		 * If we're too close to the next clock tick for
		 * comfort, we increase the safety margin by
		 * intentionally dropping the next tick(s).  We do NOT
		 * update itm.next because that would force us to call
		 * do_timer() which in turn would let our clock run
		 * too fast (with the potentially devastating effect
		 * of losing monotony of time).
		 */
		while (!time_after(new_itm, ia64_get_itc() + local_cpu_data->itm_delta/2))
			new_itm += local_cpu_data->itm_delta;
		ia64_set_itm(new_itm);
		/* double check, in case we got hit by a (slow) PMI: */
	} while (time_after_eq(ia64_get_itc(), new_itm));
	return IRQ_HANDLED;
}

/*
 * Encapsulate access to the itm structure for SMP.
 */
void
ia64_cpu_local_tick (void)
{
	int cpu = smp_processor_id();
	unsigned long shift = 0, delta;

	/* arrange for the cycle counter to generate a timer interrupt: */
	ia64_set_itv(IA64_TIMER_VECTOR);

	delta = local_cpu_data->itm_delta;
	/*
	 * Stagger the timer tick for each CPU so they don't occur all at (almost) the
	 * same time:
	 */
	if (cpu) {
		unsigned long hi = 1UL << ia64_fls(cpu);
		shift = (2*(cpu - hi) + 1) * delta/hi/2;
	}
	local_cpu_data->itm_next = ia64_get_itc() + delta + shift;
	ia64_set_itm(local_cpu_data->itm_next);
}

void __devinit
ia64_init_itm (void)
{
	unsigned long platform_base_freq, itc_freq;
	struct pal_freq_ratio itc_ratio, proc_ratio;
	long status, platform_base_drift, itc_drift;

	/*
	 * According to SAL v2.6, we need to use a SAL call to determine the platform base
	 * frequency and then a PAL call to determine the frequency ratio between the ITC
	 * and the base frequency.
	 */
	status = ia64_sal_freq_base(SAL_FREQ_BASE_PLATFORM,
				    &platform_base_freq, &platform_base_drift);
	if (status != 0) {
		printk(KERN_ERR "SAL_FREQ_BASE_PLATFORM failed: %s\n", ia64_sal_strerror(status));
	} else {
		status = ia64_pal_freq_ratios(&proc_ratio, 0, &itc_ratio);
		if (status != 0)
			printk(KERN_ERR "PAL_FREQ_RATIOS failed with status=%ld\n", status);
	}
	if (status != 0) {
		/* invent "random" values */
		printk(KERN_ERR
		       "SAL/PAL failed to obtain frequency info---inventing reasonable values\n");
		platform_base_freq = 100000000;
		platform_base_drift = -1;	/* no drift info */
		itc_ratio.num = 3;
		itc_ratio.den = 1;
	}
	if (platform_base_freq < 40000000) {
		printk(KERN_ERR "Platform base frequency %lu bogus---resetting to 75MHz!\n",
		       platform_base_freq);
		platform_base_freq = 75000000;
		platform_base_drift = -1;
	}
	if (!proc_ratio.den)
		proc_ratio.den = 1;	/* avoid division by zero */
	if (!itc_ratio.den)
		itc_ratio.den = 1;	/* avoid division by zero */

	itc_freq = (platform_base_freq*itc_ratio.num)/itc_ratio.den;
	if (platform_base_drift != -1)
		itc_drift = platform_base_drift*itc_ratio.num/itc_ratio.den;
	else
		itc_drift = -1;

	local_cpu_data->itm_delta = (itc_freq + HZ/2) / HZ;
	printk(KERN_INFO "CPU %d: base freq=%lu.%03luMHz, ITC ratio=%lu/%lu, "
	       "ITC freq=%lu.%03luMHz+/-%ldppm\n", smp_processor_id(),
	       platform_base_freq / 1000000, (platform_base_freq / 1000) % 1000,
	       itc_ratio.num, itc_ratio.den, itc_freq / 1000000, (itc_freq / 1000) % 1000,
	       itc_drift);

	local_cpu_data->proc_freq = (platform_base_freq*proc_ratio.num)/proc_ratio.den;
	local_cpu_data->itc_freq = itc_freq;
	local_cpu_data->cyc_per_usec = (itc_freq + USEC_PER_SEC/2) / USEC_PER_SEC;
	local_cpu_data->nsec_per_cyc = ((NSEC_PER_SEC<<IA64_NSEC_PER_CYC_SHIFT)
					+ itc_freq/2)/itc_freq;

	if (!(sal_platform_features & IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT)) {
		itc_interpolator.frequency = local_cpu_data->itc_freq;
		itc_interpolator.drift = itc_drift;
		register_time_interpolator(&itc_interpolator);
	}

	/* Setup the CPU local timer tick */
	ia64_cpu_local_tick();
}

static struct irqaction timer_irqaction = {
	.handler =	timer_interrupt,
	.flags =	SA_INTERRUPT,
	.name =		"timer"
};

void __init
time_init (void)
{
	register_percpu_irq(IA64_TIMER_VECTOR, &timer_irqaction);
	efi_gettimeofday(&xtime);
	ia64_init_itm();

	/*
	 * Initialize wall_to_monotonic such that adding it to xtime will yield zero, the
	 * tv_nsec field must be normalized (i.e., 0 <= nsec < NSEC_PER_SEC).
	 */
	set_normalized_timespec(&wall_to_monotonic, -xtime.tv_sec, -xtime.tv_nsec);
}
