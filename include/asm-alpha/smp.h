#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/bitops.h>
#include <asm/pal.h>

/* HACK: Cabrio WHAMI return value is bogus if more than 8 bits used.. :-( */

static __inline__ unsigned char
__hard_smp_processor_id(void)
{
	register unsigned char __r0 __asm__("$0");
	__asm__ __volatile__(
		"call_pal %1 #whami"
		: "=r"(__r0)
		:"i" (PAL_whami)
		: "$1", "$22", "$23", "$24", "$25");
	return __r0;
}

#ifdef CONFIG_SMP

#include <asm/irq.h>

struct cpuinfo_alpha {
	unsigned long loops_per_jiffy;
	unsigned long last_asn;
	int need_new_asn;
	int asn_lock;
	unsigned long ipi_count;
	unsigned long prof_multiplier;
	unsigned long prof_counter;
	unsigned char mcheck_expected;
	unsigned char mcheck_taken;
	unsigned char mcheck_extra;
} __attribute__((aligned(64)));

extern struct cpuinfo_alpha cpu_data[NR_CPUS];

#define PROC_CHANGE_PENALTY     20

#define hard_smp_processor_id()	__hard_smp_processor_id()
#define smp_processor_id()	(current_thread_info()->cpu)

extern unsigned long cpu_present_mask;
extern volatile unsigned long cpu_online_map;
extern int smp_num_cpus;

#define cpu_possible(cpu)	(cpu_present_mask & (1UL << (cpu)))
#define cpu_online(cpu)		(cpu_online_map & (1UL << (cpu)))

static inline int
num_online_cpus(void)
{
	return hweight64(cpu_online_map);
}

extern inline int
any_online_cpu(unsigned int mask)
{
        if (mask & cpu_online_map)
                return __ffs(mask & cpu_online_map);

        return -1;
}

extern int smp_call_function_on_cpu(void (*func) (void *info), void *info,int retry, int wait, unsigned long cpu);

#else /* CONFIG_SMP */

#define smp_call_function_on_cpu(func,info,retry,wait,cpu)    ({ 0; })

#endif /* CONFIG_SMP */

#define NO_PROC_ID	(-1)

#endif
