/*
 *  arch/ppc/kernel/iSeries_head.S
 *
 *  Adapted from arch/ppc/kernel/head.S
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *  Adapted for iSeries by Mike Corrigan
 *  Updated by Dave Boutcher
 *  
 *  This file contains the low-level support and setup for the
 *  iSeries LPAR platform.                                   
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *	
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/ppc_asm.h>
#include "ppc_defs.h"
#include "iSeries_asm.h"


	.text
	.globl	_stext
_stext:

/* iSeries LPAR
 *
 * In an iSeries partition, the operating system has no direct access
 * to the hashed page table.  The iSeries hypervisor manages the
 * hashed page table, and is directed by the operating system in the
 * partition.  The partition, Linux in this case, always runs with
 * MSR.IR and MSR.DR equal to 1.  The hypervisor establishes    
 * addressibility for the first 64 MB of memory at 0xC0000000 by  
 * building a hashed page table and setting segment register 12.
 *
 * The partition memory is not physically contiguous, nor necessarily
 * addressable with a 32-bit address.  The hypervisor provides functions
 * which the kernel can use to discover the layout of memory.  The  
 * iSeries LPAR specific code in the kernel will build a table that maps
 * contiguous pseudo-real addresses starting at zero to the actual 
 * physical addresses owned by this partition.  In 32-bit mode we will
 * restrict ourselves to no more than 768 MB (or maybe 1 GB)
 *
 * When Linux interrupt handlers get control, the hypervisor has 
 * already saved SRR0 and SRR1 into a control block shared between
 * the hypervisor and Linux.  This is know as the ItLpPaca.  The values
 * in the actual SRR0 and SRR1 are not valid.  This requires a change in
 * the way the SPRG registers are used.  The definitions are:
 *
 *   Register          old definition              new definition
 *
 *   SPRG0             temp - used to save gpr     reserved for hypervisor
 *   SPRG1             temp - used to save gpr     addr of Paca
 *   SPRG2             0 or kernel stack frame     temp - used to save gpr
 *   SPRG3             Linux thread                Linux thread
 *
 * The Paca contains the address of the ItLpPaca.  The Paca is known only 
 * to Linux, while the ItLpPaca is shared between Linux and the 
 * hypervisor.
 *
 * The value that used to be in SPRG2 will now be saved in the Paca,
 * as will at least one GPR.
 */
	
	.globl	__start
__start:
	b	start_here


	. = 0x020

	/* iSeries LPAR hypervisor expects a 64-bit offset of 
	   the hvReleaseData structure (see HvReleaseData.h)
	   at offset 0x20.  This is the base for all common
	   control blocks between the hypervisor and the kernel
	*/

	.long	0 	
	.long	hvReleaseData-KERNELBASE
	.long	0
	.long	msChunks-KERNELBASE
	.long	0
	.long	pidhash-KERNELBASE
	/* Pointer to start of embedded System.map */
	.long	0
	.globl	embedded_sysmap_start
embedded_sysmap_start:
	.long	0
	/* Pointer to end of embedded System.map */
	.long	0
	.globl	embedded_sysmap_end
embedded_sysmap_end:
	.long	0


	. = 0x060
	
	.globl	ste_fault_count
ste_fault_count:
	.long	0
	.globl	set_context_count
set_context_count:
	.long	0
	.globl	yield_count
yield_count:
	.long	0
	.globl	update_times_count
update_times_count:
	.long	0
	.globl	update_wall_jiffies_count
update_wall_jiffies_count:
	.long 	0
	.globl	update_wall_jiffies_ticks
update_wall_jiffies_ticks:
	.long	0


/*
 * We assume SPRG1 has the address of the Paca and SPRG3
 * has the address of the task's thread_struct.
 * SPRG2 is used as a scratch register (as required by the
 * hypervisor).  SPRG0 is reserved for the hypervisor.
 *
 * The ItLpPaca has the values of SRR0 and SRR1 that the 
 * hypervisor saved at the point of the actual interrupt.  
 *
 * The Paca contains the value that the non-LPAR PPC Linux Kernel
 * keeps in SPRG2, which is either zero (if the interrupt
 * occurred in the kernel) or the address of the available
 * space on the kernel stack (if the interrupt occurred
 * in user code).
*/
#define EXCEPTION_PROLOG_1	\
	mtspr	SPRG2,r20;		/* use SPRG2 as scratch reg */\
	mfspr	r20,SPRG1;		/* get Paca */\
	/* must do std not stw because soft disable protects	\
	 * 64-bit register use (in HvCall, maybe others)	\
	 */\
	std	r21,PACAR21(r20);	/* Save GPR21 in Paca */\
	std	r22,PACAR22(r20); 	/* Save GPR22 in Paca */\
	mfcr	r22			/* Get CR */

#define EXCEPTION_PROLOG_2	\
	lwz	r21,PACAKSAVE(r20);	/* exception stack to use */\
	cmpwi	0,r21,0;		/* user mode or kernel     */\
	bne	1f;			/* 0 -> r1, else use PACAKSAVE */\
	subi	r21,r1,INT_FRAME_SIZE;	/* alloc exc. frame */\
1:	stw	r1,GPR1(r21);	\
	mr	r1,r21;		\
	stw	r22,_CCR(r1);		/* save CR in stackframe */ \
	mflr	r22;		\
	stw	r22,_LINK(r1);		/* Save LR in stackframe */ \
	bl	save_regs;	 	/* now save everything else */ \
	ld	r22,PACALPPACA+LPPACASRR0(r20); /* Get SRR0 from ItLpPaca */\
	ld	r23,PACALPPACA+LPPACASRR1(r20) /* Get SRR1 from ItLpPaca */

#define EXCEPTION_PROLOG_EXIT	\
	mtcrf	0xff,r22;	\
	ld	r22,PACALPPACA+LPPACASRR0(r20);	\
	ld	r21,PACALPPACA+LPPACASRR1(r20);	\
	mtspr	SRR0,r22;	\
	mtspr	SRR1,r21;	\
	ld	r22,PACAR22(r20);	\
	ld	r21,PACAR21(r20);	\
	mfspr	r20,SPRG2;	\
	RFI

#define EXCEPTION_PROLOG	\
	EXCEPTION_PROLOG_1;	\
	EXCEPTION_PROLOG_2


/*
 * Note: code which follows this uses cr0.eq (set if from kernel),
 * r21, r22 (SRR0), and r23 (SRR1).
 */

/*
 * Exception vectors.
 */
#define STD_EXCEPTION(n, label, hdlr)		\
	. = n;					\
label:						\
	EXCEPTION_PROLOG;			\
	addi	r3,r1,STACK_FRAME_OVERHEAD;	\
	li	r20,0;				/* soft disabled */\
	bl	transfer_to_handler; 		\
	.long	hdlr;				\
	.long	ret_from_except

