#ifndef _TRACE_IRQ_H
#define _TRACE_IRQ_H

#include <linux/kdebug.h>
#include <linux/interrupt.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(irq_entry,
	TPPROTO(unsigned int id, struct pt_regs *regs),
	TPARGS(id, regs));
DECLARE_TRACE(irq_exit,
	TPPROTO(irqreturn_t retval),
	TPARGS(retval));
DECLARE_TRACE(irq_softirq_entry,
	TPPROTO(struct softirq_action *h, struct softirq_action *softirq_vec),
	TPARGS(h, softirq_vec));
DECLARE_TRACE(irq_softirq_exit,
	TPPROTO(struct softirq_action *h, struct softirq_action *softirq_vec),
	TPARGS(h, softirq_vec));
DECLARE_TRACE(irq_softirq_raise,
	TPPROTO(unsigned int nr),
	TPARGS(nr));
DECLARE_TRACE(irq_tasklet_low_entry,
	TPPROTO(struct tasklet_struct *t),
	TPARGS(t));
DECLARE_TRACE(irq_tasklet_low_exit,
	TPPROTO(struct tasklet_struct *t),
	TPARGS(t));
DECLARE_TRACE(irq_tasklet_high_entry,
	TPPROTO(struct tasklet_struct *t),
	TPARGS(t));
DECLARE_TRACE(irq_tasklet_high_exit,
	TPPROTO(struct tasklet_struct *t),
	TPARGS(t));

#endif
