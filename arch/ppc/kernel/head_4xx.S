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
 *    Copyright 2000 MontaVista Software Inc.
 *	PPC405 modifications
 *      PowerPC 403GCX/405GP modifications.
 * 	Author: MontaVista Software, Inc.
 *         	frank_rowand@mvista.com or source@mvista.com
 * 	   	debbie_chu@mvista.com
 *
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
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/ibm4xx.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>

/* As with the other PowerPC ports, it is expected that when code
 * execution begins here, the following registers contain valid, yet
 * optional, information:
 *
 *   r3 - Board info structure pointer (DRAM, frequency, MAC address, etc.)
 *   r4 - Starting address of the init RAM disk
 *   r5 - Ending address of the init RAM disk
 *   r6 - Start of kernel command line string (e.g. "mem=96m")
 *   r7 - End of kernel command line string
 *
 * This is all going to change RSN when we add bi_recs.......  -- Dan
 */ 
	.text
_GLOBAL(_stext)
_GLOBAL(_start)

	/* Save parameters we are passed.
	*/
	mr	r31,r3
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7

	/* We have to turn on the MMU right away so we get cache modes
	 * set correctly.
	 */
	bl	initial_mmu

/* We now have the lower 16 Meg mapped into TLB entries, and the caches
 * ready to work.
 */
turn_on_mmu:
	li	r0,MSR_KERNEL
	mtspr	SRR1,r0
	lis	r0,start_here@h
	ori	r0,r0,start_here@l
	mtspr	SRR0,r0
	SYNC
	rfi				/* enables MMU */
	b	.			/* prevent prefetch past rfi */

/*
 * This area is used for temporarily saving registers during the
 * critical exception prolog.
 */
	. = 0xc0
crit_save:
_GLOBAL(crit_r10)
	.space	4
_GLOBAL(crit_r11)
	.space	4
_GLOBAL(crit_sprg0)
	.space	4
_GLOBAL(crit_sprg1)
	.space	4
_GLOBAL(crit_sprg4)
	.space	4
_GLOBAL(crit_sprg5)
	.space	4
_GLOBAL(crit_sprg6)
	.space	4
_GLOBAL(crit_sprg7)
	.space	4
_GLOBAL(crit_pid)
	.space	4
_GLOBAL(crit_srr0)
	.space	4
_GLOBAL(crit_srr1)
	.space	4

/*
 * Exception vector entry code. This code runs with address translation
 * turned off (i.e. using physical addresses). We assume SPRG3 has the
 * physical address of the current task thread_struct.
 * Note that we have to have decremented r1 before we write to any fields
 * of the exception frame, since a critical interrupt could occur at any
 * time, and it will write to the area immediately below the current r1.
 */ 
#define NORMAL_EXCEPTION_PROLOG						     \
	mtspr	SPRN_SPRG0,r10;		/* save two registers to work with */\
	mtspr	SPRN_SPRG1,r11;						     \
	mtspr	SPRN_SPRG2,r1;						     \
	mfcr	r10;			/* save CR in r10 for now	   */\
	mfspr	r11,SPRN_SRR1;		/* check whether user or kernel    */\
	andi.	r11,r11,MSR_PR;						     \
	beq	1f;							     \
	mfspr	r1,SPRG3;		/* if from user, start at top of   */\
	lwz	r1,THREAD_INFO-THREAD(r1); /* this thread's kernel stack   */\
	addi	r1,r1,THREAD_SIZE;					     \
1:	subi	r1,r1,INT_FRAME_SIZE;	/* Allocate an exception frame     */\
	tophys(r11,r1);							     \
	stw	r10,_CCR(r11);          /* save various registers	   */\
	stw	r12,GPR12(r11);						     \
	stw	r9,GPR9(r11);						     \
	mfspr	r10,SPRG0;						     \
	stw	r10,GPR10(r11);						     \
	mfspr	r12,SPRG1;						     \
	stw	r12,GPR11(r11);						     \
	mflr	r10;							     \
	stw	r10,_LINK(r11);						     \
	mfspr	r10,SPRG2;						     \
	mfspr	r12,SRR0;						     \
	stw	r10,GPR1(r11);						     \
	mfspr	r9,SRR1;						     \
	stw	r10,0(r11);						     \
	rlwinm	r9,r9,0,14,12;		/* clear MSR_WE (necessary?)	   */\
	stw	r0,GPR0(r11);						     \
	SAVE_4GPRS(3, r11);						     \
	SAVE_2GPRS(7, r11)