/* System reset */
	. = 0x100
SystemReset:	
	mfspr	r3,SPRG3		/* Get Paca address */
	mtspr	SPRG1,r3		/* Set Linux SPRG1 -> Paca */
	lhz	r24,PACAPACAINDEX(r3)	/* Get processor # */
	cmpi	0,r24,0			/* Are we processor 0? */
	beq	start_here		/* Start up the first processor */
	mfspr	r4,CTRLF
	li	r5,RUNLATCH
	andc	r4,r4,r5		/* Turn off the run light */
	mtspr	CTRLT,r4
1:
	HMT_LOW
#ifdef CONFIG_SMP
	lbz	r23,PACAPROCSTART(r3)	/* Test if this processor 
					 * should start */
	cmpi	0,r23,0
	bne	secondary_start
secondary_smp_loop:
	/* Let the Hypervisor know we are alive */
	/* 8002 is a call to HvCallCfg::getLps, a harmless Hypervisor function */
	lis	r3,0x8002
   	rldicr 	r0,r3,32,15		/* r0 = (r3 << 32) & 0xffff000000000000 */
   	rldicl 	r3,r3,0,48		/* r3 = r3 & 0x000000000000ffff */
   	or  	r3,r3,r0		/* r3 = r3 | r0			*/
#else /* CONFIG_SMP */
	/* Yield the processor.  This is required for non-SMP kernels
	   which are running on multi-threaded machines. */
	lis	r3,0x8000
	rldicr	r3,r3,32,15		/* r3 = (r3 << 32) & 0xffff000000000000 */
	addi	r3,r3,18		/* r3 = 0x8000000000000012 which is "yield" */
	li	r4,0			/* "yield timed" */
	li	r5,-1			/* "yield forever" */
#endif /* CONFIG_SMP */
	li	r0,-1			/* r0=-1 indicates a Hypervisor call */	
	sc				/* Invoke the hypervisor via a system call */
	mfspr	r3,SPRG1 		/* Put r3 back */
	b	1b			/* If SMP not configured, secondaries
					 * loop forever */

/* Machine check */
	STD_EXCEPTION(0x200, MachineCheck, MachineCheckException)

/* Data access exception. */
	. = 0x300
DataAccess:
	EXCEPTION_PROLOG
	mfspr	r4,DAR
	stw	r4,_DAR(r1)
	mfspr	r5,DSISR
	stw	r5,_DSISR(r1)
	
	andis.	r0,r5,0x0020		/* Is this a segment fault? */
	bne	ste_fault		/* Yes - go reload segment regs */
	
	/* This should and with 0xd7ff */
	andis.	r0,r5,0xa470		/* Can we handle as little fault? */
	bne	1f			/*  */
	
	rlwinm	r3,r5,32-15,21,21	/* DSISR_STORE -> _PAGE_RW */

	/* 
	 * r3 contains the required access permissions
	 * r4 contains the faulting address
	 */

	stw	r22,_NIP(r1)		/* Help with debug if dsi loop */
	bl	hash_page	 	/* Try to handle as hpte fault */
	lwz	r4,_DAR(r1)		/* Get original DAR */
	lwz	r5,_DSISR(r1)		/* and original DSISR */

1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	lwz	r20,_SOFTE(r1)
	bl	transfer_to_handler
	.long	do_page_fault
	.long	ret_from_except


/* Instruction access exception. */
	. = 0x400
InstructionAccess:
	EXCEPTION_PROLOG
	mr	r4,r22
	mr	r5,r23

	andis.	r0,r23,0x0020		/* Is this a segment fault? */
	bne	ste_fault		/* Yes - go reload segment regs */
	
	andis.	r0,r23,0x4000		/* no pte found? */
	beq	1f			/* if so, try to put a PTE */

	li	r3,0			
	bl	hash_page		/* Try to handle as hpte fault */
	mr	r4,r22
	mr	r5,r23

1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	lwz	r20,_SOFTE(r1)
	bl	transfer_to_handler
	.long	do_page_fault
	.long	ret_from_except

/* External interrupt */
	. = 0x500;
HardwareInterrupt:
	EXCEPTION_PROLOG_1
	lbz	r21,PACAPROCENABLED(r20)
	cmpi	0,r21,0
	bne	1f
	EXCEPTION_PROLOG_EXIT
1:	EXCEPTION_PROLOG_2
do_pending_int:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r4,0
	li	r20,0			/* Soft disabled */
	bl	transfer_to_handler
	.globl do_IRQ_intercept
do_IRQ_intercept:
	.long	do_IRQ;
	.long	ret_from_intercept

/* Alignment exception */
	. = 0x600
Alignment:
	EXCEPTION_PROLOG
	mfspr	r4,DAR
	stw	r4,_DAR(r1)
	mfspr	r5,DSISR
	stw	r5,_DSISR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lbz	r20,PACAPROCENABLED(r20)	/* preserve soft en/disabled */
	bl	transfer_to_handler
	.long	AlignmentException
	.long	ret_from_except

/* Program check exception */
	. = 0x700
ProgramCheck:
	EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lbz	r20,PACAPROCENABLED(r20)	/* preserve soft en/disabled */
	bl	transfer_to_handler
	.long	ProgramCheckException
	.long	ret_from_except

/* Floating-point unavailable */
	. = 0x800
FPUnavailable:
	EXCEPTION_PROLOG
	lwz	r3,PACAKSAVE(r20)
	cmpwi	0,r3,0
	beq	1f
	b	load_up_fpu
1:
	li	r20,0			/* soft disabled */
	bl	transfer_to_handler	/* if from kernel, take a trap */
	.long	KernelFP
	.long	ret_from_except

	. = 0x900
Decrementer:
	EXCEPTION_PROLOG_1
	lbz	r21,PACAPROCENABLED(r20)
	cmpi	0,r21,0
	bne	1f
	
	li	r21,1
	stb	r21,PACALPPACA+LPPACADECRINT(r20)
	lwz	r21,PACADEFAULTDECR(r20)
	mtspr	DEC,r21
	EXCEPTION_PROLOG_EXIT
1:	EXCEPTION_PROLOG_2
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,0		/* Soft disabled */
	bl	transfer_to_handler
	.globl timer_interrupt_intercept
timer_interrupt_intercept:
	.long	timer_interrupt
	.long	ret_from_intercept

	STD_EXCEPTION(0xa00, Trap_0a, UnknownException)
	STD_EXCEPTION(0xb00, Trap_0b, UnknownException)

/* System call */
	. = 0xc00
