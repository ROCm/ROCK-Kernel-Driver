/*
 *    Copyright (c) 1995-1996 Gary Thomas <gdt@linuxppc.org>
 *      Initial PowerPC version.
 *    Copyright (c) 1996 Cort Dougan <cort@cs.nmt.edu>
 *      Rewritten for PReP
 *    Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Low-level exception handers, MMU support, and rewrite.
 *    Copyright (c) 1997 Dan Malek <dmalek@jlc.net>
 *      PowerPC 8xx modifications.
 *    Copyright (c) 1998-1999 TiVo, Inc.
 *      PowerPC 403GCX modifications.
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *      PowerPC 403GCX/405GP modifications.
 *
 *    Module name: head_4xx.S
 *
 *    Description:
 *      Kernel execution entry point code.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version
 *    2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>

#include "ppc_asm.h"


/* Preprocessor Defines */

#define	STND_EXC	0
#define	CRIT_EXC	1

###
### Check to make sure the right processor has been defined.
###

#if !defined(CONFIG_4xx)
#error "This file is only appropriate for kernels supporting the PPC4xx."
#endif

###
### Execution entry point.
###

###
### As with the other PowerPC ports, it is expected that when code
### execution begins here, the following registers contain valid, yet
### optional, information:
###
###   r3 - Board info structure pointer (DRAM, frequency, MAC address, etc.)
###   r4 - Starting address of the init RAM disk
###   r5 - Ending address of the init RAM disk
###   r6 - Start of kernel command line string (e.g. "mem=96m")
###   r7 - End of kernel command line string
### 
	
	.text
_GLOBAL(_stext)
_GLOBAL(_start)
	## Save residual data, init RAM disk, and command line parameters
	
	mr	r31,r3
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7

	## Set the ID for this CPU

	li	r24,0

	## Invalidate all TLB entries

	tlbia
	
	## We should still be executing code at physical address 0x0000xxxx
	## at this point. However, start_here is at virtual address
	## 0xC000xxxx. So, set up a TLB mapping to cover this once
	## translation is enabled.

	lis	r3,KERNELBASE@h		# Load the kernel virtual address
	ori	r3,r3,KERNELBASE@l
	tophys(r4,r3)			# Load the kernel physical address

	## Save the existing PID and load the kernel PID.
		
	mfspr	r7,SPRN_PID		# Save the old PID
	li	r0,0
	mtspr	SPRN_PID,r0		# Load the kernel PID

	## Configure and load entry into TLB slot 0.
	
	clrrwi	r4,r4,10		# Mask off the real page number
	ori	r4,r4,(TLB_WR | TLB_EX)	# Set the write and execute bits
	
	clrrwi	r3,r3,10		# Mask off the effective page number
	ori	r3,r3,(TLB_VALID | TLB_PAGESZ(PAGESZ_16M))

	tlbwe	r4,r0,TLB_DATA		# Load the data portion of the entry
	tlbwe	r3,r0,TLB_TAG		# Load the tag portion of the entry
	isync
	
	mtspr	SPRN_PID,r7		# Restore the existing PID
	
	## Establish the exception vector base
	
	lis	r4,KERNELBASE@h		# EVPR only uses the high 16-bits
	tophys(r0,r4)			# Use the physical address
	mtspr	SPRN_EVPR,r0

	## Enable the MMU and jump to the main PowerPC kernel start-up code

	mfmsr	r0			# Get the machine state register
	ori	r0,r0,(MSR_DR | MSR_IR)	# Enable data and instr. translation
	mtspr	SPRN_SRR1,r0		# Set up the new machine state register
	lis	r0,start_here@h		
	ori	r0,r0,start_here@l	
	mtspr	SPRN_SRR0,r0		# Set up the new instruction pointer
	rfi				# Jump to start_here w/ translation on


###
### Exception vector entry code. This code runs with address translation
### turned off (i.e. using physical addresses). We assume SPRG3 has the
### physical address of the current task thread_struct.
### 

	## Common exception code for all exception types.

