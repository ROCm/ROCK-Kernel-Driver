/*
 * This file contains sleep low-level functions for PowerBook G3.
 *    Copyright (C) 1999 Benjamin Herrenschmidt (bh40@calva.net)
 *    and Paul Mackerras (paulus@cs.anu.edu.au).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include "ppc_asm.tmpl"
#include <asm/processor.h>
#include <asm/page.h>

#define MAGIC	0x4c617273	/* 'Lars' */

/*
 * Structure for storing CPU registers on the stack.
 */
#define SL_SP		0
#define SL_PC		4
#define SL_MSR		8
#define SL_SDR1		0xc
#define SL_SPRG0	0x10	/* 4 sprg's */
#define SL_DBAT0	0x20
#define SL_IBAT0	0x28
#define SL_DBAT1	0x30
#define SL_IBAT1	0x38
#define SL_DBAT2	0x40
#define SL_IBAT2	0x48
#define SL_DBAT3	0x50
#define SL_IBAT3	0x58
#define SL_TB		0x60
#define SL_HID0		0x68
#define SL_R2		0x6c
#define SL_R12		0x70	/* r12 to r31 */
#define SL_SIZE		(SL_R12 + 80)

#define tophys(rd,rs)   addis   rd,rs,-KERNELBASE@h     
#define tovirt(rd,rs)	addis	rd,rs,KERNELBASE@h

	.text

_GLOBAL(low_sleep_handler)
	mflr	r0
	stw	r0,4(r1)
	stwu	r1,-SL_SIZE(r1)
	stw	r2,SL_R2(r1)
	stmw	r12,SL_R12(r1)

	/* Save MSR, SDR1, TB  */
	mfmsr	r4
	stw	r4,SL_MSR(r1)
	mfsdr1	r4
	stw	r4,SL_SDR1(r1)
1:	
	mftbu	r4
	stw	r4,SL_TB(r1)
	mftb	r5
	stw	r5,SL_TB+4(r1)
	mftbu	r3
	cmpw	r3,r4
	bne	1b
	
	/* Save SPRGs */
	mfsprg	r4,0
	stw	r4,SL_SPRG0(r1)
	mfsprg	r4,1
	stw	r4,SL_SPRG0+4(r1)
	mfsprg	r4,2
	stw	r4,SL_SPRG0+8(r1)
	mfsprg	r4,3
	stw	r4,SL_SPRG0+12(r1)

	/* Save BATs */
	mfdbatu	r4,0
	stw	r4,SL_DBAT0(r1)
	mfdbatl	r4,0
	stw	r4,SL_DBAT0+4(r1)
	mfdbatu	r4,1
	stw	r4,SL_DBAT1(r1)
	mfdbatl	r4,1
	stw	r4,SL_DBAT1+4(r1)
	mfdbatu	r4,2
	stw	r4,SL_DBAT2(r1)
	mfdbatl	r4,2
	stw	r4,SL_DBAT2+4(r1)
	mfdbatu	r4,3
	stw	r4,SL_DBAT3(r1)
	mfdbatl	r4,3
	stw	r4,SL_DBAT3+4(r1)
	mfibatu	r4,0
	stw	r4,SL_IBAT0(r1)
	mfibatl	r4,0
	stw	r4,SL_IBAT0+4(r1)
	mfibatu	r4,1
	stw	r4,SL_IBAT1(r1)
	mfibatl	r4,1
	stw	r4,SL_IBAT1+4(r1)
	mfibatu	r4,2
	stw	r4,SL_IBAT2(r1)
	mfibatl	r4,2
	stw	r4,SL_IBAT2+4(r1)
	mfibatu	r4,3
	stw	r4,SL_IBAT3(r1)
	mfibatl	r4,3
	stw	r4,SL_IBAT3+4(r1)

	/* Save HID0 */
	mfspr	r4,HID0
	stw	r4,SL_HID0(r1)

	/* Set up stuff at address 0 */
	lis	r5,wake_up@ha
	addi	r5,r5,wake_up@l
	tophys(r5,r5)
	stw	r5,SL_PC(r1)
	lis	r4,KERNELBASE@h
	tophys(r5,r1)
	addi	r5,r5,SL_PC
	lis	r6,MAGIC@ha
	addi	r6,r6,MAGIC@l
	stw	r5,0(r4)
	stw	r6,4(r4)

