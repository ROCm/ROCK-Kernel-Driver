/*
 * linux/arch/ia64/kernel/time.c
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999-2000 David Mosberger <davidm@hpl.hp.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 * Copyright (C) 1999-2000 VA Linux Systems
 * Copyright (C) 1999-2000 Walt Drummond <drummond@valinux.com>
 */
#include <linux/config.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/interrupt.h>

#include <asm/delay.h>
#include <asm/efi.h>
#include <asm/hw_irq.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/system.h>

extern rwlock_t xtime_lock;
extern unsigned long wall_jiffies;

#ifdef CONFIG_IA64_DEBUG_IRQ

unsigned long last_cli_ip;

#endif

static struct {
	unsigned long delta;
	union {
		unsigned long count;
		unsigned char pad[SMP_CACHE_BYTES];
	} next[NR_CPUS];
} itm;

static void
do_profile (unsigned long ip)
{
	extern unsigned long prof_cpu_mask;
	extern char _stext;

	if (!((1UL << smp_processor_id()) & prof_cpu_mask))
		return;

	if (prof_buffer && current->pid) {
		ip -= (unsigned long) &_stext;
		ip >>= prof_shift;
		/*
		 * Don't ignore out-of-bounds IP values silently,
		 * put them into the last histogram slot, so if
		 * present, they will show up as a sharp peak.
		 */
		if (ip > prof_len - 1)
			ip = prof_len - 1;

		atomic_inc((atomic_t *) &prof_buffer[ip]);
	} 
}

/*
 * Return the number of micro-seconds that elapsed since the last
 * update to jiffy.  The xtime_lock must be at least read-locked when
 * calling this routine.
 */
static inline unsigned long
gettimeoffset (void)
{
#ifdef CONFIG_SMP
	/*
	 * The code below doesn't work for SMP because only CPU 0
	 * keeps track of the time.
	 */
	return 0;
#else
	unsigned long now = ia64_get_itc(), last_tick;
	unsigned long elapsed_cycles, lost = jiffies - wall_jiffies;

	last_tick = (itm.next[smp_processor_id()].count - (lost+1)*itm.delta);
# if 1
	if ((long) (now - last_tick) < 0) {
		printk("Yikes: now < last_tick (now=0x%lx,last_tick=%lx)!  No can do.\n",
		       now, last_tick);
		return 0;
	}
# endif
	elapsed_cycles = now - last_tick;
	return (elapsed_cycles*my_cpu_data.usec_per_cyc) >> IA64_USEC_PER_CYC_SHIFT;
#endif
}

void
do_settimeofday (struct timeval *tv)
{
	write_lock_irq(&xtime_lock);
	{
		/*
		 * This is revolting. We need to set "xtime"
		 * correctly. However, the value in this location is
		 * the value at the most recent update of wall time.
		 * Discover what correction gettimeofday would have
		 * done, and then undo it!
		 */
		tv->tv_usec -= gettimeoffset();
		tv->tv_usec -= (jiffies - wall_jiffies) * (1000000 / HZ);

		while (tv->tv_usec < 0) {
			tv->tv_usec += 1000000;
			tv->tv_sec--;
		}

		xtime = *tv;
		time_adjust = 0;		/* stop active adjtime() */
		time_status |= STA_UNSYNC;
		time_maxerror = NTP_PHASE_LIMIT;
		time_esterror = NTP_PHASE_LIMIT;
	}
	write_unlock_irq(&xtime_lock);
}

