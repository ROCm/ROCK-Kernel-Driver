/*
 *  include/asm-s390/hardirq.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "include/asm-i386/hardirq.h"
 */

#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <asm/lowcore.h>
#include <linux/sched.h>

/* No irq_cpustat_t for s390, the data is held directly in S390_lowcore */

/*
 * Simple wrappers reducing source bloat.  S390 specific because each
 * cpu stores its data in S390_lowcore (PSA) instead of using a cache
 * aligned array element like most architectures.
 */

#ifdef CONFIG_SMP

#define softirq_active(cpu)	(safe_get_cpu_lowcore(cpu).__softirq_active)
#define softirq_mask(cpu)	(safe_get_cpu_lowcore(cpu).__softirq_mask)
#define local_irq_count(cpu)	(safe_get_cpu_lowcore(cpu).__local_irq_count)
#define local_bh_count(cpu)	(safe_get_cpu_lowcore(cpu).__local_bh_count)
#define syscall_count(cpu)	(safe_get_cpu_lowcore(cpu).__syscall_count)

#else	/* CONFIG_SMP */

/* Optimize away the cpu calculation, it is always current PSA */
#define softirq_active(cpu)	((void)(cpu), S390_lowcore.__softirq_active)
#define softirq_mask(cpu)	((void)(cpu), S390_lowcore.__softirq_mask)
#define local_irq_count(cpu)	((void)(cpu), S390_lowcore.__local_irq_count)
#define local_bh_count(cpu)	((void)(cpu), S390_lowcore.__local_bh_count)
#define syscall_count(cpu)	((void)(cpu), S390_lowcore.__syscall_count)

#endif	/* CONFIG_SMP */

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 * Special definitions for s390, always access current PSA.
 */
#define in_interrupt() ((S390_lowcore.__local_irq_count + S390_lowcore.__local_bh_count) != 0)

#define in_irq() (S390_lowcore.__local_irq_count != 0)

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(local_irq_count(cpu) == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count(cpu)++)
#define hardirq_exit(cpu)	(local_irq_count(cpu)--)

#define synchronize_irq()	do { } while (0)

#else

#include <asm/atomic.h>
#include <asm/smp.h>

extern atomic_t global_irq_holder;
extern atomic_t global_irq_lock;
extern atomic_t global_irq_count;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (atomic_read(&global_irq_holder) ==  cpu) {
		atomic_set(&global_irq_holder,NO_PROC_ID);
		clear_bit(0,&global_irq_lock);
	}
}

static inline void hardirq_enter(int cpu)
{
        ++local_irq_count(cpu);
	atomic_inc(&global_irq_count);
}

static inline void hardirq_exit(int cpu)
{
	atomic_dec(&global_irq_count);
        --local_irq_count(cpu);
}

static inline int hardirq_trylock(int cpu)
{
	return !atomic_read(&global_irq_count) && !test_bit(0,&global_irq_lock);
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* CONFIG_SMP */

#endif /* __ASM_HARDIRQ_H */
