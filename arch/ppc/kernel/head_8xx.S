/*
 *  arch/ppc/kernel/except_8xx.S
 *
 *  $Id: head_8xx.S,v 1.4 1999/09/18 18:43:19 dmalek Exp $
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *  MPC8xx modifications by Dan Malek
 *    Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *
 *  This file contains low-level support and setup for PowerPC 8xx
 *  embedded processors, including trap and interrupt dispatch.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *	
 */

#include "ppc_asm.h"
#include <asm/processor.h>
#include <asm/page.h>
#include <linux/config.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/pgtable.h>
	
/* XXX need definitions here for 16 byte cachelines on some/all 8xx
   -- paulus */
CACHELINE_BYTES = 32
LG_CACHELINE_BYTES = 5
CACHELINE_MASK = 0x1f
CACHELINE_WORDS = 8

	.text
	.globl	_stext
_stext:

/*
 * _start is defined this way because the XCOFF loader in the OpenFirmware
 * on the powermac expects the entry point to be a procedure descriptor.
 */
	.text
	.globl	_start
_start:

/* MPC8xx
 * This port was done on an MBX board with an 860.  Right now I only
 * support an ELF compressed (zImage) boot from EPPC-Bug because the
 * code there loads up some registers before calling us:
 *   r3: ptr to board info data
 *   r4: initrd_start or if no initrd then 0
 *   r5: initrd_end - unused if r4 is 0
 *   r6: Start of command line string
 *   r7: End of command line string
 *
 * I decided to use conditional compilation instead of checking PVR and
 * adding more processor specific branches around code I don't need.
 * Since this is an embedded processor, I also appreciate any memory
 * savings I can get.
 *
 * The MPC8xx does not have any BATs, but it supports large page sizes.
 * We first initialize the MMU to support 8M byte pages, then load one
 * entry into each of the instruction and data TLBs to map the first
 * 8M 1:1.  I also mapped an additional I/O space 1:1 so we can get to
 * the "internal" processor registers before MMU_init is called.
 *
 * The TLB code currently contains a major hack.  Since I use the condition
 * code register, I have to save and restore it.  I am out of registers, so
 * I just store it in memory location 0 (the TLB handlers are not reentrant).
 * To avoid making any decisions, I need to use the "segment" valid bit
 * in the first level table, but that would require many changes to the
 * Linux page directory/table functions that I don't want to do right now.
 *
 * I used to use SPRG2 for a temporary register in the TLB handler, but it
 * has since been put to other uses.  I now use a hack to save a register
 * and the CCR at memory location 0.....Someday I'll fix this.....
 *	-- Dan
 */
	
	.globl	__start
__start:
	mr	r31,r3			/* save parameters */
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7
	li	r24,0			/* cpu # */

	tlbia			/* Invalidate all TLB entries */
	li	r8, 0
	mtspr	MI_CTR, r8	/* Set instruction control to zero */
	lis	r8, MD_RESETVAL@h
#ifndef CONFIG_8xx_COPYBACK
	oris	r8, r8, MD_WTDEF@h
