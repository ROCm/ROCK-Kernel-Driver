/*
 * arch/ppc/kernel/head_44x.S
 *
 * Kernel execution entry point code.
 *
 * Matt Porter <mporter@mvista.com>
 *
 * Copyright 2002-2003 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/ibm4xx.h>
#include <asm/ibm44x.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>

/*
 * Macros
 */

#define SET_IVOR(vector_number, vector_label)		\
		li	r26,vector_label@l; 		\
		mtspr	SPRN_IVOR##vector_number,r26;	\
		sync
				
/* As with the other PowerPC ports, it is expected that when code
 * execution begins here, the following registers contain valid, yet
 * optional, information:
 *
 *   r3 - Board info structure pointer (DRAM, frequency, MAC address, etc.)
 *   r4 - Starting address of the init RAM disk
 *   r5 - Ending address of the init RAM disk
 *   r6 - Start of kernel command line string (e.g. "mem=128")
 *   r7 - End of kernel command line string
 *
 */
	.text
_GLOBAL(_stext)
_GLOBAL(_start)
	/*
	 * Reserve a word at a fixed location to store the address
	 * of abatron_pteptrs
	 */
	nop
/*
 * Save parameters we are passed
 */
	mr	r31,r3
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7
	li	r24,0		/* CPU number */

/*
 * Set up the initial MMU state
 *
 * We are still executing code at the virtual address
 * mappings set by the firmware for the base of RAM.
 *
 * We first invalidate all TLB entries but the one
 * we are running from.  We then load the KERNELBASE
 * mappings so we can begin to use kernel addresses
 * natively and so the interrupt vector locations are
 * permanently pinned (necessary since Book E
 * implementations always have translation enabled).
 *
 * TODO: Use the known TLB entry we are running from to
 *	 determine which physical region we are located
 *	 in.  This can be used to determine where in RAM
 *	 (on a shared CPU system) or PCI memory space
 *	 (on a DRAMless system) we are located.
 *       For now, we assume a perfect world which means
 *	 we are located at the base of DRAM (physical 0).
 */

/*
 * Search TLB for entry that we are currently using.
 * Invalidate all entries but the one we are using.
 */
	/* Load our current PID->MMUCR TID and MSR IS->MMUCR STS */
 	mfspr	r3,SPRN_MMUCR			/* Get MMUCR */
	lis	r4,PPC44x_MMUCR_STS@h
	ori	r4,r4,PPC44x_MMUCR_TID@l	/* Create mask */
	andc	r3,r3,r4			/* Clear out TID/STS bits */
	mfspr	r4,SPRN_PID			/* Get PID */
	or	r3,r3,r4			/* Set TID bits */
	mfmsr	r5				/* Get MSR */
	andi.	r5,r5,MSR_IS@l			/* TS=1? */
	beq	wmmucr				/* If not, leave STS=0 */
	oris	r3,r3,PPC44x_MMUCR_STS@h	/* Set STS=1 */
wmmucr:	mtspr	SPRN_MMUCR,r3			/* Put MMUCR */
	sync

	bl	invstr				/* Find our address */
invstr:	mflr	r5				/* Make it accessible */
	tlbsx	r23,0,r5			/* Find entry we are in */
	li	r4,0				/* Start at TLB entry 0 */
	li	r3,0				/* Set PAGEID inval value */
1:	cmpw	r23,r4				/* Is this our entry? */
	beq	skpinv				/* If so, skip the inval */
	tlbwe	r3,r4,PPC44x_TLB_PAGEID		/* If not, inval the entry */
skpinv:	addi	r4,r4,1				/* Increment */
	cmpwi	r4,64				/* Are we done? */
	bne	1b				/* If not, repeat */
	isync					/* If so, context change */

