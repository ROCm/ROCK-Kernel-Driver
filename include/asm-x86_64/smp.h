#ifndef __ASM_SMP_H
#define __ASM_SMP_H

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/threads.h>
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
extern unsigned long phys_cpu_present_map;
extern unsigned long cpu_online_map;
extern volatile unsigned long smp_invalidate_needed;
extern int pic_mode;
extern void smp_flush_tlb(void);
extern void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_send_reschedule(int cpu);
extern void smp_invalidate_rcv(void);		/* Process an NMI */
extern void (*mtrr_hook) (void);
extern void zap_low_mappings(void);

#define SMP_TRAMPOLINE_BASE 0x6000

/*
 * On x86 all CPUs are mapped 1:1 to the APIC space.
 * This simplifies scheduling and IPI sending and
 * compresses data structures.
 */

extern volatile unsigned long cpu_callout_map;

#define cpu_possible(cpu) (cpu_callout_map & (1<<(cpu)))
#define cpu_online(cpu) (cpu_online_map & (1<<(cpu)))

#define for_each_cpu(cpu, mask) \
	for(mask = cpu_online_map; \
	    cpu = __ffs(mask), mask != 0; \
	    mask &= ~(1UL<<cpu))

extern inline int any_online_cpu(unsigned int mask)
{
	if (mask & cpu_online_map)
		return __ffs(mask & cpu_online_map);

		return -1; 
} 

extern inline unsigned int num_online_cpus(void)
{ 
	return hweight32(cpu_online_map);
} 

static inline int num_booting_cpus(void)
{
	return hweight32(cpu_callout_map);
}

extern volatile unsigned long cpu_callout_map;

#define smp_processor_id() read_pda(cpunumber)

extern __inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned int *)(APIC_BASE+APIC_ID));
}

extern int slow_smp_processor_id(void);

extern inline int safe_smp_processor_id(void)
{ 
	if (disable_apic)
		return slow_smp_processor_id(); 
	else
		return hard_smp_processor_id();
} 

#define cpu_online(cpu) (cpu_online_map & (1<<(cpu)))
#endif /* !ASSEMBLY */

#define NO_PROC_ID		0xFF		/* No processor magic marker */

#endif
#define INT_DELIVERY_MODE 1     /* logical delivery */
#define TARGET_CPUS 1


#ifndef CONFIG_SMP
#define stack_smp_processor_id() 0
#define safe_smp_processor_id() 0
#define for_each_cpu(x) (x)=0;
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

