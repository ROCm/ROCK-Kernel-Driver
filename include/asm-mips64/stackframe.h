/* $Id: stackframe.h,v 1.3 1999/12/04 03:59:12 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1999 Ralf Baechle
 * Copyright (C) 1994, 1995, 1996 Paul M. Antoine.
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_STACKFRAME_H
#define _ASM_STACKFRAME_H

#include <linux/config.h>

#include <asm/asm.h>
#include <asm/offset.h>
#include <asm/processor.h>
#include <asm/addrspace.h>

#ifdef _LANGUAGE_C

#define __str2(x) #x
#define __str(x) __str2(x)

#define save_static(frame)                               \
	__asm__ __volatile__(                            \
		"sd\t$16,"__str(PT_R16)"(%0)\n\t"        \
		"sd\t$17,"__str(PT_R17)"(%0)\n\t"        \
		"sd\t$18,"__str(PT_R18)"(%0)\n\t"        \
		"sd\t$19,"__str(PT_R19)"(%0)\n\t"        \
		"sd\t$20,"__str(PT_R20)"(%0)\n\t"        \
		"sd\t$21,"__str(PT_R21)"(%0)\n\t"        \
		"sd\t$22,"__str(PT_R22)"(%0)\n\t"        \
		"sd\t$23,"__str(PT_R23)"(%0)\n\t"        \
		"sd\t$30,"__str(PT_R30)"(%0)\n\t"        \
		: /* No outputs */                       \
		: "r" (frame))

#endif /* _LANGUAGE_C */

#ifdef _LANGUAGE_ASSEMBLY

		.macro	SAVE_AT
		.set	push
		.set	noat
		sd	$1, PT_R1(sp)
		.set	pop
		.endm

		.macro	SAVE_TEMP
		mfhi	v1
		sd	$8, PT_R8(sp)
		sd	$9, PT_R9(sp)
		sd	v1, PT_HI(sp)
		mflo	v1
		sd	$10, PT_R10(sp)
		sd	$11, PT_R11(sp)
		sd	v1,  PT_LO(sp)
		sd	$12, PT_R12(sp)
		sd	$13, PT_R13(sp)
		sd	$14, PT_R14(sp)
		sd	$15, PT_R15(sp)
		sd	$24, PT_R24(sp)
		.endm

		.macro	SAVE_STATIC
		sd	$16, PT_R16(sp)
		sd	$17, PT_R17(sp)
		sd	$18, PT_R18(sp)
		sd	$19, PT_R19(sp)
		sd	$20, PT_R20(sp)
		sd	$21, PT_R21(sp)
		sd	$22, PT_R22(sp)
		sd	$23, PT_R23(sp)
		sd	$30, PT_R30(sp)
		.endm

		.macro	SAVE_SOME
		.set	push
		.set	reorder
		mfc0	k0, CP0_STATUS
		sll	k0, 3		/* extract cu0 bit */
		.set	noreorder
		bltz	k0, 8f
		 move	k1, sp
		.set	reorder
		/* Called from user mode, new stack. */
#ifndef CONFIG_SMP
		lui	k1, %hi(kernelsp)
		ld	k1, %lo(kernelsp)(k1)
#else
		mfc0	k0, CP0_WATCHLO
		mfc0	k1, CP0_WATCHHI
		dsll32	k0, k0, 0	/* Get rid of sign extension */
		dsrl32	k0, k0, 0	/* Get rid of sign extension */
		dsll32	k1, k1, 0
		or	k1, k1, k0
		li	k0, K0BASE
		or	k1, k1, k0
		daddiu	k1, k1, KERNEL_STACK_SIZE-32