void
do_gettimeofday (struct timeval *tv)
{
	unsigned long flags, usec, sec;

	read_lock_irqsave(&xtime_lock, flags);
	{
		usec = gettimeoffset();
	
		sec = xtime.tv_sec;
		usec += xtime.tv_usec;
	}
	read_unlock_irqrestore(&xtime_lock, flags);

	while (usec >= 1000000) {
		usec -= 1000000;
		++sec;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

static void
timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long new_itm;

	new_itm = itm.next[cpu].count;

	if (!time_after(ia64_get_itc(), new_itm))
		printk("Oops: timer tick before it's due (itc=%lx,itm=%lx)\n",
		       ia64_get_itc(), new_itm);

	while (1) {
		/*
		 * Do kernel PC profiling here.  We multiply the instruction number by
		 * four so that we can use a prof_shift of 2 to get instruction-level
		 * instead of just bundle-level accuracy.
		 */
		if (!user_mode(regs)) 
			do_profile(regs->cr_iip + 4*ia64_psr(regs)->ri);

#ifdef CONFIG_SMP
		smp_do_timer(regs);
#endif
		if (smp_processor_id() == 0) {
			/*
			 * Here we are in the timer irq handler. We have irqs locally
			 * disabled, but we don't know if the timer_bh is running on
			 * another CPU. We need to avoid to SMP race by acquiring the
			 * xtime_lock.
			 */
			write_lock(&xtime_lock);
			do_timer(regs);
			write_unlock(&xtime_lock);
		}

		new_itm += itm.delta;
		itm.next[cpu].count = new_itm;
		if (time_after(new_itm, ia64_get_itc()))
			break;
	}

	/*
	 * If we're too close to the next clock tick for comfort, we
	 * increase the saftey margin by intentionally dropping the
	 * next tick(s).  We do NOT update itm.next accordingly
	 * because that would force us to call do_timer() which in
	 * turn would let our clock run too fast (with the potentially
	 * devastating effect of losing monotony of time).
	 */
	while (!time_after(new_itm, ia64_get_itc() + itm.delta/2))
		new_itm += itm.delta;
	ia64_set_itm(new_itm);
}

#ifdef CONFIG_IA64_SOFTSDV_HACKS

/*
 * Interrupts must be disabled before calling this routine.
 */
void
ia64_reset_itm (void)
{
	timer_interrupt(0, 0, ia64_task_regs(current));
}

#endif

/*
 * Encapsulate access to the itm structure for SMP.
 */
void __init
ia64_cpu_local_tick(void)
{
#ifdef CONFIG_IA64_SOFTSDV_HACKS
	ia64_set_itc(0);
#endif

	/* arrange for the cycle counter to generate a timer interrupt: */
	ia64_set_itv(TIMER_IRQ, 0);
	itm.next[smp_processor_id()].count = ia64_get_itc() + itm.delta;
	ia64_set_itm(itm.next[smp_processor_id()].count);
}

void __init
ia64_init_itm (void)
{
	unsigned long platform_base_freq, itc_freq, drift;
	struct pal_freq_ratio itc_ratio, proc_ratio;
	long status;

	/*
	 * According to SAL v2.6, we need to use a SAL call to determine the
	 * platform base frequency and then a PAL call to determine the
	 * frequency ratio between the ITC and the base frequency.
	 */
	status = ia64_sal_freq_base(SAL_FREQ_BASE_PLATFORM, &platform_base_freq, &drift);
	if (status != 0) {
		printk("SAL_FREQ_BASE_PLATFORM failed: %s\n", ia64_sal_strerror(status));
	} else {
		status = ia64_pal_freq_ratios(&proc_ratio, 0, &itc_ratio);
		if (status != 0)
			printk("PAL_FREQ_RATIOS failed with status=%ld\n", status);
	}
	if (status != 0) {
		/* invent "random" values */
		printk("SAL/PAL failed to obtain frequency info---inventing reasonably values\n");
		platform_base_freq = 100000000;
		itc_ratio.num = 3;
		itc_ratio.den = 1;
	}
#ifdef CONFIG_IA64_SOFTSDV_HACKS
	platform_base_freq = 10000000;
	proc_ratio.num = 4; proc_ratio.den = 1;
	itc_ratio.num  = 4; itc_ratio.den  = 1;
#else
	if (platform_base_freq < 40000000) {
		printk("Platform base frequency %lu bogus---resetting to 75MHz!\n",
		       platform_base_freq);
		platform_base_freq = 75000000;
	}
#endif
	if (!proc_ratio.den)
		proc_ratio.num = 1;	/* avoid division by zero */
	if (!itc_ratio.den)
		itc_ratio.num = 1;	/* avoid division by zero */

        itc_freq = (platform_base_freq*itc_ratio.num)/itc_ratio.den;
        itm.delta = itc_freq / HZ;
        printk("CPU %d: base freq=%lu.%03luMHz, ITC ratio=%lu/%lu, ITC freq=%lu.%03luMHz\n",
	       smp_processor_id(),
	       platform_base_freq / 1000000, (platform_base_freq / 1000) % 1000,
               itc_ratio.num, itc_ratio.den, itc_freq / 1000000, (itc_freq / 1000) % 1000);

	my_cpu_data.proc_freq = (platform_base_freq*proc_ratio.num)/proc_ratio.den;
	my_cpu_data.itc_freq = itc_freq;
	my_cpu_data.cyc_per_usec = itc_freq / 1000000;
	my_cpu_data.usec_per_cyc = (1000000UL << IA64_USEC_PER_CYC_SHIFT) / itc_freq;

	/* Setup the CPU local timer tick */
	ia64_cpu_local_tick();
}

static struct irqaction timer_irqaction = {
	handler:	timer_interrupt,
	flags:		SA_INTERRUPT,
	name:		"timer"
};

void __init
time_init (void)
{
	/* we can't do request_irq() here because the kmalloc() would fail... */
	irq_desc[TIMER_IRQ].status |= IRQ_PER_CPU;
	irq_desc[TIMER_IRQ].handler = &irq_type_ia64_sapic;
	setup_irq(TIMER_IRQ, &timer_irqaction);

	efi_gettimeofday(&xtime);
	ia64_init_itm();
}