/*
 * Configure and load pinned entries into TLB slots 62 and 63.
 */

	lis	r3,KERNELBASE@h		/* Load the kernel virtual address */
	ori	r3,r3,KERNELBASE@l

	/* Kernel is at the base of RAM */
	li r4, 0			/* Load the kernel physical address */

	/* Load the kernel PID = 0 */
	li	r0,0
	mtspr	SPRN_PID,r0
	sync

	/* Load the kernel TID  = 0 */
	mfspr	r5,SPRN_MMUCR
	lis	r6, PPC44x_MMUCR_TID@h
	ori	r6,r6,PPC44x_MMUCR_TID@l
	andc	r5,r5,r6
	mtspr	SPRN_MMUCR,r5
	sync

 	/* pageid fields */
	clrrwi	r3,r3,10		/* Mask off the effective page number */
	ori	r3,r3,(PPC44x_TLB_VALID | PPC44x_TLB_PAGESZ(PPC44x_PAGESZ_256M))

	/* xlat fields */
	clrrwi	r4,r4,10		/* Mask off the real page number */
					/* ERPN is 0 for first 4GB page */

	/* attrib fields */
	/* Added guarded bit to protect against speculative loads/stores */
	li	r5,0
	ori	r5,r5,(PPC44x_TLB_SW | PPC44x_TLB_SR | PPC44x_TLB_SX | PPC44x_TLB_G)

        li      r0,62                    /* TLB slot 62 */

	tlbwe	r3,r0,PPC44x_TLB_PAGEID	/* Load the pageid fields */
	tlbwe	r4,r0,PPC44x_TLB_XLAT	/* Load the translation fields */
	tlbwe	r5,r0,PPC44x_TLB_ATTRIB	/* Load the attrib/access fields */

	/* Force context change */
	mfmsr	r0
	mtspr	SRR1, r0
	lis	r0,3f@h
	ori	r0,r0,3f@l
	mtspr	SRR0,r0
	sync
	rfi

	/* If necessary, invalidate original entry we used */
3:	cmpwi	r23,62
	beq	4f
	li	r6,0
	tlbwe   r6,r23,PPC44x_TLB_PAGEID
	sync

4:	ori	r3,r3,PPC44x_TLB_TS	/* TS = 1 */

        li      r0,63                   /* TLB slot 63 */

	tlbwe	r3,r0,PPC44x_TLB_PAGEID	/* Load the pageid fields */
	tlbwe	r4,r0,PPC44x_TLB_XLAT	/* Load the translation fields */
	tlbwe	r5,r0,PPC44x_TLB_ATTRIB	/* Load the attrib/access fields */

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	/*
	 * Add temporary UART mapping for early debug.  This
	 * mapping must be identical to that used by the early
	 * bootloader code since the same asm/serial.h parameters
	 * are used for polled operation.
	 */
 	/* pageid fields */
	lis	r3,0xe000	
	ori	r3,r3,(PPC44x_TLB_VALID | PPC44x_TLB_PAGESZ(PPC44x_PAGESZ_256M))

	/* xlat fields */
	lis	r4,0x4000		/* RPN is 0x40000000 */
	ori	r4,r4,0x0001		/* ERPN is 1 for second 4GB page */

	/* attrib fields */
	li	r5,0
	ori	r5,r5,(PPC44x_TLB_SW | PPC44x_TLB_SR | PPC44x_TLB_I | PPC44x_TLB_G)

        li      r0,60                    /* TLB slot 60 */

	tlbwe	r3,r0,PPC44x_TLB_PAGEID	/* Load the pageid fields */
	tlbwe	r4,r0,PPC44x_TLB_XLAT	/* Load the translation fields */
	tlbwe	r5,r0,PPC44x_TLB_ATTRIB	/* Load the attrib/access fields */

	ori	r3,r3,PPC44x_TLB_TS	/* Translation state 1 */

        li      r0,61			/* TLB slot 61 */

	tlbwe	r3,r0,PPC44x_TLB_PAGEID	/* Load the pageid fields */
	tlbwe	r4,r0,PPC44x_TLB_XLAT	/* Load the translation fields */
	tlbwe	r5,r0,PPC44x_TLB_ATTRIB	/* Load the attrib/access fields */
