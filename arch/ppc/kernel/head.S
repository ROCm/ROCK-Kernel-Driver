/*
 *  arch/ppc/kernel/head.S
 *
 *  $Id: head.S,v 1.154 1999/10/12 00:33:31 cort Exp $
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
 *  MPC8xx modifications Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  This file contains the low-level support and setup for the
 *  PowerPC platform, including trap and interrupt dispatch.
 *  (The PPC 8xx embedded CPUs use head_8xx.S instead.)
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
#include "mol.h"

#ifdef CONFIG_APUS
#include <asm/amigappc.h>
#endif

#ifndef CONFIG_PPC64BRIDGE
CACHELINE_BYTES = 32
LG_CACHELINE_BYTES = 5
CACHELINE_MASK = 0x1f
CACHELINE_WORDS = 8
#else
CACHELINE_BYTES = 128
LG_CACHELINE_BYTES = 7
CACHELINE_MASK = 0x7f
CACHELINE_WORDS = 32
#endif /* CONFIG_PPC64BRIDGE */

#ifdef CONFIG_PPC64BRIDGE
#define LOAD_BAT(n, reg, RA, RB)	\
	ld	RA,(n*32)+0(reg);	\
	ld	RB,(n*32)+8(reg);	\
	mtspr	IBAT##n##U,RA;		\
	mtspr	IBAT##n##L,RB;		\
	ld	RA,(n*32)+16(reg);	\
	ld	RB,(n*32)+24(reg);	\
	mtspr	DBAT##n##U,RA;		\
	mtspr	DBAT##n##L,RB;		\
	
#else /* CONFIG_PPC64BRIDGE */
	
/* 601 only have IBAT; cr0.eq is set on 601 when using this macro */
#define LOAD_BAT(n, reg, RA, RB)	\
	/* see the comment for clear_bats() -- Cort */ \
	li	RA,0;			\
	mtspr	IBAT##n##U,RA;		\
	mtspr	DBAT##n##U,RA;		\
	lwz	RA,(n*16)+0(reg);	\
	lwz	RB,(n*16)+4(reg);	\
	mtspr	IBAT##n##U,RA;		\
	mtspr	IBAT##n##L,RB;		\
	beq	1f;			\
	lwz	RA,(n*16)+8(reg);	\
	lwz	RB,(n*16)+12(reg);	\
	mtspr	DBAT##n##U,RA;		\
	mtspr	DBAT##n##L,RB;		\
1:	
#endif /* CONFIG_PPC64BRIDGE */	 

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
	/* 
	 * These are here for legacy reasons, the kernel used to
	 * need to look like a coff function entry for the pmac
	 * but we're always started by some kind of bootloader now.
	 *  -- Cort
	 */
	nop
	nop
	nop

/* PMAC
 * Enter here with the kernel text, data and bss loaded starting at
 * 0, running with virtual == physical mapping.
 * r5 points to the prom entry point (the client interface handler
 * address).  Address translation is turned on, with the prom
 * managing the hash table.  Interrupts are disabled.  The stack
 * pointer (r1) points to just below the end of the half-meg region
 * from 0x380000 - 0x400000, which is mapped in already.
 *
 * If we are booted from MacOS via BootX, we enter with the kernel
 * image loaded somewhere, and the following values in registers:
 *  r3: 'BooX' (0x426f6f58)
 *  r4: virtual address of boot_infos_t
 *  r5: 0
 *
 * APUS
 *   r3: 'APUS'
 *   r4: physical address of memory base
 *   Linux/m68k style BootInfo structure at &_end.
 *
 * PREP
 * This is jumped to on prep systems right after the kernel is relocated
 * to its proper place in memory by the boot loader.  The expected layout
 * of the regs is:	
 *   r3: ptr to residual data
 *   r4: initrd_start or if no initrd then 0
 *   r5: initrd_end - unused if r4 is 0
 *   r6: Start of command line string
 *   r7: End of command line string
 *
 * This just gets a minimal mmu environment setup so we can call
 * start_here() to do the real work.
 * -- Cort
 */
	
	.globl	__start
__start:
/*
 * We have to do any OF calls before we map ourselves to KERNELBASE,
 * because OF may have I/O devices mapped into that area
 * (particularly on CHRP).
 */
	mr	r31,r3			/* save parameters */
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7
	li	r24,0			/* cpu # */
	bl	prom_init

#ifdef CONFIG_APUS
/* On APUS the __va/__pa constants need to be set to the correct 
 * values before continuing. 
 */
	mr	r4,r30
	bl	fix_mem_constants
#endif /* CONFIG_APUS */

#ifndef CONFIG_GEMINI
/* Switch MMU off, clear BATs and flush TLB. At this point, r3 contains
 * the physical address we are running at, returned by prom_init()
 */
 	bl	mmu_off
__after_mmu_off:
	bl	clear_bats
	bl	flush_tlbs
#endif

#ifndef CONFIG_POWER4
	/* POWER4 doesn't have BATs */
	bl	initial_bats
#if !defined(CONFIG_APUS) && defined(CONFIG_BOOTX_TEXT)
	bl	setup_disp_bat
#endif
#else /* CONFIG_POWER4 */
/*
 * Load up the SDR1 and segment register values now
 * since we don't have the BATs.
 */
	bl	reloc_offset
	addis	r4,r3,_SDR1@ha		/* get the value from _SDR1 */
	lwz	r4,_SDR1@l(r4)		/* assume hash table below 4GB */
	mtspr	SDR1,r4
	slbia
	lis	r5,0x2000		/* set pseudo-segment reg 12 */
	ori	r5,r5,12
	mtsr	12,r5
#endif /* CONFIG_POWER4 */

