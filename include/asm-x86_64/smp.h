#ifndef __ASM_SMP_H
#define __ASM_SMP_H

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>
extern int disable_apic;
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#ifndef __ASSEMBLY__
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif
#include <asm/apic.h>
#include <asm/thread_info.h>
#endif
#endif

#ifdef CONFIG_SMP
#ifndef ASSEMBLY

#include <asm/pda.h>

struct pt_regs;

/*
 * Private routines/data
 */
 
extern void smp_alloc_memory(void);
extern cpumask_t cpu_online_map;
extern volatile unsigned long smp_invalidate_needed;
extern int pic_mode;
extern void smp_flush_tlb(void);
extern void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_send_reschedule(int cpu);
extern void smp_invalidate_rcv(void);		/* Process an NMI */
extern void (*mtrr_hook) (void);
extern void zap_low_mappings(void);
void smp_stop_cpu(void);


#define SMP_TRAMPOLINE_BASE 0x6000

/*
 * On x86 all CPUs are mapped 1:1 to the APIC space.
 * This simplifies scheduling and IPI sending and
 * compresses data structures.
 */

extern cpumask_t cpu_callout_map;

#define cpu_possible(cpu) cpu_isset(cpu, cpu_callout_map)
#define cpu_online(cpu) cpu_isset(cpu, cpu_online_map)

static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

#define smp_processor_id() read_pda(cpunumber)

extern __inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned int *)(APIC_BASE+APIC_ID));
}

#define safe_smp_processor_id() (cpuid_ebx(1) >> 24) 

#define cpu_online(cpu) cpu_isset(cpu, cpu_online_map)
#endif /* !ASSEMBLY */

#define NO_PROC_ID		0xFF		/* No processor magic marker */

#endif
#define INT_DELIVERY_MODE 1     /* logical delivery */
#define TARGET_CPUS 1

#ifndef ASSEMBLY
static inline unsigned int cpu_mask_to_apicid(cpumask_const_t cpumask)
{
	return cpus_coerce_const(cpumask);
}
#endif

#ifndef CONFIG_SMP
#define stack_smp_processor_id() 0
#define safe_smp_processor_id() 0
#define cpu_logical_map(x) (x)
#else
#include <asm/thread_info.h>
#define stack_smp_processor_id() \
({ 								\
	struct thread_info *ti;					\
	__asm__("andq %%rsp,%0; ":"=r" (ti) : "0" (CURRENT_MASK));	\
	ti->cpu;						\
})
#endif

#endif