#endif /* CONFIG_SERIAL_TEXT_DEBUG */

	/* Force context change */
	isync

	/* Establish the interrupt vector offsets */
	SET_IVOR(0,  CriticalInput);
	SET_IVOR(1,  MachineCheck);
	SET_IVOR(2,  DataStorage);
	SET_IVOR(3,  InstructionStorage);
	SET_IVOR(4,  ExternalInput);
	SET_IVOR(5,  Alignment);
	SET_IVOR(6,  Program);
	SET_IVOR(7,  FloatingPointUnavailable);
	SET_IVOR(8,  SystemCall);
	SET_IVOR(9,  AuxillaryProcessorUnavailable);
	SET_IVOR(10, Decrementer);
	SET_IVOR(11, FixedIntervalTimer);
	SET_IVOR(12, WatchdogTimer);
	SET_IVOR(13, DataTLBError);
	SET_IVOR(14, InstructionTLBError);
	SET_IVOR(15, Debug);

	/* Establish the interrupt vector base */
	lis	r4,interrupt_base@h	/* IVPR only uses the high 16-bits */
	mtspr	SPRN_IVPR,r4

	/*
	 * This is where the main kernel code starts.
	 */

	/* ptr to current */
	lis	r2,init_task@h
	ori	r2,r2,init_task@l

	/* ptr to current thread */
	addi	r4,r2,THREAD	/* init task's THREAD */
	mtspr	SPRG3,r4

	/* stack */
	lis	r1,init_thread_union@h
	ori	r1,r1,init_thread_union@l
	li	r0,0
	stwu	r0,THREAD_SIZE-STACK_FRAME_OVERHEAD(r1)

	bl	early_init

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

	/* Setup PTE pointers for the Abatron bdiGDB */
	lis	r6, swapper_pg_dir@h
	ori	r6, r6, swapper_pg_dir@l
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	lis	r4, KERNELBASE@h
	ori	r4, r4, KERNELBASE@l
	stw	r5, 0(r4)	/* Save abatron_pteptrs at a fixed location */
	stw	r6, 0(r5)

	/* Let's move on */
	lis	r4,start_kernel@h
	ori	r4,r4,start_kernel@l
	lis	r3,MSR_KERNEL@h
	ori	r3,r3,MSR_KERNEL@l
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	rfi			/* change context and jump to start_kernel */

/*
 * Interrupt vector entry code
 *
 * The Book E MMUs are always on so we don't need to handle
 * interrupts in real mode as with previous PPC processors. In
 * this case we handle interrupts in the kernel virtual address
 * space.
 *
 * Interrupt vectors are dynamically placed relative to the
 * interrupt prefix as determined by the address of interrupt_base.
 * The interrupt vectors offsets are programmed using the labels
 * for each interrupt vector entry.
 *
 * Interrupt vectors must be aligned on a 16 byte boundary.
 * We align on a 32 byte cache line boundary for good measure.
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
/* XXX but we don't have RAM mapped at 0 in space 0  -- paulus. */
#define CRITICAL_EXCEPTION_PROLOG					     \
	stw	r10,crit_r10@l(0);	/* save two registers to work with */\
	stw	r11,crit_r11@l(0);					     \
	mfspr	r10,SPRG0;						     \
	stw	r10,crit_sprg0@l(0);					     \
	mfspr	r10,SPRG1;						     \
	stw	r10,crit_sprg1@l(0);					     \
	mfspr	r10,SPRG4R;						     \
	stw	r10,crit_sprg4@l(0);					     \
	mfspr	r10,SPRG5R;						     \
	stw	r10,crit_sprg5@l(0);					     \
	mfspr	r10,SPRG6R;						     \
	stw	r10,crit_sprg6@l(0);					     \
	mfspr	r10,SPRG7R;						     \
	stw	r10,crit_sprg7@l(0);					     \
	mfspr	r10,SPRN_PID;						     \
	stw	r10,crit_pid@l(0);					     \
	mfspr	r10,SRR0;						     \
	stw	r10,crit_srr0@l(0);					     \
	mfspr	r10,SRR1;						     \
	stw	r10,crit_srr1@l(0);					     \
	mfcr	r10;			/* save CR in r10 for now	   */\
	mfspr	r11,SPRN_CSRR1;		/* check whether user or kernel    */\
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
	mfspr	r12,CSRR0;						     \
	stw	r1,GPR1(r11);						     \
	mfspr	r9,CSRR1;						     \
	stw	r1,0(r11);						     \
	tovirt(r1,r11);							     \
	rlwinm	r9,r9,0,14,12;		/* clear MSR_WE (necessary?)	   */\
	stw	r0,GPR0(r11);						     \
	SAVE_4GPRS(3, r11);						     \
	SAVE_2GPRS(7, r11)