#ifndef CONFIG_APUS
/*
 * We need to run with _start at physical address 0.
 * On CHRP, we are loaded at 0x10000 since OF on CHRP uses
 * the exception vectors at 0 (and therefore this copy
 * overwrites OF's exception vectors with our own).
 * If the MMU is already turned on, we copy stuff to KERNELBASE,
 * otherwise we copy it to 0.
 */
	bl	reloc_offset
	mr	r26,r3
	addis	r4,r3,KERNELBASE@h	/* current address of _start */
	cmpwi	0,r4,0			/* are we already running at 0? */
	bne	relocate_kernel
#endif /* CONFIG_APUS */
/*
 * we now have the 1st 16M of ram mapped with the bats.
 * prep needs the mmu to be turned on here, but pmac already has it on.
 * this shouldn't bother the pmac since it just gets turned on again
 * as we jump to our code at KERNELBASE. -- Cort
 * Actually no, pmac doesn't have it on any more. BootX enters with MMU
 * off, and in other cases, we now turn it off before changing BATs above.
 */
turn_on_mmu:
	mfmsr	r0
	ori	r0,r0,MSR_DR|MSR_IR
	mtspr	SRR1,r0
	lis	r0,start_here@h
	ori	r0,r0,start_here@l
	mtspr	SRR0,r0
	SYNC
	RFI				/* enables MMU */

#ifdef CONFIG_SMP
	.globl	__secondary_hold
__secondary_hold:
	/* tell the master we're here */
	stw	r3,4(0)
100:	lwz	r4,0(0)
	/* wait until we're told to start */
	cmpw	0,r4,r3
	bne	100b
	/* our cpu # was at addr 0 - go */
	mr	r24,r3			/* cpu # */
	b	__secondary_start
#endif

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
1:	CLR_TOP32(r21);		\
	stw	r20,_CCR(r21);		/* save registers */ \
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

#define STD_MOL_EXCEPTION(n, label, hdlr, hook)	\
	. = n;					\
label:						\
	EXCEPTION_PROLOG;			\
	MOL_HOOK(hook); 			\
	addi	r3,r1,STACK_FRAME_OVERHEAD;	\
	li	r20,MSR_KERNEL;			\
	bl	transfer_to_handler; 		\
	.long	hdlr;				\
	.long	ret_from_except

/* System reset */
#ifdef CONFIG_SMP /* MVME/MTX and gemini start the secondary here */
#ifdef CONFIG_GEMINI
	. = 0x100
	b	__secondary_start_gemini
#else /* CONFIG_GEMINI */
	STD_EXCEPTION(0x100, Reset, __secondary_start_psurge)
#endif /* CONFIG_GEMINI */
#else
	STD_EXCEPTION(0x100, Reset, UnknownException)
#endif

/* Machine check */
	STD_EXCEPTION(0x200, MachineCheck, MachineCheckException)

/* Data access exception. */
	. = 0x300
#ifdef CONFIG_PPC64BRIDGE
	b	DataAccess
DataAccessCont:
#else
DataAccess:
	EXCEPTION_PROLOG
#endif /* CONFIG_PPC64BRIDGE */
	MOL_HOOK(0)
	mfspr	r20,DSISR
	andis.	r0,r20,0xa470		/* weird error? */
	bne	1f			/* if not, try to put a PTE */
	mfspr	r3,DAR			/* into the hash table */
	rlwinm	r4,r23,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	rlwimi	r4,r20,32-23,29,29	/* DSISR_STORE -> _PAGE_RW */
	bl	hash_page
1:	stw	r20,_DSISR(r21)
	mr	r5,r20
	mfspr	r4,DAR
	stw	r4,_DAR(r21)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	do_page_fault
	.long	ret_from_except

#ifdef CONFIG_PPC64BRIDGE
/* SLB fault on data access. */
	. = 0x380
	b	DataSegment
DataSegmentCont:
	mfspr	r4,DAR
	stw	r4,_DAR(r21)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	UnknownException
	.long	ret_from_except
#endif /* CONFIG_PPC64BRIDGE */

/* Instruction access exception. */
	. = 0x400
#ifdef CONFIG_PPC64BRIDGE
	b	InstructionAccess
InstructionAccessCont:
#else
InstructionAccess:
	EXCEPTION_PROLOG
#endif /* CONFIG_PPC64BRIDGE */
	MOL_HOOK(1)
	andis.	r0,r23,0x4000		/* no pte found? */
	beq	1f			/* if so, try to put a PTE */
	mr	r3,r22			/* into the hash table */
	rlwinm	r4,r23,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	mr	r20,r23			/* SRR1 has reason bits */
	bl	hash_page
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	mr	r4,r22
	mr	r5,r23
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	do_page_fault
	.long	ret_from_except

#ifdef CONFIG_PPC64BRIDGE
/* SLB fault on instruction access. */
	. = 0x480
	b	InstructionSegment
InstructionSegmentCont:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	UnknownException
	.long	ret_from_except
#endif /* CONFIG_PPC64BRIDGE */

/* External interrupt */
	. = 0x500;
HardwareInterrupt:
	EXCEPTION_PROLOG;
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
#ifndef CONFIG_APUS
	li	r4,0
	bl	transfer_to_handler
	.globl do_IRQ_intercept
do_IRQ_intercept:
	.long	do_IRQ;
	.long	ret_from_intercept
#else
	bl	apus_interrupt_entry
#endif /* CONFIG_APUS */

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
	MOL_HOOK(2)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	rlwimi	r20,r23,0,16,16		/* copy EE bit from saved MSR */
	bl	transfer_to_handler
	.long	ProgramCheckException
	.long	ret_from_except

/* Floating-point unavailable */
	. = 0x800
FPUnavailable:
	EXCEPTION_PROLOG
	MOL_HOOK_RESTORE(3)
	bne	load_up_fpu		/* if from user, just load it up */
	li	r20,MSR_KERNEL
	bl	transfer_to_handler	/* if from kernel, take a trap */
	.long	KernelFP
	.long	ret_from_except

	. = 0x900
