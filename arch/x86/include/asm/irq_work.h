#ifndef _ASM_IRQ_WORK_H
#define _ASM_IRQ_WORK_H

#include <asm/processor.h>

static inline bool arch_irq_work_has_interrupt(void)
{
#ifndef CONFIG_XEN
	return cpu_has_apic;
#elif defined(CONFIG_SMP)
	return 1;
#else
	return 0;
#endif
}

#endif /* _ASM_IRQ_WORK_H */