/*
 * Exception vectors.
 */
#define	START_EXCEPTION(label)						     \
        .align 5;              						     \
label:

#define FINISH_EXCEPTION(func)					\
	bl	transfer_to_handler_full;			\
	.long	func;						\
	.long	ret_from_except_full

#define EXCEPTION(n, label, hdlr, xfer)				\
	START_EXCEPTION(label);					\
	NORMAL_EXCEPTION_PROLOG;				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	xfer(n, hdlr)

#define CRITICAL_EXCEPTION(n, label, hdlr)			\
	START_EXCEPTION(label);					\
	CRITICAL_EXCEPTION_PROLOG;				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	EXC_XFER_TEMPLATE(hdlr, n+2, (MSR_KERNEL & ~(MSR_ME|MSR_DE|MSR_CE)), \
			  NOCOPY, transfer_to_handler_full, \
			  ret_from_except_full)

#define EXC_XFER_TEMPLATE(hdlr, trap, msr, copyee, tfer, ret)	\
	li	r10,trap;					\
	stw	r10,TRAP(r11);					\
	lis	r10,msr@h;					\
	ori	r10,r10,msr@l;					\
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

interrupt_base:
	/* Critical Input Interrupt */
	CRITICAL_EXCEPTION(0x0100, CriticalInput, UnknownException)

	/* Machine Check Interrupt */
	CRITICAL_EXCEPTION(0x0200, MachineCheck, MachineCheckException)

	/* Data Storage Interrupt */
	START_EXCEPTION(DataStorage)
	mtspr	SPRG0, r10		/* Save some working registers */
	mtspr	SPRG1, r11
	mtspr	SPRG4W, r12
	mtspr	SPRG5W, r13
	mtspr	SPRG6W, r14
	mfcr	r11
	mtspr	SPRG7W, r11

	/*
	 * Check if it was a store fault, if not then bail
	 * because a user tried to access a kernel or
	 * read-protected page.  Otherwise, get the
	 * offending address and handle it.
	 */
	mfspr	r10, SPRN_ESR
	andis.	r10, r10, ESR_ST@h
	beq	2f

	mfspr	r10, SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andis.	r11, r10, 0x8000
	beq	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr   r12,SPRN_MMUCR          /* Set TID to 0 */
	li      r13,PPC44x_MMUCR_TID@l
	andc    r12,r12,r13
	mtspr   SPRN_MMUCR,r12

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRG3
	lwz	r11,PGDIR(r11)

	/* Load MMUCR with our PID and STS=<current TS> */
	mfspr	r12,SPRN_MMUCR			/* Get MMUCR */
	lis     r13,PPC44x_MMUCR_STS@h
	ori     r13,r13,PPC44x_MMUCR_TID@l      /* Create mask */
	andc    r12,r12,r13                     /* Clear out TID/STS bits */
	mfspr   r13,SPRN_PID                    /* Get PID */
	or      r12,r12,r13			/* Set TID bits */
	mfspr	r14,SPRN_SRR1			/* Get SRR1 */
	andi.	r14,r14,MSR_IS@l		/* TS=1? */
	beq	4f				/* If not, leave STS=0 */
	oris	r12,r12,PPC44x_MMUCR_STS@h	/* Set STS=1 */
	mtspr   SPRN_MMUCR,r12
