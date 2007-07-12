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
#include <linux/sysdev.h>
#include <linux/bcd.h>
#include <linux/efi.h>
#include <linux/mca.h>
#include <linux/sysctl.h>
#include <linux/percpu.h>
#include <linux/kernel_stat.h>
#include <linux/posix-timers.h>

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/msr.h>
#include <asm/delay.h>
#include <asm/mpspec.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/timer.h>
#include <asm/time.h>
#include <asm/sections.h>

#include "mach_time.h"

#include <linux/timex.h>
#include <linux/clocksource.h>

#include <asm/hpet.h>

#include <asm/arch_hooks.h>

#include <xen/evtchn.h>
#include <xen/interface/vcpu.h>

#ifdef CONFIG_X86_32
#include <asm/i8253.h>
DEFINE_SPINLOCK(i8253_lock);
EXPORT_SYMBOL(i8253_lock);
#else
volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;
#endif

#define XEN_SHIFT 22

unsigned int cpu_khz;	/* Detected as we calibrate the TSC */
EXPORT_SYMBOL(cpu_khz);

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

/* These are peridically updated in shared_info, and then copied here. */
struct shadow_time_info {
	u64 tsc_timestamp;     /* TSC at last update of time vals.  */
	u64 system_timestamp;  /* Time, in nanosecs, since boot.    */
	u32 tsc_to_nsec_mul;
	u32 tsc_to_usec_mul;
	int tsc_shift;
	u32 version;
};
static DEFINE_PER_CPU(struct shadow_time_info, shadow_time);
static struct timespec shadow_tv;
static u32 shadow_tv_version;

/* Keep track of last time we did processing/updating of jiffies and xtime. */
static u64 processed_system_time;   /* System time (ns) at last processing. */
static DEFINE_PER_CPU(u64, processed_system_time);

/* How much CPU time was spent blocked and how much was 'stolen'? */
static DEFINE_PER_CPU(u64, processed_stolen_time);
static DEFINE_PER_CPU(u64, processed_blocked_time);

/* Current runstate of each CPU (updated automatically by the hypervisor). */
static DEFINE_PER_CPU(struct vcpu_runstate_info, runstate);

/* Must be signed, as it's compared with s64 quantities which can be -ve. */
#define NS_PER_TICK (1000000000LL/HZ)

static void __clock_was_set(struct work_struct *unused)
{
	clock_was_set();
}
static DECLARE_WORK(clock_was_set_work, __clock_was_set);

static inline void __normalize_time(time_t *sec, s64 *nsec)
{
	while (*nsec >= NSEC_PER_SEC) {
		(*nsec) -= NSEC_PER_SEC;
		(*sec)++;
	}
	while (*nsec < 0) {
		(*nsec) += NSEC_PER_SEC;
		(*sec)--;
	}
}

/* Does this guest OS track Xen time, or set its wall clock independently? */
static int independent_wallclock = 0;
static int __init __independent_wallclock(char *str)
{
	independent_wallclock = 1;
	return 1;
}
__setup("independent_wallclock", __independent_wallclock);

