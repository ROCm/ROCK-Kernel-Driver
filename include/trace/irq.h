#ifndef _TRACE_IRQ_H
#define _TRACE_IRQ_H

#include <linux/kdebug.h>
#include <linux/interrupt.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(irq_entry,
	TPPROTO(unsigned int id, struct pt_regs *regs),
	TPARGS(id, regs));
DEFINE_TRACE(irq_exit,
	TPPROTO(irqreturn_t retval),
	TPARGS(retval));
DEFINE_TRACE(irq_softirq_entry,
	TPPROTO(struct softirq_action *h, struct softirq_action *softirq_vec),
	TPARGS(h, softirq_vec));
DEFINE_TRACE(irq_softirq_exit,
	TPPROTO(struct softirq_action *h, struct softirq_action *softirq_vec),
	TPARGS(h, softirq_vec));
DEFINE_TRACE(irq_softirq_raise,
	TPPROTO(unsigned int nr),
	TPARGS(nr));
DEFINE_TRACE(irq_tasklet_low_entry,
	TPPROTO(struct tasklet_struct *t),
	TPARGS(t));
DEFINE_TRACE(irq_tasklet_low_exit,
	TPPROTO(struct tasklet_struct *t),
	TPARGS(t));
DEFINE_TRACE(irq_tasklet_high_entry,
	TPPROTO(struct tasklet_struct *t),
	TPARGS(t));
DEFINE_TRACE(irq_tasklet_high_exit,
	TPPROTO(struct tasklet_struct *t),
	TPARGS(t));

#endif
