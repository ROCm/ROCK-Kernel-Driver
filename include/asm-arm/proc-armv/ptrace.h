/*
 *  linux/include/asm-arm/proc-armv/ptrace.h
 *
 *  Copyright (C) 1996-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PROC_PTRACE_H
#define __ASM_PROC_PTRACE_H

#include <linux/config.h>

/*
 * PSR bits
 */
#define USR26_MODE	0x00000000
#define FIQ26_MODE	0x00000001
#define IRQ26_MODE	0x00000002
#define SVC26_MODE	0x00000003
#define USR_MODE	0x00000010
#define FIQ_MODE	0x00000011
#define IRQ_MODE	0x00000012
#define SVC_MODE	0x00000013
#define ABT_MODE	0x00000017
#define UND_MODE	0x0000001b
#define SYSTEM_MODE	0x0000001f
#define MODE32_BIT	0x00000010
#define MODE_MASK	0x0000001f
#define PSR_T_BIT	0x00000020
#define PSR_F_BIT	0x00000040
#define PSR_I_BIT	0x00000080
#define PSR_J_BIT	0x01000000
#define PSR_Q_BIT	0x08000000
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_N_BIT	0x80000000
#define PCMASK		0

/*
 * Groups of PSR bits
 */
#define PSR_f		0xff000000	/* Flags		*/
#define PSR_s		0x00ff0000	/* Status		*/
#define PSR_x		0x0000ff00	/* Extension		*/
#define PSR_c		0x000000ff	/* Control		*/

/*
 * CR1 bits
 */
#define CR1_M		0x00000001	/* MMU			*/
#define CR1_A		0x00000002	/* Alignment fault	*/
#define CR1_C		0x00000004	/* Dcache		*/
#define CR1_W		0x00000008	/* Write buffer		*/
#define CR1_P		0x00000010	/* Prog32		*/
#define CR1_D		0x00000020	/* Data32		*/
#define CR1_L		0x00000040	/* Late abort		*/
#define CR1_B		0x00000080	/* Big endian		*/
#define CR1_S		0x00000100	/* System protection	*/
#define CR1_R		0x00000200	/* ROM protection	*/
#define CR1_F		0x00000400
#define CR1_Z		0x00000800	/* BTB enable		*/
#define CR1_I		0x00001000	/* Icache		*/
#define CR1_V		0x00002000	/* Vector relocation	*/
#define CR1_RR		0x00004000	/* Round Robin		*/

#ifndef __ASSEMBLY__

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct pt_regs {
	long uregs[18];
};

#define ARM_cpsr	uregs[16]
#define ARM_pc		uregs[15]
#define ARM_lr		uregs[14]
#define ARM_sp		uregs[13]
#define ARM_ip		uregs[12]
#define ARM_fp		uregs[11]
#define ARM_r10		uregs[10]
#define ARM_r9		uregs[9]
#define ARM_r8		uregs[8]
#define ARM_r7		uregs[7]
#define ARM_r6		uregs[6]
#define ARM_r5		uregs[5]
#define ARM_r4		uregs[4]
#define ARM_r3		uregs[3]
#define ARM_r2		uregs[2]
#define ARM_r1		uregs[1]
#define ARM_r0		uregs[0]
#define ARM_ORIG_r0	uregs[17]

#ifdef __KERNEL__

#define user_mode(regs)	\
	(((regs)->ARM_cpsr & 0xf) == 0)

#ifdef CONFIG_ARM_THUMB
#define thumb_mode(regs) \
	(((regs)->ARM_cpsr & PSR_T_BIT))
#else
#define thumb_mode(regs) (0)
#endif

#define processor_mode(regs) \
	((regs)->ARM_cpsr & MODE_MASK)

#define interrupts_enabled(regs) \
	(!((regs)->ARM_cpsr & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->ARM_cpsr & PSR_F_BIT))

#define condition_codes(regs) \
	((regs)->ARM_cpsr & (PSR_V_BIT|PSR_C_BIT|PSR_Z_BIT|PSR_N_BIT))
	
/* Are the current registers suitable for user mode?
 * (used to maintain security in signal handlers)
 */
static inline int valid_user_regs(struct pt_regs *regs)
{
	if (user_mode(regs) &&
	    (regs->ARM_cpsr & (PSR_F_BIT|PSR_I_BIT)) == 0)
		return 1;

	/*
	 * Force CPSR to something logical...
	 */
	regs->ARM_cpsr &= PSR_f | PSR_s | PSR_x | PSR_T_BIT | MODE32_BIT;

	return 0;
}

#endif	/* __KERNEL__ */

#endif	/* __ASSEMBLY__ */

#endif