/*
 * Flush the L1 data cache by reading the first 64kB of RAM
 * and then flushing the same area with the dcbf instruction.
 * The L2 cache has already been disabled.
 */
	li	r4,0x0800	/* 64kB / 32B */
	mtctr	r4
	lis	r4,KERNELBASE@h
1:
	lwz	r0,0(r4)
	addi	r4,r4,0x0020		/* Go to start of next cache line */
	bdnz	1b
	sync
	
	li	r4,0x0800	/* 64k */
	mtctr	r4
	lis	r4,KERNELBASE@h
1:
	dcbf	r0,r4
	addi	r4,r4,0x0020	/* Go to start of next cache line */
	bdnz	1b
	sync

/*
 * Set the HID0 and MSR for sleep.
 */
	mfspr	r2,HID0
	rlwinm	r2,r2,0,10,7	/* clear doze, nap */
	oris	r2,r2,HID0_SLEEP@h
	sync
	mtspr	HID0,r2
	sync
	
	mfmsr	r2
	oris	r2,r2,MSR_POW@h
1:	sync
	mtmsr	r2
	isync
	b	1b

/* 
 * Here is the resume code.
 * r1 has the physical address of SL_PC(sp).
 */
	
wake_up:
	/* Flash inval the instruction cache */
	mfspr	r3,HID0
	ori	r3,r3, HID0_ICFI
	mtspr	HID0,r3
	isync
	/* Restore the HID0 register.  This turns on the L1 caches. */
	subi	r1,r1,SL_PC
	lwz	r3,SL_HID0(r1)
	sync
	isync
	mtspr	HID0,r3
	sync

	/* Restore the kernel's segment registers, the
	   BATs, and SDR1.  Then we can turn on the MMU. */
	li	r0,16		/* load up segment register values */
	mtctr	r0		/* for context 0 */
	lis	r3,0x2000	/* Ku = 1, VSID = 0 */
	li	r4,0
3:	mtsrin	r3,r4
	addi	r3,r3,1		/* increment VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	
	lwz	r4,SL_SDR1(r1)
	mtsdr1	r4
	lwz	r4,SL_SPRG0(r1)
	mtsprg	0,r4
	lwz	r4,SL_SPRG0+4(r1)
	mtsprg	1,r4
	lwz	r4,SL_SPRG0+8(r1)
	mtsprg	2,r4
	lwz	r4,SL_SPRG0+12(r1)
	mtsprg	3,r4

	lwz	r4,SL_DBAT0(r1)
	mtdbatu	0,r4
	lwz	r4,SL_DBAT0+4(r1)
	mtdbatl	0,r4
	lwz	r4,SL_DBAT1(r1)
	mtdbatu	1,r4
	lwz	r4,SL_DBAT1+4(r1)
	mtdbatl	1,r4
	lwz	r4,SL_DBAT2(r1)
	mtdbatu	2,r4
	lwz	r4,SL_DBAT2+4(r1)
	mtdbatl	2,r4
	lwz	r4,SL_DBAT3(r1)
	mtdbatu	3,r4
	lwz	r4,SL_DBAT3+4(r1)
	mtdbatl	3,r4
	lwz	r4,SL_IBAT0(r1)
	mtibatu	0,r4
	lwz	r4,SL_IBAT0+4(r1)
	mtibatl	0,r4
	lwz	r4,SL_IBAT1(r1)
	mtibatu	1,r4
	lwz	r4,SL_IBAT1+4(r1)
	mtibatl	1,r4
	lwz	r4,SL_IBAT2(r1)
	mtibatu	2,r4
	lwz	r4,SL_IBAT2+4(r1)
	mtibatl	2,r4
	lwz	r4,SL_IBAT3(r1)
	mtibatu	3,r4
	lwz	r4,SL_IBAT3+4(r1)
	mtibatl	3,r4

	/* restore the MSR and turn on the MMU */
	lwz	r3,SL_MSR(r1)
	bl	turn_on_mmu	

	/* get back the stack pointer */
	tovirt(r1,r1)

	/* Restore TB */
	lwz	r3,SL_TB(r1)
	lwz	r4,SL_TB+4(r1)
	mttbu	r3
	mttbl	r4

	/* Restore the callee-saved registers and return */
	lwz	r2,SL_R2(r1)
	lmw	r12,SL_R12(r1)
	addi	r1,r1,SL_SIZE
	lwz	r0,4(r1)
	mtlr	r0
	blr

turn_on_mmu:
	mflr	r4
	tovirt(r4,r4)
	mtsrr0	r4
	mtsrr1	r3
	sync
	rfi