/* Permitted clock jitter, in nsecs, beyond which a warning will be printed. */
static unsigned long permitted_clock_jitter = 10000000UL; /* 10ms */
static int __init __permitted_clock_jitter(char *str)
{
	permitted_clock_jitter = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("permitted_clock_jitter=", __permitted_clock_jitter);

#if 0
static void delay_tsc(unsigned long loops)
{
	unsigned long bclock, now;

	rdtscl(bclock);
	do {
		rep_nop();
		rdtscl(now);
	} while ((now - bclock) < loops);
}

struct timer_opts timer_tsc = {
	.name = "tsc",
	.delay = delay_tsc,
};
#endif

/*
 * Scale a 64-bit delta by scaling and multiplying by a 32-bit fraction,
 * yielding a 64-bit result.
 */
static inline u64 scale_delta(u64 delta, u32 mul_frac, int shift)
{
	u64 product;
#ifdef __i386__
	u32 tmp1, tmp2;
#endif

	if (shift < 0)
		delta >>= -shift;
	else
		delta <<= shift;

#ifdef __i386__
	__asm__ (
		"mul  %5       ; "
		"mov  %4,%%eax ; "
		"mov  %%edx,%4 ; "
		"mul  %5       ; "
		"xor  %5,%5    ; "
		"add  %4,%%eax ; "
		"adc  %5,%%edx ; "
		: "=A" (product), "=r" (tmp1), "=r" (tmp2)
		: "a" ((u32)delta), "1" ((u32)(delta >> 32)), "2" (mul_frac) );
#else
	__asm__ (
		"mul %%rdx ; shrd $32,%%rdx,%%rax"
		: "=a" (product) : "0" (delta), "d" ((u64)mul_frac) );
#endif

	return product;
}

#if 0 /* defined (__i386__) */
int read_current_timer(unsigned long *timer_val)
{
	rdtscl(*timer_val);
	return 0;
}
#endif

static void init_cpu_khz(void)
{
	u64 __cpu_khz = 1000000ULL << 32;
	struct vcpu_time_info *info = &vcpu_info(0)->time;
	do_div(__cpu_khz, info->tsc_to_system_mul);
	if (info->tsc_shift < 0)
		cpu_khz = __cpu_khz << -info->tsc_shift;
	else
		cpu_khz = __cpu_khz >> info->tsc_shift;
}

static u64 get_nsec_offset(struct shadow_time_info *shadow)
{
	u64 now, delta;
	rdtscll(now);
	delta = now - shadow->tsc_timestamp;
	return scale_delta(delta, shadow->tsc_to_nsec_mul, shadow->tsc_shift);
}

static void __update_wallclock(time_t sec, long nsec)
{
	long wtm_nsec, xtime_nsec;
	time_t wtm_sec, xtime_sec;
	u64 tmp, wc_nsec;

	/* Adjust wall-clock time base. */
	wc_nsec = processed_system_time;
	wc_nsec += sec * (u64)NSEC_PER_SEC;
	wc_nsec += nsec;

	/* Split wallclock base into seconds and nanoseconds. */
	tmp = wc_nsec;
	xtime_nsec = do_div(tmp, 1000000000);
	xtime_sec  = (time_t)tmp;

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - xtime_sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - xtime_nsec);

	set_normalized_timespec(&xtime, xtime_sec, xtime_nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();
}

static void update_wallclock(void)
{
	shared_info_t *s = HYPERVISOR_shared_info;

	do {
		shadow_tv_version = s->wc_version;
		rmb();
		shadow_tv.tv_sec  = s->wc_sec;
		shadow_tv.tv_nsec = s->wc_nsec;
		rmb();
	} while ((s->wc_version & 1) | (shadow_tv_version ^ s->wc_version));

	if (!independent_wallclock)
		__update_wallclock(shadow_tv.tv_sec, shadow_tv.tv_nsec);
}

/*
 * Reads a consistent set of time-base values from Xen, into a shadow data
 * area.
 */
static void get_time_values_from_xen(int cpu)
{
	struct vcpu_time_info   *src;
	struct shadow_time_info *dst;

	src = &vcpu_info(cpu)->time;
	dst = &per_cpu(shadow_time, cpu);

	do {
		dst->version = src->version;
		rmb();
		dst->tsc_timestamp     = src->tsc_timestamp;
		dst->system_timestamp  = src->system_time;
		dst->tsc_to_nsec_mul   = src->tsc_to_system_mul;
		dst->tsc_shift         = src->tsc_shift;
		rmb();
	} while ((src->version & 1) | (dst->version ^ src->version));

	dst->tsc_to_usec_mul = dst->tsc_to_nsec_mul / 1000;
}

static inline int time_values_up_to_date(int cpu)
{
	struct vcpu_time_info   *src;
	struct shadow_time_info *dst;

	src = &vcpu_info(cpu)->time;
	dst = &per_cpu(shadow_time, cpu);

	rmb();
	return (dst->version == src->version);
}

/*
 * This is a special lock that is owned by the CPU and holds the index
 * register we are working with.  It is required for NMI access to the
 * CMOS/RTC registers.  See include/asm-i386/mc146818rtc.h for details.
 */
volatile unsigned long cmos_lock = 0;
EXPORT_SYMBOL(cmos_lock);

/* Routines for accessing the CMOS RAM/RTC. */
unsigned char rtc_cmos_read(unsigned char addr)
{
	unsigned char val;
	lock_cmos_prefix(addr);
	outb_p(addr, RTC_PORT(0));
	val = inb_p(RTC_PORT(1));
	lock_cmos_suffix(addr);
	return val;
}
EXPORT_SYMBOL(rtc_cmos_read);

void rtc_cmos_write(unsigned char val, unsigned char addr)
{
	lock_cmos_prefix(addr);
	outb_p(addr, RTC_PORT(0));
	outb_p(val, RTC_PORT(1));
	lock_cmos_suffix(addr);
}
EXPORT_SYMBOL(rtc_cmos_write);

static void sync_xen_wallclock(unsigned long dummy);
static DEFINE_TIMER(sync_xen_wallclock_timer, sync_xen_wallclock, 0, 0);
static void sync_xen_wallclock(unsigned long dummy)
{
	time_t sec;
	s64 nsec;
	struct xen_platform_op op;

	if (!ntp_synced() || independent_wallclock || !is_initial_xendomain())
		return;

	write_seqlock_irq(&xtime_lock);

	sec  = xtime.tv_sec;
	nsec = xtime.tv_nsec;
	__normalize_time(&sec, &nsec);

	op.cmd = XENPF_settime;
	op.u.settime.secs        = sec;
	op.u.settime.nsecs       = nsec;
	op.u.settime.system_time = processed_system_time;
	HYPERVISOR_platform_op(&op);

	update_wallclock();

	write_sequnlock_irq(&xtime_lock);

	/* Once per minute. */
	mod_timer(&sync_xen_wallclock_timer, jiffies + 60*HZ);
}

static int set_rtc_mmss(unsigned long nowtime)
{
	int retval;
	unsigned long flags;

	if (independent_wallclock || !is_initial_xendomain())
		return 0;

	/* gets recalled with irq locally disabled */
	/* XXX - does irqsave resolve this? -johnstul */
	spin_lock_irqsave(&rtc_lock, flags);
	retval = set_wallclock(nowtime);
	spin_unlock_irqrestore(&rtc_lock, flags);

	return retval;
}

unsigned long long sched_clock(void)
{
	int cpu = get_cpu();
	struct shadow_time_info *shadow = &per_cpu(shadow_time, cpu);
	u64 time;
	u32 local_time_version;

	do {
		local_time_version = shadow->version;
		barrier();
		time = shadow->system_timestamp + get_nsec_offset(shadow);
		if (!time_values_up_to_date(cpu))
			get_time_values_from_xen(cpu);
		barrier();
	} while (local_time_version != shadow->version);

	put_cpu();

	return time;
}

unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

#ifdef __x86_64__
	/* Assume the lock function has either no stack frame or only a single word.
	   This checks if the address on the stack looks like a kernel text address.
	   There is a small window for false hits, but in that case the tick
	   is just accounted to the spinlock function.
	   Better would be to write these functions in assembler again
	   and check exactly. */
	if (!user_mode_vm(regs) && in_lock_functions(pc)) {
		char *v = *(char **)regs->rsp;
		if ((v >= _stext && v <= _etext) ||
			(v >= _sinittext && v <= _einittext) ||
			(v >= (char *)MODULES_VADDR  && v <= (char *)MODULES_END))
			return (unsigned long)v;
		return ((unsigned long *)regs->rsp)[1];
	}
#else
#ifdef CONFIG_SMP
	if (!v8086_mode(regs) && SEGMENT_IS_KERNEL_CODE(regs->xcs) &&
	    in_lock_functions(pc)) {
#ifdef CONFIG_FRAME_POINTER
		return *(unsigned long *)(regs->ebp + 4);
#else
		unsigned long *sp = (unsigned long *)&regs->esp;

		/* Return address is either directly at stack pointer
		   or above a saved eflags. Eflags has bits 22-31 zero,
		   kernel addresses don't. */
 		if (sp[0] >> 22)
			return sp[0];
		if (sp[1] >> 22)
			return sp[1];
#endif
	}
#endif
#endif

	return pc;
}
EXPORT_SYMBOL(profile_pc);

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	s64 delta, delta_cpu, stolen, blocked;
	u64 sched_time;
	int i, cpu = smp_processor_id();
	struct shadow_time_info *shadow = &per_cpu(shadow_time, cpu);
	struct vcpu_runstate_info *runstate = &per_cpu(runstate, cpu);

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);

	do {
		get_time_values_from_xen(cpu);

		/* Obtain a consistent snapshot of elapsed wallclock cycles. */
		delta = delta_cpu =
			shadow->system_timestamp + get_nsec_offset(shadow);
		delta     -= processed_system_time;
		delta_cpu -= per_cpu(processed_system_time, cpu);

		/*
		 * Obtain a consistent snapshot of stolen/blocked cycles. We
		 * can use state_entry_time to detect if we get preempted here.
		 */
		do {
			sched_time = runstate->state_entry_time;
			barrier();
			stolen = runstate->time[RUNSTATE_runnable] +
				runstate->time[RUNSTATE_offline] -
				per_cpu(processed_stolen_time, cpu);
			blocked = runstate->time[RUNSTATE_blocked] -
				per_cpu(processed_blocked_time, cpu);
			barrier();
		} while (sched_time != runstate->state_entry_time);
	} while (!time_values_up_to_date(cpu));

	if ((unlikely(delta < -(s64)permitted_clock_jitter) ||
	     unlikely(delta_cpu < -(s64)permitted_clock_jitter))
	    && printk_ratelimit()) {
		printk("Timer ISR/%d: Time went backwards: "
		       "delta=%lld delta_cpu=%lld shadow=%lld "
		       "off=%lld processed=%lld cpu_processed=%lld\n",
		       cpu, delta, delta_cpu, shadow->system_timestamp,
		       (s64)get_nsec_offset(shadow),
		       processed_system_time,
		       per_cpu(processed_system_time, cpu));
		for (i = 0; i < num_online_cpus(); i++)
			printk(" %d: %lld\n", i,
			       per_cpu(processed_system_time, i));
	}

	/* System-wide jiffy work. */
	if (delta >= NS_PER_TICK) {
		do_div(delta, NS_PER_TICK);
		processed_system_time += delta * NS_PER_TICK;
		while (delta > HZ) {
			do_timer(HZ);
			delta -= HZ;
		}
		do_timer(delta);
	}

	if (shadow_tv_version != HYPERVISOR_shared_info->wc_version) {
		update_wallclock();
		if (keventd_up())
			schedule_work(&clock_was_set_work);
	}

	write_sequnlock(&xtime_lock);

	/*
	 * Account stolen ticks.
	 * HACK: Passing NULL to account_steal_time()
	 * ensures that the ticks are accounted as stolen.
	 */
	if ((stolen > 0) && (delta_cpu > 0)) {
		delta_cpu -= stolen;
		if (unlikely(delta_cpu < 0))
			stolen += delta_cpu; /* clamp local-time progress */
		do_div(stolen, NS_PER_TICK);
		per_cpu(processed_stolen_time, cpu) += stolen * NS_PER_TICK;
		per_cpu(processed_system_time, cpu) += stolen * NS_PER_TICK;
		account_steal_time(NULL, (cputime_t)stolen);
	}

	/*
	 * Account blocked ticks.
	 * HACK: Passing idle_task to account_steal_time()
	 * ensures that the ticks are accounted as idle/wait.
	 */
	if ((blocked > 0) && (delta_cpu > 0)) {
		delta_cpu -= blocked;
		if (unlikely(delta_cpu < 0))
			blocked += delta_cpu; /* clamp local-time progress */
		do_div(blocked, NS_PER_TICK);
		per_cpu(processed_blocked_time, cpu) += blocked * NS_PER_TICK;
		per_cpu(processed_system_time, cpu)  += blocked * NS_PER_TICK;
		account_steal_time(idle_task(cpu), (cputime_t)blocked);
	}

	/* Account user/system ticks. */
	if (delta_cpu > 0) {
		do_div(delta_cpu, NS_PER_TICK);
		per_cpu(processed_system_time, cpu) += delta_cpu * NS_PER_TICK;
		if (user_mode_vm(get_irq_regs()))
			account_user_time(current, (cputime_t)delta_cpu);
		else
			account_system_time(current, HARDIRQ_OFFSET,
					    (cputime_t)delta_cpu);
	}

	/* Offlined for more than a few seconds? Avoid lockup warnings. */
	if (stolen > 5*HZ)
		touch_softlockup_watchdog();

	/* Local timer processing (see update_process_times()). */
	run_local_timers();
	if (rcu_pending(cpu))
		rcu_check_callbacks(cpu, user_mode_vm(get_irq_regs()));
	scheduler_tick();
	run_posix_cpu_timers(current);
	profile_tick(CPU_PROFILING);

	return IRQ_HANDLED;
}

