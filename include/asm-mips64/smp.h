#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/config.h>

#ifdef CONFIG_SMP

#include <linux/threads.h>
#include <linux/irq.h>

#if 0
struct cpuinfo_mips {				/* XXX  */
	unsigned long loops_per_sec;
	unsigned long last_asn;
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
	unsigned long ipi_count;
	unsigned long irq_attempt[NR_IRQS];
	unsigned long smp_local_irq_count;
	unsigned long prof_multiplier;
	unsigned long prof_counter;
} __attribute__((aligned(64)));

extern struct cpuinfo_mips cpu_data[NR_CPUS];
#endif

#define smp_processor_id()	(current->processor)

#define PROC_CHANGE_PENALTY	20

/* Map from cpu id to sequential logical cpu number.  This will only
   not be idempotent when cpus failed to come on-line.  */
extern int __cpu_number_map[NR_CPUS];
#define cpu_number_map(cpu)  __cpu_number_map[cpu]

/* The reverse map from sequential logical cpu number to cpu id.  */
extern int __cpu_logical_map[NR_CPUS];
#define cpu_logical_map(cpu)  __cpu_logical_map[cpu]

/* Good enough for toy^Wupto 64 CPU Origins.  */
extern unsigned long cpu_present_mask;

#endif

#define NO_PROC_ID	(-1)

#endif __ASM_SMP_H
