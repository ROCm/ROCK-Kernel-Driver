#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/irq.h>
#include <asm/pda.h>

#define __ARCH_IRQ_STAT 1

/* Generate a lvalue for a pda member. Should fix softirq.c instead to use
   special access macros. This would generate better code. */ 
#define __IRQ_STAT(cpu,member) (read_pda(me)->member)

typedef struct {
	/* Empty. All the fields have moved to the PDA. */
} irq_cpustat_t; 

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() \
	((read_pda(__local_irq_count) +  read_pda(__local_bh_count)) != 0)
#define in_irq() (read_pda(__local_irq_count) != 0)

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(local_irq_count() == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define irq_enter(cpu, irq)	(local_irq_count()++)
#define irq_exit(cpu, irq)	(local_irq_count()--)

#define synchronize_irq()	barrier()

#define release_irqlock(cpu)	do { } while (0)

#else

#include <asm/atomic.h>
#include <asm/smp.h>

extern unsigned char global_irq_holder;
extern unsigned volatile long global_irq_lock; /* long for set_bit -RR */

static inline int irqs_running (void)
{
	int i;

	for (i = 0; i < smp_num_cpus; i++)
		if (read_pda(__local_irq_count))
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

static inline void irq_enter(int cpu, int irq)
{
	add_pda(__local_irq_count, 1);

	while (test_bit(0,&global_irq_lock)) {
		cpu_relax();
	}
}

static inline void irq_exit(int cpu, int irq)
{
	sub_pda(__local_irq_count, 1);
}

static inline int hardirq_trylock(int cpu)
{
	return !read_pda(__local_irq_count) && !test_bit(0,&global_irq_lock);
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* CONFIG_SMP */

#endif /* __ASM_HARDIRQ_H */