#endif
	mtspr	MD_CTR, r8	/* Set data TLB control */

	/* Now map the lower 8 Meg into the TLBs.  For this quick hack,
	 * we can load the instruction and data TLB registers with the
	 * same values.
	 */
	lis	r8, KERNELBASE@h	/* Create vaddr for TLB */
	ori	r8, r8, MI_EVALID	/* Mark it valid */
	mtspr	MI_EPN, r8
	mtspr	MD_EPN, r8
	li	r8, MI_PS8MEG		/* Set 8M byte page */
	ori	r8, r8, MI_SVALID	/* Make it valid */
	mtspr	MI_TWC, r8
	mtspr	MD_TWC, r8
	li	r8, MI_BOOTINIT		/* Create RPN for address 0 */
	mtspr	MI_RPN, r8		/* Store TLB entry */
	mtspr	MD_RPN, r8
	lis	r8, MI_Kp@h		/* Set the protection mode */
	mtspr	MI_AP, r8
	mtspr	MD_AP, r8

	/* Map another 8 MByte at the IMMR to get the processor
	 * internal registers (among other things).
	 */
	mfspr	r9, 638			/* Get current IMMR */
	andis.	r9, r9, 0xff80		/* Get 8Mbyte boundary */

	mr	r8, r9			/* Create vaddr for TLB */
	ori	r8, r8, MD_EVALID	/* Mark it valid */
	mtspr	MD_EPN, r8
	li	r8, MD_PS8MEG		/* Set 8M byte page */
	ori	r8, r8, MD_SVALID	/* Make it valid */
	mtspr	MD_TWC, r8
	mr	r8, r9			/* Create paddr for TLB */
	ori	r8, r8, MI_BOOTINIT|0x2 /* Inhibit cache -- Cort */
	mtspr	MD_RPN, r8

	/* Since the cache is enabled according to the information we
	 * just loaded into the TLB, invalidate and enable the caches here.
	 * We should probably check/set other modes....later.
	 */
	lis	r8, IDC_INVALL@h
	mtspr	IC_CST, r8
	mtspr	DC_CST, r8
	lis	r8, IDC_ENABLE@h
	mtspr	IC_CST, r8
#ifdef CONFIG_8xx_COPYBACK
	mtspr	DC_CST, r8
#else
	/* For a debug option, I left this here to easily enable
	 * the write through cache mode
	 */
	lis	r8, DC_SFWT@h
	mtspr	DC_CST, r8
	lis	r8, IDC_ENABLE@h
	mtspr	DC_CST, r8
#endif

/* We now have the lower 8 Meg mapped into TLB entries, and the caches
 * ready to work.
 */

turn_on_mmu:
	mfmsr	r0
	ori	r0,r0,MSR_DR|MSR_IR
	mtspr	SRR1,r0
	lis	r0,start_here@h
	ori	r0,r0,start_here@l
	mtspr	SRR0,r0
	SYNC
	rfi				/* enables MMU */

/*
 * Exception entry code.  This code runs with address translation
 * turned off, i.e. using physical addresses.
 * We assume sprg3 has the physical address of the current
 * task's thread_struct.
 */
#define EXCEPTION_PROLOG	\
	mtspr	SPRG0,r20;	\
	mtspr	SPRG1,r21;	\
	mfcr	r20;		\
	mfspr	r21,SPRG2;		/* exception stack to use from */ \
	cmpwi	0,r21,0;		/* user mode or RTAS */ \
	bne	1f;		\
	tophys(r21,r1);			/* use tophys(kernel sp) otherwise */ \
	subi	r21,r21,INT_FRAME_SIZE;	/* alloc exc. frame */\
1:	stw	r20,_CCR(r21);		/* save registers */ \
	stw	r22,GPR22(r21);	\
	stw	r23,GPR23(r21);	\
	mfspr	r20,SPRG0;	\
	stw	r20,GPR20(r21);	\
	mfspr	r22,SPRG1;	\
	stw	r22,GPR21(r21);	\
	mflr	r20;		\
	stw	r20,_LINK(r21);	\
	mfctr	r22;		\
	stw	r22,_CTR(r21);	\
	mfspr	r20,XER;	\
	stw	r20,_XER(r21);	\
	mfspr	r22,SRR0;	\
	mfspr	r23,SRR1;	\
	stw	r0,GPR0(r21);	\
	stw	r1,GPR1(r21);	\
	stw	r2,GPR2(r21);	\
	stw	r1,0(r21);	\
	tovirt(r1,r21);			/* set new kernel sp */	\
	SAVE_4GPRS(3, r21);	\
	SAVE_GPR(7, r21);
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
	li	r20,MSR_KERNEL;			\
	bl	transfer_to_handler; 		\
	.long	hdlr;				\
	.long	ret_from_except

