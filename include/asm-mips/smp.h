/*
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * Copyright (C) 2000 - 2001 by Kanoj Sarcar (kanoj@sgi.com)
 * Copyright (C) 2000 - 2001 by Silicon Graphics, Inc.
 * Copyright (C) 2000, 2001, 2002 Ralf Baechle
 * Copyright (C) 2000, 2001 Broadcom Corporation
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/config.h>

#ifdef CONFIG_SMP

#include <linux/bitops.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/atomic.h>

#define smp_processor_id()	(current_thread_info()->cpu)

/* Map from cpu id to sequential logical cpu number.  This will only
   not be idempotent when cpus failed to come on-line.  */
extern int __cpu_number_map[NR_CPUS];
#define cpu_number_map(cpu)  __cpu_number_map[cpu]

/* The reverse map from sequential logical cpu number to cpu id.  */
extern int __cpu_logical_map[NR_CPUS];
#define cpu_logical_map(cpu)  __cpu_logical_map[cpu]

#define NO_PROC_ID	(-1)

struct call_data_struct {
	void		(*func)(void *);
	void		*info;
	atomic_t	started;
	atomic_t	finished;
	int		wait;
};

extern struct call_data_struct *call_data;

#define SMP_RESCHEDULE_YOURSELF	0x1	/* XXX braindead */
#define SMP_CALL_FUNCTION	0x2

extern cpumask_t phys_cpu_present_map;
extern cpumask_t cpu_online_map;

#define cpu_possible(cpu)	cpu_isset(cpu, phys_cpu_present_map)
#define cpu_online(cpu)		cpu_isset(cpu, cpu_online_map)

static inline unsigned int num_online_cpus(void)
{
	return cpus_weight(cpu_online_map);
}

extern cpumask_t cpu_callout_map;
/* We don't mark CPUs online until __cpu_up(), so we need another measure */
static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

#endif /* CONFIG_SMP */

#endif /* __ASM_SMP_H */