void mark_tsc_unstable(char *reason)
{
#ifndef CONFIG_XEN /* XXX Should tell the hypervisor about this fact. */
	tsc_unstable = 1;
#endif
}
EXPORT_SYMBOL_GPL(mark_tsc_unstable);

static cycle_t xen_clocksource_read(void)
{
	int cpu = get_cpu();
	struct shadow_time_info *shadow = &per_cpu(shadow_time, cpu);
	cycle_t ret;

	get_time_values_from_xen(cpu);

	ret = shadow->system_timestamp + get_nsec_offset(shadow);

	put_cpu();

#ifdef CONFIG_SMP
	for (;;) {
		static cycle_t last_ret;
#ifndef CONFIG_64BIT
		cycle_t last = cmpxchg64(&last_ret, 0, 0);
#else
		cycle_t last = last_ret;
#define cmpxchg64 cmpxchg
#endif

		if ((s64)(ret - last) < 0) {
			if (last - ret > permitted_clock_jitter
			    && printk_ratelimit())
				printk(KERN_WARNING "clocksource/%d: "
				       "Time went backwards: "
				       "delta=%Ld shadow=%Lu offset=%Lu\n",
				       cpu, ret - last,
				       shadow->system_timestamp,
				       get_nsec_offset(shadow));
			ret = last;
		}
		if (cmpxchg64(&last_ret, last, ret) == last)
			break;
	}
#endif

	return ret;
}

