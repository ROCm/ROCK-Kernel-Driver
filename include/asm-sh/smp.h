/*
 * Copyright (C) 2002 Paul Mundt
 */
#ifndef __ASM_SH_SMP_H
#define __ASM_SH_SMP_H

#include <linux/config.h>
#include <linux/bitops.h>

#ifdef CONFIG_SMP

#include <asm/spinlock.h>
#include <asm/atomic.h>
#include <asm/current.h>

extern unsigned long cpu_online_map;

#define cpu_online(cpu)		(cpu_online_map & (1 << (cpu)))
#define cpu_possible(cpu)	(cpu_online(cpu))

#define smp_processor_id()	(current_thread_info()->cpu)

extern inline unsigned int num_online_cpus(void)
{
	return hweight32(cpu_online_map);
}

/* I've no idea what the real meaning of this is */
#define PROC_CHANGE_PENALTY	20

#define NO_PROC_ID	(-1)

struct smp_fn_call_struct {
	spinlock_t lock;
	atomic_t   finished;
	void (*fn)(void *);
	void *data;
};

extern struct smp_fn_call_struct smp_fn_call;

#define SMP_MSG_RESCHEDULE	0x0001

#endif /* CONFIG_SMP */

#endif /* __ASM_SH_SMP_H */