SystemCall:
	EXCEPTION_PROLOG
	/* Store r3 to the kernel stack */
	stw	r3,ORIG_GPR3(r1)
	lbz	r20,PACAPROCENABLED(r20)	/* preserve soft en/disabled */
	bl	transfer_to_handler
	.long	DoSyscall
	.long	ret_from_except

/* Single step - not used on 601 */
	STD_EXCEPTION(0xd00, SingleStep, SingleStepException)
/*
	STD_EXCEPTION(0xe00, Trap_0e, UnknownException)
	STD_EXCEPTION(0xf00, Trap_0f, UnknownException)
*/
	STD_EXCEPTION(0x1300, Trap_13, InstructionBreakpoint)
/*
	STD_EXCEPTION(0x1400, SMI, SMIException)
	STD_EXCEPTION(0x1500, Trap_15, UnknownException)
	STD_EXCEPTION(0x1600, Trap_16, UnknownException)
	STD_EXCEPTION(0x1700, Trap_17, TAUException)
	STD_EXCEPTION(0x1800, Trap_18, UnknownException)
	STD_EXCEPTION(0x1900, Trap_19, UnknownException)
	STD_EXCEPTION(0x1a00, Trap_1a, UnknownException)
	STD_EXCEPTION(0x1b00, Trap_1b, UnknownException)
	STD_EXCEPTION(0x1c00, Trap_1c, UnknownException)
	STD_EXCEPTION(0x1d00, Trap_1d, UnknownException)
	STD_EXCEPTION(0x1e00, Trap_1e, UnknownException)
	STD_EXCEPTION(0x1f00, Trap_1f, UnknownException)
	STD_EXCEPTION(0x2000, RunMode, RunModeException)
	STD_EXCEPTION(0x2100, Trap_21, UnknownException)
	STD_EXCEPTION(0x2200, Trap_22, UnknownException)
	STD_EXCEPTION(0x2300, Trap_23, UnknownException)
	STD_EXCEPTION(0x2400, Trap_24, UnknownException)
	STD_EXCEPTION(0x2500, Trap_25, UnknownException)
	STD_EXCEPTION(0x2600, Trap_26, UnknownException)
	STD_EXCEPTION(0x2700, Trap_27, UnknownException)
	STD_EXCEPTION(0x2800, Trap_28, UnknownException)
	STD_EXCEPTION(0x2900, Trap_29, UnknownException)
	STD_EXCEPTION(0x2a00, Trap_2a, UnknownException)
	STD_EXCEPTION(0x2b00, Trap_2b, UnknownException)
	STD_EXCEPTION(0x2c00, Trap_2c, UnknownException)
	STD_EXCEPTION(0x2d00, Trap_2d, UnknownException)
	STD_EXCEPTION(0x2e00, Trap_2e, UnknownException)
	STD_EXCEPTION(0x2f00, Trap_2f, UnknownException)
*/
	. = 0x3000

	/* This code saves: CTR, XER, DAR, DSISR, SRR0, SRR1, */
	/*                  r0, r2-r13, r20-r24               */
	/*		    It uses R22 as a scratch register */
save_regs:
	ld 	r22,PACAR21(r20)	/* Get GPR21 from Paca */
	stw	r22,GPR21(r1)		/* Save GPR21 in stackframe */
	ld	r22,PACAR22(r20)	/* Get GPR22 from Paca */
	stw	r22,GPR22(r1)		/* Save GPR22 in stackframe */
	stw	r23,GPR23(r1)		/* Save GPR23 in stackframe */
	stw	r24,GPR24(r1)		/* Save GPR24 in stackframe */
	mfspr	r22,SPRG2		/* Get GPR20 from SPRG2 */
	stw	r22,GPR20(r1)		/* Save GPR20 in stackframe */
	mfctr	r22		
	stw	r22,_CTR(r1)	
	mfspr	r22,XER	
	stw	r22,_XER(r1)	
	lbz	r22,PACAPROCENABLED(r20)/* Get soft enabled/disabled */
	stw	r22,_SOFTE(r1)
	stw	r0,GPR0(r1) 	
	SAVE_8GPRS(2, r1)	
	SAVE_4GPRS(10, r1)	
	blr
	
ste_fault:
	bl	set_kernel_segregs

	mfspr	r3,SPRG1
	li	r4,0
	stb	r4,PACAPROCENABLED(r3)	/* Soft disable prevents going to */
					/* do_pending_int on recursive fault */ 

	lis	r3,ste_fault_count@ha
	lwz	r4,ste_fault_count@l(r3)
	addi	r4,r4,1
	stw	r4,ste_fault_count@l(r3)

	mfspr	r3,SPRG3		/* get thread */
	addi	r3,r3,-THREAD		/* get 'current' */
	lwz	r3,MM(r3)		/* get mm */
	cmpi	0,r3,0			/* if no mm */
	beq	1f			/* then use context 0 (kernel) */
	lwz	r3,CONTEXT(r3)		/* get context */
1:	
	/* set_context kills r0, r3, r4 and CTR */
	bl	set_context
	
	lwz	r3,_SOFTE(r1)
	cmpi	0,r3,0
	beq	5f			/* skip checks if restoring disabled */

	CHECKANYINT(r4,r5,r6) 		/* if pending interrupts, process them */
	bne-	do_pending_int
5:
	mfspr	r4,SPRG1
	stb	r3,PACAPROCENABLED(r4)	/* Restore enabled/disabled */
	
	b	fault_exit
	
/*
 * This code finishes saving the registers to the exception frame
 * and jumps to the appropriate handler for the exception, turning
 * on address translation.
 *
 * At this point r0-r13, r20-r24, CCR, CTR, LINK, XER, DAR and DSISR
 * are saved on a stack.  SRR0 is in r22, SRR1 is in r23
 * r1 points to the stackframe, r1 points to the kernel stackframe
 * We no longer have any dependency on data saved in the PACA, SRR0, SRR1
 * DAR or DSISR.  We now copy the registers to the kernel stack (which
 * might cause little faults).  Any little fault will be handled without
 * saving state.  Thus when the little fault is completed, it will rfi
 * back to the original faulting instruction.
 */
	.globl	transfer_to_handler
transfer_to_handler:

	mfspr	r6,SPRG1
	li	r7,0
	stw	r7,PACAKSAVE(r6)	/* Force new frame for recursive fault */

	/* Restore the regs used above -- parameters to syscall */
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)

	stw	r22,_NIP(r1)
	stw	r23,_MSR(r1)
	SAVE_4GPRS(14, r1)
	SAVE_2GPRS(18, r1)
	SAVE_4GPRS(25, r1)
	SAVE_2GPRS(29, r1)
	SAVE_GPR(31, r1)

	andi.	r23,r23,MSR_PR
	mfspr	r23,SPRG3		/* if from user, fix up THREAD.regs */
	beq	2f
	addi	r24,r1,STACK_FRAME_OVERHEAD
	stw	r24,PT_REGS(r23)
