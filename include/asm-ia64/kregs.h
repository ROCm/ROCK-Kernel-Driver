#ifndef _ASM_IA64_KREGS_H
#define _ASM_IA64_KREGS_H

/*
 * Copyright (C) 2001 Hewlett-Packard Co
 * Copyright (C) 2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */
/*
 * This file defines the kernel register usage convention used by Linux/ia64.
 */

/*
 * Kernel registers:
 */
#define IA64_KR_IO_BASE		0	/* ar.k0: legacy I/O base address */
#define IA64_KR_TSSD		1	/* ar.k1: IVE uses this as the TSSD */
#define IA64_KR_CURRENT_STACK	4	/* ar.k4: what's mapped in IA64_TR_CURRENT_STACK */
#define IA64_KR_FPU_OWNER	5	/* ar.k5: fpu-owner (UP only, at the moment) */
#define IA64_KR_CURRENT		6	/* ar.k6: "current" task pointer */
#define IA64_KR_PT_BASE		7	/* ar.k7: page table base address (physical) */

#define _IA64_KR_PASTE(x,y)	x##y
#define _IA64_KR_PREFIX(n)	_IA64_KR_PASTE(ar.k, n)
#define IA64_KR(n)		_IA64_KR_PREFIX(IA64_KR_##n)

/*
 * Translation registers:
 */
#define IA64_TR_KERNEL		0	/* itr0, dtr0: maps kernel image (code & data) */
#define IA64_TR_PALCODE		1	/* itr1: maps PALcode as required by EFI */
#define IA64_TR_PERCPU_DATA	1	/* dtr1: percpu data */
#define IA64_TR_CURRENT_STACK	2	/* dtr2: maps kernel's memory- & register-stacks */

#endif /* _ASM_IA64_kREGS_H */
