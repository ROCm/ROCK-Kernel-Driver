/*
 *  arch/s390/kernel/time.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "arch/i386/kernel/time.c"
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/module.h>
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
#include <linux/types.h>
#include <linux/profile.h>
#include <linux/timex.h>
#include <linux/config.h>

#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/s390_ext.h>
#include <asm/div64.h>
#include <asm/irq.h>
#ifdef CONFIG_VIRT_TIMER
#include <asm/timer.h>
#endif

/* change this if you have some constant time drift */
#define USECS_PER_JIFFY     ((unsigned long) 1000000/HZ)
#define CLK_TICKS_PER_JIFFY ((unsigned long) USECS_PER_JIFFY << 12)

/*
 * Create a small time difference between the timer interrupts
 * on the different cpus to avoid lock contention.
 */
#define CPU_DEVIATION       (smp_processor_id() << 12)

#define TICK_SIZE tick

u64 jiffies_64 = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

static ext_int_info_t ext_int_info_cc;
static u64 init_timer_cc;
static u64 jiffies_timer_cc;
static u64 xtime_cc;

extern unsigned long wall_jiffies;

#ifdef CONFIG_VIRT_TIMER
#define VTIMER_MAGIC (0x4b87ad6e + 1)
static ext_int_info_t ext_int_info_timer;
DEFINE_PER_CPU(struct vtimer_queue, virt_cpu_timer);
#endif

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return ((get_clock() - jiffies_timer_cc) * 1000) >> 12;
}

void tod_to_timeval(__u64 todval, struct timespec *xtime)
{
	unsigned long long sec;

	sec = todval >> 12;
	do_div(sec, 1000000);
	xtime->tv_sec = sec;
	todval -= (sec * 1000000) << 12;
	xtime->tv_nsec = ((todval * 1000) >> 12);
}

static inline unsigned long do_gettimeoffset(void) 
{
	__u64 now;

        now = (get_clock() - jiffies_timer_cc) >> 12;
	/* We require the offset from the latest update of xtime */
	now -= (__u64) wall_jiffies*USECS_PER_JIFFY;
	return (unsigned long) now;
}

/*
 * This version of gettimeofday has microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);

		sec = xtime.tv_sec;
		usec = xtime.tv_nsec / 1000 + do_gettimeoffset();
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

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
	/* This is revolting. We need to set the xtime.tv_nsec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
	nsec -= do_gettimeoffset() * 1000;

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

#ifndef CONFIG_ARCH_S390X

static inline __u32
__calculate_ticks(__u64 elapsed)
{
	register_pair rp;

	rp.pair = elapsed >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (CLK_TICKS_PER_JIFFY >> 1));
	return rp.subreg.odd;
}

#else /* CONFIG_ARCH_S390X */

static inline __u32
__calculate_ticks(__u64 elapsed)
{
	return elapsed / CLK_TICKS_PER_JIFFY;
}

#endif /* CONFIG_ARCH_S390X */


#ifdef CONFIG_PROFILING
extern char _stext, _etext;

/*
 * The profiling function is SMP safe. (nothing can mess
 * around with "current", and the profiling counters are
 * updated with atomic operations). This is especially
 * useful with a profiling multiplier != 1
 */
static inline void s390_do_profile(struct pt_regs * regs)
{
	unsigned long eip;
	extern cpumask_t prof_cpu_mask;

	profile_hook(regs);

	if (user_mode(regs))
		return;

	if (!prof_buffer)
		return;

	eip = instruction_pointer(regs);

	/*
	 * Only measure the CPUs specified by /proc/irq/prof_cpu_mask.
	 * (default is all CPUs.)
	 */
	if (!cpu_isset(smp_processor_id(), prof_cpu_mask))
		return;

	eip -= (unsigned long) &_stext;
	eip >>= prof_shift;
	/*
	 * Don't ignore out-of-bounds EIP values silently,
	 * put them into the last histogram slot, so if
	 * present, they will show up as a sharp peak.
	 */
	if (eip > prof_len-1)
		eip = prof_len-1;
	atomic_inc((atomic_t *)&prof_buffer[eip]);
}
#else
#define s390_do_profile(regs)  do { ; } while(0)
#endif /* CONFIG_PROFILING */