4:
	rlwinm  r12, r10, 13, 19, 29    /* Compute pgdir/pmd offset */
	lwzx    r11, r12, r11           /* Get pgd/pmd entry */
	rlwinm. r12, r11, 0, 0, 20      /* Extract pt base address */
	beq     2f                      /* Bail if no table */

	rlwimi  r12, r10, 23, 20, 28    /* Compute pte address */
	lwz     r11, 4(r12)             /* Get pte entry */

	andi.	r13, r11, _PAGE_RW	/* Is it writeable? */
	beq	2f			/* Bail if not */

	/* Update 'changed'.
	*/
	ori	r11, r11, _PAGE_DIRTY|_PAGE_ACCESSED|_PAGE_HWWRITE
	stw	r11, 4(r12)		/* Update Linux page table */

	/* FIXME: Staticly setting some permissions */
	li	r13, 0x003f		/* Set UX,UW,UR,SX,SW,SR */
	andi.	r11,r11,0xffff		/* Clear MS 16 bits */
	/* FIXME: Force attributes */
	ori	r11,r11, 0x0100		/* Set G */
	/* FIXME: Already set in PTE */
	rlwimi	r11,r13,0,26,31		/* Insert static perms */

	lis	r13,0xffff
	ori	r13,r13,0x0fff			/* Set U0-U3 mask */
	and	r11,r11,r13			/* Clear U0-U3 */

	/* find the TLB index that caused the fault.  It has to be here. */
	tlbsx	r14, 0, r10

	tlbwe	r11, r14, PPC44x_TLB_ATTRIB		/* Write ATTRIB */

	/* Done...restore registers and get out of here.
	*/
	mfspr	r11, SPRG7R
	mtcr	r11
	mfspr	r14, SPRG6R
	mfspr	r13, SPRG5R
	mfspr	r12, SPRG4R

	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	rfi			/* Force context change */