2:	addi	r2,r23,-THREAD		/* set r2 to current */
	li	r22,RESULT
	stwcx.	r22,r22,r1		/* to clear the reservation */
	li	r22,0
	stw	r22,RESULT(r1)
	mfspr	r23,SPRG1		/* Get Paca address */
	stb	r20,PACAPROCENABLED(r23) /* soft enable or disabled */
	mflr	r23
	andi.	r24,r23,0x3f00		/* get vector offset */
	stw	r24,TRAP(r1)
	addi	r24,r2,TASK_STRUCT_SIZE	/* check for kernel stack overflow */
	cmplw	0,r1,r2
	cmplw	1,r1,r24
	crand	1,1,4
	bgt-	stack_ovf		/* if r2 < r1 < r2+TASK_STRUCT_SIZE */
	lwz	r24,0(r23)		/* virtual address of handler */
	lwz	r23,4(r23)		/* where to go when done */
	li	r20,MSR_KERNEL
	ori	r20,r20,MSR_EE		/* Always hard enabled */
	FIX_SRR1(r20,r22)
	mtspr	SRR0,r24
	mtspr	SRR1,r20
	mtlr	r23
	RFI				/* jump to handler, enable MMU */

/*
 * On kernel stack overflow, load up an initial stack pointer
 * and call StackOverflow(regs), which should not return.
 */
stack_ovf:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lis	r1,init_task_union@ha
	addi	r1,r1,init_task_union@l
	addi	r1,r1,TASK_UNION_SIZE-STACK_FRAME_OVERHEAD
	mfspr	r24,SPRG1
	li	r20,0
	stb	r20,PACAPROCENABLED(r24)	/* soft disable */
	lis	r24,StackOverflow@ha
	addi	r24,r24,StackOverflow@l
	li	r20,MSR_KERNEL
	ori	r20,r20,MSR_EE		/* Always hard enabled */
	FIX_SRR1(r20,r22)
	mtspr	SRR0,r24
	mtspr	SRR1,r20
	RFI

/*
 * Disable FP for the task which had the FPU previously,
 * and save its floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 * On SMP we know the fpu is free, since we give it up every
 * switch.  -- Cort
 * Assume r20 points to PACA on entry
 */
load_up_fpu:
	mfmsr	r5
	ori	r5,r5,MSR_FP
	SYNC
	MTMSRD(r5)			/* enable use of fpu now */
	isync
/*
 * For SMP, we don't do lazy FPU switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_fpu in switch_to.
 */