static struct clocksource clocksource_xen = {
	.name			= "xen",
	.rating			= 400,
	.read			= xen_clocksource_read,
	.mask			= CLOCKSOURCE_MASK(64),
	.mult			= 1 << XEN_SHIFT,		/* time directly in nanoseconds */
	.shift			= XEN_SHIFT,
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void init_missing_ticks_accounting(int cpu)
{
	struct vcpu_register_runstate_memory_area area;
	struct vcpu_runstate_info *runstate = &per_cpu(runstate, cpu);

	memset(runstate, 0, sizeof(*runstate));

	area.addr.v = runstate;
	HYPERVISOR_vcpu_op(VCPUOP_register_runstate_memory_area, cpu, &area);

	per_cpu(processed_blocked_time, cpu) =
		runstate->time[RUNSTATE_blocked];
	per_cpu(processed_stolen_time, cpu) =
		runstate->time[RUNSTATE_runnable] +
		runstate->time[RUNSTATE_offline];
}

/* not static: needed by APM */
unsigned long read_persistent_clock(void)
{
	unsigned long retval;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);

	retval = get_wallclock();

	spin_unlock_irqrestore(&rtc_lock, flags);

	return retval;
}

static void sync_cmos_clock(unsigned long dummy);

static DEFINE_TIMER(sync_cmos_timer, sync_cmos_clock, 0, 0);
int no_sync_cmos_clock;