/*
 * Exception prolog for critical exceptions.  This is a little different
 * from the normal exception prolog above since a critical exception
 * can potentially occur at any point during normal exception processing.
 * Thus we cannot use the same SPRG registers as the normal prolog above.
 * Instead we use a couple of words of memory at low physical addresses.
 * This is OK since we don't support SMP on these processors.
 */
#define CRITICAL_EXCEPTION_PROLOG					     \
	stw	r10,crit_r10@l(0);	/* save two registers to work with */\
	stw	r11,crit_r11@l(0);					     \
	mfspr	r10,SPRG0;						     \
	stw	r10,crit_sprg0@l(0);					     \
	mfspr	r10,SPRG1;						     \
	stw	r10,crit_sprg1@l(0);					     \
	mfspr	r10,SPRG4;						     \
	stw	r10,crit_sprg4@l(0);					     \
	mfspr	r10,SPRG5;						     \
	stw	r10,crit_sprg5@l(0);					     \
	mfspr	r10,SPRG6;						     \
	stw	r10,crit_sprg6@l(0);					     \
	mfspr	r10,SPRG7;						     \
	stw	r10,crit_sprg7@l(0);					     \
	mfspr	r10,SPRN_PID;						     \
	stw	r10,crit_pid@l(0);					     \
	mfspr	r10,SRR0;						     \
	stw	r10,crit_srr0@l(0);					     \
	mfspr	r10,SRR1;						     \
	stw	r10,crit_srr1@l(0);					     \
	mfcr	r10;			/* save CR in r10 for now	   */\
	mfspr	r11,SPRN_SRR3;		/* check whether user or kernel    */\
	andi.	r11,r11,MSR_PR;						     \
	lis	r11,critical_stack_top@h;				     \
	ori	r11,r11,critical_stack_top@l;				     \
	beq	1f;							     \
	/* COMING FROM USER MODE */					     \
	mfspr	r11,SPRG3;		/* if from user, start at top of   */\
	lwz	r11,THREAD_INFO-THREAD(r11); /* this thread's kernel stack */\
	addi	r11,r11,THREAD_SIZE;					     \
1:	subi	r11,r11,INT_FRAME_SIZE;	/* Allocate an exception frame     */\
	tophys(r11,r11);						     \
	stw	r10,_CCR(r11);          /* save various registers	   */\
	stw	r12,GPR12(r11);						     \
	stw	r9,GPR9(r11);						     \
	mflr	r10;							     \
	stw	r10,_LINK(r11);						     \
	mfspr	r12,SPRN_DEAR;		/* save DEAR and ESR in the frame  */\
	stw	r12,_DEAR(r11);		/* since they may have had stuff   */\
	mfspr	r9,SPRN_ESR;		/* in them at the point where the  */\
	stw	r9,_ESR(r11);		/* exception was taken		   */\
	mfspr	r12,SRR2;						     \
	stw	r1,GPR1(r11);						     \
	mfspr	r9,SRR3;						     \
	stw	r1,0(r11);						     \
	tovirt(r1,r11);							     \
	rlwinm	r9,r9,0,14,12;		/* clear MSR_WE (necessary?)	   */\
	stw	r0,GPR0(r11);						     \
	SAVE_4GPRS(3, r11);						     \
	SAVE_2GPRS(7, r11)

#if 0
#define CRITICAL_EXCEPTION_PROLOG					     \
	stw	r10,crit_save@l(0);	/* save two registers to work with */\
	stw	r11,4+crit_save@l(0);					     \
	mfcr	r10;			/* save CR in r10 for now	   */\
	mfspr	r11,SPRN_SRR3;		/* check whether user or kernel    */\
	andi.	r11,r11,MSR_PR;						     \
	mr	r11,r1;							     \
	beq	1f;							     \
	mfspr	r11,SPRG3;		/* if from user, start at top of   */\
	lwz	r11,THREAD_INFO-THREAD(r11); /* this thread's kernel stack */\
	addi	r11,r11,THREAD_SIZE;					     \