#ifndef CONFIG_SMP
	lis	r3,last_task_used_math@ha
	lwz	r4,last_task_used_math@l(r3)
	cmpi	0,r4,0
	beq	1f
	addi	r4,r4,THREAD		/* want last_task_used_math->thread */
	SAVE_32FPRS(0, r4)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR-4(r4)
	lwz	r5,PT_REGS(r4)
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r20,MSR_FP|MSR_FE0|MSR_FE1
	andc	r4,r4,r20		/* disable FP for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	/* enable use of FP after return */
	ori	r23,r23,MSR_FP|MSR_FE0|MSR_FE1
	mfspr	r5,SPRG3		/* current task's THREAD (phys) */
	lfd	fr0,THREAD_FPSCR-4(r5)
	mtfsf	0xff,fr0
	REST_32FPRS(0, r5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	stw	r4,last_task_used_math@l(r3)
#endif /* CONFIG_SMP */
	/* restore registers and return */
	lwz	r3,_CCR(r21)
	lwz	r4,_LINK(r21)
	mtcrf	0xff,r3
	mtlr	r4
	REST_GPR(1, r21)
	REST_4GPRS(3, r21)
	/* we haven't used ctr or xer */
	mtspr	SRR1,r23
	mtspr	SRR0,r22

	REST_GPR(20, r21)
	REST_2GPRS(22, r21)
	lwz	r21,GPR21(r21)
	SYNC
	RFI
	
/*
 * FP unavailable trap from kernel - print a message, but let
 * the task use FP in the kernel until it returns to user mode.
 */
KernelFP:
	lwz	r3,_MSR(r1)
	ori	r3,r3,MSR_FP
	stw	r3,_MSR(r1)		/* enable use of FP after return */
	lis	r3,86f@h
	ori	r3,r3,86f@l
	mr	r4,r2			/* current */
	lwz	r5,_NIP(r1)
	bl	printk
	b	ret_from_except
86:	.string	"floating point used in kernel (task=%p, pc=%x)\n"
	.align	4

/*
 * giveup_fpu(tsk)
 * Disable FP for the task given as the argument,
 * and save the floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 */
	.globl	giveup_fpu
giveup_fpu:
	mfmsr	r5
	ori	r5,r5,MSR_FP
	mtmsr	r5			/* enable use of fpu now */
	cmpi	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD	        /* want THREAD of task */
	lwz	r5,PT_REGS(r3)
	cmpi	0,r5,0
	SAVE_32FPRS(0, r3)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR-4(r3)
	beq	1f
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r3,MSR_FP|MSR_FE0|MSR_FE1
	andc	r4,r4,r3		/* disable FP for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	lis	r4,last_task_used_math@ha
	stw	r5,last_task_used_math@l(r4)
#endif /* CONFIG_SMP */
	blr

#ifdef CONFIG_SMP 
secondary_start:
	lis	r3,0,
	mr	r4,r24
	bl	identify_cpu
	bl	call_setup_cpu		/* Call setup_cpu for this CPU */

	/* get current */
	HMT_MEDIUM			/* Set thread priority to MEDIUM */
	lis	r2,current_set@h
	ori	r2,r2,current_set@l
	slwi	r24,r24,2		/* get current_set[cpu#] */
	lwzx	r2,r2,r24

	/* stack */
	addi	r1,r2,TASK_UNION_SIZE-STACK_FRAME_OVERHEAD
	li	r0,0
	stw	r0,0(r1)

	/* load up the MMU */
	bl	load_up_mmu

	/* ptr to phys current thread */
	addi	r4,r2,THREAD	/* phys address of our thread_struct */
	rlwinm	r4,r4,0,0,31
	mtspr	SPRG3,r4

	/* Set up address of Paca in current thread */
	lis	r23,xPaca@ha
	addi	r23,r23,xPaca@l
	/* r24 has CPU # * 4 at this point.  The Paca is 2048 bytes
	   long so multiply r24 by 512 to index into the array of Pacas */
	slwi	r24,r24,9
	add	r23,r23,r24
	rlwinm	r23,r23,0,0,31
	mtspr	SPRG1,r23

	li	r3,0
	stw	r3,PACAKSAVE(r23)	/* 0 => r1 has kernel sp */

	stb	r3,PACAPROCENABLED(r23)	/* Soft disabled */

	/* enable MMU and jump to start_secondary */
	
	li	r4,MSR_KERNEL
	ori	r4,r4,MSR_EE		/* Hard enabled */
	lis	r3,start_secondary@h
	ori	r3,r3,start_secondary@l
	mtspr	SRR0,r3
	FIX_SRR1(r4,r3)
	mtspr	SRR1,r4
	RFI
#endif /* CONFIG_SMP */

/*
 * Load stuff into the MMU.  Intended to be called with
 * IR=0 and DR=0.
 */
load_up_mmu:
	li	r0,16		/* load up segment register values */
	mtctr	r0		/* for context 0 */
	lis	r3,0x2000	/* Ku = 1, VSID = 0 */
	li	r4,0
3:	mtsrin	r3,r4
 	addi	r3,r3,0x111	/* increment VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	blr

/*
 * This is where the main kernel code starts.
 */
start_here:

	/* ptr to current */

	lis	r2,init_task_union@h
	ori	r2,r2,init_task_union@l

	/* Set up for using our exception vectors */

	addi	r4,r2,THREAD	/* init task's THREAD */
	rlwinm	r4,r4,0,0,31
	mtspr	SPRG3,r4

	/* Get address of Paca for processor 0 */
	lis	r11,xPaca@ha
	addi	r11,r11,xPaca@l
	rlwinm	r11,r11,0,0,31
	mtspr	SPRG1,r11

	li	r3,0
	stw  	r3,PACAKSAVE(r11)	/* 0 => r1 has kernel sp */

	stb	r3,PACAPROCENABLED(r11)	/* Soft disabled */

	addi	r1,r2,TASK_UNION_SIZE
	li	r0,0
	stwu	r0,-STACK_FRAME_OVERHEAD(r1)

	/* fix klimit for system map */
	lis	r6,embedded_sysmap_end@ha
	lwz	r7,embedded_sysmap_end@l(r6)

	cmpi	0,r7,0
	beq 	5f

	lis     r6, KERNELBASE@h
	add 	r7,r7,r6
	addi	r7,r7,4095
	li	r6,0x0FFF
	andc	r7,r7,r6

	lis	r6,klimit@ha
	stw	r7,klimit@l(r6)
5:	
	
/*
 * Decide what sort of machine this is and initialize the MMU.
 */
	bl	early_init	/* We have to do this with MMU on */

	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27
	li	r6,0		/* No cmdline parameters */
	bl	platform_init
	bl	MMU_init

	bl	load_up_mmu

	li	r4,MSR_KERNEL
	ori	r4,r4,MSR_EE	/* Hard enabled */
	FIX_SRR1(r4,r5)
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
	mtspr	SRR0,r3
	mtspr	SRR1,r4
	RFI				/* ensure correct MSR and jump to
					   start_kernel */
hash_page:
	mflr	r21			/* Save LR in r21 */

	/* 
	 * We hard enable here (but first soft disable) so that the hash_page
	 * code can spin on the hash_table_lock without problem on a shared
	 * processor
	 */
	li	r0,0
	stb	r0,PACAPROCENABLED(r20)	/* Soft disable */

	mfmsr	r0
	ori	r0,r0,MSR_EE
	mtmsr	r0			/* Hard enable */

	bl	create_hpte		/* add the hash table entry */
	/*
	 * Now go back to hard disabled
	 */
	mfmsr	r0
	li	r4,0
	ori	r4,r4,MSR_EE
	andc	r0,r0,r4
	mtmsr	r0			/* Hard disable */

	lwz	r0,_SOFTE(r1)
	mtlr	r21			/* restore LR */
	mr	r21,r1			/* restore r21 */

	cmpi	0,r0,0			/* See if we will soft enable in */
					/* fault_exit */
	beq	5f			/* if not, skip checks */

	CHECKANYINT(r4,r5,r6) 		/* if pending interrupts, process them */
	bne-	do_pending_int

5:	stb	r0,PACAPROCENABLED(r20)	/* Restore soft enable/disable */

	cmpi	0,r3,0                  /* check return code form create_hpte */
	bnelr

/*
 * htab_reloads counts the number of times we have to fault an
 * HPTE into the hash table.  This should only happen after a
 * fork (because fork does a flush_tlb_mm) or a vmalloc or ioremap.
 * Where a page is faulted into a process's address space,
 * update_mmu_cache gets called to put the HPTE into the hash table
 * and those are counted as preloads rather than reloads.
 */
	lis	r2,htab_reloads@ha
	lwz	r3,htab_reloads@l(r2)
	addi	r3,r3,1
	stw	r3,htab_reloads@l(r2)

fault_exit:
	
	lwz	r3,_CCR(r1)
	lwz	r4,_LINK(r1)
	lwz	r5,_CTR(r1)
	lwz	r6,_XER(r1)

	mtcrf	0xff,r3
	mtlr	r4
	mtctr	r5
	mtspr	XER,r6

	lwz	r0,GPR0(r1)
	REST_8GPRS(2, r1)
	REST_4GPRS(10, r1)
	FIX_SRR1(r23,r20)
	mtspr	SRR1,r23
	mtspr	SRR0,r22
	REST_4GPRS(20, r1)

	lwz	r1,GPR1(r1)
	RFI

/*
 * Set up the segment registers for a new context.
 *   context in r3
 */
_GLOBAL(set_context)
	mulli	r3,r3,897	/* multiply context by skew factor */
	rlwinm	r3,r3,4,8,27	/* VSID = (context & 0xfffff) << 4 */
	addis	r3,r3,0x6000	/* Set Ks, Ku bits */
	li	r0,NUM_USER_SEGMENTS
	mtctr	r0

	li	r4,0
3:
	mtsrin	r3,r4
	addi	r3,r3,0x111	/* next VSID */
	rlwinm	r3,r3,0,8,3	/* clear out any overflow from VSID field */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	isync

set_kernel_segregs:

/* 
 * Reload the last four segment registers because they 
 * might have been clobbered by the hypervisor if we
 * are running on a shared processor
 */
 	lis	r3,0x2000	/* Set Ku = 1 */
	addi	r3,r3,0xCCC	/* Set VSID = CCC */
	lis	r4,0xC000	/* Set SR = C */
	li	r0,4		/* Load regs C, D, E and F */
	mtctr	r0
4:	mtsrin	r3,r4
 	addi	r3,r3,0x111	/* increment VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	4b
	isync

	blr


/* Hypervisor call
 *
 * Invoke the iSeries hypervisor (PLIC) via the System Call instruction.
 * Parameters are passed to this routine in registers r3 - r10 and are
 * converted to 64-bit by combining registers. eg. r3 <- r3 
 * r4 <- r5,r6, r5 <- r7,r8, r6 <- r9,r10
 * 
 * r3 contains the HV function to be called
 * r5,r6 contain the first 64-bit operand
 * r7,r8 contain the second 64-bit operand
 * r9,r10 contain the third 64-bit operand
 * caller's stack frame +8 contains the fourth 64-bit operand
 * caller's stack frame +16 contains the fifth 64-bit operand
 * caller's stack frame +24 contains the sixth 64-bit operand
 * caller's stack frame +32 contains the seventh 64-bit operand
 *
 */

_GLOBAL(HvCall) 
_GLOBAL(HvCall0)
_GLOBAL(HvCall1)
_GLOBAL(HvCall2)
_GLOBAL(HvCall3)
_GLOBAL(HvCall4)
_GLOBAL(HvCall5)
_GLOBAL(HvCall6)
_GLOBAL(HvCall7)
   /* 
    * Stack a frame and save one reg so we can hang on to
    * the old MSR
    */
   stwu   r1,-64(r1)
   stw	  r31,60(r1)

   stw    r22,24(r1)
   stw    r23,28(r1)
   stw    r24,32(r1)
   stw    r25,36(r1)
   stw    r26,40(r1)
   stw    r27,44(r1)
   stw    r28,48(r1)
   stw	  r29,52(r1)

   /*
    * The hypervisor assumes CR fields 0-4 are volatile, but
    * gcc assumes CR fields 2-7 are non-volatile.  
    * We must save and restore the CR here
    */
   mfcr   r31
   stw	  r31,20(r1)
   
   /* Before we can convert to using 64-bit registers we must
    * soft disable external interrupts as the interrupt handlers
    * don't preserve the high half of the registers
    */

   mfspr  r11,SPRG1		/* Get the Paca pointer */
   lbz    r31,PACAPROCENABLED(r11) /* Get soft enable/disable flag */
   li	  r0,0
   stb	  r0,PACAPROCENABLED(r11)  /* Soft disable */

   /* Get parameters four through seven */

   lwz    r22,72(r1)
   lwz    r23,76(r1)
   lwz    r24,80(r1)
   lwz    r25,84(r1)
   lwz    r26,88(r1)
   lwz    r27,92(r1)
   lwz    r28,96(r1)
   lwz    r29,100(r1)
   /* Now it is safe to operate on 64-bit registers
    * 
    * Format the operands into the 64-bit registers
    *
    */
   rldicr r5,r5,32,31		/* r5 = r5 << 32		*/
   rldicl r6,r6,0,32		/* r6 = r6 & 0x00000000ffffffff */
   or     r4,r5,r6		/* r4 = r5 | r6			*/
   rldicr r7,r7,32,31		/* r7 = r7 << 32		*/
   rldicl r8,r8,0,32		/* r8 = r8 & 0x00000000ffffffff */
   or	  r5,r7,r8		/* r5 = r7 | r8			*/
   rldicr r9,r9,32,31		/* r9 = r9 << 32		*/
   rldicl r10,r10,0,32		/* r10 = r10 & 0x00000000ffffffff */
   or	  r6,r9,r10		/* r6 = r9 | r10		*/
   rldicr r22,r22,32,31		/* r22 = r22 << 32              */
   rldicl r23,r23,0,32		/* r23 = r23 & 0x00000000ffffffff */
   or     r7,r22,r23		/* r7 = r22 | r23               */
   rldicr r24,r24,32,31		/* r24 = r24 << 32              */
   rldicl r25,r25,0,32		/* r25 = r25 & 0x00000000ffffffff */
   or     r8,r24,r25		/* r8 = r24 | r25               */
   rldicr r26,r26,32,31		/* r26 = r26 << 32              */
   rldicl r27,r27,0,32		/* r27 = r27 & 0x00000000ffffffff */
   or     r9,r26,r27		/* r9 = r26 | r27               */
   rldicr r28,r28,32,31		/* r28 = r28 << 32              */
   rldicl r29,r29,0,32		/* r29 = r29 & 0x00000000ffffffff */
   or     r10,r28,r29		/* r10 = r28 | r29              */
   
   /* 
    * Extract the hypervisor function call number from R3
    * and format it into the 64-bit R3.
    */
   rldicr r0,r3,32,15		/* r0 = (r3 << 32) & 0xffff000000000000 */
   rldicl r3,r3,0,48		/* r3 = r3 & 0x000000000000ffff */
   or	  r3,r3,r0		/* r3 = r3 | r0			*/
   
   /*
    * r0 = 0xffffffffffffffff indicates a hypervisor call 
    */
   li	  r0,-1			/* r1 = 0xffffffffffffffff      */
   
   /* Invoke the hypervisor via the System Call instruction */

   sc
  
   HMT_MEDIUM
  
   /* Return value in 64-bit R3
    * format it into R3 and R4
    */
   rldicl r4,r3,0,32		/* r4 = r3 & 0x00000000ffffffff */
   rldicl r3,r3,32,32		/* r3 = (r3 >> 32) & 0x00000000ffffffff */

   /* We are now done with 64-bit registers it is safe to touch
    * the stack again.
    */
   lwz    r22,24(r1)
   lwz    r23,28(r1)
   lwz    r24,32(r1)
   lwz    r25,36(r1)
   lwz    r26,40(r1)
   lwz    r27,44(r1)
   lwz    r28,48(r1)
   lwz    r29,52(r1)

   /* While we were running in the hypervisor, a decrementer or
    * external interrupt may have occured.  If we are about to
    * enable here, we must check for these and process them
    */

   cmpi   0,r31,0		/* check if going to enable */
   beq	  1f			/* skip checks if staying disabled */

   /* Save r3, r4 and LR */
   stw	  r3,52(r1)
   stw    r4,48(r1)
   mflr   r3
   stw    r3,44(r1)

   /* enable and check for decrementers/lpEvents */
   mr	r3,r31
   bl	__restore_flags

   /* Restore r3, r4 and LR */
   lwz    r3,44(r1)
   mtlr   r3
   lwz    r3,52(r1)
   lwz    r4,48(r1)

1:
   /* 
    * Unstack the frame and restore r31 and the CR
    */
   lwz    r31,20(r1)
   mtcrf  0xff,r31 
   lwz    r31,60(r1)
   lwz	  r1,0(r1)
    
   blr 
   
/* Hypervisor call with return data
 *
 * Invoke the iSeries hypervisor (PLIC) via the System Call instruction.
 * The Hv function ID is passed in r3
 * The address of the return data structure is passed in r4
 * Parameters are passed to this routine in registers r5 - r10 and are
 * converted to 64-bit by combining registers. eg. r3 <- r3 
 * r4 <- r5,r6, r5 <- r7,r8, r6 <- r9,r10
 * 
 * r3 contains the HV function to be called
 * r4 contains the address of the return data structure
 * r5,r6 contain the first 64-bit operand
 * r7,r8 contain the second 64-bit operand
 * r9,r10 contain the third 64-bit operand
 * caller's stack frame +8 contains the fourth 64-bit operand
 * caller's stack frame +16 contains the fifth 64-bit operand
 * caller's stack frame +24 contains the sixth 64-bit operand
 * caller's stack frame +32 contains the seventh 64-bit operand
 *
 */

_GLOBAL(HvCallRet16) 
_GLOBAL(HvCall0Ret16)
_GLOBAL(HvCall1Ret16)
_GLOBAL(HvCall2Ret16)
_GLOBAL(HvCall3Ret16)
_GLOBAL(HvCall4Ret16)
_GLOBAL(HvCall5Ret16)
_GLOBAL(HvCall6Ret16)
_GLOBAL(HvCall7Ret16)

   /*
    * Stack a frame and save some regs
    */
   stwu	  r1,-64(r1)
   stw	  r31,60(r1)
   stw	  r30,56(r1)

   stw    r22,24(r1)
   stw    r23,28(r1)
   stw    r24,32(r1)
   stw    r25,36(r1)
   stw    r26,40(r1)
   stw    r27,44(r1)
   stw    r28,48(r1)
   stw	  r29,52(r1)

   mr     r30,r4		/* Save return data address */

   /*
    * The hypervisor assumes CR fields 0-4 are volatile, but
    * gcc assumes CR fields 2-7 are non-volatile.  
    * We must save and restore the CR here
    */
   mfcr   r31
   stw	  r31,20(r1)

   /* Before we can convert to using 64-bit registers we must
    * soft disable external interrupts as the interrupt handlers
    * don't preserve the high half of the registers
    */

   mfspr  r11,SPRG1		/* Get the Paca pointer */
   lbz    r31,PACAPROCENABLED(r11) /* Get soft enable/disable flag */
   li	  r0,0
   stb	  r0,PACAPROCENABLED(r11)  /* Soft disable */

   /* Get parameters four through seven */

   lwz    r22,76(r1)
   lwz    r23,80(r1)
   lwz    r24,84(r1)
   lwz    r25,88(r1)
   lwz    r26,92(r1)
   lwz    r27,96(r1)
   lwz    r28,100(r1)
   lwz    r29,104(r1)

   /* Now it is safe to operate on 64-bit registers
    *
    */
    
   /* 
    * Format the operands into the 64-bit registers
    */

   rldicr r5,r5,32,31		/* r5 = r5 << 32		*/
   rldicl r6,r6,0,32		/* r6 = r6 & 0x00000000ffffffff */
   or     r4,r5,r6		/* r4 = r5 | r6			*/
   rldicr r7,r7,32,31		/* r7 = r7 << 32		*/
   rldicl r8,r8,0,32		/* r8 = r8 & 0x00000000ffffffff */
   or	  r5,r7,r8		/* r5 = r7 | r8			*/
   rldicr r9,r9,32,31		/* r9 = r9 << 32		*/
   rldicl r10,r10,0,32		/* r10 = r10 & 0x00000000ffffffff */
   or	  r6,r9,r10		/* r6 = r9 | r10		*/
   rldicr r22,r22,32,31		/* r22 = r22 << 32              */
   rldicl r23,r23,0,32		/* r23 = r23 & 0x00000000ffffffff */
   or     r7,r22,r23		/* r7 = r22 | r23               */
   rldicr r24,r24,32,31		/* r24 = r24 << 32              */
   rldicl r25,r25,0,32		/* r25 = r25 & 0x00000000ffffffff */
   or     r8,r24,r25		/* r8 = r24 | r25               */
   rldicr r26,r26,32,31		/* r26 = r26 << 32              */
   rldicl r27,r27,0,32		/* r27 = r27 & 0x00000000ffffffff */
   or     r9,r26,r27		/* r9 = r26 | r27               */
   rldicr r28,r28,32,31		/* r28 = r28 << 32              */
   rldicl r29,r29,0,32		/* r29 = r29 & 0x00000000ffffffff */
   or     r10,r28,r29		/* r10 = r28 | r29              */
   /* 
    * Extract the hypervisor function call number from R3
    * and format it into the 64-bit R3.
    */
   rldicr r0,r3,32,15		/* r4 = (r3 << 32) & 0xffff000000000000 */
   rldicl r3,r3,0,48		/* r3 = r3 & 0x000000000000ffff */
   or	  r3,r3,r0		/* r3 = r3 | r4			*/
   
   /*
    * r0 = 0xffffffffffffffff indicates a hypervisor call 
    */
   li	  r0,-1			/* r1 = 0xffffffffffffffff      */
   
   /* Invoke the hypervisor via the System Call instruction */

   sc
  
   HMT_MEDIUM
  
   /* Return values in 64-bit R3, R4, R5 and R6
    * place R3 and R4 into data structure, R5 into R3,R4 
    */
   rldicl r6,r3,32,32		/* r6 = (r3 >> 32) & 0x00000000ffffffff */
   rldicl r7,r3,0,32		/* r7 = r3 & 0x00000000ffffffff */
   rldicl r8,r4,32,32		/* r8 = (r4 >> 32) & 0x00000000ffffffff */
   rldicl r9,r4,0,32		/* r9 = r4 & 0x00000000ffffffff */
       
   rldicl r4,r5,0,32		/* r4 = r5 & 0x00000000ffffffff */
   rldicl r3,r5,32,32		/* r3 = (r5 >> 32) & 0x00000000ffffffff */

   /* We are now done with 64-bit registers it is safe to touch
    * the stack again.
    */
   stw    r6,0(r30)		/* Save returned data */
   stw    r7,4(r30)
   stw    r8,8(r30)
   stw    r9,12(r30)

   lwz    r22,24(r1)
   lwz    r23,28(r1)
   lwz    r24,32(r1)
   lwz    r25,36(r1)
   lwz    r26,40(r1)
   lwz    r27,44(r1)
   lwz    r28,48(r1)
   lwz    r29,52(r1)

   /* While we were running in the hypervisor, a decrementer or
    * external interrupt may have occured.  If we are about to
    * enable here, we must check for these and process them
    */

   cmpi   0,r31,0		/* check if going to enable */
   beq	  1f			/* skip checks if staying disabled */

   /* Save r3, r4 and LR */
   stw	  r3,48(r1)
   stw    r4,44(r1)
   mflr   r3
   stw    r3,40(r1)

   /* enable and check for decrementers/lpEvents */
   mr	  r3,r31
   bl	__restore_flags

   lwz    r3,40(r1)
   mtlr   r3
   lwz    r3,48(r1)
   lwz    r4,44(r1)
   
1:
   /* 
    * Unstack the frame and restore r30, r31 and CR
    */
   lwz    r31,20(r1)
   mtcrf  0xff,r31 
   lwz    r30,56(r1)
   lwz	  r31,60(r1)
   lwz	  r1,0(r1)
   
   blr 


/* Hypervisor call (use no stack)
 *
 * These functions must be called with interrupts soft disabled.
 * The caller is responsible for saving the non-volatile CR
 * The operands should already be formatted into the 64-bit registers
 *
 * Invoke the iSeries hypervisor (PLIC) via the System Call instruction.
 * 
 * r3 contains the HV function to be called
 * r4 contains the first 64-bit operand
 * r5 contains the second 64-bit operand
 * r6 contains the third 64-bit operand
 * r7 contains the fourth 64-bit operand
 * r8 contains the fifth 64-bit operand
 * r9 contains the sixth 64-bit operand
 * r10 contains the seventh 64-bit operand
 *
 * data is returned in 64-bit registers r3-r6
 *
 */

_GLOBAL(HvXCall) 
_GLOBAL(HvXCall0)
_GLOBAL(HvXCall1)
_GLOBAL(HvXCall2)
_GLOBAL(HvXCall3)
   /* 
    * Extract the hypervisor function call number from R3
    * and format it into the 64-bit R3.
    */
   rldicr r0,r3,32,15		/* r0 = (r3 << 32) & 0xffff000000000000 */
   rldicl r3,r3,0,48		/* r3 = r3 & 0x000000000000ffff */
   or	  r3,r3,r0		/* r3 = r3 | r0			*/
   
   /*
    * r0 = 0xffffffffffffffff indicates a hypervisor call 
    */
   li	  r0,-1			/* r1 = 0xffffffffffffffff      */
   
   /* Invoke the hypervisor via the System Call instruction */

   sc
   
   blr 
   
_GLOBAL(__setup_cpu_power3)
	blr
_GLOBAL(__setup_cpu_power4)
	blr
_GLOBAL(__setup_cpu_generic)
	blr

_GLOBAL(iSeries_check_intr)
	mflr	r31
	lwz 	r5,_SOFTE(r1)
	cmpi    0,r5,0  
	beqlr
	mfspr   r5,SPRG1
	lbz 	r5,PACAPROCENABLED(r5)
	cmpi    0,r5,0  
	bnelr
	/* Check for lost decrementer interrupts.
	 * (If decrementer popped while we were in the hypervisor)
    	 * (calls timer_interrupt if so)
    	 */
3: CHECKDECR(r4,r5)
   	/* Check for pending interrupts.  If no interrupts pending,
    	 * then CR0 = "eq" and r4 == 0 
    	 * (kills registers r5 and r6) 
    	 */
	beq+    1f
	addi    r3,r1,STACK_FRAME_OVERHEAD
	bl  	timer_interrupt
1:
	CHECKLPQUEUE(r4,r5,r6)  
	beq+    2f
	addi    r3,r1,STACK_FRAME_OVERHEAD
	bl  	do_IRQ  
	b   	3b  

2:  	mtlr	r31
	blr

/*
 * Fake an interrupt from kernel mode.
 * This is used when enable_irq loses an interrupt.
 * We only fill in the stack frame minimally.
 */
_GLOBAL(fake_interrupt)
	mflr    r0
	stw 	r0,4(r1)
	stwu    r1,-INT_FRAME_SIZE(r1)
	stw 	r0,_NIP(r1)
	stw 	r0,_LINK(r1)
	mfmsr   r3
	stw 	r3,_MSR(r1)
	li  	r0,0x0fac
	stw 	r0,TRAP(r1)
	addi    r3,r1,STACK_FRAME_OVERHEAD
	li  	r4,1
	bl  	do_IRQ
	addi    r1,r1,INT_FRAME_SIZE
	lwz 	r0,4(r1)

	mtlr    r0
	blr

/*
 * Fake a decrementer from kernel mode.
 * This is used when the decrementer pops in
 * the hypervisor.  We only fill in the stack
 * frame minimally
 */
_GLOBAL(fake_decrementer)
	mflr    r0
	stw 	r0,4(r1)
	stwu    r1,-INT_FRAME_SIZE(r1)
	stw 	r0,_NIP(r1)
	stw 	r0,_LINK(r1)
	mfmsr   r3
	stw 	r3,_MSR(r1)
	li  	r0,0x0fac
	stw 	r0,TRAP(r1)
	addi    r3,r1,STACK_FRAME_OVERHEAD
	bl  	timer_interrupt
	addi    r1,r1,INT_FRAME_SIZE
	lwz 	r0,4(r1)

	mtlr    r0
	blr

_GLOBAL(create_hpte)
	stwu	r1,-INT_FRAME_SIZE(r1)
	stw	r0,GPR0(r1)
	lwz	r0,0(r1)
	stw	r0,GPR1(r1)
	/* r3-r13 are caller saved  */
	stw	r2,GPR2(r1)
	SAVE_8GPRS(4, r1)
	SAVE_2GPRS(12, r1)
	SAVE_4GPRS(20,r1)
  	mfspr	r20,XER
 	mfctr	r22
	stw	r20,_XER(r1)
	stw	r22,_CTR(r1)
	mflr	r20		
	mfmsr	r22
	stw	r20,_NIP(r1)
	stw	r22,_MSR(r1)
	stw	r20,_LINK(r1)
	bl	iSeries_create_hpte
	lwz	r0,GPR0(r1)
	lwz	r2,GPR2(r1)
	REST_8GPRS(4, r1)
	REST_2GPRS(12, r1)
	lwz	r20,_NIP(r1)	
	lwz	r22,_XER(r1)
	mtlr	r20
	mtspr	XER,r22
	lwz	r20,_CTR(r1)
	mtctr	r20
	
	REST_4GPRS(20,r1)
	addi	r1,r1,INT_FRAME_SIZE
	blr

###
### extern void abort(void)
###
### Invoke the hypervisor to kill the partition.          
### 
	
_GLOBAL(abort)


/*
 * We put a few things here that have to be page-aligned.
 * This stuff goes at the beginning of the data segment,
 * which is page-aligned.
 */
	.data
	.globl	sdata
sdata:

	.globl	empty_zero_page
empty_zero_page:
	.space	4096

	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	4096	

/*
 * This space gets a copy of optional info passed to us by the bootstrap
 * Used to pass parameters into the kernel like root=/dev/sda1, etc.
 */	
	.globl	cmd_line
cmd_line:
	.space	512