/* System reset */
#ifdef CONFIG_SMP /* MVME/MTX start the secondary here */
	STD_EXCEPTION(0x100, Reset, __secondary_start_psurge)
#else
	STD_EXCEPTION(0x100, Reset, UnknownException)
#endif	

/* Machine check */
	STD_EXCEPTION(0x200, MachineCheck, MachineCheckException)

/* Data access exception.
 * This is "never generated" by the MPC8xx.  We jump to it for other
 * translation errors.
 */
	. = 0x300
DataAccess:
	EXCEPTION_PROLOG
	mfspr	r20,DSISR
	stw	r20,_DSISR(r21)
	mr	r5,r20
	mfspr	r4,DAR
	stw	r4,_DAR(r21)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	do_page_fault
	.long	ret_from_except

/* Instruction access exception.
 * This is "never generated" by the MPC8xx.  We jump to it for other
 * translation errors.
 */
	. = 0x400
InstructionAccess:
	EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	mr	r4,r22
	mr	r5,r23
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	do_page_fault
	.long	ret_from_except

/* External interrupt */
	. = 0x500;
HardwareInterrupt:
	EXCEPTION_PROLOG;
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	li	r4,0
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
	stw	r4,_DAR(r21)
	mfspr	r5,DSISR
	stw	r5,_DSISR(r21)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	AlignmentException
	.long	ret_from_except

/* Program check exception */
	. = 0x700
ProgramCheck:
	EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	ProgramCheckException
	.long	ret_from_except

/* No FPU on MPC8xx.  This exception is not supposed to happen.
*/
	STD_EXCEPTION(0x800, FPUnavailable, UnknownException)

	. = 0x900
Decrementer:
	EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
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
	stw	r3,ORIG_GPR3(r21)
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	DoSyscall
	.long	ret_from_except

/* Single step - not used on 601 */
	STD_EXCEPTION(0xd00, SingleStep, SingleStepException)

	STD_EXCEPTION(0xe00, Trap_0e, UnknownException)
	STD_EXCEPTION(0xf00, Trap_0f, UnknownException)

/* On the MPC8xx, this is a software emulation interrupt.  It occurs
 * for all unimplemented and illegal instructions.
 */
	STD_EXCEPTION(0x1000, SoftEmu, SoftwareEmulation)

	. = 0x1100
/*
 * For the MPC8xx, this is a software tablewalk to load the instruction
 * TLB.  It is modelled after the example in the Motorola manual.  The task
 * switch loads the M_TWB register with the pointer to the first level table.
 * If we discover there is no second level table (the value is zero), the
 * plan was to load that into the TLB, which causes another fault into the
 * TLB Error interrupt where we can handle such problems.  However, that did
 * not work, so if we discover there is no second level table, we restore
 * registers and branch to the error exception.  We have to use the MD_xxx
 * registers for the tablewalk because the equivalent MI_xxx registers
 * only perform the attribute functions.
 */
InstructionTLBMiss:
#ifdef CONFIG_8xx_CPU6
	stw	r3, 8(r0)
	li	r3, 0x3f80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	M_TW, r20	/* Save a couple of working registers */
	mfcr	r20
	stw	r20, 0(r0)
	stw	r21, 4(r0)
	mfspr	r20, SRR0	/* Get effective address of fault */
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x3780
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MD_EPN, r20	/* Have to use MD_EPN for walk, MI_EPN can't */
	mfspr	r20, M_TWB	/* Get level 1 table entry address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andi.	r21, r20, 0x0800	/* Address >= 0x80000000 */
	beq	3f
	lis	r21, swapper_pg_dir@h
	ori	r21, r21, swapper_pg_dir@l
	rlwimi	r20, r21, 0, 2, 19