#define COMMON_PROLOG							     \
0:	mtspr	SPRN_SPRG0,r20;		/* We need r20, move it to SPRG0   */\
	mtspr	SPRN_SPRG1,r21;		/* We need r21, move it to SPRG1   */\
	mfcr	r20;			/* We need the CR, move it to r20  */\
	mfspr	r21,SPRN_SPRG2;		/* Exception stack to use	   */\
	cmpwi	cr0,r21,0;		/* From user mode or RTAS?	   */\
	bne	1f;			/* Not RTAS, branch		   */\
	tophys(r21, r1);		/* Convert vka in r1 to pka in r21 */\
	subi	r21,r21,INT_FRAME_SIZE;	/* Allocate an exception frame	   */\
1:	stw	r20,_CCR(r21);		/* Save CR on the stack		   */\
	stw	r22,GPR22(r21);		/* Save r22 on the stack	   */\
	stw	r23,GPR23(r21);		/* r23 Save on the stack	   */\
	mfspr	r20,SPRN_SPRG0;		/* Get r20 back out of SPRG0	   */\
	stw	r20,GPR20(r21);		/* Save r20 on the stack	   */\
	mfspr	r22,SPRN_SPRG1;		/* Get r21 back out of SPRG0	   */\
	stw	r22,GPR21(r21);		/* Save r21 on the stack	   */\
	mflr	r20;							     \
	stw	r20,_LINK(r21);		/* Save LR on the stack		   */\
	mfctr	r22;							     \
	stw	r22,_CTR(r21);		/* Save CTR on the stack	   */\
	mfspr	r20,XER;						     \
	stw	r20,_XER(r21);		/* Save XER on the stack	   */

#define	COMMON_EPILOG							     \
	stw	r0,GPR0(r21);		/* Save r0 on the stack		   */\
	stw	r1,GPR1(r21);		/* Save r1 on the stack		   */\
	stw	r2,GPR2(r21);		/* Save r2 on the stack		   */\
	stw	r1,0(r21);						     \
	tovirt(r1,r21);			/* Set-up new kernel stack pointer */\
	SAVE_4GPRS(3, r21);		/* Save r3 through r6 on the stack */\
	SAVE_GPR(7, r21);		/* Save r7 on the stack		   */

	## Common exception code for standard (non-critical) exceptions.

#define	STND_EXCEPTION_PROLOG						     \
	COMMON_PROLOG;							     \
	mfspr	r22,SPRN_SRR0;		/* Faulting instruction address	   */\
	mfspr	r23,SPRN_SRR1;		/* MSR at the time of fault	   */\
	COMMON_EPILOG;

	## Common exception code for critical exceptions.
	
#define	CRIT_EXCEPTION_PROLOG						     \
	COMMON_PROLOG;							     \
	mfspr	r22,SPRN_SRR2;		/* Faulting instruction address	   */\
	mfspr	r23,SPRN_SRR3;		/* MSR at the time of fault	   */\
	COMMON_EPILOG;

###
### Macros for specific exception types
### 

#define	START_EXCEPTION(n, label)					     \
	. = n;								     \
label:


#define FINISH_EXCEPTION(func)						     \
	bl	transfer_to_handler;					     \
	.long	func;							     \
	.long	ret_from_except
	
		
#define STND_EXCEPTION(n, label, func)					     \
	START_EXCEPTION(n, label);					     \
	STND_EXCEPTION_PROLOG;						     \
	addi	r3,r1,STACK_FRAME_OVERHEAD;				     \
	li	r7,STND_EXC;						     \
	li	r20,MSR_KERNEL;						     \
	FINISH_EXCEPTION(func)

	
#define	CRIT_EXCEPTION(n, label, func)					     \
	START_EXCEPTION(n, label);					     \
	CRIT_EXCEPTION_PROLOG;						     \
	addi	r3,r1,STACK_FRAME_OVERHEAD;				     \
	li	r7,CRIT_EXC;						     \
	li	r20,MSR_KERNEL;						     \
	FINISH_EXCEPTION(func)

	
###
### Exception vectors.
### 
	
### 0x0100 - Critical Interrupt Exception

	CRIT_EXCEPTION(0x0100,	CriticalInterrupt,	UnknownException)

### 0x0200 - Machine Check Exception
	
	CRIT_EXCEPTION(0x0200,	MachineCheck,		MachineCheckException)