static void sync_cmos_clock(unsigned long dummy)
{
	struct timeval now, next;
	int fail = 1;

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 * This code is run on a timer.  If the clock is set, that timer
	 * may not expire at the correct time.  Thus, we adjust...
	 */
	if (!ntp_synced())
		/*
		 * Not synced, exit, do not restart a timer (if one is
		 * running, let it run out).
		 */
		return;

	do_gettimeofday(&now);
	if (now.tv_usec >= USEC_AFTER - ((unsigned) TICK_SIZE) / 2 &&
	    now.tv_usec <= USEC_BEFORE + ((unsigned) TICK_SIZE) / 2)
		fail = set_rtc_mmss(now.tv_sec);

	next.tv_usec = USEC_AFTER - now.tv_usec;
	if (next.tv_usec <= 0)
		next.tv_usec += USEC_PER_SEC;

	if (!fail)
		next.tv_sec = 659;
	else
		next.tv_sec = 0;

	if (next.tv_usec >= USEC_PER_SEC) {
		next.tv_sec++;
		next.tv_usec -= USEC_PER_SEC;
	}
	mod_timer(&sync_cmos_timer, jiffies + timeval_to_jiffies(&next));
}

void notify_arch_cmos_timer(void)
{
	if (!no_sync_cmos_clock)
		mod_timer(&sync_cmos_timer, jiffies + 1);
	mod_timer(&sync_xen_wallclock_timer, jiffies + 1);
}

static long clock_cmos_diff;
static unsigned long sleep_start;

static int timer_suspend(struct sys_device *dev, pm_message_t state)
{
	/*
	 * Estimate time zone so that set_time can update the clock
	 */
	unsigned long ctime = read_persistent_clock();

	clock_cmos_diff = -ctime;
	clock_cmos_diff += get_seconds();
	sleep_start = ctime;
	return 0;
}