/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
void account_ticks(struct pt_regs *regs)
{
	__u64 tmp;
	__u32 ticks;

	/* Calculate how many ticks have passed. */
	tmp = S390_lowcore.int_clock - S390_lowcore.jiffy_timer;
	if (tmp >= 2*CLK_TICKS_PER_JIFFY) {  /* more than two ticks ? */
		ticks = __calculate_ticks(tmp) + 1;
		S390_lowcore.jiffy_timer +=
			CLK_TICKS_PER_JIFFY * (__u64) ticks;
	} else if (tmp >= CLK_TICKS_PER_JIFFY) {
		ticks = 2;
		S390_lowcore.jiffy_timer += 2*CLK_TICKS_PER_JIFFY;
	} else {
		ticks = 1;
		S390_lowcore.jiffy_timer += CLK_TICKS_PER_JIFFY;
	}

	/* set clock comparator for next tick */
	tmp = S390_lowcore.jiffy_timer + CPU_DEVIATION;
        asm volatile ("SCKC %0" : : "m" (tmp));

#ifdef CONFIG_SMP
	/*
	 * Do not rely on the boot cpu to do the calls to do_timer.
	 * Spread it over all cpus instead.
	 */
	write_seqlock(&xtime_lock);
	if (S390_lowcore.jiffy_timer > xtime_cc) {
		__u32 xticks;

		tmp = S390_lowcore.jiffy_timer - xtime_cc;
		if (tmp >= 2*CLK_TICKS_PER_JIFFY) {
			xticks = __calculate_ticks(tmp);
			xtime_cc += (__u64) xticks * CLK_TICKS_PER_JIFFY;
		} else {
			xticks = 1;
			xtime_cc += CLK_TICKS_PER_JIFFY;
		}
		while (xticks--)
			do_timer(regs);
	}
	write_sequnlock(&xtime_lock);
	while (ticks--)
		update_process_times(user_mode(regs));
#else
	while (ticks--)
		do_timer(regs);
#endif
	s390_do_profile(regs);
}

#ifdef CONFIG_VIRT_TIMER
void start_cpu_timer(void)
{
	struct vtimer_queue *vt_list;

	vt_list = &per_cpu(virt_cpu_timer, smp_processor_id());
	set_vtimer(vt_list->idle);
}

int stop_cpu_timer(void)
{
	__u64 done;
	struct vtimer_queue *vt_list;

	vt_list = &per_cpu(virt_cpu_timer, smp_processor_id());

	/* nothing to do */
	if (list_empty(&vt_list->list)) {
		vt_list->idle = VTIMER_MAX_SLICE;
		goto fire;
	}

	/* store progress */
	asm volatile ("STPT %0" : "=m" (done));

	/*
	 * If done is negative we do not stop the CPU timer
	 * because we will get instantly an interrupt that
	 * will start the CPU timer again.
	 */
	if (done & 1LL<<63)
		return 1;
	else
		vt_list->offset += vt_list->to_expire - done;

	/* save the actual expire value */
	vt_list->idle = done;

	/*
	 * We cannot halt the CPU timer, we just write a value that
	 * nearly never expires (only after 71 years) and re-write
	 * the stored expire value if we continue the timer
	 */
 fire:
	set_vtimer(VTIMER_MAX_SLICE);
	return 0;
}

void set_vtimer(__u64 expires)
{
	asm volatile ("SPT %0" : : "m" (expires));

	/* store expire time for this CPU timer */
	per_cpu(virt_cpu_timer, smp_processor_id()).to_expire = expires;
}

/*
 * Sorted add to a list. List is linear searched until first bigger
 * element is found.
 */
void list_add_sorted(struct vtimer_list *timer, struct list_head *head)
{
	struct vtimer_list *event;

	list_for_each_entry(event, head, entry) {
		if (event->expires > timer->expires) {
			list_add_tail(&timer->entry, &event->entry);
			return;
		}
	}
	list_add_tail(&timer->entry, head);
}

/*
 * Do the callback functions of expired vtimer events.
 * Called from within the interrupt handler.
 */
