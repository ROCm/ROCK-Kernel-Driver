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
	unsigned int __syscall_count;
	struct task_struct * __ksoftirqd_task;
	unsigned int __last_jiffy_stamp;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define last_jiffy_stamp(cpu) __IRQ_STAT((cpu), __last_jiffy_stamp)

#define IRQ_OFFSET	64

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt()	((preempt_count() & ~PREEMPT_ACTIVE) >= IRQ_OFFSET)
#define in_irq		in_interrupt

#define irq_enter()	(preempt_count() += IRQ_OFFSET)
#define irq_exit()	(preempt_count() -= IRQ_OFFSET)

#ifndef CONFIG_SMP
#define synchronize_irq(irq)	barrier()

#else /* CONFIG_SMP */
extern void synchronize_irq(unsigned int irq);

#endif /* CONFIG_SMP */

#endif /* __ASM_HARDIRQ_H */
#endif /* __KERNEL__ */