3:
	lwz	r21, 0(r20)	/* Get the level 1 entry */
	rlwinm.	r20, r21,0,0,19	/* Extract page descriptor page address */
	beq	2f		/* If zero, don't try to find a pte */

	/* We have a pte table, so load the MI_TWC with the attributes
	 * for this page, which has only bit 31 set.
	 */
	tophys(r21,r21)
	ori	r21,r21,1		/* Set valid bit */
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x2b80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MI_TWC, r21	/* Set page attributes */
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x3b80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MD_TWC, r21	/* Load pte table base address */
	mfspr	r21, MD_TWC	/* ....and get the pte address */
	lwz	r20, 0(r21)	/* Get the pte */
#if 0
	ori	r20, r20, _PAGE_ACCESSED
	stw	r20, 0(r21)
#endif

	/* Set four subpage valid bits (24, 25, 26, and 27).
	 * Clear bit 28 (which should be in the PTE, but we do this anyway).
	 */
	li	r21, 0x00f0
	rlwimi	r20, r21, 0, 24, 28
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x2d80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MI_RPN, r20	/* Update TLB entry */

	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
#ifdef CONFIG_8xx_CPU6
	lwz	r3, 8(r0)
#endif
	rfi

2:	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
#ifdef CONFIG_8xx_CPU6
	lwz	r3, 8(r0)
#endif
	b	InstructionAccess

	. = 0x1200
DataStoreTLBMiss:
#ifdef CONFIG_8xx_CPU6
	stw	r3, 8(r0)
	li	r3, 0x3f80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	M_TW, r20	/* Save a couple of working registers */
	mfcr	r20
	stw	r20, 0(r0)
	stw	r21, 4(r0)
	mfspr	r20, M_TWB	/* Get level 1 table entry address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andi.	r21, r20, 0x0800
	beq	3f
	lis	r21, swapper_pg_dir@h
	ori	r21, r21, swapper_pg_dir@l
	rlwimi r20, r21, 0, 2, 19
3:
	lwz	r21, 0(r20)	/* Get the level 1 entry */
	rlwinm.	r20, r21,0,0,19	/* Extract page descriptor page address */
	beq	2f		/* If zero, don't try to find a pte */

	/* We have a pte table, so load fetch the pte from the table.
	 */
	tophys(r21, r21)
	ori	r21, r21, 1	/* Set valid bit in physical L2 page */
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x3b80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MD_TWC, r21	/* Load pte table base address */
	mfspr	r20, MD_TWC	/* ....and get the pte address */
	lwz	r20, 0(r20)	/* Get the pte */

	/* Insert the Guarded flag into the TWC from the Linux PTE.
	 * It is bit 27 of both the Linux PTE and the TWC (at least
	 * I got that right :-).  It will be better when we can put
	 * this into the Linux pgd/pmd and load it in the operation
	 * above.
	 */
	rlwimi	r21, r20, 0, 27, 27
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x3b80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MD_TWC, r21

	/* Set four subpage valid bits (24, 25, 26, and 27).
	 * Clear bit 28 (which should be in the PTE, but we do this anyway).
	 */
#if 0
	ori	r20, r20, 0x00f0
#else
	li	r21, 0x00f0
	rlwimi	r20, r21, 0, 24, 28
#endif
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x3d80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MD_RPN, r20	/* Update TLB entry */

	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
#ifdef CONFIG_8xx_CPU6
	lwz	r3, 8(r0)
#endif
	rfi

2:	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
#ifdef CONFIG_8xx_CPU6
	lwz	r3, 8(r0)
#endif
	b	DataAccess

/* This is an instruction TLB error on the MPC8xx.  This could be due
 * to many reasons, such as executing guarded memory or illegal instruction
 * addresses.  There is nothing to do but handle a big time error fault.
 */
	. = 0x1300
InstructionTLBError:
	b	InstructionAccess

/* This is the data TLB error on the MPC8xx.  This could be due to
 * many reasons, including a dirty update to a pte.  We can catch that
 * one here, but anything else is an error.  First, we track down the
 * Linux pte.  If it is valid, write access is allowed, but the
 * page dirty bit is not set, we will set it and reload the TLB.  For
 * any other case, we bail out to a higher level function that can
 * handle it.
 */
	. = 0x1400
