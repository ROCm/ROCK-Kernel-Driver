/* smp.h: PPC specific SMP stuff.
 *
 * Taken from asm-sparc/smp.h
 */

#ifdef __KERNEL__
#ifndef _PPC_SMP_H
#define _PPC_SMP_H

#include <linux/config.h>
#include <linux/kernel.h>

#ifdef CONFIG_SMP

#ifndef __ASSEMBLY__

struct cpuinfo_PPC {
	unsigned long loops_per_sec;
	unsigned long pvr;
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
};
extern struct cpuinfo_PPC cpu_data[NR_CPUS];

extern unsigned long smp_proc_in_lock[NR_CPUS];

extern void smp_store_cpu_info(int id);
extern void smp_send_tlb_invalidate(int);
extern void smp_send_xmon_break(int cpu);
struct pt_regs;
extern void smp_message_recv(int, struct pt_regs *);

#define NO_PROC_ID		0xFF            /* No processor magic marker */
#define PROC_CHANGE_PENALTY	20

/* 1 to 1 mapping on PPC -- Cort */
#define cpu_logical_map(cpu) (cpu)
#define cpu_number_map(x) (x)
extern volatile unsigned long cpu_callin_map[NR_CPUS];

#define smp_processor_id() (current->processor)

extern int smp_hw_index[NR_CPUS];
#define hard_smp_processor_id() (smp_hw_index[smp_processor_id()])

struct klock_info_struct {
	unsigned long kernel_flag;
	unsigned char akp;
};

extern struct klock_info_struct klock_info;
#define KLOCK_HELD       0xffffffff
#define KLOCK_CLEAR      0x0

#endif /* __ASSEMBLY__ */

#else /* !(CONFIG_SMP) */

#endif /* !(CONFIG_SMP) */

#endif /* !(_PPC_SMP_H) */
#endif /* __KERNEL__ */
