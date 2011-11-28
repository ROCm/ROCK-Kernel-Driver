#ifndef _ASM_X86_SMP_PROCESSOR_ID_H
#define _ASM_X86_SMP_PROCESSOR_ID_H

#if defined(CONFIG_SMP) && !defined(__ASSEMBLY__)

#include <asm/percpu.h>

DECLARE_PER_CPU(int, cpu_number);

/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup. We map APIC_BASE very early in page_setup(),
 * so this is correct in the x86 case.
 */
#define raw_smp_processor_id() percpu_read(cpu_number)
#define safe_smp_processor_id() smp_processor_id()

#ifdef CONFIG_X86_64_SMP
#define stack_smp_processor_id()					\
({									\
	struct thread_info *ti;						\
	__asm__("andq %%rsp,%0; ":"=r" (ti) : "0" (CURRENT_MASK));	\
	ti->cpu;							\
})
#endif

#ifdef CONFIG_DEBUG_PREEMPT
extern unsigned int debug_smp_processor_id(void);
# define smp_processor_id() debug_smp_processor_id()
#else
# define smp_processor_id() raw_smp_processor_id()
#endif

#endif /* SMP && !__ASSEMBLY__ */

#endif /* _ASM_X86_SMP_PROCESSOR_ID_H */
