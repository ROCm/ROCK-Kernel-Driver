#ifndef _ASM_IA64_SOFTIRQ_H
#define _ASM_IA64_SOFTIRQ_H

#include <linux/compiler.h>

/*
 * Copyright (C) 1998-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/compiler.h>
#include <linux/preempt.h>

#include <asm/hardirq.h>

#define __local_bh_enable()	do { barrier(); preempt_count() -= SOFTIRQ_OFFSET; } while (0)

#define local_bh_disable()	do { preempt_count() += SOFTIRQ_OFFSET; barrier(); } while (0)
#define local_bh_enable()						\
do {									\
	__local_bh_enable();						\
	if (unlikely(!in_interrupt() && local_softirq_pending()))	\
		do_softirq();						\
	preempt_check_resched();					\
} while (0)

#endif /* _ASM_IA64_SOFTIRQ_H */