Decrementer:
	EXCEPTION_PROLOG
	MOL_HOOK(4)	
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
	STD_MOL_EXCEPTION(0xd00, SingleStep, SingleStepException, 5)
	STD_EXCEPTION(0xe00, Trap_0e, UnknownException)

/*
 * The Altivec unavailable trap is at 0x0f20.  Foo.
 * We effectively remap it to 0x3000.
 */
	. = 0xf00
	b	Trap_0f
trap_0f_cont:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r20,MSR_KERNEL
	bl	transfer_to_handler
	.long	UnknownException
	.long	ret_from_except

	. = 0xf20
#ifdef CONFIG_ALTIVEC
	b	AltiVecUnavailable
#endif
Trap_0f:
	EXCEPTION_PROLOG
	b	trap_0f_cont
	
/*
 * Handle TLB miss for instruction on 603/603e.
 * Note: we get an alternate set of r0 - r3 to use automatically.
 */
	. = 0x1000
InstructionTLBMiss:
	MOL_HOOK_TLBMISS( 14 )
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r3,IMISS
	lis	r1,KERNELBASE@h		/* check if kernel address */
	cmplw	0,r3,r1
	mfspr	r2,SPRG3
	lwz	r2,PGDIR(r2)
	blt+	112f
	lis	r2,swapper_pg_dir@ha	/* if kernel address, use */
	addi	r2,r2,swapper_pg_dir@l	/* kernel page table */
112:	tophys(r2,r2)
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	InstructionAddressInvalid	/* return if no mapping */
	tophys(r2,r2)
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r1,0(r2)		/* get linux-style pte */
	/* setup access flags in r3 */
	mfmsr	r3
	rlwinm	r3,r3,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	ori	r3,r3,1			/* set _PAGE_PRESENT bit in access */
	andc.	r3,r3,r1		/* check access & ~permission */
	bne-	InstructionAddressInvalid /* return if access not permitted */
	ori	r1,r1,0x100		/* set _PAGE_ACCESSED in pte */
	stw	r1,0(r2)		/* update PTE (accessed bit) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	/* this computation could be done better -- Cort */
	rlwinm	r3,r1,32-9,31,31	/* _PAGE_HWWRITE -> PP lsb */
	rlwimi	r1,r1,32-1,31,31	/* _PAGE_USER -> PP (both bits now) */
	ori	r3,r3,0xe04		/* clear out reserved bits */
	andc	r1,r1,r3		/* PP=2 or 0, when _PAGE_HWWRITE */
	mtspr	RPA,r1
	mfspr	r3,IMISS
	tlbli	r3
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi
InstructionAddressInvalid:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */

	addis	r1,r1,0x2000
	mtspr	DSISR,r1	/* (shouldn't be needed) */
	mtctr	r0		/* Restore CTR */
	andi.	r2,r3,0xFFFF	/* Clear upper bits of SRR1 */
	or	r2,r2,r1
	mtspr	SRR1,r2
	mfspr	r1,IMISS	/* Get failing address */
	rlwinm.	r2,r2,0,31,31	/* Check for little endian access */
	rlwimi	r2,r2,1,30,30	/* change 1 -> 3 */
	xor	r1,r1,r2
	mtspr	DAR,r1		/* Set fault address */
	mfmsr	r0		/* Restore "normal" registers */
	xoris	r0,r0,MSR_TGPR>>16
	mtcrf	0x80,r3		/* Restore CR0 */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0
	b	InstructionAccess

/*
 * Handle TLB miss for DATA Load operation on 603/603e
 */
	. = 0x1100
DataLoadTLBMiss:
	MOL_HOOK_TLBMISS( 15 )
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r3,DMISS
	lis	r1,KERNELBASE@h		/* check if kernel address */
	cmplw	0,r3,r1
	mfspr	r2,SPRG3
	lwz	r2,PGDIR(r2)
	blt+	112f
	lis	r2,swapper_pg_dir@ha	/* if kernel address, use */
	addi	r2,r2,swapper_pg_dir@l	/* kernel page table */
112:	tophys(r2,r2)
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	DataAddressInvalid	/* return if no mapping */
	tophys(r2,r2)
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r1,0(r2)		/* get linux-style pte */
	/* setup access flags in r3 */
	mfmsr	r3
	rlwinm	r3,r3,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	ori	r3,r3,1			/* set _PAGE_PRESENT bit in access */
	/* save r2 and use it as scratch for the andc. */	
	andc.	r3,r3,r1		/* check access & ~permission */
	bne-	DataAddressInvalid	/* return if access not permitted */
	ori	r1,r1,0x100		/* set _PAGE_ACCESSED in pte */
	stw	r1,0(r2)		/* update PTE (accessed bit) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	/* this computation could be done better -- Cort */
	rlwinm	r3,r1,32-9,31,31	/* _PAGE_HWWRITE -> PP lsb */
	rlwimi	r1,r1,32-1,31,31	/* _PAGE_USER -> PP (both bits now) */
	ori	r3,r3,0xe04		/* clear out reserved bits */
	andc	r1,r1,r3		/* PP=2 or 0, when _PAGE_HWWRITE */
	mtspr	RPA,r1
	mfspr	r3,DMISS
	tlbld	r3
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi
DataAddressInvalid:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
	addis	r1,r1,0x2000
	mtspr	DSISR,r1
	mtctr	r0		/* Restore CTR */
	andi.	r2,r3,0xFFFF	/* Clear upper bits of SRR1 */
	mtspr	SRR1,r2
	mfspr	r1,DMISS	/* Get failing address */
	rlwinm.	r2,r2,0,31,31	/* Check for little endian access */
	beq	20f		/* Jump if big endian */
	xori	r1,r1,3
