#ifndef _ASM_M32R_SMP_H
#define _ASM_M32R_SMP_H

/* $Id$ */

#include <linux/config.h>

#ifdef CONFIG_SMP
#ifndef __ASSEMBLY__

#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <asm/m32r.h>

extern cpumask_t phys_cpu_present_map;

/*
 * Some lowlevel functions might want to know about
 * the real CPU ID <-> CPU # mapping.
 */
extern volatile int physid_2_cpu[NR_CPUS];
extern volatile int cpu_2_physid[NR_CPUS];
#define physid_to_cpu(physid)	physid_2_cpu[physid]
#define cpu_to_physid(cpu_id)	cpu_2_physid[cpu_id]

#define smp_processor_id()	(current_thread_info()->cpu)

extern cpumask_t cpu_callout_map;
#define cpu_possible_map cpu_callout_map

static __inline__ int hard_smp_processor_id(void)
{
	return (int)*(volatile long *)M32R_CPUID_PORTL;
}

static __inline__ int cpu_logical_map(int cpu)
{
	return cpu;
}

static __inline__ int cpu_number_map(int cpu)
{
	return cpu;
}

static __inline__ unsigned int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

extern void smp_send_timer(void);
extern void calibrate_delay(void);
extern unsigned long send_IPI_mask_phys(unsigned long, int, int);

#endif	/* not __ASSEMBLY__ */

#define NO_PROC_ID (0xff)	/* No processor magic marker */

#define PROC_CHANGE_PENALTY	(15)	/* Schedule penalty */

/*
 * M32R-mp IPI
 */
#define RESCHEDULE_IPI		(M32R_IRQ_IPI0-M32R_IRQ_IPI0)
#define INVALIDATE_TLB_IPI	(M32R_IRQ_IPI1-M32R_IRQ_IPI0)
#define CALL_FUNCTION_IPI	(M32R_IRQ_IPI2-M32R_IRQ_IPI0)
#define LOCAL_TIMER_IPI		(M32R_IRQ_IPI3-M32R_IRQ_IPI0)
#define INVALIDATE_CACHE_IPI	(M32R_IRQ_IPI4-M32R_IRQ_IPI0)
#define CPU_BOOT_IPI		(M32R_IRQ_IPI5-M32R_IRQ_IPI0)

#define IPI_SHIFT	(0)
#define NR_IPIS		(8)

#endif	/* CONFIG_SMP */

#endif	/* _ASM_M32R_SMP_H */

