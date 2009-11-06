/*
 *  Copyright (c) 1991,1992,1995  Linus Torvalds
 *  Copyright (c) 1994  Alan Modra
 *  Copyright (c) 1995  Markus Kuhn
 *  Copyright (c) 1996  Ingo Molnar
 *  Copyright (c) 1998  Andrea Arcangeli
 *  Copyright (c) 2002,2006  Vojtech Pavlik
 *  Copyright (c) 2003  Andi Kleen
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/mca.h>
#include <linux/sysctl.h>
#include <linux/percpu.h>
#include <linux/kernel_stat.h>
#include <linux/posix-timers.h>
#include <linux/cpufreq.h>
#include <linux/clocksource.h>
#include <linux/sysdev.h>

#include <asm/vsyscall.h>
#include <asm/delay.h>
#include <asm/time.h>
#include <asm/timer.h>

#include <xen/evtchn.h>
#include <xen/sysctl.h>
#include <xen/interface/vcpu.h>

#include <asm/i8253.h>
DEFINE_SPINLOCK(i8253_lock);
EXPORT_SYMBOL(i8253_lock);

#ifdef CONFIG_X86_64
volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;
#endif

#define XEN_SHIFT 22

unsigned int cpu_khz;	/* Detected as we calibrate the TSC */
EXPORT_SYMBOL(cpu_khz);

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

static struct vcpu_set_periodic_timer xen_set_periodic_tick = {
	.period_ns = NS_PER_TICK
};

static void __clock_was_set(struct work_struct *unused)
{
	clock_was_set();
}
static DECLARE_WORK(clock_was_set_work, __clock_was_set);

/*
 * GCC 4.3 can turn loops over an induction variable into division. We do
 * not support arbitrary 64-bit division, and so must break the induction.
 */
#define clobber_induction_variable(v) asm ( "" : "+r" (v) )

