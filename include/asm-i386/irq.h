#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 *	linux/include/asm/irq.h
 *
 *	(C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 *	IRQ/IPI changes taken from work by Thomas Radke
 *	<tomsoft@informatik.tu-chemnitz.de>
 */

#include <linux/config.h>
#include <linux/sched.h>
/* include comes from machine specific directory */
#include "irq_vectors.h"
#include <asm/thread_info.h>

static __inline__ int irq_canonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);
extern void release_x86_irqs(struct task_struct *);
extern int can_request_irq(unsigned int, unsigned long flags);

#ifdef CONFIG_X86_LOCAL_APIC
#define ARCH_HAS_NMI_WATCHDOG		/* See include/linux/nmi.h */
#endif

#ifdef CONFIG_4KSTACKS
/*
 * per-CPU IRQ handling contexts (thread information and stack)
 */
union irq_ctx {
	struct thread_info      tinfo;
	u32                     stack[THREAD_SIZE/sizeof(u32)];
};

extern union irq_ctx *hardirq_ctx[NR_CPUS];
extern union irq_ctx *softirq_ctx[NR_CPUS];

extern void irq_ctx_init(int cpu);

#define __ARCH_HAS_DO_SOFTIRQ
#else
#define irq_ctx_init(cpu) do { ; } while (0)
#endif

struct irqaction;
struct pt_regs;
asmlinkage int handle_IRQ_event(unsigned int, struct pt_regs *,
				struct irqaction *);

#endif /* _ASM_IRQ_H */