static void do_callbacks(struct list_head *cb_list, struct pt_regs *regs)
{
	struct vtimer_queue *vt_list;
	struct vtimer_list *event, *tmp;
	void (*fn)(unsigned long, struct pt_regs*);
	unsigned long data;

	if (list_empty(cb_list))
		return;

	vt_list = &per_cpu(virt_cpu_timer, smp_processor_id());

	list_for_each_entry_safe(event, tmp, cb_list, entry) {
		fn = event->function;
		data = event->data;
		fn(data, regs);

		if (!event->interval)
			/* delete one shot timer */
			list_del_init(&event->entry);
		else {
			/* move interval timer back to list */
			spin_lock(&vt_list->lock);
			list_del_init(&event->entry);
			list_add_sorted(event, &vt_list->list);
			spin_unlock(&vt_list->lock);
		}
	}
}

/*
 * Handler for the virtual CPU timer.
 */
static void do_cpu_timer_interrupt(struct pt_regs *regs, __u16 error_code)
{
	int cpu;
	__u64 next, delta;
	struct vtimer_queue *vt_list;
	struct vtimer_list *event, *tmp;
	struct list_head *ptr;
	/* the callback queue */
	struct list_head cb_list;

	INIT_LIST_HEAD(&cb_list);
	cpu = smp_processor_id();
	vt_list = &per_cpu(virt_cpu_timer, cpu);

	/* walk timer list, fire all expired events */
	spin_lock(&vt_list->lock);

	if (vt_list->to_expire < VTIMER_MAX_SLICE)
		vt_list->offset += vt_list->to_expire;

	list_for_each_entry_safe(event, tmp, &vt_list->list, entry) {
		if (event->expires > vt_list->offset)
			/* found first unexpired event, leave */
			break;

		/* re-charge interval timer, we have to add the offset */
		if (event->interval)
			event->expires = event->interval + vt_list->offset;

		/* move expired timer to the callback queue */
		list_move_tail(&event->entry, &cb_list);
	}
	spin_unlock(&vt_list->lock);
	do_callbacks(&cb_list, regs);

	/* next event is first in list */
	spin_lock(&vt_list->lock);
	if (!list_empty(&vt_list->list)) {
		ptr = vt_list->list.next;
		event = list_entry(ptr, struct vtimer_list, entry);
		next = event->expires - vt_list->offset;

		/* add the expired time from this interrupt handler
		 * and the callback functions
		 */
		asm volatile ("STPT %0" : "=m" (delta));
		delta = 0xffffffffffffffffLL - delta + 1;
		vt_list->offset += delta;
		next -= delta;
	} else {
		vt_list->offset = 0;
		next = VTIMER_MAX_SLICE;
	}
	spin_unlock(&vt_list->lock);
	set_vtimer(next);
}
#endif

#ifdef CONFIG_NO_IDLE_HZ

#ifdef CONFIG_NO_IDLE_HZ_INIT
int sysctl_hz_timer = 0;
#else
int sysctl_hz_timer = 1;
#endif

/*
 * Start the HZ tick on the current CPU.
 * Only cpu_idle may call this function.
 */
void start_hz_timer(struct pt_regs *regs)
{
	__u64 tmp;
	__u32 ticks;

	if (!cpu_isset(smp_processor_id(), nohz_cpu_mask))
		return;

	/* Calculate how many ticks have passed */
	asm volatile ("STCK 0(%0)" : : "a" (&tmp) : "memory", "cc");
	tmp = tmp + CLK_TICKS_PER_JIFFY - S390_lowcore.jiffy_timer;
	ticks = __calculate_ticks(tmp);
	S390_lowcore.jiffy_timer += CLK_TICKS_PER_JIFFY * (__u64) ticks;

	/* Set the clock comparator to the next tick. */
	tmp = S390_lowcore.jiffy_timer + CPU_DEVIATION;
	asm volatile ("SCKC %0" : : "m" (tmp));

	/* Charge the ticks. */
	if (ticks > 0) {
#ifdef CONFIG_SMP
		/*
		 * Do not rely on the boot cpu to do the calls to do_timer.
		 * Spread it over all cpus instead.
		 */
		write_seqlock(&xtime_lock);
		if (S390_lowcore.jiffy_timer > xtime_cc) {
			__u32 xticks;

			tmp = S390_lowcore.jiffy_timer - xtime_cc;
			if (tmp >= 2*CLK_TICKS_PER_JIFFY) {
				xticks = __calculate_ticks(tmp);
				xtime_cc += (__u64) xticks*CLK_TICKS_PER_JIFFY;
			} else {
				xticks = 1;
				xtime_cc += CLK_TICKS_PER_JIFFY;
			}
			while (xticks--)
				do_timer(regs);
		}
		write_sequnlock(&xtime_lock);
		while (ticks--)
			update_process_times(user_mode(regs));
#else
		while (ticks--)
			do_timer(regs);
#endif
	}
	cpu_clear(smp_processor_id(), nohz_cpu_mask);
}

