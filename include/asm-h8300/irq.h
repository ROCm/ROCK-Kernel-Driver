#ifndef _H8300_IRQ_H_
#define _H8300_IRQ_H_

#define SYS_IRQS 64

#define NR_IRQS 64

#include <asm/ptrace.h>

/*
 * "Generic" interrupt sources
 */

#define IRQ_SCHED_TIMER	(40)    /* interrupt source for scheduling timer */

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

extern void enable_irq(unsigned int);
extern void disable_irq(unsigned int);

extern int sys_request_irq(unsigned int, 
	void (*)(int, void *, struct pt_regs *), 
	unsigned long, const char *, void *);
extern void sys_free_irq(unsigned int, void *);

typedef struct irq_node {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
	struct irq_node *next;
} irq_node_t;

/*
 * This structure has only 4 elements for speed reasons
 */
typedef struct irq_handler {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
} irq_handler_t;

/* count of spurious interrupts */
extern volatile unsigned int num_spurious;

/*
 * Some drivers want these entry points
 */
#define enable_irq_nosync(x)	enable_irq(x)
#define disable_irq_nosync(x)	disable_irq(x)

#endif /* _H8300_IRQ_H_ */