DataTLBError:
#ifdef CONFIG_8xx_CPU6
	stw	r3, 8(r0)
	li	r3, 0x3f80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	M_TW, r20	/* Save a couple of working registers */
	mfcr	r20
	stw	r20, 0(r0)
	stw	r21, 4(r0)

	/* First, make sure this was a store operation.
	*/
	mfspr	r20, DSISR
	andis.	r21, r20, 0x0200	/* If set, indicates store op */
	beq	2f

	mfspr	r20, M_TWB	/* Get level 1 table entry address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andi.	r21, r20, 0x0800
	beq	3f
	lis	r21, swapper_pg_dir@h
	ori	r21, r21, swapper_pg_dir@l
	rlwimi	r20, r21, 0, 2, 19
3:
	lwz	r21, 0(r20)	/* Get the level 1 entry */
	rlwinm.	r20, r21,0,0,19	/* Extract page descriptor page address */
	beq	2f		/* If zero, bail */

	/* We have a pte table, so fetch the pte from the table.
	 */
	tophys(r21, r21)
	ori	r21, r21, 1		/* Set valid bit in physical L2 page */
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x3b80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MD_TWC, r21		/* Load pte table base address */
	mfspr	r21, MD_TWC		/* ....and get the pte address */
	lwz	r20, 0(r21)		/* Get the pte */

	andi.	r21, r20, _PAGE_RW	/* Is it writeable? */
	beq	2f			/* Bail out if not */

	/* Update 'changed', among others.
	*/
	ori	r20, r20, _PAGE_DIRTY|_PAGE_HWWRITE|_PAGE_ACCESSED
	mfspr	r21, MD_TWC		/* Get pte address again */
	stw	r20, 0(r21)		/* and update pte in table */

	/* Set four subpage valid bits (24, 25, 26, and 27).
	 * Clear bit 28 (which should be in the PTE, but we do this anyway).
	 */
	li	r21, 0x00f0
	rlwimi	r20, r21, 0, 24, 28
#ifdef CONFIG_8xx_CPU6
	li	r3, 0x3d80
	stw	r3, 12(r0)
	lwz	r3, 12(r0)
#endif
	mtspr	MD_RPN, r20	/* Update TLB entry */

	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
#ifdef CONFIG_8xx_CPU6
	lwz	r3, 8(r0)
#endif
	rfi
2:
	mfspr	r20, M_TW	/* Restore registers */
	lwz	r21, 0(r0)
	mtcr	r21
	lwz	r21, 4(r0)
#ifdef CONFIG_8xx_CPU6
	lwz	r3, 8(r0)
#endif
	b	DataAccess

	STD_EXCEPTION(0x1500, Trap_15, UnknownException)
	STD_EXCEPTION(0x1600, Trap_16, UnknownException)
	STD_EXCEPTION(0x1700, Trap_17, TAUException)
	STD_EXCEPTION(0x1800, Trap_18, UnknownException)
	STD_EXCEPTION(0x1900, Trap_19, UnknownException)
	STD_EXCEPTION(0x1a00, Trap_1a, UnknownException)
	STD_EXCEPTION(0x1b00, Trap_1b, UnknownException)

/* On the MPC8xx, these next four traps are used for development
 * support of breakpoints and such.  Someday I will get around to
 * using them.
 */
	STD_EXCEPTION(0x1c00, Trap_1c, UnknownException)
	STD_EXCEPTION(0x1d00, Trap_1d, UnknownException)
	STD_EXCEPTION(0x1e00, Trap_1e, UnknownException)
	STD_EXCEPTION(0x1f00, Trap_1f, UnknownException)

	. = 0x2000

/*
 * This code finishes saving the registers to the exception frame
 * and jumps to the appropriate handler for the exception, turning
 * on address translation.
 */
	.globl	transfer_to_handler