/*
 * Stop the HZ tick on the current CPU.
 * Only cpu_idle may call this function.
 */
int stop_hz_timer(void)
{
	__u64 timer;

	if (sysctl_hz_timer != 0)
		return 1;

	/*
	 * Leave the clock comparator set up for the next timer
	 * tick if either rcu or a softirq is pending.
	 */
	if (rcu_pending(smp_processor_id()) || local_softirq_pending())
		return 1;

	/*
	 * This cpu is going really idle. Set up the clock comparator
	 * for the next event.
	 */
	cpu_set(smp_processor_id(), nohz_cpu_mask);
	timer = (__u64) (next_timer_interrupt() - jiffies) + jiffies_64;
	timer = jiffies_timer_cc + timer * CLK_TICKS_PER_JIFFY;
	asm volatile ("SCKC %0" : : "m" (timer));

	return 0;
}
#endif

#if defined(CONFIG_VIRT_TIMER) || defined(CONFIG_NO_IDLE_HZ)

void do_monitor_call(struct pt_regs *regs, long interruption_code)
{
	/* disable monitor call class 0 */
	__ctl_clear_bit(8, 15);

#ifdef CONFIG_VIRT_TIMER
	start_cpu_timer();
#endif
#ifdef CONFIG_NO_IDLE_HZ
	start_hz_timer(regs);
#endif
}

/*
 * called from cpu_idle to stop any timers
 * returns 1 if CPU should not be stopped
 */
int stop_timers(void)
{
#ifdef CONFIG_VIRT_TIMER
	if (stop_cpu_timer())
		return 1;
#endif

#ifdef CONFIG_NO_IDLE_HZ
	if (stop_hz_timer())
		return 1;
#endif

	/* enable monitor call class 0 */
	__ctl_set_bit(8, 15);

	return 0;
}

#endif

/*
 * Start the clock comparator and the virtual CPU timer
 * on the current CPU.
 */
void init_cpu_timer(void)
{
	unsigned long cr0;
	__u64 timer;
#ifdef CONFIG_VIRT_TIMER
	struct vtimer_queue *vt_list;
#endif

	timer = jiffies_timer_cc + jiffies_64 * CLK_TICKS_PER_JIFFY;
	S390_lowcore.jiffy_timer = timer + CLK_TICKS_PER_JIFFY;
	timer += CLK_TICKS_PER_JIFFY + CPU_DEVIATION;
	asm volatile ("SCKC %0" : : "m" (timer));
        /* allow clock comparator timer interrupt */
	__ctl_store(cr0, 0, 0);
        cr0 |= 0x800;
	__ctl_load(cr0, 0, 0);

#ifdef CONFIG_VIRT_TIMER
	/* kick the virtual timer */
	timer = VTIMER_MAX_SLICE;
	asm volatile ("SPT %0" : : "m" (timer));
	__ctl_store(cr0, 0, 0);
	cr0 |= 0x400;
	__ctl_load(cr0, 0, 0);

	vt_list = &per_cpu(virt_cpu_timer, smp_processor_id());
	INIT_LIST_HEAD(&vt_list->list);
	spin_lock_init(&vt_list->lock);
	vt_list->to_expire = 0;
	vt_list->offset = 0;
	vt_list->idle = 0;
#endif
}

/*
 * Initialize the TOD clock and the CPU timer of
 * the boot cpu.
 */