1:	subi	r11,r11,INT_FRAME_SIZE;	/* Allocate an exception frame     */\
	tophys(r11,r11);						     \
	stw	r10,_CCR(r11);          /* save various registers	   */\
	stw	r12,GPR12(r11);						     \
	stw	r9,GPR9(r11);						     \
	lwz	r10,crit_save@l(0);					     \
	stw	r10,GPR10(r11);						     \
	lwz	r12,4+crit_save@l(0);					     \
	stw	r12,GPR11(r11);						     \
	mflr	r10;							     \
	stw	r10,_LINK(r11);						     \
	mfspr	r12,SRR0;		/* save SRR0 and SRR1 in the frame */\
	stw	r12,_SRR0(r11);		/* since they may have had stuff   */\
	mfspr	r9,SRR1;		/* in them at the point where the  */\
	stw	r9,_SRR1(r11);		/* exception was taken		   */\
	mfspr	r12,SRR2;						     \
	stw	r1,GPR1(r11);						     \
	mfspr	r9,SRR3;						     \
	stw	r1,0(r11);						     \
	rlwinm	r9,r9,0,14,12;		/* clear MSR_WE (necessary?)	   */\
	stw	r0,GPR0(r11);						     \
	SAVE_4GPRS(3, r11);						     \
	SAVE_2GPRS(7, r11)
#endif

/*
 * Exception vectors.
 */
#define	START_EXCEPTION(n, label)					     \
	. = n;								     \
label:

#define FINISH_EXCEPTION(func)					\
	bl	transfer_to_handler_full;			\
	.long	func;						\
	.long	ret_from_except_full


#define EXCEPTION(n, label, hdlr, xfer)				\
	START_EXCEPTION(n, label);				\
	NORMAL_EXCEPTION_PROLOG;				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	xfer(n, hdlr)

#define CRITICAL_EXCEPTION(n, label, hdlr)			\
	START_EXCEPTION(n, label);				\
	CRITICAL_EXCEPTION_PROLOG;				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	EXC_XFER_TEMPLATE(hdlr, n+2, (MSR_KERNEL & ~(MSR_ME|MSR_DE|MSR_CE)), \
			  NOCOPY, transfer_to_handler_full, \
			  ret_from_except_full)

#define EXC_XFER_TEMPLATE(hdlr, trap, msr, copyee, tfer, ret)	\
	li	r10,trap;					\
	stw	r10,TRAP(r11);					\
	li	r10,msr;					\
	copyee(r10, r9);					\
	bl	tfer;		 				\
	.long	hdlr;						\
	.long	ret

#define COPY_EE(d, s)		rlwimi d,s,0,16,16
#define NOCOPY(d, s)

#define EXC_XFER_STD(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n, MSR_KERNEL, NOCOPY, transfer_to_handler_full, \
			  ret_from_except_full)

#define EXC_XFER_LITE(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n+1, MSR_KERNEL, NOCOPY, transfer_to_handler, \
			  ret_from_except)

#define EXC_XFER_EE(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n, MSR_KERNEL, COPY_EE, transfer_to_handler_full, \
			  ret_from_except_full)

#define EXC_XFER_EE_LITE(n, hdlr)	\
	EXC_XFER_TEMPLATE(hdlr, n+1, MSR_KERNEL, COPY_EE, transfer_to_handler, \
			  ret_from_except)


/*
 * 0x0100 - Critical Interrupt Exception
 */
	CRITICAL_EXCEPTION(0x0100, CriticalInterrupt, UnknownException)

/*
 * 0x0200 - Machine Check Exception
 */
	CRITICAL_EXCEPTION(0x0200, MachineCheck, MachineCheckException)

/*
 * 0x0300 - Data Storage Exception
 * This happens for just a few reasons.  U0 set (but we don't do that),
 * or zone protection fault (user violation, write to protected page).
 * If this is just an update of modified status, we do that quickly
 * and exit.  Otherwise, we call heavywight functions to do the work.
 */
	START_EXCEPTION(0x0300,	DataStorage)
	mtspr	SPRG0, r10		/* Save some working registers */
	mtspr	SPRG1, r11
#ifdef CONFIG_403GCX
	stw     r12, 0(r0)
	stw     r9, 4(r0)
	mfcr    r11
	mfspr   r12, SPRN_PID
	stw     r11, 8(r0)
	stw     r12, 12(r0)
#else
	mtspr	SPRG4, r12
	mtspr	SPRG5, r9
	mfcr	r11
	mfspr	r12, SPRN_PID
	mtspr	SPRG7, r11
	mtspr	SPRG6, r12
#endif

	/* First, check if it was a zone fault (which means a user
	* tried to access a kernel or read-protected page - always
	* a SEGV).  All other faults here must be stores, so no
	* need to check ESR_DST as well. */
	mfspr	r10, SPRN_ESR
	andis.	r10, r10, ESR_DIZ@h
	bne	2f

	mfspr	r10, SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andis.	r11, r10, 0x8000
	beq	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l
	li	r9, 0
	mtspr	SPRN_PID, r9		/* TLB will have 0 TID */
	b	4f

	/* Get the PGD for the current thread.
	 */
3:
	mfspr	r11,SPRG3
	lwz	r11,PGDIR(r11)