#endif
8:		move	k0, sp
		dsubu	sp, k1, PT_SIZE
		sd	k0, PT_R29(sp)
		sd	$3, PT_R3(sp)
		sd	$0, PT_R0(sp)
		dmfc0	v1, CP0_STATUS
		sd	$2, PT_R2(sp)
		sd	v1, PT_STATUS(sp)
		sd	$4, PT_R4(sp)
		dmfc0	v1, CP0_CAUSE
		sd	$5, PT_R5(sp)
		sd	v1, PT_CAUSE(sp)
		sd	$6, PT_R6(sp)
		dmfc0	v1, CP0_EPC
		sd	$7, PT_R7(sp)
		sd	v1, PT_EPC(sp)
		sd	$25, PT_R25(sp)
		sd	$28, PT_R28(sp)
		sd	$31, PT_R31(sp)
		ori	$28, sp, 0x3fff
		xori	$28, 0x3fff
		.set	pop
		.endm

		.macro	SAVE_ALL
		SAVE_SOME
		SAVE_AT
		SAVE_TEMP
		SAVE_STATIC
		.endm

		.macro	RESTORE_AT
		.set	push
		.set	noat
		ld	$1,  PT_R1(sp)
		.set	pop
		.endm

		.macro	RESTORE_SP
		ld	sp,  PT_R29(sp)
		.endm

		.macro	RESTORE_TEMP
		ld	$24, PT_LO(sp)
		ld	$8, PT_R8(sp)
		ld	$9, PT_R9(sp)
		mtlo	$24
		ld	$24, PT_HI(sp)
		ld	$10, PT_R10(sp)
		ld	$11, PT_R11(sp)
		mthi	$24
		ld	$12, PT_R12(sp)
		ld	$13, PT_R13(sp)
		ld	$14, PT_R14(sp)
		ld	$15, PT_R15(sp)
		ld	$24, PT_R24(sp)
		.endm

		.macro	RESTORE_STATIC
		ld	$16, PT_R16(sp)
		ld	$17, PT_R17(sp)
		ld	$18, PT_R18(sp)
		ld	$19, PT_R19(sp)
		ld	$20, PT_R20(sp)
		ld	$21, PT_R21(sp)
		ld	$22, PT_R22(sp)
		ld	$23, PT_R23(sp)
		ld	$30, PT_R30(sp)
		.endm

		.macro	RESTORE_SOME
		.set	push
		.set	reorder
		mfc0	t0, CP0_STATUS
		.set	pop
		ori	t0, 0x1f
		xori	t0, 0x1f
		mtc0	t0, CP0_STATUS
		li	v1, 0xff00
		and	t0, v1
		ld	v0, PT_STATUS(sp)
		nor	v1, $0, v1
		and	v0, v1
		or	v0, t0
		dmtc0	v0, CP0_STATUS
		ld	v1, PT_EPC(sp)
		dmtc0	v1, CP0_EPC
		ld	$31, PT_R31(sp)
		ld	$28, PT_R28(sp)
		ld	$25, PT_R25(sp)
		ld	$7,  PT_R7(sp)
		ld	$6,  PT_R6(sp)
		ld	$5,  PT_R5(sp)
		ld	$4,  PT_R4(sp)
		ld	$3,  PT_R3(sp)
		ld	$2,  PT_R2(sp)
		.endm

		.macro	RESTORE_ALL
		RESTORE_SOME
		RESTORE_AT
		RESTORE_TEMP
		RESTORE_STATIC
		RESTORE_SP
		.endm

/*
 * Move to kernel mode and disable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	CLI
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0|0x1f
		or	t0, t1
		xori	t0, 0x1f
		mtc0	t0, CP0_STATUS
		.endm

/*
 * Move to kernel mode and enable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	STI
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | 0x1f
		or	t0, t1
		xori	t0, 0x1e
		mtc0	t0, CP0_STATUS
		.endm

/*
 * Just move to kernel mode and leave interrupts as they are.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	KMODE
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | 0x1e
		or	t0, t1
		xori	t0, 0x1e
		mtc0	t0, CP0_STATUS
		.endm

#endif /* _LANGUAGE_ASSEMBLY */

#endif /* _ASM_STACKFRAME_H */