### 0x0300 - Data Storage Exception

	START_EXCEPTION(0x0300,	DataAccess)
	STND_EXCEPTION_PROLOG
	mfspr	r5,SPRN_ESR		# Grab the ESR, save it, pass as arg3
	stw	r5,_ESR(r21)
	mfspr	r4,SPRN_DEAR		# Grab the DEAR, save it, pass as arg2
	stw	r4,_DEAR(r21)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r7,STND_EXC		# This is a standard exception
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		# Copy EE bit from the saved MSR
	FINISH_EXCEPTION(do_page_fault)	# do_page_fault(regs, ESR, DEAR)
	
### 0x0400 - Instruction Storage Exception

	START_EXCEPTION(0x0400, InstructionAccess)
	STND_EXCEPTION_PROLOG
	mr	r4,r22			# Pass SRR0 as arg2
	mr	r5,r23			# Pass SRR1 as arg3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r7,STND_EXC		# This is a standard exception
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		# Copy EE bit from the saved MSR
	FINISH_EXCEPTION(do_page_fault)	# do_page_fault(regs, SRR0, SRR1)
	
### 0x0500 - External Interrupt Exception

	START_EXCEPTION(0x0500, HardwareInterrupt)
	STND_EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r7,STND_EXC
	li	r20,MSR_KERNEL
	li	r4,0
	bl	transfer_to_handler
_GLOBAL(do_IRQ_intercept)
	.long	do_IRQ
	.long	ret_from_intercept

### 0x0600 - Alignment Exception
	
	START_EXCEPTION(0x0600, Alignment)
	STND_EXCEPTION_PROLOG
	mfspr	r4,SPRN_DEAR		# Grab the DEAR and save it
	stw	r4,_DEAR(r21)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r7,STND_EXC		# This is a standard exception
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		# Copy EE bit from the saved MSR
	FINISH_EXCEPTION(AlignmentException)

### 0x0700 - Program Exception

	START_EXCEPTION(0x0700,	ProgramCheck)
	STND_EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r7,STND_EXC		# This is a standard exception
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		# Copy EE bit from the saved MSR
	FINISH_EXCEPTION(ProgramCheckException)
	
	STND_EXCEPTION(0x0800,	Trap_08,		UnknownException)
	STND_EXCEPTION(0x0900,	Trap_09,		UnknownException)
	STND_EXCEPTION(0x0A00,	Trap_0A,		UnknownException)
	STND_EXCEPTION(0x0B00,	Trap_0B,		UnknownException)		
### 0x0C00 - System Call Exception

	START_EXCEPTION(0x0C00,	SystemCall)
	STND_EXCEPTION_PROLOG
	stw	r3,ORIG_GPR3(r21)
	li	r7,STND_EXC		# This is a standard exception
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		# Copy EE bit from the saved MSR
	FINISH_EXCEPTION(DoSyscall)

	STND_EXCEPTION(0x0D00,	Trap_0D,		UnknownException)
	STND_EXCEPTION(0x0E00,	Trap_0E,		UnknownException)
	STND_EXCEPTION(0x0F00,	Trap_0F,		UnknownException)

### 0x1000 - Programmable Interval Timer (PIT) Exception

	START_EXCEPTION(0x1000,	Decrementer)
	STND_EXCEPTION_PROLOG
	lis	r0,TSR_PIS@h		# Set-up the PIT exception mask
	mtspr	SPRN_TSR,r0		# Clear the PIT exception
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r7,STND_EXC		# This is a standard exception
	li	r20,MSR_KERNEL
	bl	transfer_to_handler
_GLOBAL(timer_interrupt_intercept)
	.long	timer_interrupt
	.long	ret_from_intercept

#if 0
### 0x1010 - Fixed Interval Timer (FIT) Exception
	
	STND_EXCEPTION(0x1010,	FITException,		UnknownException)

### 0x1020 - Watchdog Timer (WDT) Exception

	CRIT_EXCEPTION(0x1020,	WDTException,		UnknownException)
#endif

### 0x1100 - Data TLB Miss Exception

	STND_EXCEPTION(0x1100,	DTLBMiss, 		PPC4xx_dtlb_miss)

