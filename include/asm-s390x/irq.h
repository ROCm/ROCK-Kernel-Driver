#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#ifdef __KERNEL__
#include <asm/hardirq.h>

/*
 * the definition of irqs has changed in 2.5.46:
 * NR_IRQS is no longer the number of i/o
 * interrupts (65536), but rather the number
 * of interrupt classes (6).
 */

enum interruption_class {
	EXTERNAL_INTERRUPT,
	IO_INTERRUPT,
	MACHINE_CHECK_INTERRUPT,
	PROGRAM_INTERRUPT,
	RESTART_INTERRUPT,
	SUPERVISOR_CALL,

	NR_IRQS,
};

#define touch_nmi_watchdog() do { } while(0)

#endif /* __KERNEL__ */
#endif

