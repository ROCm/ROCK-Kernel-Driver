/*
 * BK Id: %F% %I% %G% %U% %#%
 */
#ifdef __KERNEL__
#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <asm/smp.h>

/* The __last_jiffy_stamp field is needed to ensure that no decrementer 
 * interrupt is lost on SMP machines. Since on most CPUs it is in the same 
 * cache line as local_irq_count, it is cheap to access and is also used on UP 
 * for uniformity.
 */
typedef struct {
	unsigned long __softirq_pending;	/* set_bit is used on this */
	unsigned int __local_irq_count;
	unsigned int __local_bh_count;
	unsigned int __syscall_count;
	struct task_struct * __ksoftirqd_task;
	unsigned int __last_jiffy_stamp;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define last_jiffy_stamp(cpu) __IRQ_STAT((cpu), __last_jiffy_stamp)
/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ int __cpu = smp_processor_id(); \
	(local_irq_count(__cpu) + local_bh_count(__cpu) != 0); })

#define in_irq() (local_irq_count(smp_processor_id()) != 0)

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(local_irq_count(cpu) == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	do { preempt_disable(); local_irq_count(cpu)++; } while (0)
#define hardirq_exit(cpu)	do { local_irq_count(cpu)--; preempt_enable(); } while (0)

#define synchronize_irq()	do { } while (0)
#define release_irqlock(cpu)    do { } while (0)

#else /* CONFIG_SMP */

#include <asm/atomic.h>

extern unsigned char global_irq_holder;
extern unsigned volatile long global_irq_lock;

static inline int irqs_running (void)
{
	int i;

	for (i = 0; i < NR_CPUS; i++)
		if (local_irq_count(i))
			return 1;
	return 0;
}

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (global_irq_holder == (unsigned char) cpu) {
		global_irq_holder = NO_PROC_ID;
		clear_bit(0,&global_irq_lock);
	}
}

static inline void hardirq_enter(int cpu)
{
	unsigned int loops = 10000000;

	preempt_disable();
	++local_irq_count(cpu);
	while (test_bit(0,&global_irq_lock)) {
		if (cpu == global_irq_holder) {
			printk("uh oh, interrupt while we hold global irq lock! (CPU %d)\n", cpu);
#ifdef CONFIG_XMON
			xmon(0);
#endif
			break;
		}
		if (loops-- == 0) {
			printk("do_IRQ waiting for irq lock (holder=%d)\n", global_irq_holder);
#ifdef CONFIG_XMON
			xmon(0);
#endif
		}
	}
}

static inline void hardirq_exit(int cpu)
{
	--local_irq_count(cpu);
	preempt_enable();
}

static inline int hardirq_trylock(int cpu)
{
	return !test_bit(0,&global_irq_lock);
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* CONFIG_SMP */

#endif /* __ASM_HARDIRQ_H */
#endif /* __KERNEL__ */