void __init time_init(void)
{
	__u64 set_time_cc;
	int cc;

        /* kick the TOD clock */
        asm volatile ("STCK 0(%1)\n\t"
                      "IPM  %0\n\t"
                      "SRL  %0,28" : "=r" (cc) : "a" (&init_timer_cc) 
				   : "memory", "cc");
        switch (cc) {
        case 0: /* clock in set state: all is fine */
                break;
        case 1: /* clock in non-set state: FIXME */
                printk("time_init: TOD clock in non-set state\n");
                break;
        case 2: /* clock in error state: FIXME */
                printk("time_init: TOD clock in error state\n");
                break;
        case 3: /* clock in stopped or not-operational state: FIXME */
                printk("time_init: TOD clock stopped/non-operational\n");
                break;
        }
	jiffies_timer_cc = init_timer_cc - jiffies_64 * CLK_TICKS_PER_JIFFY;

	/* set xtime */
	xtime_cc = init_timer_cc + CLK_TICKS_PER_JIFFY;
	set_time_cc = init_timer_cc - 0x8126d60e46000000LL +
		(0x3c26700LL*1000000*4096);
        tod_to_timeval(set_time_cc, &xtime);
        set_normalized_timespec(&wall_to_monotonic,
                                -xtime.tv_sec, -xtime.tv_nsec);

	/* request the clock comparator external interrupt */
        if (register_early_external_interrupt(0x1004, 0,
					      &ext_int_info_cc) != 0)
                panic("Couldn't request external interrupt 0x1004");

#ifdef CONFIG_VIRT_TIMER
	/* request the cpu timer external interrupt */
	if (register_early_external_interrupt(0x1005, do_cpu_timer_interrupt,
					      &ext_int_info_timer) != 0)
		panic("Couldn't request external interrupt 0x1005");
#endif

        init_cpu_timer();
}

#ifdef CONFIG_VIRT_TIMER
void init_virt_timer(struct vtimer_list *timer)
{
	timer->magic = VTIMER_MAGIC;
	timer->function = NULL;
	INIT_LIST_HEAD(&timer->entry);
	spin_lock_init(&timer->lock);
}

static inline int check_vtimer(struct vtimer_list *timer)
{
	if (timer->magic != VTIMER_MAGIC)
		return -EINVAL;
	return 0;
}

static inline int vtimer_pending(struct vtimer_list *timer)
{
	return (!list_empty(&timer->entry));
}

/*
 * this function should only run on the specified CPU
 */
static void internal_add_vtimer(struct vtimer_list *timer)
{
	unsigned long flags;
	__u64 done;
	struct vtimer_list *event;
	struct vtimer_queue *vt_list;

	vt_list = &per_cpu(virt_cpu_timer, timer->cpu);
	spin_lock_irqsave(&vt_list->lock, flags);

	if (timer->cpu != smp_processor_id())
		printk("internal_add_vtimer: BUG, running on wrong CPU");

	/* if list is empty we only have to set the timer */
	if (list_empty(&vt_list->list)) {
		/* reset the offset, this may happen if the last timer was
		 * just deleted by mod_virt_timer and the interrupt
		 * didn't happen until here
		 */
		vt_list->offset = 0;
		goto fire;
	}

	/* save progress */
	asm volatile ("STPT %0" : "=m" (done));

	/* calculate completed work */
	done = vt_list->to_expire - done + vt_list->offset;
	vt_list->offset = 0;

	list_for_each_entry(event, &vt_list->list, entry)
		event->expires -= done;

 fire:
	list_add_sorted(timer, &vt_list->list);

	/* get first element, which is the next vtimer slice */
	event = list_entry(vt_list->list.next, struct vtimer_list, entry);

	set_vtimer(event->expires);
	spin_unlock_irqrestore(&vt_list->lock, flags);
	/* release CPU aquired in prepare_vtimer or mod_virt_timer() */
	put_cpu();
}

