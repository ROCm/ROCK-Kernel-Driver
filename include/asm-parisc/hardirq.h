/* hardirq.h: 32-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998-99 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#ifndef __PARISC_HARDIRQ_H
#define __PARISC_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>

typedef struct {
	unsigned int __softirq_active;
	unsigned int __softirq_mask;
	unsigned int __local_irq_count;
	unsigned int __local_bh_count;
	unsigned int __syscall_count;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ int __cpu = smp_processor_id(); \
	(local_irq_count(__cpu) + local_bh_count(__cpu) != 0); })

#define in_irq() ({ int __cpu = smp_processor_id(); \
	(local_irq_count(__cpu) != 0); })

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(local_irq_count(cpu) == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define irq_enter(cpu, irq)	(local_irq_count(cpu)++)
#define irq_exit(cpu, irq)	(local_irq_count(cpu)--)

#define synchronize_irq()	barrier()

#else

#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/smp.h>

extern unsigned char global_irq_holder;
extern spinlock_t global_irq_lock;
extern atomic_t global_irq_count;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (global_irq_holder == (unsigned char) cpu) {
		global_irq_holder = NO_PROC_ID;
		spin_unlock(&global_irq_lock);
	}
}

static inline void irq_enter(int cpu)
{
	++local_irq_count(cpu);
	atomic_inc(&global_irq_count);
}

static inline void irq_exit(int cpu)
{
	atomic_dec(&global_irq_count);
	--local_irq_count(cpu);
}

static inline int hardirq_trylock(int cpu)
{
	return (! atomic_read(&global_irq_count) &&
		! spin_is_locked (&global_irq_lock));
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* CONFIG_SMP */

#endif /* __PARISC_HARDIRQ_H */