### 0x1200 - Instruction TLB Miss Exception

	STND_EXCEPTION(0x1200,	ITLBMiss, 		PPC4xx_itlb_miss)

	STND_EXCEPTION(0x1300,	Trap_13,		UnknownException)
	STND_EXCEPTION(0x1400,	Trap_14,		UnknownException)
	STND_EXCEPTION(0x1500,	Trap_15,		UnknownException)
	STND_EXCEPTION(0x1600,	Trap_16,		UnknownException)
	STND_EXCEPTION(0x1700,	Trap_17,		UnknownException)
	STND_EXCEPTION(0x1800,	Trap_18,		UnknownException)
	STND_EXCEPTION(0x1900,	Trap_19,		UnknownException)
	STND_EXCEPTION(0x1A00,	Trap_1A,		UnknownException)
	STND_EXCEPTION(0x1B00,	Trap_1B,		UnknownException)
	STND_EXCEPTION(0x1C00,	Trap_1C,		UnknownException)
	STND_EXCEPTION(0x1D00,	Trap_1D,		UnknownException)
	STND_EXCEPTION(0x1E00,	Trap_1E,		UnknownException)
	STND_EXCEPTION(0x1F00,	Trap_1F,		UnknownException)
	
### 0x2000 - Debug Exception

	CRIT_EXCEPTION(0x2000,	DebugTrap,		UnknownException)

###
### Other PowerPC processors, namely those derived from the 6xx-series
### have vectors from 0x2100 through 0x2F00 defined, but marked as reserved.
### However, for the 4xx-series processors these are neither defined nor
### reserved.
### 

### 
### This code finishes saving the registers to the exception frame
### and jumps to the appropriate handler for the exception, turning
### on address translation.
###

_GLOBAL(transfer_to_handler)
	stw	r22,_NIP(r21)		# Save the faulting IP on the stack
	stw	r23,_MSR(r21)		# Save the exception MSR on the stack
	SAVE_4GPRS(8, r21)		# Save r8 through r11 on the stack
	SAVE_8GPRS(12, r21)		# Save r12 through r19 on the stack
	SAVE_8GPRS(24, r21)		# Save r24 through r31 on the stack
	andi.	r23,r23,MSR_PR		# Is this from user space?
	mfspr	r23,SPRN_SPRG3		# If from user, fix up THREAD.regs
	beq	2f			# No, it is from the kernel; branch.
	addi	r24,r1,STACK_FRAME_OVERHEAD
	stw	r24,PT_REGS(r23)	# 
2:	addi	r2,r23,-THREAD		# Set r2 to current thread
	tovirt(r2,r2)
	mflr	r23
	andi.	r24,r23,0x3f00		# Get vector offset
	stw	r24,TRAP(r21)
	li	r22,RESULT
	stwcx.	r22,r22,r21		# Clear the reservation
	li	r22,0
	stw	r22,RESULT(r21)
	mtspr	SPRN_SPRG2,r22		# r1 is now the kernel stack pointer
	addi	r24,r2,TASK_STRUCT_SIZE	# Check for kernel stack overflow
	cmplw	cr0,r1,r2
	cmplw	cr1,r1,r24
	crand	cr1,cr1,cr4
	bgt-	stack_ovf		# If r2 < r1 < r2 + TASK_STRUCT_SIZE
	lwz	r24,0(r23)		# Virtual address of the handler
	lwz	r23,4(r23)		# Handler return pointer
	cmpwi	cr0,r7,STND_EXC		# What type of exception is this?
	bne	3f			# It is a critical exception...

	## Standard exception jump path
	
	mtspr	SPRN_SRR0,r24		# Set up the instruction pointer
	mtspr	SPRN_SRR1,r20		# Set up the machine state register
	mtlr	r23			# Set up the return pointer
	SYNC
	rfi				# Enable the MMU, jump to the handler

	## Critical exception jump path

3:	mtspr	SPRN_SRR2,r24		# Set up the instruction pointer
	mtspr	SPRN_SRR3,r20		# Set up the machine state register
	mtlr	r23			# Set up the return pointer
	SYNC
	rfci				# Enable the MMU, jump to the handler

