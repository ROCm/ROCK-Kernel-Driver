/*
 *  linux/include/asm-arm/proc-armo/ptrace.h
 *
 *  Copyright (C) 1996-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PROC_PTRACE_H
#define __ASM_PROC_PTRACE_H

#define USR26_MODE	0x00000000
#define FIQ26_MODE	0x00000001
#define IRQ26_MODE	0x00000002
#define SVC26_MODE	0x00000003
#define USR_MODE	USR26_MODE
#define FIQ_MODE	FIQ26_MODE
#define IRQ_MODE	IRQ26_MODE
#define SVC_MODE	SVC26_MODE
#define MODE_MASK	0x00000003
#define PSR_F_BIT	0x04000000
#define PSR_I_BIT	0x08000000
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_N_BIT	0x80000000
#define PCMASK		0xfc000003

#ifndef __ASSEMBLY__

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct pt_regs {
	long uregs[17];
};

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
#define ARM_ORIG_r0	uregs[16]

#ifdef __KERNEL__

#define processor_mode(regs) \
	((regs)->ARM_pc & MODE_MASK)

#define user_mode(regs) \
	(processor_mode(regs) == USR26_MODE)

#define thumb_mode(regs) (0)

#define interrupts_enabled(regs) \
	(!((regs)->ARM_pc & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->ARM_pc & PSR_F_BIT))

#define condition_codes(regs) \
	((regs)->ARM_pc & (PSR_V_BIT|PSR_C_BIT|PSR_Z_BIT|PSR_N_BIT))

/* Are the current registers suitable for user mode?
 * (used to maintain security in signal handlers)
 */
static inline int valid_user_regs(struct pt_regs *regs)
{
	if (user_mode(regs) &&
	    (regs->ARM_pc & (PSR_F_BIT | PSR_I_BIT)) == 0)
		return 1;

	/*
	 * force it to be something sensible
	 */
	regs->ARM_pc &= ~(MODE_MASK | PSR_F_BIT | PSR_I_BIT);

	return 0;
}

#endif	/* __KERNEL__ */

#endif	/* __ASSEMBLY__ */

#endif