transfer_to_handler:
	stw	r22,_NIP(r21)
	lis	r22,MSR_POW@h
	andc	r23,r23,r22
	stw	r23,_MSR(r21)
	SAVE_4GPRS(8, r21)
	SAVE_8GPRS(12, r21)
	SAVE_8GPRS(24, r21)
	andi.	r23,r23,MSR_PR
	mfspr	r23,SPRG3		/* if from user, fix up THREAD.regs */
	beq	2f
	addi	r24,r1,STACK_FRAME_OVERHEAD
	stw	r24,PT_REGS(r23)
2:	addi	r2,r23,-THREAD		/* set r2 to current */
	tovirt(r2,r2)
	mflr	r23
	andi.	r24,r23,0x3f00		/* get vector offset */
	stw	r24,TRAP(r21)
	li	r22,RESULT
	stwcx.	r22,r22,r21		/* to clear the reservation */
	li	r22,0
	stw	r22,RESULT(r21)
	mtspr	SPRG2,r22		/* r1 is now kernel sp */
	addi	r24,r2,TASK_STRUCT_SIZE	/* check for kernel stack overflow */
	cmplw	0,r1,r2
	cmplw	1,r1,r24
	crand	1,1,4
	bgt-	stack_ovf		/* if r2 < r1 < r2+TASK_STRUCT_SIZE */
	lwz	r24,0(r23)		/* virtual address of handler */
	lwz	r23,4(r23)		/* where to go when done */
	mtspr	SRR0,r24
	mtspr	SRR1,r20
	mtlr	r23
	SYNC
	rfi				/* jump to handler, enable MMU */

/*
 * On kernel stack overflow, load up an initial stack pointer
 * and call StackOverflow(regs), which should not return.
 */
stack_ovf:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lis	r1,init_task_union@ha
	addi	r1,r1,init_task_union@l
	addi	r1,r1,TASK_UNION_SIZE-STACK_FRAME_OVERHEAD
	lis	r24,StackOverflow@ha
	addi	r24,r24,StackOverflow@l
	li	r20,MSR_KERNEL
	mtspr	SRR0,r24
	mtspr	SRR1,r20
	SYNC
	rfi

	.globl	giveup_fpu
giveup_fpu:
	blr

/*
 * This code is jumped to from the startup code to copy
 * the kernel image to physical address 0.
 */
relocate_kernel:
	lis	r9,0x426f		/* if booted from BootX, don't */
	addi	r9,r9,0x6f58		/* translate source addr */
	cmpw	r31,r9			/* (we have to on chrp) */
	beq	7f
	rlwinm	r4,r4,0,8,31		/* translate source address */
	add	r4,r4,r3		/* to region mapped with BATs */
7:	addis	r9,r26,klimit@ha	/* fetch klimit */
	lwz	r25,klimit@l(r9)
	addis	r25,r25,-KERNELBASE@h
	li	r6,0			/* Destination offset */
	li	r5,0x4000		/* # bytes of memory to copy */
	bl	copy_and_flush		/* copy the first 0x4000 bytes */
	addi	r0,r3,4f@l		/* jump to the address of 4f */
	mtctr	r0			/* in copy and do the rest. */
	bctr				/* jump to the copy */
4:	mr	r5,r25
	bl	copy_and_flush		/* copy the rest */
	b	turn_on_mmu

/*
 * Copy routine used to copy the kernel to start at physical address 0
 * and flush and invalidate the caches as needed.
 * r3 = dest addr, r4 = source addr, r5 = copy limit, r6 = start offset
 * on exit, r3, r4, r5 are unchanged, r6 is updated to be >= r5.
 */
copy_and_flush:
	addi	r5,r5,-4
	addi	r6,r6,-4
4:	li	r0,CACHELINE_WORDS
	mtctr	r0