20:	mtspr	DAR,r1		/* Set fault address */
	mfmsr	r0		/* Restore "normal" registers */
	xoris	r0,r0,MSR_TGPR>>16
	mtcrf	0x80,r3		/* Restore CR0 */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0
	b	DataAccess
	
/*
 * Handle TLB miss for DATA Store on 603/603e
 */
	. = 0x1200
DataStoreTLBMiss:
	MOL_HOOK_TLBMISS( 16 )
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r3,DMISS
	lis	r1,KERNELBASE@h		/* check if kernel address */
	cmplw	0,r3,r1
	mfspr	r2,SPRG3
	lwz	r2,PGDIR(r2)
	blt+	112f
	lis	r2,swapper_pg_dir@ha	/* if kernel address, use */
	addi	r2,r2,swapper_pg_dir@l	/* kernel page table */
112:	tophys(r2,r2)
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	DataAddressInvalid	/* return if no mapping */
	tophys(r2,r2)
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r1,0(r2)		/* get linux-style pte */
	/* setup access flags in r3 */
	mfmsr	r3
	rlwinm	r3,r3,32-13,30,30	/* MSR_PR -> _PAGE_USER */
	ori	r3,r3,0x5		/* _PAGE_PRESENT|_PAGE_RW */
	/* save r2 and use it as scratch for the andc. */	
	andc.	r3,r3,r1		/* check access & ~permission */
	bne-	DataAddressInvalid	/* return if access not permitted */
	ori	r1,r1,0x384		/* set _PAGE_ACCESSED|_PAGE_DIRTY|_PAGE_RW|_PAGE_HWWRITE in pte */
	stw	r1,0(r2)		/* update PTE (accessed bit) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	/* this computation could be done better -- Cort */
	rlwinm	r3,r1,32-9,31,31	/* _PAGE_HWWRITE -> PP lsb */
	rlwimi	r1,r1,32-1,31,31	/* _PAGE_USER -> PP (both bits now) */
	ori	r3,r3,0xe04		/* clear out reserved bits */
	andc	r1,r1,r3		/* PP=2 or 0, when _PAGE_HWWRITE */
	mtspr	RPA,r1
	mfspr	r3,DMISS
	tlbld	r3
	mfspr	r3,SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi

	STD_MOL_EXCEPTION(0x1300, Trap_13, InstructionBreakpoint, 11)
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
	STD_MOL_EXCEPTION(0x2000, RunMode, RunModeException, 5)
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

	. = 0x3000

#ifdef CONFIG_ALTIVEC
AltiVecUnavailable:
	EXCEPTION_PROLOG
	MOL_HOOK_RESTORE(12)
	bne	load_up_altivec		/* if from user, just load it up */
	li	r20,MSR_KERNEL
	bl	transfer_to_handler	/* if from kernel, take a trap */
	.long	KernelAltiVec
	.long	ret_from_except
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_PPC64BRIDGE
DataAccess:
	EXCEPTION_PROLOG
	b	DataAccessCont
InstructionAccess:
	EXCEPTION_PROLOG
	b	InstructionAccessCont
DataSegment:
	EXCEPTION_PROLOG
	b	DataSegmentCont
InstructionSegment:
	EXCEPTION_PROLOG
	b	InstructionSegmentCont
#endif /* CONFIG_PPC64BRIDGE */

/*
 * This code finishes saving the registers to the exception frame
 * and jumps to the appropriate handler for the exception, turning
 * on address translation.
 */
	.globl	transfer_to_handler
transfer_to_handler:
	stw	r22,_NIP(r21)
	stw	r23,_MSR(r21)
	SAVE_4GPRS(8, r21)
	SAVE_8GPRS(12, r21)
	SAVE_8GPRS(24, r21)
	andi.	r23,r23,MSR_PR
	mfspr	r23,SPRG3		/* if from user, fix up THREAD.regs */
	beq	2f
	addi	r24,r1,STACK_FRAME_OVERHEAD
	stw	r24,PT_REGS(r23)
#ifdef CONFIG_ALTIVEC
	mfpvr	r24			/* check if we are on a G4 */
	srwi	r24,r24,16
	cmpwi	r24,PVR_7400@h
	bne	2f
	mfspr	r22,SPRN_VRSAVE		/* if so, save vrsave register value */
	stw	r22,THREAD_VRSAVE(r23)
#endif /* CONFIG_ALTIVEC */
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
	FIX_SRR1(r20,r22)
	MOL_HOOK(6)
	mtspr	SRR0,r24
	mtspr	SRR1,r20
	mtlr	r23
	SYNC
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
	lis	r24,StackOverflow@ha
	addi	r24,r24,StackOverflow@l
	li	r20,MSR_KERNEL
	FIX_SRR1(r20,r22)
	mtspr	SRR0,r24
	mtspr	SRR1,r20
	SYNC
	RFI

/*
 * Disable FP for the task which had the FPU previously,
 * and save its floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 * On SMP we know the fpu is free, since we give it up every
 * switch.  -- Cort
 */
load_up_fpu:
	mfmsr	r5
	ori	r5,r5,MSR_FP
#ifdef CONFIG_PPC64BRIDGE
	clrldi	r5,r5,1			/* turn off 64-bit mode */
#endif /* CONFIG_PPC64BRIDGE */
	SYNC
	MTMSRD(r5)			/* enable use of fpu now */
	SYNC
/*
 * For SMP, we don't do lazy FPU switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_fpu in switch_to.
 */
#ifndef CONFIG_SMP
	lis	r6,0                    /* get __pa constant */
	tophys(r6,r6)
	addis	r3,r6,last_task_used_math@ha
	lwz	r4,last_task_used_math@l(r3)
	cmpi	0,r4,0
	beq	1f
	add	r4,r4,r6
	addi	r4,r4,THREAD	        /* want THREAD of last_task_used_math */
	SAVE_32FPRS(0, r4)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR-4(r4)
	lwz	r5,PT_REGS(r4)
	add	r5,r5,r6
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
	sub	r4,r4,r6
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