4:
	tophys(r11, r11)
	rlwimi	r11, r10, 12, 20, 29	/* Create L1 (pgdir/pmd) address */
	lwz	r11, 0(r11)		/* Get L1 entry */
	rlwinm.	r12, r11, 0, 0, 19	/* Extract L2 (pte) base address */
	beq	2f			/* Bail if no table */

	rlwimi	r12, r10, 22, 20, 29	/* Compute PTE address */
	lwz	r11, 0(r12)		/* Get Linux PTE */

	andi.	r9, r11, _PAGE_RW	/* Is it writeable? */
	beq	2f			/* Bail if not */

	/* Update 'changed'.
	*/
	ori	r11, r11, _PAGE_DIRTY|_PAGE_ACCESSED|_PAGE_HWWRITE
	stw	r11, 0(r12)		/* Update Linux page table */

	/* Most of the Linux PTE is ready to load into the TLB LO.
	 * We set ZSEL, where only the LS-bit determines user access.
	 * We set execute, because we don't have the granularity to
	 * properly set this at the page level (Linux problem).
	 * If shared is set, we cause a zero PID->TID load.
	 * Many of these bits are software only.  Bits we don't set
	 * here we (properly should) assume have the appropriate value.
	 */
	li	r12, 0x0ce2
	andc	r11, r11, r12		/* Make sure 20, 21 are zero */

	/* find the TLB index that caused the fault.  It has to be here.
	*/
	tlbsx	r9, 0, r10

	tlbwe	r11, r9, TLB_DATA		/* Load TLB LO */

	/* Done...restore registers and get out of here.
	*/
#ifdef CONFIG_403GCX
	lwz     r12, 12(r0)
	lwz     r11, 8(r0)
	mtspr   SPRN_PID, r12
	mtcr    r11
	lwz     r9, 4(r0)
	lwz     r12, 0(r0)
#else
	mfspr	r12, SPRG6
	mfspr	r11, SPRG7
	mtspr	SPRN_PID, r12
	mtcr	r11
	mfspr	r9, SPRG5
	mfspr	r12, SPRG4
#endif
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	PPC405_ERR77_SYNC
	rfi			/* Should sync shadow TLBs */
	b	.		/* prevent prefetch past rfi */

2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
#ifdef CONFIG_403GCX
	lwz     r12, 12(r0)
	lwz     r11, 8(r0)
	mtspr   SPRN_PID, r12
	mtcr    r11
	lwz     r9, 4(r0)
	lwz     r12, 0(r0)
#else
	mfspr	r12, SPRG6
	mfspr	r11, SPRG7
	mtspr	SPRN_PID, r12
	mtcr	r11
	mfspr	r9, SPRG5
	mfspr	r12, SPRG4
#endif
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	b	DataAccess

/*
 * 0x0400 - Instruction Storage Exception
 * This is caused by a fetch from non-execute or guarded pages.
 */
	START_EXCEPTION(0x0400, InstructionAccess)
	NORMAL_EXCEPTION_PROLOG
	mr	r4,r12			/* Pass SRR0 as arg2 */
	li	r5,0			/* Pass zero as arg3 */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0x400, do_page_fault)

/* 0x0500 - External Interrupt Exception */
	EXCEPTION(0x0500, HardwareInterrupt, do_IRQ, EXC_XFER_LITE)