3:	addi	r6,r6,4			/* copy a cache line */
	lwzx	r0,r6,r4
	stwx	r0,r6,r3
	bdnz	3b
	dcbst	r6,r3			/* write it to memory */
	sync
	icbi	r6,r3			/* flush the icache line */
	cmplw	0,r6,r5
	blt	4b
	isync
	addi	r5,r5,4
	addi	r6,r6,4
	blr

#ifdef CONFIG_SMP
	.globl	__secondary_start_psurge
__secondary_start_psurge:
	li	r24,1			/* cpu # */
	b	__secondary_start

	.globl	__secondary_hold
__secondary_hold:
	/* tell the master we're here */
	lis	r5,0x4@h
	ori	r5,r5,0x4@l
	stw	r3,0(r5)
	dcbf	0,r5
100:
	lis	r5,0
	dcbi	0,r5
	lwz	r4,0(r5)
	/* wait until we're told to start */
	cmp	0,r4,r3
	bne	100b
	/* our cpu # was at addr 0 - go */
	lis	r5,__secondary_start@h
	ori	r5,r5,__secondary_start@l
	tophys(r5,r5)
	mtlr	r5
	mr	r24,r3			/* cpu # */
	blr
#endif /* CONFIG_SMP */
	
/*
 * This is where the main kernel code starts.
 */
start_here:
#ifdef CONFIG_SMP
	/* if we're the second cpu stack and r2 are different
	* and we want to not clear the bss -- Cort */
	lis	r5,first_cpu_booted@h
	ori	r5,r5,first_cpu_booted@l
	lwz	r5,0(r5)
	cmpi	0,r5,0
	beq	99f

	/* get current */
	lis	r2,current_set@h
	ori	r2,r2,current_set@l
	slwi	r24,r24,2			/* cpu # to current_set[cpu#] */
	add	r2,r2,r24
	lwz	r2,0(r2)
	b	10f
99:	
#endif /* CONFIG_SMP */
	/* ptr to current */
	lis	r2,init_task_union@h
	ori	r2,r2,init_task_union@l
	/* Clear out the BSS */
	lis	r11,_end@ha
	addi	r11,r11,_end@l
	lis	r8,__bss_start@ha
	addi	r8,r8,__bss_start@l
	subf	r11,r8,r11
	addi	r11,r11,3
	rlwinm.	r11,r11,30,2,31
	beq	2f
	addi	r8,r8,-4
	mtctr	r11
	li	r0,0
3:	stwu	r0,4(r8)
	bdnz	3b
2:
#ifdef CONFIG_SMP	
10:
#endif /* CONFIG_SMP */
	/* stack */
	addi	r1,r2,TASK_UNION_SIZE
	li	r0,0
	stwu	r0,-STACK_FRAME_OVERHEAD(r1)
/*
 * Decide what sort of machine this is and initialize the MMU.
 */
	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27
	bl	identify_machine
	bl	MMU_init

/*
 * Go back to running unmapped so we can load up new values
 * for SDR1 (hash table pointer) and the segment registers
 * and change to using our exception vectors.
 * On the 8xx, all we have to do is invalidate the TLB to clear
 * the old 8M byte TLB mappings and load the page table base register.
 */
	/* The right way to do this would be to track it down through
	 * init's THREAD like the context switch code does, but this is
	 * easier......until someone changes init's static structures.
	 */
	lis	r6, swapper_pg_dir@h
	tophys(r6,r6)
	ori	r6, r6, swapper_pg_dir@l
#ifdef CONFIG_8xx_CPU6
	lis	r4, cpu6_errata_word@h
	ori	r4, r4, cpu6_errata_word@l
	li	r3, 0x3980
	stw	r3, 12(r4)
	lwz	r3, 12(r4)
#endif
	mtspr	M_TWB, r6
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	rfi
/* Load up the kernel context */
2:
	SYNC			/* Force all PTE updates to finish */
	tlbia			/* Clear all TLB entries */
	sync			/* wait for tlbia/tlbie to finish */
