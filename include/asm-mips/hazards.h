/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle
 */
#ifndef _ASM_HAZARDS_H
#define _ASM_HAZARDS_H

#include <linux/config.h>

#ifdef __ASSEMBLY__

/*
 * RM9000 hazards.  When the JTLB is updated by tlbwi or tlbwr, a subsequent
 * use of the JTLB for instructions should not occur for 4 cpu cycles and use
 * for data translations should not occur for 3 cpu cycles.
 */
#ifdef CONFIG_CPU_RM9000
#define mtc0_tlbw_hazard						\
	.set	push;							\
	.set	mips32;							\
	ssnop; ssnop; ssnop; ssnop;					\
	.set	pop

#define tlbw_eret_hazard						\
	.set	push;							\
	.set	mips32;							\
	ssnop; ssnop; ssnop; ssnop;					\
	.set	pop

#else

/*
 * The taken branch will result in a two cycle penalty for the two killed
 * instructions on R4000 / R4400.  Other processors only have a single cycle
 * hazard so this is nice trick to have an optimal code for a range of
 * processors.
 */
#define mtc0_tlbw_hazard						\
	b	. + 8
#define tlbw_eret_hazard
#endif

#else /* __ASSEMBLY__ */

/*
 * RM9000 hazards.  When the JTLB is updated by tlbwi or tlbwr, a subsequent
 * use of the JTLB for instructions should not occur for 4 cpu cycles and use
 * for data translations should not occur for 3 cpu cycles.
 */
#ifdef CONFIG_CPU_RM9000

#define mtc0_tlbw_hazard()						\
	__asm__ __volatile__(						\
		".set\tmips32\n\t"					\
		"ssnop; ssnop; ssnop; ssnop\n\t"			\
		".set\tmips0")

#define tlbw_use_hazard()						\
	__asm__ __volatile__(						\
		".set\tmips32\n\t"					\
		"ssnop; ssnop; ssnop; ssnop\n\t"			\
		".set\tmips0")
#else

/*
 * Overkill warning ...
 */
#define mtc0_tlbw_hazard()						\
	__asm__ __volatile__(						\
		".set noreorder\n\t"					\
		"nop; nop; nop; nop; nop; nop;\n\t"			\
		".set reorder\n\t")

#define tlbw_use_hazard()						\
	__asm__ __volatile__(						\
		".set noreorder\n\t"					\
		"nop; nop; nop; nop; nop; nop;\n\t"			\
		".set reorder\n\t")

#endif

#endif /* __ASSEMBLY__ */

#endif /* _ASM_HAZARDS_H */