static inline void __normalize_time(time_t *sec, s64 *nsec)
{
	while (*nsec >= NSEC_PER_SEC) {
		clobber_induction_variable(*nsec);
		(*nsec) -= NSEC_PER_SEC;
		(*sec)++;
	}
	while (*nsec < 0) {
		clobber_induction_variable(*nsec);
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

int xen_independent_wallclock(void)
{
	return independent_wallclock;
}

/* Permitted clock jitter, in nsecs, beyond which a warning will be printed. */
static unsigned long permitted_clock_jitter = 10000000UL; /* 10ms */
static int __init __permitted_clock_jitter(char *str)
{
	permitted_clock_jitter = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("permitted_clock_jitter=", __permitted_clock_jitter);

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

static inline u64 get64(volatile u64 *ptr)
{
#ifndef CONFIG_64BIT
	return cmpxchg64(ptr, 0, 0);
#else
	return *ptr;
#endif
}

static inline u64 get64_local(volatile u64 *ptr)
{
#ifndef CONFIG_64BIT
	return cmpxchg64_local(ptr, 0, 0);
#else
	return *ptr;
#endif
}

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
static void get_time_values_from_xen(unsigned int cpu)
{
	struct vcpu_time_info   *src;
	struct shadow_time_info *dst;
	unsigned long flags;
	u32 pre_version, post_version;

	src = &vcpu_info(cpu)->time;
	dst = &per_cpu(shadow_time, cpu);

	local_irq_save(flags);

	do {
		pre_version = dst->version = src->version;
		rmb();
		dst->tsc_timestamp     = src->tsc_timestamp;
		dst->system_timestamp  = src->system_time;
		dst->tsc_to_nsec_mul   = src->tsc_to_system_mul;
		dst->tsc_shift         = src->tsc_shift;
		rmb();
		post_version = src->version;
	} while ((pre_version & 1) | (pre_version ^ post_version));

	dst->tsc_to_usec_mul = dst->tsc_to_nsec_mul / 1000;

	local_irq_restore(flags);
}

static inline int time_values_up_to_date(void)
{
	rmb();
	return percpu_read(shadow_time.version) == vcpu_info_read(time.version);
}

static void sync_xen_wallclock(unsigned long dummy);
static DEFINE_TIMER(sync_xen_wallclock_timer, sync_xen_wallclock, 0, 0);
static void sync_xen_wallclock(unsigned long dummy)
{
	time_t sec;
	s64 nsec;
	struct xen_platform_op op;

	BUG_ON(!is_initial_xendomain());
	if (!ntp_synced() || independent_wallclock)
		return;

	write_seqlock_irq(&xtime_lock);

	sec  = xtime.tv_sec;
	nsec = xtime.tv_nsec;
	__normalize_time(&sec, &nsec);

	op.cmd = XENPF_settime;
	op.u.settime.secs        = sec;
	op.u.settime.nsecs       = nsec;
	op.u.settime.system_time = processed_system_time;
	WARN_ON(HYPERVISOR_platform_op(&op));

	update_wallclock();

	write_sequnlock_irq(&xtime_lock);

	/* Once per minute. */
	mod_timer(&sync_xen_wallclock_timer, jiffies + 60*HZ);
}

static unsigned long long local_clock(void)
{
	unsigned int cpu = get_cpu();
	struct shadow_time_info *shadow = &per_cpu(shadow_time, cpu);
	u64 time;
	u32 local_time_version;

	do {
		local_time_version = shadow->version;
		rdtsc_barrier();
		time = shadow->system_timestamp + get_nsec_offset(shadow);
		if (!time_values_up_to_date())
			get_time_values_from_xen(cpu);
		barrier();
	} while (local_time_version != shadow->version);

	put_cpu();

	return time;
}

/*
 * Runstate accounting
 */
static void get_runstate_snapshot(struct vcpu_runstate_info *res)
{
	u64 state_time;
	struct vcpu_runstate_info *state;

	BUG_ON(preemptible());

	state = &__get_cpu_var(runstate);

	do {
		state_time = get64_local(&state->state_entry_time);
		*res = *state;
	} while (get64_local(&state->state_entry_time) != state_time);

	WARN_ON_ONCE(res->state != RUNSTATE_running);
}

/*
 * Xen sched_clock implementation.  Returns the number of unstolen
 * nanoseconds, which is nanoseconds the VCPU spent in RUNNING+BLOCKED
 * states.
 */
unsigned long long sched_clock(void)
{
	struct vcpu_runstate_info runstate;
	cycle_t now;
	u64 ret;
	s64 offset;

	/*
	 * Ideally sched_clock should be called on a per-cpu basis
	 * anyway, so preempt should already be disabled, but that's
	 * not current practice at the moment.
	 */
	preempt_disable();

	now = local_clock();

	get_runstate_snapshot(&runstate);

	offset = now - runstate.state_entry_time;
	if (offset < 0)
		offset = 0;

	ret = offset + runstate.time[RUNSTATE_running]
	      + runstate.time[RUNSTATE_blocked];

	preempt_enable();

	return ret;
}

unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (!user_mode_vm(regs) && in_lock_functions(pc)) {
#ifdef CONFIG_FRAME_POINTER
		return *(unsigned long *)(regs->bp + sizeof(long));
#else
		unsigned long *sp =
			(unsigned long *)kernel_stack_pointer(regs);

		/*
		 * Return address is either directly at stack pointer
		 * or above a saved flags. Eflags has bits 22-31 zero,
		 * kernel addresses don't.
		 */
		if (sp[0] >> 22)
			return sp[0];
		if (sp[1] >> 22)
			return sp[1];
#endif
	}

	return pc;
}
EXPORT_SYMBOL(profile_pc);

/*
 * Default timer interrupt handler
 */
irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	s64 delta, delta_cpu, stolen, blocked;
	unsigned int i, cpu = smp_processor_id();
	struct shadow_time_info *shadow = &per_cpu(shadow_time, cpu);
	struct vcpu_runstate_info runstate;

	/* Keep nmi watchdog up to date */
	inc_irq_stat(irq0_irqs);

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

		get_runstate_snapshot(&runstate);
	} while (!time_values_up_to_date());

	if ((unlikely(delta < -(s64)permitted_clock_jitter) ||
	     unlikely(delta_cpu < -(s64)permitted_clock_jitter))
	    && printk_ratelimit()) {
		printk("Timer ISR/%u: Time went backwards: "
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
			clobber_induction_variable(delta);
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
	 * ensures that the ticks are accounted as stolen.
	 */
	stolen = runstate.time[RUNSTATE_runnable]
		 + runstate.time[RUNSTATE_offline]
		 - per_cpu(processed_stolen_time, cpu);
	if ((stolen > 0) && (delta_cpu > 0)) {
		delta_cpu -= stolen;
		if (unlikely(delta_cpu < 0))
			stolen += delta_cpu; /* clamp local-time progress */
		do_div(stolen, NS_PER_TICK);
		per_cpu(processed_stolen_time, cpu) += stolen * NS_PER_TICK;
		per_cpu(processed_system_time, cpu) += stolen * NS_PER_TICK;
		account_steal_time((cputime_t)stolen);
	}

	/*
	 * Account blocked ticks.
	 * ensures that the ticks are accounted as idle/wait.
	 */
	blocked = runstate.time[RUNSTATE_blocked]
		  - per_cpu(processed_blocked_time, cpu);
	if ((blocked > 0) && (delta_cpu > 0)) {
		delta_cpu -= blocked;
		if (unlikely(delta_cpu < 0))
			blocked += delta_cpu; /* clamp local-time progress */
		do_div(blocked, NS_PER_TICK);
		per_cpu(processed_blocked_time, cpu) += blocked * NS_PER_TICK;
		per_cpu(processed_system_time, cpu)  += blocked * NS_PER_TICK;
		account_idle_time((cputime_t)blocked);
	}

	/* Account user/system ticks. */
	if (delta_cpu > 0) {
		do_div(delta_cpu, NS_PER_TICK);
		per_cpu(processed_system_time, cpu) += delta_cpu * NS_PER_TICK;
		if (user_mode_vm(get_irq_regs()))
			account_user_time(current, (cputime_t)delta_cpu,
					  (cputime_t)delta_cpu);
		else if (current != idle_task(cpu))
			account_system_time(current, HARDIRQ_OFFSET,
					    (cputime_t)delta_cpu,
					    (cputime_t)delta_cpu);
		else
			account_idle_time((cputime_t)delta_cpu);
	}

	/* Offlined for more than a few seconds? Avoid lockup warnings. */
	if (stolen > 5*HZ)
		touch_softlockup_watchdog();

	/* Local timer processing (see update_process_times()). */
	run_local_timers();
	rcu_check_callbacks(cpu, user_mode_vm(get_irq_regs()));
	printk_tick();
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

static void init_missing_ticks_accounting(unsigned int cpu)
{
	struct vcpu_register_runstate_memory_area area;
	struct vcpu_runstate_info *runstate = &per_cpu(runstate, cpu);
	int rc;

	memset(runstate, 0, sizeof(*runstate));

	area.addr.v = runstate;
	rc = HYPERVISOR_vcpu_op(VCPUOP_register_runstate_memory_area, cpu, &area);
	WARN_ON(rc && rc != -ENOSYS);

	per_cpu(processed_blocked_time, cpu) =
		runstate->time[RUNSTATE_blocked];
	per_cpu(processed_stolen_time, cpu) =
		runstate->time[RUNSTATE_runnable] +
		runstate->time[RUNSTATE_offline];
}

static cycle_t cs_last;

static cycle_t xen_clocksource_read(struct clocksource *cs)
{
#ifdef CONFIG_SMP
	cycle_t last = get64(&cs_last);
	cycle_t ret = local_clock();

	if (unlikely((s64)(ret - last) < 0)) {
		if (last - ret > permitted_clock_jitter
		    && printk_ratelimit()) {
			unsigned int cpu = get_cpu();
			struct shadow_time_info *shadow = &per_cpu(shadow_time, cpu);

			printk(KERN_WARNING "clocksource/%u: "
			       "Time went backwards: "
			       "ret=%Lx delta=%Ld shadow=%Lx offset=%Lx\n",
			       cpu, ret, ret - last, shadow->system_timestamp,
			       get_nsec_offset(shadow));
			put_cpu();
		}
		return last;
	}

	for (;;) {
		cycle_t cur = cmpxchg64(&cs_last, last, ret);

		if (cur == last || (s64)(ret - cur) < 0)
			return ret;
		last = cur;
	}
#else
	return local_clock();
#endif
}

/* No locking required. Interrupts are disabled on all CPUs. */
static void xen_clocksource_resume(void)
{
	unsigned int cpu;

	init_cpu_khz();

	for_each_online_cpu(cpu) {
		switch (HYPERVISOR_vcpu_op(VCPUOP_set_periodic_timer, cpu,
					   &xen_set_periodic_tick)) {
		case 0:
#if CONFIG_XEN_COMPAT <= 0x030004
		case -ENOSYS:
#endif
			break;
		default:
			BUG();
		}
		get_time_values_from_xen(cpu);
		per_cpu(processed_system_time, cpu) =
			per_cpu(shadow_time, 0).system_timestamp;
		init_missing_ticks_accounting(cpu);
	}

	processed_system_time = per_cpu(shadow_time, 0).system_timestamp;

	cs_last = local_clock();
}

static struct clocksource clocksource_xen = {
	.name			= "xen",
	.rating			= 400,
	.read			= xen_clocksource_read,
	.mask			= CLOCKSOURCE_MASK(64),
	.mult			= 1 << XEN_SHIFT,		/* time directly in nanoseconds */
	.shift			= XEN_SHIFT,
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS,
	.resume			= xen_clocksource_resume,
};

void xen_read_persistent_clock(struct timespec *ts)
{
	const shared_info_t *s = HYPERVISOR_shared_info;
	u32 version, sec, nsec;
	u64 delta;

	do {
		version = s->wc_version;
		rmb();
		sec     = s->wc_sec;
		nsec    = s->wc_nsec;
		rmb();
	} while ((s->wc_version & 1) | (version ^ s->wc_version));

	delta = local_clock() + (u64)sec * NSEC_PER_SEC + nsec;
	do_div(delta, NSEC_PER_SEC);

	ts->tv_sec = delta;
	ts->tv_nsec = 0;
}

int xen_update_persistent_clock(void)
{
	if (!is_initial_xendomain())
		return -1;
	mod_timer(&sync_xen_wallclock_timer, jiffies + 1);
	return 0;
}

/* Dynamically-mapped IRQ. */
static int __read_mostly timer_irq = -1;
static struct irqaction timer_action = {
	.handler = timer_interrupt,
	.flags   = IRQF_DISABLED|IRQF_TIMER,
	.name    = "timer"
};

static void __init setup_cpu0_timer_irq(void)
{
	timer_irq = bind_virq_to_irqaction(VIRQ_TIMER, 0, &timer_action);
	BUG_ON(timer_irq < 0);
}

void __init time_init(void)
{
	init_cpu_khz();
	printk(KERN_INFO "Xen reported: %u.%03u MHz processor.\n",
	       cpu_khz / 1000, cpu_khz % 1000);

	switch (HYPERVISOR_vcpu_op(VCPUOP_set_periodic_timer, 0,
				   &xen_set_periodic_tick)) {
	case 0:
#if CONFIG_XEN_COMPAT <= 0x030004
	case -ENOSYS:
#endif
		break;
	default:
		BUG();
	}

	get_time_values_from_xen(0);

	processed_system_time = per_cpu(shadow_time, 0).system_timestamp;
	per_cpu(processed_system_time, 0) = processed_system_time;
	init_missing_ticks_accounting(0);

	clocksource_register(&clocksource_xen);

	update_wallclock();

	use_tsc_delay();

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

	cpumask_set_cpu(cpu, nohz_cpu_mask);

	/* See matching smp_mb in rcu_start_batch in rcupdate.c.  These mbs  */
	/* ensure that if __rcu_pending (nested in rcu_needs_cpu) fetches a  */
	/* value of rcp->cur that matches rdp->quiescbatch and allows us to  */
	/* stop the hz timer then the cpumasks created for subsequent values */
	/* of cur in rcu_start_batch are guaranteed to pick up the updated   */
	/* nohz_cpu_mask and so will not depend on this cpu.                 */

	smp_mb();

	/* Leave ourselves in tick mode if rcu or softirq or timer pending. */
	if (rcu_needs_cpu(cpu) || printk_needs_cpu(cpu) ||
	    local_softirq_pending() ||
	    (j = get_next_timer_interrupt(jiffies),
	     time_before_eq(j, jiffies))) {
		cpumask_clear_cpu(cpu, nohz_cpu_mask);
		j = jiffies + 1;
	}

	singleshot.timeout_abs_ns = jiffies_to_st(j) + NS_PER_TICK/2;
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
	cpumask_clear_cpu(smp_processor_id(), nohz_cpu_mask);
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
		VOID(HYPERVISOR_vcpu_op(VCPUOP_down, smp_processor_id(), NULL));
}
EXPORT_SYMBOL(xen_halt);

#ifdef CONFIG_SMP
int __cpuinit local_setup_timer(unsigned int cpu)
{
	int seq, irq;

	BUG_ON(cpu == 0);

	switch (HYPERVISOR_vcpu_op(VCPUOP_set_periodic_timer, cpu,
			   &xen_set_periodic_tick)) {
	case 0:
#if CONFIG_XEN_COMPAT <= 0x030004
	case -ENOSYS:
#endif
		break;
	default:
		BUG();
	}

	do {
		seq = read_seqbegin(&xtime_lock);
		/* Use cpu0 timestamp: cpu's shadow is not initialised yet. */
		per_cpu(processed_system_time, cpu) =
			per_cpu(shadow_time, 0).system_timestamp;
		init_missing_ticks_accounting(cpu);
	} while (read_seqretry(&xtime_lock, seq));

	irq = bind_virq_to_irqaction(VIRQ_TIMER, cpu, &timer_action);
	if (irq < 0)
		return irq;
	BUG_ON(timer_irq != irq);

	return 0;
}

void __cpuinit local_teardown_timer(unsigned int cpu)
{
	BUG_ON(cpu == 0);
	unbind_from_per_cpu_irq(timer_irq, cpu, &timer_action);
}
#endif

#ifdef CONFIG_CPU_FREQ
static int time_cpufreq_notifier(struct notifier_block *nb, unsigned long val, 
				void *data)
{
	struct cpufreq_freqs *freq = data;
	struct xen_platform_op op;

	if (cpu_has(&cpu_data(freq->cpu), X86_FEATURE_CONSTANT_TSC))
		return 0;

	if (val == CPUFREQ_PRECHANGE)
		return 0;

	op.cmd = XENPF_change_freq;
	op.u.change_freq.flags = 0;
	op.u.change_freq.cpu = freq->cpu;
	op.u.change_freq.freq = (u64)freq->new * 1000;
	WARN_ON(HYPERVISOR_platform_op(&op));

	return 0;
}

static struct notifier_block time_cpufreq_notifier_block = {
	.notifier_call = time_cpufreq_notifier
};

static int __init cpufreq_time_setup(void)
{
	if (!cpufreq_register_notifier(&time_cpufreq_notifier_block,
			CPUFREQ_TRANSITION_NOTIFIER)) {
		printk(KERN_ERR "failed to set up cpufreq notifier\n");
		return -ENODEV;
	}
	return 0;
}

core_initcall(cpufreq_time_setup);
#endif

/*
 * /proc/sys/xen: This really belongs in another file. It can stay here for
 * now however.
 */
static ctl_table xen_subtable[] = {
	{
		.ctl_name	= CTL_XEN_INDEPENDENT_WALLCLOCK,
		.procname	= "independent_wallclock",
		.data		= &independent_wallclock,
		.maxlen		= sizeof(independent_wallclock),
		.mode		= 0644,
		.strategy	= sysctl_data,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= CTL_XEN_PERMITTED_CLOCK_JITTER,
		.procname	= "permitted_clock_jitter",
		.data		= &permitted_clock_jitter,
		.maxlen		= sizeof(permitted_clock_jitter),
		.mode		= 0644,
		.strategy	= sysctl_data,
		.proc_handler	= proc_doulongvec_minmax
	},
	{ }
};
static ctl_table xen_table[] = {
	{
		.ctl_name	= CTL_XEN,
		.procname	= "xen",
		.mode		= 0555,
		.child		= xen_subtable
	},
	{ }
};
static int __init xen_sysctl_init(void)
{
	(void)register_sysctl_table(xen_table);
	return 0;
}
__initcall(xen_sysctl_init);