#ifdef CONFIG_SMP
	tlbsync			/* ... on all CPUs */
	sync
#endif
/* Set up for using our exception vectors */
	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* init task's THREAD */
	mtspr	SPRG3,r4
	li	r3,0
	mtspr	SPRG2,r3	/* 0 => r1 has kernel sp */
/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
#ifdef CONFIG_SMP
	/* the second time through here we go to
	 * start_secondary(). -- Cort
	 */
	lis	r5,first_cpu_booted@h
	ori	r5,r5,first_cpu_booted@l
	tophys(r5,r5)
	lwz	r5,0(r5)
	cmpi	0,r5,0
	beq	10f
	lis	r3,start_secondary@h
	ori	r3,r3,start_secondary@l
10:	
#endif /* CONFIG_SMP */
	mtspr	SRR0,r3
	mtspr	SRR1,r4
	rfi			/* enable MMU and jump to start_kernel */

/*
 * Set up to use a given MMU context.
 *
 * The MPC8xx has something that currently happens "automagically."
 * Unshared user space address translations are subject to ASID (context)
 * match.  During each task switch, the ASID is incremented.  We can
 * guarantee (I hope :-) that no entries currently match this ASID
 * because every task will cause at least a TLB entry to be loaded for
 * the first instruction and data access, plus the kernel running will
 * have displaced several more TLBs.  The MMU contains 32 entries for
 * each TLB, and there are 16 contexts, so we just need to make sure
 * two pages get replaced for every context switch, which currently
 * happens.  There are other TLB management techniques that I will
 * eventually implement, but this is the easiest for now.  -- Dan
 *
 * On the MPC8xx, we place the physical address of the new task
 * page directory loaded into the MMU base register, and set the
 * ASID compare register with the new "context".
 */
_GLOBAL(set_context)
#ifdef CONFIG_8xx_CPU6
	lis	r6, cpu6_errata_word@h
	ori	r6, r6, cpu6_errata_word@l
	tophys	(r4, r4)
	li	r7, 0x3980
	stw	r7, 12(r6)
	lwz	r7, 12(r6)
        mtspr   M_TWB, r4               /* Update MMU base address */
	li	r7, 0x3380
	stw	r7, 12(r6)
	lwz	r7, 12(r6)
        mtspr   M_CASID, r3             /* Update context */
#else
        mtspr   M_CASID,r3		/* Update context */
	tophys	(r4, r4)
	mtspr	M_TWB, r4		/* and pgd */
#endif
        tlbia
	SYNC
	blr

/* Jump into the system reset for the rom.
 * We first disable the MMU, and then jump to the ROM reset address.
 *
 * r3 is the board info structure, r4 is the location for starting.
 * I use this for building a small kernel that can load other kernels,
 * rather than trying to write or rely on a rom monitor that can tftp load.
 */
       .globl  m8xx_gorom
m8xx_gorom:
       li      r5,MSR_KERNEL & ~(MSR_IR|MSR_DR)
       lis     r6,2f@h
       addis   r6,r6,-KERNELBASE@h
       ori     r6,r6,2f@l
       mtspr   SRR0,r6
       mtspr   SRR1,r5
       rfi
2:
       mtlr    r4
       blr

#ifdef CONFIG_8xx_CPU6
/* It's here because it is unique to the 8xx.
 * It is important we get called with interrupts disabled.  I used to
 * do that, but it appears that all code that calls this already had
 * interrupt disabled.
 */
	.globl	set_dec_cpu6
set_dec_cpu6:
	lis	r7, cpu6_errata_word@h
	ori	r7, r7, cpu6_errata_word@l
	li	r4, 0x2c00
	stw	r4, 8(r7)
	lwz	r4, 8(r7)
        mtspr   22, r3		/* Update Decrementer */
	SYNC
	blr
#endif
	
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

#ifdef CONFIG_8xx_CPU6
	.globl	cpu6_errata_word
cpu6_errata_word:
	.space	16
#endif