#ifdef CONFIG_ALTIVEC
/* Note that the AltiVec support is closely modeled after the FP
 * support.  Changes to one are likely to be applicable to the
 * other!  */
load_up_altivec:
/*
 * Disable AltiVec for the task which had AltiVec previously,
 * and save its AltiVec registers in its thread_struct.
 * Enables AltiVec for use in the kernel on return.
 * On SMP we know the AltiVec units are free, since we give it up every
 * switch.  -- Kumar
 */
	mfmsr	r5
	oris	r5,r5,MSR_VEC@h
	SYNC
	mtmsr	r5			/* enable use of AltiVec now */
	SYNC
/*
 * For SMP, we don't do lazy AltiVec switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_altivec in switch_to.
 */
#ifndef CONFIG_SMP
#ifndef CONFIG_APUS
	lis	r6,-KERNELBASE@h
#else
	lis	r6,CYBERBASEp@h
	lwz	r6,0(r6)
#endif
	addis	r3,r6,last_task_used_altivec@ha
	lwz	r4,last_task_used_altivec@l(r3)
	cmpi	0,r4,0
	beq	1f
	add	r4,r4,r6
	addi	r4,r4,THREAD	/* want THREAD of last_task_used_altivec */
	SAVE_32VR(0,r20,r4)
	MFVSCR(vr0)
	li	r20,THREAD_VSCR
	STVX(vr0,r20,r4)
	lwz	r5,PT_REGS(r4)
	add	r5,r5,r6
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r20,MSR_VEC@h
	andc	r4,r4,r20	/* disable altivec for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	/* enable use of AltiVec after return */
	oris	r23,r23,MSR_VEC@h
	mfspr	r5,SPRG3		/* current task's THREAD (phys) */
	li	r20,THREAD_VSCR
	LVX(vr0,r20,r5)
	MTVSCR(vr0)
	REST_32VR(0,r20,r5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	sub	r4,r4,r6
	stw	r4,last_task_used_altivec@l(r3)
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
 * AltiVec unavailable trap from kernel - print a message, but let
 * the task use AltiVec in the kernel until it returns to user mode.
 */
KernelAltiVec:
	lwz	r3,_MSR(r1)
	oris	r3,r3,MSR_VEC@h
	stw	r3,_MSR(r1)	/* enable use of AltiVec after return */
	lis	r3,87f@h
	ori	r3,r3,87f@l
	mr	r4,r2		/* current */
	lwz	r5,_NIP(r1)
	bl	printk
	b	ret_from_except
87:	.string	"AltiVec used in kernel  (task=%p, pc=%x)  \n"
	.align	4

/*
 * giveup_altivec(tsk)
 * Disable AltiVec for the task given as the argument,
 * and save the AltiVec registers in its thread_struct.
 * Enables AltiVec for use in the kernel on return.
 */

	.globl	giveup_altivec
giveup_altivec:
#ifdef CONFIG_MOL
	mflr	r4
	MOL_HOOK_MMU(13, r5)
	mtlr	r4
#endif
	mfmsr	r5
	oris	r5,r5,MSR_VEC@h
	SYNC
	mtmsr	r5			/* enable use of AltiVec now */
	SYNC
	cmpi	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD		/* want THREAD of task */
	lwz	r5,PT_REGS(r3)
	cmpi	0,r5,0
	SAVE_32VR(0, r4, r3)
	MFVSCR(vr0)
	li	r4,THREAD_VSCR
	STVX(vr0, r4, r3)
	beq	1f
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r3,MSR_VEC@h
	andc	r4,r4,r3		/* disable AltiVec for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	lis	r4,last_task_used_altivec@ha
	stw	r5,last_task_used_altivec@l(r4)
#endif /* CONFIG_SMP */
	blr
#endif /* CONFIG_ALTIVEC */
	
/*
 * giveup_fpu(tsk)
 * Disable FP for the task given as the argument,
 * and save the floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 */
	.globl	giveup_fpu
giveup_fpu:
#ifdef CONFIG_MOL
	mflr	r4
	MOL_HOOK_MMU(7, r5)
	mtlr	r4
#endif		
	mfmsr	r5
	ori	r5,r5,MSR_FP
	SYNC
	mtmsr	r5			/* enable use of fpu now */
	SYNC
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

/*
 * This code is jumped to from the startup code to copy
 * the kernel image to physical address 0.
 */
relocate_kernel:
	addis	r9,r26,klimit@ha	/* fetch klimit */
	lwz	r25,klimit@l(r9)
	addis	r25,r25,-KERNELBASE@h
	li	r3,0			/* Destination base address */
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

#ifdef CONFIG_APUS
/*
 * On APUS the physical base address of the kernel is not known at compile
 * time, which means the __pa/__va constants used are incorect. In the
 * __init section is recorded the virtual addresses of instructions using
 * these constants, so all that has to be done is fix these before
 * continuing the kernel boot.
 *
 * r4 = The physical address of the kernel base.
 */
fix_mem_constants:
	mr	r10,r4
	addis	r10,r10,-KERNELBASE@h    /* virt_to_phys constant */
	neg	r11,r10	                 /* phys_to_virt constant */

	lis	r12,__vtop_table_begin@h
	ori	r12,r12,__vtop_table_begin@l
	add	r12,r12,r10	         /* table begin phys address */
	lis	r13,__vtop_table_end@h
	ori	r13,r13,__vtop_table_end@l
	add	r13,r13,r10	         /* table end phys address */
	subi	r12,r12,4
	subi	r13,r13,4
1:	lwzu	r14,4(r12)               /* virt address of instruction */
	add     r14,r14,r10              /* phys address of instruction */
	lwz     r15,0(r14)               /* instruction, now insert top */
	rlwimi  r15,r10,16,16,31         /* half of vp const in low half */
	stw	r15,0(r14)               /* of instruction and restore. */
	dcbst	r0,r14			 /* write it to memory */
	sync
	icbi	r0,r14			 /* flush the icache line */
	cmpw	r12,r13
	bne     1b

/*
 * Map the memory where the exception handlers will
 * be copied to when hash constants have been patched.  
 */
#ifdef CONFIG_APUS_FAST_EXCEPT
	lis	r8,0xfff0
#else
	lis	r8,0
#endif
	ori	r8,r8,0x2		/* 128KB, supervisor */
	mtspr	DBAT3U,r8
	mtspr	DBAT3L,r8

	lis	r12,__ptov_table_begin@h
	ori	r12,r12,__ptov_table_begin@l
	add	r12,r12,r10	         /* table begin phys address */
	lis	r13,__ptov_table_end@h
	ori	r13,r13,__ptov_table_end@l
	add	r13,r13,r10	         /* table end phys address */
	subi	r12,r12,4
	subi	r13,r13,4
1:	lwzu	r14,4(r12)               /* virt address of instruction */
	add     r14,r14,r10              /* phys address of instruction */
	lwz     r15,0(r14)               /* instruction, now insert top */
	rlwimi  r15,r11,16,16,31         /* half of pv const in low half*/
	stw	r15,0(r14)               /* of instruction and restore. */
	dcbst	r0,r14			 /* write it to memory */
	sync
	icbi	r0,r14			 /* flush the icache line */
	cmpw	r12,r13
	bne     1b

	isync				/* No speculative loading until now */
	blr
	
apus_interrupt_entry:
	/* This is horrible, but there's no way around it. Enable the
	 * data cache so the IRQ hardware register can be accessed
	 * without cache intervention. Then disable interrupts and get
	 * the current emulated m68k IPL value. 
	 */
	
	mfmsr	20
	xori	r20,r20,MSR_DR
	sync
	mtmsr	r20
	sync

	lis	r4,APUS_IPL_EMU@h

	li	r20,(IPLEMU_SETRESET|IPLEMU_DISABLEINT)
	stb	r20,APUS_IPL_EMU@l(r4)
	eieio

	lbz	r3,APUS_IPL_EMU@l(r4)

	li	r2,IPLEMU_IPLMASK
	rlwinm. r20,r3,32-3,29,31
	bne	2f
	mr	r20,r2		/* lvl7! Need to reset state machine. */
	b	3f
2:	cmp	0,r20,r2
	beq	1f
3:	eieio
	stb     r2,APUS_IPL_EMU@l(r4)
	ori	r20,r20,IPLEMU_SETRESET
	eieio
	stb     r20,APUS_IPL_EMU@l(r4)
1:	eieio
	li	r20,IPLEMU_DISABLEINT
	stb	r20,APUS_IPL_EMU@l(r4)

	/* At this point we could do some magic to avoid the overhead
	 * of calling the C interrupt handler in case of a spurious
	 * interrupt. Could not get a simple hack to work though.
	 */
	
	mfmsr	r20
	xori	r20,r20,MSR_DR
	sync
	mtmsr	r20
	sync

	stw	r3,(_CCR+4)(r21);

	addi	r3,r1,STACK_FRAME_OVERHEAD;
	li	r20,MSR_KERNEL;
	bl	transfer_to_handler;
	.long	do_IRQ;
	.long	ret_from_except

/***********************************************************************
 *  Please note that on APUS the exception handlers are located at the
 *  physical address 0xfff0000. For this reason, the exception handlers
 *  cannot use relative branches to access the code below.
 ***********************************************************************/
#endif /* CONFIG_APUS */

#ifdef CONFIG_SMP
#ifdef CONFIG_GEMINI
	.globl	__secondary_start_gemini
__secondary_start_gemini:
        mfspr   r4,HID0
        ori     r4,r4,HID0_ICFI
        li      r3,0
        ori     r3,r3,HID0_ICE
        andc    r4,r4,r3
        mtspr   HID0,r4
        sync
        bl      prom_init
        b       __secondary_start
#endif /* CONFIG_GEMINI */
	
	.globl	__secondary_start_psurge
__secondary_start_psurge:
	li	r24,1			/* cpu # */
	/* we come in here with IR=0 and DR=1, and DBAT 0
	   set to map the 0xf0000000 - 0xffffffff region */
	mfmsr	r0
	rlwinm	r0,r0,0,28,26		/* clear DR (0x10) */
	sync
	mtmsr	r0
	isync

	.globl	__secondary_start
__secondary_start:
#ifdef CONFIG_PPC64BRIDGE
	mfmsr	r0
	clrldi	r0,r0,1		/* make sure it's in 32-bit mode */
	sync
	MTMSRD(r0)
	isync
#else
	bl	enable_caches
#endif

	/* get current */
	lis	r2,current_set@h
	ori	r2,r2,current_set@l
	tophys(r2,r2)
	slwi	r24,r24,2		/* get current_set[cpu#] */
	lwzx	r2,r2,r24

	/* stack */
	addi	r1,r2,TASK_UNION_SIZE-STACK_FRAME_OVERHEAD
	li	r0,0
	tophys(r3,r1)
	stw	r0,0(r3)

	/* load up the MMU */
	bl	load_up_mmu

	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* phys address of our thread_struct */
	CLR_TOP32(r4)
	mtspr	SPRG3,r4
	li	r3,0
	mtspr	SPRG2,r3	/* 0 => r1 has kernel sp */

	/* enable MMU and jump to start_secondary */
	li	r4,MSR_KERNEL
	lis	r3,start_secondary@h
	ori	r3,r3,start_secondary@l
	mtspr	SRR0,r3
	mtspr	SRR1,r4
	SYNC
	RFI
#endif /* CONFIG_SMP */

/*
 * Enable caches and 604-specific features if necessary.
 */
enable_caches:
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31
	cmpi	0,r9,1
	beq	6f			/* not needed for 601 */
	mfspr	r11,HID0
	andi.	r0,r11,HID0_DCE
	ori	r11,r11,HID0_ICE|HID0_DCE
	ori	r8,r11,HID0_ICFI
	bne	3f			/* don't invalidate the D-cache */
	ori	r8,r8,HID0_DCI		/* unless it wasn't enabled */
3:
	sync
	mtspr	HID0,r8			/* enable and invalidate caches */
	sync
	mtspr	HID0,r11		/* enable caches */
	sync
	isync
	cmpi	0,r9,4			/* check for 604 */
	cmpi	1,r9,9			/* or 604e */
	cmpi	2,r9,10			/* or mach5 / 604r */
	cmpi	3,r9,8			/* check for 750 (G3) */
	cmpi	4,r9,12			/* or 7400 (G4) */
	cror	2,2,6
	cror	2,2,10
	bne	4f
	ori	r11,r11,HID0_SIED|HID0_BHTE /* for 604[e|r], enable */
	bne	2,5f
	ori	r11,r11,HID0_BTCD	/* superscalar exec & br history tbl */
	b	5f
4:
	cror	14,14,18
	bne	3,6f
	/* for G3/G4:
	 * enable Store Gathering (SGE), Address Brodcast (ABE),
	 * Branch History Table (BHTE), Branch Target ICache (BTIC)
	 */
	ori	r11,r11,HID0_SGE | HID0_ABE | HID0_BHTE | HID0_BTIC
	oris	r11,r11,HID0_DPM@h	/* enable dynamic power mgmt */
	li	r3,HID0_SPD
	andc	r11,r11,r3		/* clear SPD: enable speculative */
 	li	r3,0
 	mtspr	ICTC,r3			/* Instruction Cache Throttling off */
5:	isync
	mtspr	HID0,r11
	sync
	isync
6:	blr

/*
 * Load stuff into the MMU.  Intended to be called with
 * IR=0 and DR=0.
 */
load_up_mmu:
	/* Load the SDR1 register (hash table base & size) */
	lis	r6,_SDR1@ha
	tophys(r6,r6)
	lwz	r6,_SDR1@l(r6)
	mtspr	SDR1,r6
#ifdef CONFIG_PPC64BRIDGE	
	/* clear the ASR so we only use the pseudo-segment registers. */
	li	r6,0
	mtasr	r6
#endif /* CONFIG_PPC64BRIDGE */
	li	r0,16		/* load up segment register values */
	mtctr	r0		/* for context 0 */
	lis	r3,0x2000	/* Ku = 1, VSID = 0 */
	li	r4,0
3:	mtsrin	r3,r4
	addi	r3,r3,1		/* increment VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
#ifndef CONFIG_POWER4
/* Load the BAT registers with the values set up by MMU_init.
   MMU_init takes care of whether we're on a 601 or not. */
	mfpvr	r3
	srwi	r3,r3,16
	cmpwi	r3,1
	lis	r3,BATS@ha
	addi	r3,r3,BATS@l
	tophys(r3,r3)
	LOAD_BAT(0,r3,r4,r5)
	LOAD_BAT(1,r3,r4,r5)
	LOAD_BAT(2,r3,r4,r5)
	LOAD_BAT(3,r3,r4,r5)
#endif /* CONFIG_POWER4 */
	blr

/*
 * This is where the main kernel code starts.
 */
start_here:
#ifndef CONFIG_PPC64BRIDGE
	bl	enable_caches
#endif

	/* ptr to current */
	lis	r2,init_task_union@h
	ori	r2,r2,init_task_union@l
	/* Set up for using our exception vectors */
	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* init task's THREAD */
	CLR_TOP32(r4)
	mtspr	SPRG3,r4
	li	r3,0
	mtspr	SPRG2,r3	/* 0 => r1 has kernel sp */

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

#ifdef CONFIG_APUS
	/* Copy exception code to exception vector base on APUS. */
	lis	r4,KERNELBASE@h
#ifdef CONFIG_APUS_FAST_EXCEPT
	lis	r3,0xfff0		/* Copy to 0xfff00000 */
#else
	lis	r3,0			/* Copy to 0x00000000 */
#endif
	li	r5,0x4000		/* # bytes of memory to copy */
	li	r6,0
	bl	copy_and_flush		/* copy the first 0x4000 bytes */
#endif  /* CONFIG_APUS */

/*
 * Go back to running unmapped so we can load up new values
 * for SDR1 (hash table pointer) and the segment registers
 * and change to using our exception vectors.
 */
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	FIX_SRR1(r3,r5)
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	SYNC
	RFI
/* Load up the kernel context */
2:
	SYNC			/* Force all PTE updates to finish */
	tlbia			/* Clear all TLB entries */
	sync			/* wait for tlbia/tlbie to finish */
#ifdef CONFIG_SMP
	tlbsync			/* ... on all CPUs */
	sync
#endif

	bl	load_up_mmu
/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	FIX_SRR1(r4,r5)
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
	mtspr	SRR0,r3
	mtspr	SRR1,r4
	SYNC
	RFI

/*
 * Set up the segment registers for a new context.
 */
_GLOBAL(set_context)
	rlwinm	r3,r3,4,8,27	/* VSID = context << 4 */
	addis	r3,r3,0x6000	/* Set Ks, Ku bits */
	li	r0,12		/* TASK_SIZE / SEGMENT_SIZE */
	mtctr	r0
	li	r4,0
3:
#ifdef CONFIG_PPC64BRIDGE
	slbie	r4
#endif /* CONFIG_PPC64BRIDGE */
	mtsrin	r3,r4
	addi	r3,r3,1		/* next VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	SYNC
	blr

/* 
 * An undocumented "feature" of 604e requires that the v bit
 * be cleared before changing BAT values.
 *
 * Also, newer IBM firmware does not clear bat3 and 4 so
 * this makes sure it's done.
 *  -- Cort 
 */
clear_bats:
#if !defined(CONFIG_GEMINI)
	li	r20,0
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpwi	r9, 1
	beq	1f
	
	mtspr	DBAT0U,r20
	mtspr	DBAT0L,r20	
	mtspr	DBAT1U,r20
	mtspr	DBAT1L,r20
	mtspr	DBAT2U,r20
	mtspr	DBAT2L,r20	
	mtspr	DBAT3U,r20
	mtspr	DBAT3L,r20
1:	
	mtspr	IBAT0U,r20
	mtspr	IBAT0L,r20
	mtspr	IBAT1U,r20
	mtspr	IBAT1L,r20
	mtspr	IBAT2U,r20
	mtspr	IBAT2L,r20
	mtspr	IBAT3U,r20
	mtspr	IBAT3L,r20
#endif /* !defined(CONFIG_GEMINI) */
	blr

#ifndef CONFIG_GEMINI
flush_tlbs:
	lis	r20, 0x40
1:	addic.	r20, r20, -0x1000
	tlbie	r20
	blt	1b
	sync
	blr

mmu_off:
 	addi	r4, r3, __after_mmu_off - _start
	mfmsr	r3
	andi.	r0,r3,MSR_DR|MSR_IR		/* MMU enabled? */
	beqlr
	andc	r3,r3,r0
	mtspr	SRR0,r4
	mtspr	SRR1,r3
	sync
	RFI
#endif

#ifndef CONFIG_POWER4	
/*
 * Use the first pair of BAT registers to map the 1st 16MB
 * of RAM to KERNELBASE.  From this point on we can't safely
 * call OF any more.
 */
initial_bats:
	lis	r11,KERNELBASE@h
#ifndef CONFIG_PPC64BRIDGE
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpi	0,r9,1
	bne	4f
	ori	r11,r11,4		/* set up BAT registers for 601 */
	li	r8,0x7f			/* valid, block length = 8MB */
	oris	r9,r11,0x800000@h	/* set up BAT reg for 2nd 8M */
	oris	r10,r8,0x800000@h	/* set up BAT reg for 2nd 8M */
	mtspr	IBAT0U,r11		/* N.B. 601 has valid bit in */
	mtspr	IBAT0L,r8		/* lower BAT register */
	mtspr	IBAT1U,r9
	mtspr	IBAT1L,r10
	isync
	blr
#endif /* CONFIG_PPC64BRIDGE */

4:	tophys(r8,r11)
#ifdef CONFIG_SMP
	ori	r8,r8,0x12		/* R/W access, M=1 */
#else
	ori	r8,r8,2			/* R/W access */
#endif /* CONFIG_SMP */
#ifdef CONFIG_APUS
	ori	r11,r11,BL_8M<<2|0x2	/* set up 8MB BAT registers for 604 */
#else
	ori	r11,r11,BL_256M<<2|0x2	/* set up BAT registers for 604 */
#endif /* CONFIG_APUS */
	
#ifdef CONFIG_PPC64BRIDGE
	/* clear out the high 32 bits in the BAT */
	clrldi	r11,r11,32
	clrldi	r8,r8,32
#endif /* CONFIG_PPC64BRIDGE */
	mtspr	DBAT0L,r8		/* N.B. 6xx (not 601) have valid */
	mtspr	DBAT0U,r11		/* bit in upper BAT register */
	mtspr	IBAT0L,r8
	mtspr	IBAT0U,r11
	isync
	blr

#if !defined(CONFIG_APUS) && defined(CONFIG_BOOTX_TEXT)
setup_disp_bat:
	/*
	 * setup the display bat prepared for us in prom.c
	 */
	mflr	r8
	bl	reloc_offset
	mtlr	r8
	addis	r8,r3,disp_BAT@ha
	addi	r8,r8,disp_BAT@l
	lwz	r11,0(r8)
	lwz	r8,4(r8)
	mfspr	r9,PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpi	0,r9,1
	beq	1f
	mtspr	DBAT3L,r8
	mtspr	DBAT3U,r11
	blr
1:	mtspr	IBAT3L,r8
	mtspr	IBAT3U,r11
	blr

#endif /* !defined(CONFIG_APUS) && defined(CONFIG_BOOTX_TEXT) */
#endif /* CONFIG_POWER4 */

#ifdef CONFIG_8260
/* Jump into the system reset for the rom.
 * We first disable the MMU, and then jump to the ROM reset address.
 *
 * r3 is the board info structure, r4 is the location for starting.
 * I use this for building a small kernel that can load other kernels,
 * rather than trying to write or rely on a rom monitor that can tftp load.
 */
       .globl  m8260_gorom
m8260_gorom:
	mfmsr	r0
	rlwinm	r0,r0,0,17,15	/* clear MSR_EE in r0 */
	sync
	mtmsr	r0
	sync
	mfspr	r11, HID0
	lis	r10, 0
	ori	r10,r10,HID0_ICE|HID0_DCE
	andc	r11, r11, r10
	mtspr	HID0, r11
	isync
	li	r5, MSR_
	lis	r6,2f@h
	addis	r6,r6,-KERNELBASE@h
	ori	r6,r6,2f@l
	mtspr	SRR0,r6
	mtspr	SRR1,r5
	isync
	sync
	rfi
2:
	mtlr	r4
	blr
#endif

#ifdef CONFIG_MOL
/*
 * Mac-on-linux hook_table. Don't put this in the data section -
 * the base address must be within the first 32KB of RAM.
 */
	.globl mol_interface
mol_interface:
	.long   MOL_INTERFACE_VERSION
	.fill	24,4,0		/* space for 24 hooks */
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