2:
	/*
	 * The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRG7R
	mtcr	r11
	mfspr	r14, SPRG6R
	mfspr	r13, SPRG5R
	mfspr	r12, SPRG4R

	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	b	data_access

	/* Instruction Storage Interrupt */
	START_EXCEPTION(InstructionStorage)
	NORMAL_EXCEPTION_PROLOG
	mr      r4,r12                  /* Pass SRR0 as arg2 */
	li      r5,0                    /* Pass zero as arg3 */
	addi    r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0x0400, do_page_fault)

	/* External Input Interrupt */
	EXCEPTION(0x0500, ExternalInput, do_IRQ, EXC_XFER_LITE)

	/* Alignment Interrupt */
	START_EXCEPTION(Alignment)
	NORMAL_EXCEPTION_PROLOG
	mfspr   r4,SPRN_DEAR            /* Grab the DEAR and save it */
	stw     r4,_DEAR(r11)
	addi    r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE(0x0600, AlignmentException)

	/* Program Interrupt */
	START_EXCEPTION(Program)
	NORMAL_EXCEPTION_PROLOG
	mfspr	r4,SPRN_ESR		/* Grab the ESR and save it */
	stw	r4,_ESR(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE(0x700, ProgramCheckException)

	/* Floating Point Unavailable Interrupt */
	EXCEPTION(0x2010, FloatingPointUnavailable, UnknownException, EXC_XFER_EE)

	/* System Call Interrupt */
	START_EXCEPTION(SystemCall)
	NORMAL_EXCEPTION_PROLOG
	EXC_XFER_EE_LITE(0x0c00, DoSyscall)

	/* Auxillary Processor Unavailable Interrupt */
	EXCEPTION(0x2020, AuxillaryProcessorUnavailable, UnknownException, EXC_XFER_EE)

	/* Decrementer Interrupt */
	START_EXCEPTION(Decrementer)
	NORMAL_EXCEPTION_PROLOG
	lis     r0,TSR_DIS@h            /* Setup the DEC interrupt mask */
	mtspr   SPRN_TSR,r0		/* Clear the DEC interrupt */
	addi    r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_LITE(0x1000, timer_interrupt)

	/* Fixed Internal Timer Interrupt */
	/* TODO: Add FIT support */
	EXCEPTION(0x1010, FixedIntervalTimer, UnknownException, EXC_XFER_EE)

	/* Watchdog Timer Interrupt */
	/* TODO: Add watchdog support */
	CRITICAL_EXCEPTION(0x1020, WatchdogTimer, UnknownException)

	/* Data TLB Error Interrupt */
	START_EXCEPTION(DataTLBError)
	mtspr	SPRG0, r10		/* Save some working registers */
	mtspr	SPRG1, r11
	mtspr	SPRG4W, r12
	mtspr	SPRG5W, r13
	mtspr	SPRG6W, r14
	mfcr	r11
	mtspr	SPRG7W, r11
	mfspr	r10, SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andis.	r11, r10, 0x8000
	beq	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MMUCR		/* Set TID to 0 */
	li	r13,PPC44x_MMUCR_TID@l
	andc	r12,r12,r13
	mtspr	SPRN_MMUCR,r12

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRG3
	lwz	r11,PGDIR(r11)

	/* Load PID into MMUCR TID */
	li      r13,PPC44x_MMUCR_TID@l       	 /* Create mask */
	andc    r12,r12,r13                      /* Clear out TID/STS bits */
	mfspr   r13,SPRN_PID                     /* Get PID */
	or      r12,r12,r13
	mtspr	SPRN_MMUCR,r12
4:
	rlwinm 	r12, r10, 13, 19, 29	/* Compute pgdir/pmd offset */
	lwzx	r11, r12, r11		/* Get pgd/pmd entry */
	rlwinm.	r12, r11, 0, 0, 20	/* Extract pt base address */
	beq	2f			/* Bail if no table */

	rlwimi	r12, r10, 23, 20, 28	/* Compute pte address */
	lwz	r11, 4(r12)		/* Get pte entry */
	andi.	r13, r11, _PAGE_PRESENT	/* Is the page present? */
	beq	2f			/* Bail if not present */

	ori	r11, r11, _PAGE_ACCESSED
	stw	r11, 4(r12)

	 /* Jump to common tlb load */
	b	finish_tlb_load

2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRG7R
	mtcr	r11
	mfspr	r14, SPRG6R
	mfspr	r13, SPRG5R
	mfspr	r12, SPRG4R
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	b	data_access

	/* Instruction TLB Error Interrupt */
	/*
	 * Nearly the same as above, except we get our
	 * information from different registers and bailout
	 * to a different point.
	 */
	START_EXCEPTION(InstructionTLBError)
	mtspr	SPRG0, r10		/* Save some working registers */
	mtspr	SPRG1, r11
	mtspr	SPRG4W, r12
	mtspr	SPRG5W, r13
	mtspr	SPRG6W, r14
	mfcr	r11
	mtspr	SPRG7W, r11
	mfspr	r10, SRR0		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	andis.	r11, r10, 0x8000
	beq	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MMUCR		/* Set TID to 0 */
	li	r13,PPC44x_MMUCR_TID@l
	andc	r12,r12,r13
	mtspr	SPRN_MMUCR,r12

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRG3
	lwz	r11,PGDIR(r11)

	/* Load PID into MMUCR TID */
	li      r13,PPC44x_MMUCR_TID@l       	 /* Create mask */
	andc    r12,r13,r13                      /* Clear out TID/STS bits */
	mfspr   r13,SPRN_PID                     /* Get PID */
	or      r12,r12,r13
	mtspr	SPRN_MMUCR,r12

4:
	rlwinm	r12, r10, 13, 19, 29	/* Compute pgdir/pmd offset */
	lwzx	r11, r12, r11		/* Get pgd/pmd entry */
	rlwinm.	r12, r11, 0, 0, 20	/* Extract pt base address */
	beq	2f			/* Bail if no table */

	rlwimi	r12, r10, 23, 20, 28	/* Compute pte address */
	lwz	r11, 4(r12)		/* Get pte entry */
	andi.	r13, r11, _PAGE_PRESENT	/* Is the page present? */
	beq	2f			/* Bail if not present */

	ori	r11, r11, _PAGE_ACCESSED
	stw	r11, 4(r12)

	/* Jump to common TLB load point */
	b	finish_tlb_load

2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRG7R
	mtcr	r11
	mfspr	r14, SPRG6R
	mfspr	r13, SPRG5R
	mfspr	r12, SPRG4R
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	b	InstructionStorage

/* Check for a single step debug exception while in an exception
 * handler before state has been saved.  This is to catch the case
 * where an instruction that we are trying to single step causes
 * an exception (eg ITLB/DTLB miss) and thus the first instruction of
 * the exception handler generates a single step debug exception.
 *
 * If we get a debug trap on the first instruction of an exception handler,
 * we reset the MSR_DE in the _exception handler's_ MSR (the debug trap is
 * a critical exception, so we are using SPRN_CSRR1 to manipulate the MSR).
 * The exception handler was handling a non-critical interrupt, so it will
 * save (and later restore) the MSR via SPRN_SRR1, which will still have
 * the MSR_DE bit set.
 */
	/* Debug Interrupt */
	CRITICAL_EXCEPTION(0x2000, Debug, DebugException)
#if 0
	START_EXCEPTION(Debug)
	/* This first instruction was already executed by the exception
	 * handler and must be the first instruction of every exception
	 * handler.
	 */
	mtspr	SPRN_SPRG0,r10		/* Save some working registers... */
	mtspr	SPRN_SPRG1,r11
	mtspr	SPRN_SPRG4W,r12
	mfcr	r10			/* ..and the cr because we change it */

	mfspr   r11,SPRN_CSRR1		/* MSR at the time of fault */
	andi.   r11,r11,MSR_PR
	bne+    2f			/* trapped from problem state */

	mfspr   r11,SPRN_CSRR0		/* Faulting instruction address */
	lis	r12, KERNELBASE@h
	ori	r12, r12, KERNELBASE@l
	cmplw   r11,r12
	blt+    2f			/* addr below exception vectors */

	lis	r12, Debug@h
	ori	r12, r12, Debug@l
	cmplw   r11,r12
	bgt+    2f			/* addr above TLB exception vectors */

	lis     r11,DBSR_IC@h           /* Remove the trap status */
	mtspr   SPRN_DBSR,r11

	mfspr	r11,SPRN_CSRR1
	rlwinm	r11,r11,0,23,21		/* clear MSR_DE */
	mtspr	SPRN_CSRR1, r11		/* restore MSR at rcfi without DE */

	mtcrf   0xff,r10                /* restore registers */
	mfspr	r12,SPRN_SPRG4R
	mfspr   r11,SPRN_SPRG1
	mfspr   r10,SPRN_SPRG0

	sync
	rfci                            /* return to the exception handler  */
	b	.			/* prevent prefetch past rfci */

2:
	mtcrf   0xff,r10                /* restore registers */
	mfspr   r12,SPRN_SPRG4R
	mfspr   r11,SPRN_SPRG1
	mfspr   r10,SPRN_SPRG0

	CRIT_EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r7,CRIT_EXC;
        li      r9,MSR_KERNEL
	FINISH_EXCEPTION(DebugException)
#endif

/*
 * Local functions
 */
	/*
	 * Data TLB exceptions will bail out to this point
	 * if they can't resolve the lightweight TLB fault.
	 */
data_access:
	NORMAL_EXCEPTION_PROLOG
	mfspr	r5,SPRN_ESR		/* Grab the ESR, save it, pass arg3 */
	stw	r5,_ESR(r11)
	mfspr	r4,SPRN_DEAR		/* Grab the DEAR, save it, pass arg2 */
	stw	r4,_DEAR(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0x0300, do_page_fault)

/*

 * Both the instruction and data TLB miss get to this
 * point to load the TLB.
 * 	r10 - EA of fault
 * 	r11 - available to use
 *	r12 - Pointer to the 64-bit PTE
 *	r13 - available to use
 *	r14 - available to use
 *	MMUCR - loaded with proper value when we get here
 *	Upon exit, we reload everything and RFI.
 */
finish_tlb_load:
	/*
	 * We set execute, because we don't have the granularity to
	 * properly set this at the page level (Linux problem).
	 * If shared is set, we cause a zero PID->TID load.
	 * Many of these bits are software only.  Bits we don't set
	 * here we (properly should) assume have the appropriate value.
	 */

	/* Load the next available TLB index */
	lis	r13, tlb_44x_index@h
	ori	r13, r13, tlb_44x_index@l
	lwz	r14, 0(r13)
	/* Load the TLB high watermark */
	lis	r13, tlb_44x_hwater@h
	ori	r13, r13, tlb_44x_hwater@l
	lwz	r11, 0(r13)


	/* Increment, rollover, and store TLB index */
	addi	r14, r14, 1
	cmpw	0, r14, r11		/* reserve entries 62-63 for kernel */
	ble	7f
	li	r14, 0
7:
	/* Load the next available TLB index */
	lis     r13, tlb_44x_index@h
	ori     r13, r13, tlb_44x_index@l
	stw	r14, 0(r13)

6:
	lwz	r13, 0(r12)			/* Get MS word of PTE */
	lwz	r11, 4(r12)			/* Get LS word of PTE */
	rlwimi	r13, r11, 0, 0 , 19		/* Insert RPN */
	tlbwe	r13, r14, PPC44x_TLB_XLAT	/* Write XLAT */

	/*
	 * Create PAGEID. This is the faulting address plus
	 * a set of static bits. The static bits are page
	 * size and valid. Bits 20  and 21 should be zero
	 * for a page size of 4KB.
	 */
	li	r12, 0x0210			/* Set size and valid */
	mfspr	r13, SPRN_SRR1			/* Get SRR1 */
	andi.	r13, r13, MSR_IS@l
	beq	7f
	ori	r12, r12, PPC44x_TLB_TS@l	/* Set TS=1 */
7:	rlwimi	r10, r12, 0, 20, 31		/* Insert statics */
	tlbwe	r10, r14, PPC44x_TLB_PAGEID	/* Write PAGEID */

	/* FIXME: Staticly setting some permissions */
	li	r13, 0x002d			/* Set UX,UR,SX,SR */
	andi.	r11, r11, 0xffff		/* Clear MS 16 bits */
	andi.	r12, r11, 0x0002		/* _PAGE_HWWRITE? */
	beq	8f
	ori	r13, r13, 0x0002		/* Set SW */
	/* FIXME: Force attributes */
8:	ori	r11, r11, 0x0100		/* Set G */
	/* FIXME: Already set in PTE */
	rlwimi	r11, r13, 0, 26, 31		/* Insert static perms */

	lis	r13,0xffff
	ori	r13,r13,0x0fff			/* Set U0-U3 mask */
	and	r11,r11,r13			/* Clear U0-U3 */
	tlbwe	r11, r14, PPC44x_TLB_ATTRIB	/* Write ATTRIB */

	/* Done...restore registers and get out of here.
	*/
	mfspr	r11, SPRG7R
	mtcr	r11
	mfspr	r14, SPRG6R
	mfspr	r13, SPRG5R
	mfspr	r12, SPRG4R
	mfspr	r11, SPRG1
	mfspr	r10, SPRG0
	rfi					/* Force context change */

/*
 * Global functions
 */

/*
 * extern void giveup_altivec(struct task_struct *prev)
 *
 * The 44x core does not have an AltiVec unit.
 */
_GLOBAL(giveup_altivec)
	blr

/*
 * extern void giveup_fpu(struct task_struct *prev)
 *
 * The 44x core does not have an FPU.
 */
_GLOBAL(giveup_fpu)
	blr

/*
 * extern void abort(void)
 *
 * At present, this routine just applies a system reset.
 */
_GLOBAL(abort)
        mfspr   r13,SPRN_DBCR0
        oris    r13,r13,DBCR0_RST_SYSTEM@h
        mtspr   SPRN_DBCR0,r13

_GLOBAL(set_context)

#ifdef CONFIG_BDI_SWITCH
	/* Context switch the PTE pointer for the Abatron BDI2000.
	 * The PGDIR is the second parameter.
	 */
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	stw	r4, 0x4(r5)
#endif
	mtspr	SPRN_PID,r3
	isync			/* Force context change */
	blr

/*
 * We put a few things here that have to be page-aligned. This stuff
 * goes at the beginning of the data segment, which is page-aligned.
 */
	.data
_GLOBAL(sdata)
_GLOBAL(empty_zero_page)
	.space	4096

/*
 * To support >32-bit physical addresses, we use an 8KB pgdir.
 */
_GLOBAL(swapper_pg_dir)
	.space	8192

/* Stack for handling critical exceptions from kernel mode */
	.section .bss
critical_stack_bottom:
	.space 4096
critical_stack_top:
	.previous

/*
 * This space gets a copy of optional info passed to us by the bootstrap
 * which is used to pass parameters into the kernel like root=/dev/sda1, etc.
 */
_GLOBAL(cmd_line)
	.space	512

/*
 * Room for two PTE pointers, usually the kernel and current user pointers
 * to their respective root page table.
 */
abatron_pteptrs:
	.space	8

/*
 * This area is used for temporarily saving registers during the
 * critical exception prolog.
 */
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