/* 0x0600 - Alignment Exception */
	START_EXCEPTION(0x0600, Alignment)
	NORMAL_EXCEPTION_PROLOG
	mfspr	r4,SPRN_DEAR		/* Grab the DEAR and save it */
	stw	r4,_DEAR(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE(0x600, AlignmentException)

/* 0x0700 - Program Exception */
	EXCEPTION(0x700, ProgramCheck, ProgramCheckException, EXC_XFER_EE)

	EXCEPTION(0x0800, Trap_08, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x0900, Trap_09, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x0A00, Trap_0A, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x0B00, Trap_0B, UnknownException, EXC_XFER_EE)

/* 0x0C00 - System Call Exception */
	START_EXCEPTION(0x0C00,	SystemCall)
	NORMAL_EXCEPTION_PROLOG
	EXC_XFER_EE_LITE(0xc00, DoSyscall)

	EXCEPTION(0x0D00, Trap_0D, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x0E00, Trap_0E, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x0F00, Trap_0F, UnknownException, EXC_XFER_EE)

/* 0x1000 - Programmable Interval Timer (PIT) Exception */
	START_EXCEPTION(0x1000, Decrementer)
	NORMAL_EXCEPTION_PROLOG
	lis	r0,TSR_PIS@h
	mtspr	SPRN_TSR,r0		/* Clear the PIT exception */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE(0x1000, timer_interrupt)

#if 0
/* NOTE:
 * FIT and WDT handlers are not implemented yet.
 */

/* 0x1010 - Fixed Interval Timer (FIT) Exception
*/
	STND_EXCEPTION(0x1010,	FITException,		UnknownException)

/* 0x1020 - Watchdog Timer (WDT) Exception
*/

	CRITICAL_EXCEPTION(0x1020, WDTException, UnknownException)
#endif

/* 0x1100 - Data TLB Miss Exception
 * As the name implies, translation is not in the MMU, so search the
 * page tables and fix it.  The only purpose of this function is to
 * load TLB entries from the page table if they exist.
 */
	START_EXCEPTION(0x1100,	DTLBMiss)
	mtspr	SPRG0, r10		/* Save some working registers */
	mtspr	SPRG1, r11
#ifdef CONFIG_403GCX
	stw     r12, 0(r0)
	stw     r9, 4(r0)
	mfcr    r11
	mfspr   r12, SPRN_PID
	stw     r11, 8(r0)
	stw     r12, 12(r0)
#else
	mtspr	SPRG4, r12
	mtspr	SPRG5, r9
	mfcr	r11
	mfspr	r12, SPRN_PID
	mtspr	SPRG7, r11
	mtspr	SPRG6, r12
#endif
	mfspr	r10, SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andis.	r11, r10, 0x8000
	beq	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l
	li	r9, 0
	mtspr	SPRN_PID, r9		/* TLB will have 0 TID */
	b	4f

	/* Get the PGD for the current thread.
	 */
3:
	mfspr	r11,SPRG3
	lwz	r11,PGDIR(r11)
4:
	tophys(r11, r11)
	rlwimi	r11, r10, 12, 20, 29	/* Create L1 (pgdir/pmd) address */
	lwz	r11, 0(r11)		/* Get L1 entry */
	rlwinm.	r12, r11, 0, 0, 19	/* Extract L2 (pte) base address */
	beq	2f			/* Bail if no table */

	rlwimi	r12, r10, 22, 20, 29	/* Compute PTE address */
	lwz	r11, 0(r12)		/* Get Linux PTE */
	andi.	r9, r11, _PAGE_PRESENT
	beq	2f

	ori	r11, r11, _PAGE_ACCESSED
	stw	r11, 0(r12)

	/* Most of the Linux PTE is ready to load into the TLB LO.
	 * We set ZSEL, where only the LS-bit determines user access.
	 * We set execute, because we don't have the granularity to
	 * properly set this at the page level (Linux problem).
	 * If shared is set, we cause a zero PID->TID load.
	 * Many of these bits are software only.  Bits we don't set
	 * here we (properly should) assume have the appropriate value.
	 */
	li	r12, 0x0ce2
	andc	r11, r11, r12		/* Make sure 20, 21 are zero */

	b	finish_tlb_load


2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
#ifdef CONFIG_403GCX
	lwz     r12, 12(r0)
	lwz     r11, 8(r0)
	mtspr   SPRN_PID, r12
	mtcr    r11
	lwz     r9, 4(r0)
	lwz     r12, 0(r0)
#else
	mfspr	r12, SPRG6
	mfspr	r11, SPRG7
	mtspr	SPRN_PID, r12
	mtcr	r11
	mfspr	r9, SPRG5
	mfspr	r12, SPRG4
#endif
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	b	DataAccess

/* 0x1200 - Instruction TLB Miss Exception
 * Nearly the same as above, except we get our information from different
 * registers and bailout to a different point.
 */
	START_EXCEPTION(0x1200,	ITLBMiss)
	mtspr	SPRG0, r10		/* Save some working registers */
	mtspr	SPRG1, r11
#ifdef CONFIG_403GCX
	stw     r12, 0(r0)
	stw     r9, 4(r0)
	mfcr    r11
	mfspr   r12, SPRN_PID
	stw     r11, 8(r0)
	stw     r12, 12(r0)
#else
	mtspr	SPRG4, r12
	mtspr	SPRG5, r9
	mfcr	r11
	mfspr	r12, SPRN_PID
	mtspr	SPRG7, r11
	mtspr	SPRG6, r12
#endif
	mfspr	r10, SRR0		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andis.	r11, r10, 0x8000
	beq	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l
	li	r9, 0
	mtspr	SPRN_PID, r9		/* TLB will have 0 TID */
	b	4f

	/* Get the PGD for the current thread.
	 */
3:
	mfspr	r11,SPRG3
	lwz	r11,PGDIR(r11)
4:
	tophys(r11, r11)
	rlwimi	r11, r10, 12, 20, 29	/* Create L1 (pgdir/pmd) address */
	lwz	r11, 0(r11)		/* Get L1 entry */
	rlwinm.	r12, r11, 0, 0, 19	/* Extract L2 (pte) base address */
	beq	2f			/* Bail if no table */

	rlwimi	r12, r10, 22, 20, 29	/* Compute PTE address */
	lwz	r11, 0(r12)		/* Get Linux PTE */
	andi.	r9, r11, _PAGE_PRESENT
	beq	2f

	ori	r11, r11, _PAGE_ACCESSED
	stw	r11, 0(r12)

	/* Most of the Linux PTE is ready to load into the TLB LO.
	 * We set ZSEL, where only the LS-bit determines user access.
	 * We set execute, because we don't have the granularity to
	 * properly set this at the page level (Linux problem).
	 * If shared is set, we cause a zero PID->TID load.
	 * Many of these bits are software only.  Bits we don't set
	 * here we (properly should) assume have the appropriate value.
	 */
	li	r12, 0x0ce2
	andc	r11, r11, r12		/* Make sure 20, 21 are zero */

	b	finish_tlb_load

	/* Done...restore registers and get out of here.
	*/
#ifdef CONFIG_403GCX
	lwz     r12, 12(r0)
	lwz     r11, 8(r0)
	mtspr   SPRN_PID, r12
	mtcr    r11
	lwz     r9, 4(r0)
	lwz     r12, 0(r0)
#else
	mfspr	r12, SPRG6
	mfspr	r11, SPRG7
	mtspr	SPRN_PID, r12
	mtcr	r11
	mfspr	r9, SPRG5
	mfspr	r12, SPRG4
#endif
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	PPC405_ERR77_SYNC
	rfi			/* Should sync shadow TLBs */
	b	.		/* prevent prefetch past rfi */

2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
#ifdef CONFIG_403GCX
	lwz     r12, 12(r0)
	lwz     r11, 8(r0)
	mtspr   SPRN_PID, r12
	mtcr    r11
	lwz     r9, 4(r0)
	lwz     r12, 0(r0)
#else
	mfspr	r12, SPRG6
	mfspr	r11, SPRG7
	mtspr	SPRN_PID, r12
	mtcr	r11
	mfspr	r9, SPRG5
	mfspr	r12, SPRG4
#endif
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	b	InstructionAccess

	EXCEPTION(0x1300, Trap_13, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1400, Trap_14, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1500, Trap_15, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1600, Trap_16, UnknownException, EXC_XFER_EE)
#ifdef CONFIG_IBM405_ERR51
	/* 405GP errata 51 */
	START_EXCEPTION(0x1700, Trap_17)
	b DTLBMiss
#else
	EXCEPTION(0x1700, Trap_17, UnknownException, EXC_XFER_EE)
#endif
	EXCEPTION(0x1800, Trap_18, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1900, Trap_19, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1A00, Trap_1A, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1B00, Trap_1B, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1C00, Trap_1C, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1D00, Trap_1D, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1E00, Trap_1E, UnknownException, EXC_XFER_EE)
	EXCEPTION(0x1F00, Trap_1F, UnknownException, EXC_XFER_EE)

/* 0x2000 - Debug Exception
*/
	CRITICAL_EXCEPTION(0x2000, DebugTrap, DebugException)

/*
 * The other Data TLB exceptions bail out to this point
 * if they can't resolve the lightweight TLB fault.
 */
DataAccess:
	NORMAL_EXCEPTION_PROLOG
	mfspr	r5,SPRN_ESR		/* Grab the ESR, save it, pass arg3 */
	stw	r5,_ESR(r11)
	mfspr	r4,SPRN_DEAR		/* Grab the DEAR, save it, pass arg2 */
	stw	r4,_DEAR(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0x300, do_page_fault)

#if 0
/* Check for a single step debug exception while in an exception
 * handler before state has been saved.  This is to catch the case
 * where an instruction that we are trying to single step causes
 * an exception (eg ITLB miss) and thus the first instruction of
 * the exception handler generates a single step debug exception.
 *
 * If we get a debug trap on the first instruction of an exception handler,
 * we reset the MSR_DE in the _exception handlers_ MSR (the debug trap is
 * a critical exception, so we are using SPRN_SRR3 to manipulate the MSR).
 * The exception handler was handling a non-critical interrupt, so it will
 * save (and later restore) the MSR via SPRN_SRR1, which will still have
 * the MSR_DE bit set.
 */
check_single_step_in_exception:

	/* This first instruction was already executed by the exception
	 * handler and must be the first instruction of every exception
	 * handler.
	 */
	mtspr	SPRN_SPRG0,r10		/* Save some working registers... */
	mtspr	SPRN_SPRG1,r11
	mfcr	r10			/* ..and the cr because we change it */

	mfspr   r11,SPRN_SRR3		/* MSR at the time of fault */
	andi.   r11,r11,MSR_PR
	bne+    2f			/* trapped from problem state */

	mfspr   r11,SPRN_SRR2		/* Faulting instruction address */
	cmplwi  r11,0x2100
	bgt+    2f			/* address above exception vectors */

	lis     r11,DBSR_IC@h           /* Remove the trap status */
	mtspr   SPRN_DBSR,r11

	mfspr	r11,SPRN_SRR3
	rlwinm	r11,r11,0,23,21		/* clear MSR_DE */
	mtspr	SPRN_SRR3, r11		/* restore MSR at rcfi without DE */

	mtcrf   0xff,r10                /* restore registers */
	mfspr   r11,SPRN_SPRG1
	mfspr   r10,SPRN_SPRG0

	sync
	rfci                            /* return to the exception handler  */
	b	.			/* prevent prefetch past rfi */

2:
	mtcrf   0xff,r10                /* restore registers */
	mfspr   r11,SPRN_SPRG1
	mfspr   r10,SPRN_SPRG0
	b       ret_to_debug_exception
#endif 0

/* Other PowerPC processors, namely those derived from the 6xx-series
 * have vectors from 0x2100 through 0x2F00 defined, but marked as reserved.
 * However, for the 4xx-series processors these are neither defined nor
 * reserved.
 */ 

	/* Damn, I came up one instruction too many to fit into the
	 * exception space :-).  Both the instruction and data TLB
	 * miss get to this point to load the TLB.
	 * 	r10 - EA of fault
	 * 	r11 - TLB LO (info from Linux PTE)
	 *	r12, r9 - avilable to use
	 *	PID - loaded with proper value when we get here
	 *	Upon exit, we reload everything and RFI.
	 * Actually, it will fit now, but oh well.....a common place
	 * to load the TLB.
	 */
tlb_4xx_index:
	.long	0
finish_tlb_load:
	/* load the next available TLB index.
	*/
	lwz	r9, tlb_4xx_index@l(0)
	addi	r9, r9, 1
	cmpwi	0, r9, 61		/* reserve entries 62, 63 for kernel */
	ble	7f
	li	r9, 0
7:
	stw	r9, tlb_4xx_index@l(0)

6:
	tlbwe	r11, r9, TLB_DATA		/* Load TLB LO */

	/* Create EPN.  This is the faulting address plus a static
	 * set of bits.  These are size, valid, E, U0, and ensure
	 * bits 20 and 21 are zero.
	 */
	li	r12, 0x00c0
	rlwimi	r10, r12, 0, 20, 31
	tlbwe	r10, r9, TLB_TAG		/* Load TLB HI */

	/* Done...restore registers and get out of here.
	*/
#ifdef CONFIG_403GCX
	lwz     r12, 12(r0)
	lwz     r11, 8(r0)
	mtspr   SPRN_PID, r12
	mtcr    r11
	lwz     r9, 4(r0)
	lwz     r12, 0(r0)
#else
	mfspr	r12, SPRG6
	mfspr	r11, SPRG7
	mtspr	SPRN_PID, r12
	mtcr	r11
	mfspr	r9, SPRG5
	mfspr	r12, SPRG4
#endif
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	PPC405_ERR77_SYNC
	rfi			/* Should sync shadow TLBs */
	b	.		/* prevent prefetch past rfi */

/* extern void giveup_fpu(struct task_struct *prev)
 *
 * The PowerPC 4xx family of processors do not have an FPU, so this just
 * returns.
 */
_GLOBAL(giveup_fpu)
	blr

/* This is where the main kernel code starts.
 */ 
start_here:

	/* ptr to current */
	lis	r2,init_task@h
	ori	r2,r2,init_task@l

	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* init task's THREAD */
	mtspr	SPRG3,r4

	/* stack */
	lis	r1,init_thread_union@ha
	addi	r1,r1,init_thread_union@l
	li	r0,0
	stwu	r0,THREAD_SIZE-STACK_FRAME_OVERHEAD(r1)

	bl	early_init	/* We have to do this with MMU on */

/*
 * Decide what sort of machine this is and initialize the MMU.
 */
	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27
	bl	machine_init
	bl	MMU_init

/* Go back to running unmapped so we can load up new values
 * and change to using our exception vectors.
 * On the 4xx, all we have to do is invalidate the TLB to clear
 * the old 16M byte TLB mappings.
 */
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	rfi
	b	.		/* prevent prefetch past rfi */

/* Load up the kernel context */
2:
	SYNC			/* Force all PTE updates to finish */

	/* set up the PTE pointers for the Abatron bdiGDB.
	*/
	lis	r6, swapper_pg_dir@h
	ori	r6, r6, swapper_pg_dir@l
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	stw	r5, 0xf0(r0)	/* Must match your Abatron config file */
	tophys(r5,r5)
	stw	r6, 0(r5)

/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
	mtspr	SRR0,r3
	mtspr	SRR1,r4
	rfi			/* enable MMU and jump to start_kernel */
	b	.		/* prevent prefetch past rfi */

/* Set up the initial MMU state so we can do the first level of
 * kernel initialization.  This maps the first 16 MBytes of memory 1:1
 * virtual to physical and more importantly sets the cache mode.
 */
initial_mmu:
	tlbia			/* Invalidate all TLB entries */
	sync 

	/* We should still be executing code at physical address 0x0000xxxx
	 * at this point. However, start_here is at virtual address
	 * 0xC000xxxx. So, set up a TLB mapping to cover this once
	 * translation is enabled.
	 */

	lis	r3,KERNELBASE@h		/* Load the kernel virtual address */
	ori	r3,r3,KERNELBASE@l
	tophys(r4,r3)			/* Load the kernel physical address */

	/* Load the kernel PID.
	*/
	li	r0,0
	mtspr	SPRN_PID,r0
	sync

	/* Configure and load two entries into TLB slots 62 and 63.
	 * In case we are pinning TLBs, these are reserved in by the
	 * other TLB functions.  If not reserving, then it doesn't
	 * matter where they are loaded.
	 */
	clrrwi	r4,r4,10		/* Mask off the real page number */
	ori	r4,r4,(TLB_WR | TLB_EX)	/* Set the write and execute bits */

	clrrwi	r3,r3,10		/* Mask off the effective page number */
	ori	r3,r3,(TLB_VALID | TLB_PAGESZ(PAGESZ_16M))

        li      r0,62                    /* TLB slot 62 */

	tlbwe	r4,r0,TLB_DATA		/* Load the data portion of the entry */
	tlbwe	r3,r0,TLB_TAG		/* Load the tag portion of the entry */

	addis	r4, r4, 0x0100		/* Map next 16 M entries */
	addis	r3, r3, 0x0100

        li      r0,63                    /* TLB slot 63 */

	tlbwe	r4,r0,TLB_DATA
	tlbwe	r3,r0,TLB_TAG
	isync

	/* Establish the exception vector base
	*/
	lis	r4,KERNELBASE@h		/* EVPR only uses the high 16-bits */
	tophys(r0,r4)			/* Use the physical address */
	mtspr	SPRN_EVPR,r0

	blr

_GLOBAL(abort)
        mfspr   r13,SPRN_DBCR0
        oris    r13,r13,DBCR_RST(DBCR_RST_SYSTEM)@h
        mtspr   SPRN_DBCR0,r13

_GLOBAL(set_context)

#ifdef CONFIG_BDI_SWITCH
	/* Context switch the PTE pointer for the Abatron BDI2000.
	 * The PGDIR is the second parameter.
	 */
	lis	r5, KERNELBASE@h
	lwz	r5, 0xf0(r5)
	stw	r4, 0x4(r5)
#endif
	sync
	mtspr	SPRN_PID,r3
	isync				/* Need an isync to flush shadow */
					/* TLBs after changing PID */
	blr

/* We put a few things here that have to be page-aligned. This stuff
 * goes at the beginning of the data segment, which is page-aligned.
 */
	.data
_GLOBAL(sdata)
_GLOBAL(empty_zero_page)
	.space	4096
_GLOBAL(swapper_pg_dir)
	.space	4096

	
/* Stack for handling critical exceptions from kernel mode */
	.section .bss
critical_stack_bottom:
	.space	4096
critical_stack_top:
	.previous

/* This space gets a copy of optional info passed to us by the bootstrap
 * which is used to pass parameters into the kernel like root=/dev/sda1, etc.
 */
_GLOBAL(cmd_line)
	.space	512

/* Room for two PTE pointers, usually the kernel and current user pointers
 * to their respective root page table.
 */
abatron_pteptrs:
	.space	8