###
### On kernel stack overlow, load up an initial stack pointer and call
### StackOverflow(regs), which should NOT return.
### 

stack_ovf:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lis	r1,init_task_union@ha
	addi	r1,r1,init_task_union@l
	addi	r1,r1,TASK_UNION_SIZE - STACK_FRAME_OVERHEAD
	lis	r24,StackOverflow@ha
	addi	r24,r24,StackOverflow@l
	li	r20,MSR_KERNEL
	mtspr	SPRN_SRR0,r24		# Set up the instruction pointer
	mtspr	SPRN_SRR1,r20		# Set up the machine state register
	SYNC
	rfi				# Enable the MMU, jump to StackOverflow

###
### extern void giveup_altivec(struct task_struct *prev)
###
### The PowerPC 4xx family of processors do not have AltiVec capabilities, so
### this just returns.
###

_GLOBAL(giveup_altivec)
	blr
	
###
### extern void giveup_fpu(struct task_struct *prev)
###
### The PowerPC 4xx family of processors do not have an FPU, so this just
### returns.
###

_GLOBAL(giveup_fpu)
	blr

###
### extern void abort(void)
###
### At present, this routine just applies a system reset.
### 
	
_GLOBAL(abort)
	mfspr	r13,SPRN_DBCR
	oris	r13,r13,DBCR_RST(DBCR_RST_SYSTEM)@h
	mtspr	SPRN_DBCR,r13


### 
### This is where the main kernel code starts.
### 

start_here:
	## Establish a pointer to the current task
	
	lis	r2,init_task_union@h
	ori	r2,r2,init_task_union@l
	
	## Clear out the BSS as per ANSI C requirements

	lis	r7,_end@ha
	addi	r7,r7,_end@l
	lis	r8,__bss_start@ha
	addi	r8,r8,__bss_start@l
	subf	r7,r8,r7
	addi	r7,r7,3
	srwi.	r7,r7,2
	beq	2f
	addi	r8,r8,-4
	mtctr	r7
	li	r0,0
3:	stwu	r0,4(r8)
	bdnz	3b

	## Stack
	
2:	addi	r1,r2,TASK_UNION_SIZE
	li	r0,0
	stwu	r0,-STACK_FRAME_OVERHEAD(r1)

	## Determine what type of platform this is.

	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27
	bl	identify_machine
	
	## Initialize the memory management unit.

	bl	MMU_init

	## Go back to running unmapped so that we can change to our
	## exception vectors.

	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	mtspr	SPRN_SRR0,r4		# Set up the instruction pointer
	mtspr	SPRN_SRR1,r3		# Set up the machine state register
	rfi

	## Load up the kernel context

2:	SYNC				# Force all PTE updates to finish
#	tlbia				# Clear all TLB entries
#	sync				# Wait for tlbia to finish...

	## Set up for using our exception vectors
	
	tophys(r4,r2)			# Pointer to physical current thread
	addi	r4,r4,THREAD		# The init task thread
	mtspr	SPRN_SPRG3,r4		# Save it for exceptions later
	li	r3,0			# 
	mtspr	SPRN_SPRG2,r3		# 0 implies r1 has kernel stack pointer
	
	## Really turn on the MMU and jump into the kernel

        lis     r4,MSR_KERNEL@h
        ori     r4,r4,MSR_KERNEL@l
        lis     r3,start_kernel@h
        ori     r3,r3,start_kernel@l
        mtspr   SPRN_SRR0,r3		# Set up the instruction pointer
        mtspr   SPRN_SRR1,r4		# Set up the machine state register
        rfi				# Enable the MMU, jump to the kernel

_GLOBAL(set_context)
	mtspr	SPRN_PID,r3
	blr

###
### We put a few things here that have to be page-aligned. This stuff
### goes at the beginning of the data segment, which is page-aligned.
###

	.data
_GLOBAL(sdata)
_GLOBAL(empty_zero_page)
	.space	4096
_GLOBAL(swapper_pg_dir)
	.space	4096	

###
### This space gets a copy of optional info passed to us by the bootstrap
### which is used to pass parameters into the kernel like root=/dev/sda1, etc.
###

_GLOBAL(cmd_line)
	.space	512