static inline int prepare_vtimer(struct vtimer_list *timer)
{
	if (check_vtimer(timer) || !timer->function) {
		printk("add_virt_timer: uninitialized timer\n");
		return -EINVAL;
	}

	if (!timer->expires || timer->expires > VTIMER_MAX_SLICE) {
		printk("add_virt_timer: invalid timer expire value!\n");
		return -EINVAL;
	}

	if (vtimer_pending(timer)) {
		printk("add_virt_timer: timer pending\n");
		return -EBUSY;
	}

	timer->cpu = get_cpu();
	return 0;
}

/*
 * add_virt_timer - add an oneshot virtual CPU timer
 */
void add_virt_timer(void *new)
{
	struct vtimer_list *timer;

	timer = (struct vtimer_list *)new;

	if (prepare_vtimer(timer) < 0)
		return;

	timer->interval = 0;
	internal_add_vtimer(timer);
}

/*
 * add_virt_timer_int - add an interval virtual CPU timer
 */
void add_virt_timer_periodic(void *new)
{
	struct vtimer_list *timer;

	timer = (struct vtimer_list *)new;

	if (prepare_vtimer(timer) < 0)
		return;

	timer->interval = timer->expires;
	internal_add_vtimer(timer);
}

/*
 * If we change a pending timer the function must be called on the CPU
 * where the timer is running on, e.g. by smp_call_function_on()
 *
 * The original mod_timer adds the timer if it is not pending. For compatibility
 * we do the same. The timer will be added on the current CPU as a oneshot timer.
 *
 * returns whether it has modified a pending timer (1) or not (0)
 */
int mod_virt_timer(struct vtimer_list *timer, __u64 expires)
{
	struct vtimer_queue *vt_list;
	unsigned long flags;
	int cpu;

	if (check_vtimer(timer) || !timer->function) {
		printk("mod_virt_timer: uninitialized timer\n");
		return	-EINVAL;
	}

	if (!expires || expires > VTIMER_MAX_SLICE) {
		printk("mod_virt_timer: invalid expire range\n");
		return -EINVAL;
	}

	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	if (timer->expires == expires && vtimer_pending(timer))
		return 1;

	cpu = get_cpu();
	vt_list = &per_cpu(virt_cpu_timer, cpu);

	/* disable interrupts before test if timer is pending */
	spin_lock_irqsave(&vt_list->lock, flags);

	/* if timer isn't pending add it on the current CPU */
	if (!vtimer_pending(timer)) {
		spin_unlock_irqrestore(&vt_list->lock, flags);
		/* we do not activate an interval timer with mod_virt_timer */
		timer->interval = 0;
		timer->expires = expires;
		timer->cpu = cpu;
		internal_add_vtimer(timer);
		return 0;
	}

	/* check if we run on the right CPU */
	if (timer->cpu != cpu) {
		printk("mod_virt_timer: running on wrong CPU, check your code\n");
		spin_unlock_irqrestore(&vt_list->lock, flags);
		put_cpu();
		return -EINVAL;
	}

	list_del_init(&timer->entry);
	timer->expires = expires;

	/* also change the interval if we have an interval timer */
	if (timer->interval)
		timer->interval = expires;

	/* the timer can't expire anymore so we can release the lock */
	spin_unlock_irqrestore(&vt_list->lock, flags);
	internal_add_vtimer(timer);
	return 1;
}

/*
 * delete a virtual timer
 *
 * returns whether the deleted timer was pending (1) or not (0)
 */
int del_virt_timer(struct vtimer_list *timer)
{
	unsigned long flags;
	struct vtimer_queue *vt_list;

	if (check_vtimer(timer)) {
		printk("del_virt_timer: timer not initialized\n");
		return -EINVAL;
	}

	/* check if timer is pending */
	if (!vtimer_pending(timer))
		return 0;

	if (!cpu_online(timer->cpu)) {
		printk("del_virt_timer: CPU not present!\n");
		return -1;
	}

	vt_list = &per_cpu(virt_cpu_timer, timer->cpu);
	spin_lock_irqsave(&vt_list->lock, flags);

	/* we don't interrupt a running timer, just let it expire! */
	list_del_init(&timer->entry);

	/* last timer removed */
	if (list_empty(&vt_list->list)) {
		vt_list->to_expire = 0;
		vt_list->offset = 0;
	}

	spin_unlock_irqrestore(&vt_list->lock, flags);
	return 1;
}
#endif