static int timer_resume(struct sys_device *dev)
{
	unsigned long flags;
	unsigned long sec;
	unsigned long ctime = read_persistent_clock();
	long sleep_length = (ctime - sleep_start) * HZ;
	struct timespec ts;

	if (sleep_length < 0) {
		printk(KERN_WARNING "CMOS clock skew detected in timer resume!\n");
		/* The time after the resume must not be earlier than the time
		 * before the suspend or some nasty things will happen
		 */
		sleep_length = 0;
		ctime = sleep_start;
	}

	sec = ctime + clock_cmos_diff;
	ts.tv_sec = sec;
	ts.tv_nsec = 0;
	do_settimeofday(&ts);
	write_seqlock_irqsave(&xtime_lock, flags);
	jiffies_64 += sleep_length;
	write_sequnlock_irqrestore(&xtime_lock, flags);
	touch_softlockup_watchdog();
	return 0;
}

static struct sysdev_class timer_sysclass = {
	.resume = timer_resume,
	.suspend = timer_suspend,
	set_kset_name("timer"),
};


/* XXX this driverfs stuff should probably go elsewhere later -john */
static struct sys_device device_timer = {
	.id	= 0,
	.cls	= &timer_sysclass,
};

static int time_init_device(void)
{
	int error = sysdev_class_register(&timer_sysclass);
	if (!error)
		error = sysdev_register(&device_timer);
	return error;
}

device_initcall(time_init_device);

extern void (*late_time_init)(void);

/* Dynamically-mapped IRQ. */
DEFINE_PER_CPU(int, timer_irq);

static void setup_cpu0_timer_irq(void)
{
	per_cpu(timer_irq, 0) =
		bind_virq_to_irqhandler(
			VIRQ_TIMER,
			0,
			timer_interrupt,
			IRQF_DISABLED,
			"timer0",
			NULL);
	BUG_ON(per_cpu(timer_irq, 0) < 0);
}

static struct vcpu_set_periodic_timer xen_set_periodic_tick = {
	.period_ns = NS_PER_TICK
};

void __init time_init(void)
{
	init_cpu_khz();
	printk(KERN_INFO "Xen reported: %u.%03u MHz processor.\n",
	       cpu_khz / 1000, cpu_khz % 1000);

	HYPERVISOR_vcpu_op(VCPUOP_set_periodic_timer, 0,
			   &xen_set_periodic_tick);

	get_time_values_from_xen(0);

	processed_system_time = per_cpu(shadow_time, 0).system_timestamp;
	per_cpu(processed_system_time, 0) = processed_system_time;
	init_missing_ticks_accounting(0);

	clocksource_register(&clocksource_xen);

	update_wallclock();

#ifndef CONFIG_X86_64
	use_tsc_delay();
#endif

	/* Cannot request_irq() until kmem is initialised. */
	late_time_init = setup_cpu0_timer_irq;
}

/* Convert jiffies to system time. */
u64 jiffies_to_st(unsigned long j)
{
	unsigned long seq;
	long delta;
	u64 st;

	do {
		seq = read_seqbegin(&xtime_lock);
		delta = j - jiffies;
		if (delta < 1) {
			/* Triggers in some wrap-around cases, but that's okay:
			 * we just end up with a shorter timeout. */
			st = processed_system_time + NS_PER_TICK;
		} else if (((unsigned long)delta >> (BITS_PER_LONG-3)) != 0) {
			/* Very long timeout means there is no pending timer.
			 * We indicate this to Xen by passing zero timeout. */
			st = 0;
		} else {
			st = processed_system_time + delta * (u64)NS_PER_TICK;
		}
	} while (read_seqretry(&xtime_lock, seq));

	return st;
}
EXPORT_SYMBOL(jiffies_to_st);

/*
 * stop_hz_timer / start_hz_timer - enter/exit 'tickless mode' on an idle cpu
 * These functions are based on implementations from arch/s390/kernel/time.c
 */
static void stop_hz_timer(void)
{
	struct vcpu_set_singleshot_timer singleshot;
	unsigned int cpu = smp_processor_id();
	unsigned long j;
	int rc;

	cpu_set(cpu, nohz_cpu_mask);

	/* See matching smp_mb in rcu_start_batch in rcupdate.c.  These mbs  */
	/* ensure that if __rcu_pending (nested in rcu_needs_cpu) fetches a  */
	/* value of rcp->cur that matches rdp->quiescbatch and allows us to  */
	/* stop the hz timer then the cpumasks created for subsequent values */
	/* of cur in rcu_start_batch are guaranteed to pick up the updated   */
	/* nohz_cpu_mask and so will not depend on this cpu.                 */

	smp_mb();

	/* Leave ourselves in tick mode if rcu or softirq or timer pending. */
	if (rcu_needs_cpu(cpu) || local_softirq_pending() ||
	    (j = next_timer_interrupt(), time_before_eq(j, jiffies))) {
		cpu_clear(cpu, nohz_cpu_mask);
		j = jiffies + 1;
	}

	singleshot.timeout_abs_ns = jiffies_to_st(j);
	singleshot.flags = 0;
	rc = HYPERVISOR_vcpu_op(VCPUOP_set_singleshot_timer, cpu, &singleshot);
#if CONFIG_XEN_COMPAT <= 0x030004
	if (rc) {
		BUG_ON(rc != -ENOSYS);
		rc = HYPERVISOR_set_timer_op(singleshot.timeout_abs_ns);
	}
#endif
	BUG_ON(rc);
}

static void start_hz_timer(void)
{
	cpu_clear(smp_processor_id(), nohz_cpu_mask);
}

void xen_safe_halt(void)
{
	stop_hz_timer();
	/* Blocking includes an implicit local_irq_enable(). */
	HYPERVISOR_block();
	start_hz_timer();
}
EXPORT_SYMBOL(xen_safe_halt);

void xen_halt(void)
{
	if (irqs_disabled())
		HYPERVISOR_vcpu_op(VCPUOP_down, smp_processor_id(), NULL);
}
EXPORT_SYMBOL(xen_halt);

/* No locking required. Interrupts are disabled on all CPUs. */
void time_resume(void)
{
	unsigned int cpu;

	init_cpu_khz();

	for_each_online_cpu(cpu) {
		HYPERVISOR_vcpu_op(VCPUOP_set_periodic_timer, cpu,
				   &xen_set_periodic_tick);
		get_time_values_from_xen(cpu);
		per_cpu(processed_system_time, cpu) =
			per_cpu(shadow_time, 0).system_timestamp;
		init_missing_ticks_accounting(cpu);
	}

	processed_system_time = per_cpu(shadow_time, 0).system_timestamp;

	update_wallclock();
}

#ifdef CONFIG_SMP
static char timer_name[NR_CPUS][15];

int local_setup_timer(unsigned int cpu)
{
	int seq, irq;

	BUG_ON(cpu == 0);

	HYPERVISOR_vcpu_op(VCPUOP_set_periodic_timer, cpu,
			   &xen_set_periodic_tick);

	do {
		seq = read_seqbegin(&xtime_lock);
		/* Use cpu0 timestamp: cpu's shadow is not initialised yet. */
		per_cpu(processed_system_time, cpu) =
			per_cpu(shadow_time, 0).system_timestamp;
		init_missing_ticks_accounting(cpu);
	} while (read_seqretry(&xtime_lock, seq));

	sprintf(timer_name[cpu], "timer%d", cpu);
	irq = bind_virq_to_irqhandler(VIRQ_TIMER,
				      cpu,
				      timer_interrupt,
				      IRQF_DISABLED,
				      timer_name[cpu],
				      NULL);
	if (irq < 0)
		return irq;
	per_cpu(timer_irq, cpu) = irq;

	return 0;
}

void local_teardown_timer(unsigned int cpu)
{
	BUG_ON(cpu == 0);
	unbind_from_irqhandler(per_cpu(timer_irq, cpu), NULL);
}
#endif

/*
 * /proc/sys/xen: This really belongs in another file. It can stay here for
 * now however.
 */
static ctl_table xen_subtable[] = {
	{
		.ctl_name	= 1,
		.procname	= "independent_wallclock",
		.data		= &independent_wallclock,
		.maxlen		= sizeof(independent_wallclock),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= 2,
		.procname	= "permitted_clock_jitter",
		.data		= &permitted_clock_jitter,
		.maxlen		= sizeof(permitted_clock_jitter),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax
	},
	{ 0 }
};
static ctl_table xen_table[] = {
	{
		.ctl_name	= 123,
		.procname	= "xen",
		.mode		= 0555,
		.child		= xen_subtable},
	{ 0 }
};
static int __init xen_sysctl_init(void)
{
	(void)register_sysctl_table(xen_table);
	return 0;
}
__initcall(xen_sysctl_init);
