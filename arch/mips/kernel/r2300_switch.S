/* $Id: r2300_switch.S,v 1.8 1999/08/18 23:37:44 ralf Exp $
 *
 * r2300_switch.S: R2300 specific task switching code.
 *
 * Copyright (C) 1994, 1995, 1996, 1999 by Ralf Baechle
 * Copyright (C) 1994, 1995, 1996 by Andreas Busse
 *
 * Multi-cpu abstraction and macros for easier reading:
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * Further modifications to make this work:
 * Copyright (c) 1998 Harald Koerfgen
 */
#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/current.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/offset.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

#include <asm/asmmacro.h>

	.set	mips1
	.align	5

/*
 * task_struct *r4xx0_resume(task_struct *prev,
 *                           task_struct *next)
 */
LEAF(resume)
	.set	reorder
	mfc0	t1, CP0_STATUS
	.set	noreorder
	sw	t1, THREAD_STATUS(a0)
	CPU_SAVE_NONSCRATCH(a0)
	sw	ra, THREAD_REG31(a0)

	/*
	 * The order of restoring the registers takes care of the race
	 * updating $28, $29 and kernelsp without disabling ints.
	 */
	move	$28, a1
	CPU_RESTORE_NONSCRATCH($28)
	addiu	t0, $28, KERNEL_STACK_SIZE-32
	sw	t0, kernelsp
	mfc0	t1, CP0_STATUS		/* Do we really need this? */
	li	a3, 0xff00
	and	t1, a3
	lw	a2, THREAD_STATUS($28)
	nor	a3, $0, a3
	and	a2, a3
	or	a2, t1
	mtc0	a2, CP0_STATUS
	jr	ra
	 move	v0, a0
	END(resume)

/*
 * Do lazy fpu context switch.  Saves FPU context to the process in a0
 * and loads the new context of the current process.
 */

#define ST_OFF (KERNEL_STACK_SIZE - 32 - PT_SIZE + PT_STATUS)

LEAF(lazy_fpu_switch)
	mfc0	t0, CP0_STATUS			# enable cp1
	li	t3, 0x20000000
	or	t0, t3
	mtc0	t0, CP0_STATUS

	beqz	a0, 2f				# Save floating point state
	 nor	t3, zero, t3
	.set	reorder
	lw	t1, ST_OFF(a0)			# last thread looses fpu
	.set	noreorder
	and	t1, t3
	sw	t1, ST_OFF(a0)
	swc1	$f0, (THREAD_FPU + 0x00)(a0)
	FPU_SAVE_SINGLE(a0, t1)			# clobbers t1

2:
	lwc1	$f0, (THREAD_FPU + 0x00)($28)
	.set	reorder
	FPU_RESTORE_SINGLE($28, t0)		# clobbers t0
	jr	ra
	END(lazy_fpu_switch)

/*
 * Save a thread's fp context.
 */
	.set	noreorder
LEAF(save_fp)
	FPU_SAVE_SINGLE(a0, t1)			# clobbers t1
	jr	ra
	 swc1	$f0, (THREAD_FPU + 0x00)(a0)
	END(save_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter wether considered as single or as double
 * precission represents signaling NANS.
 *
 * We initialize fcr31 to rounding to nearest, no exceptions.
 */

#define FPU_DEFAULT  0x00000000

LEAF(init_fpu)
	mfc0	t0, CP0_STATUS
	li	t1, 0x20000000
	or	t0, t1
	mtc0	t0, CP0_STATUS

	li	t1, FPU_DEFAULT
	ctc1	t1, fcr31

	li	t0, -1

	mtc1	t0, $f0
	mtc1	t0, $f1
	mtc1	t0, $f2
	mtc1	t0, $f3
	mtc1	t0, $f4
	mtc1	t0, $f5
	mtc1	t0, $f6
	mtc1	t0, $f7
	mtc1	t0, $f8
	mtc1	t0, $f9
	mtc1	t0, $f10
	mtc1	t0, $f11
	mtc1	t0, $f12
	mtc1	t0, $f13
	mtc1	t0, $f14
	mtc1	t0, $f15
	mtc1	t0, $f16
	mtc1	t0, $f17
	mtc1	t0, $f18
	mtc1	t0, $f19
	mtc1	t0, $f20
	mtc1	t0, $f21
	mtc1	t0, $f22
	mtc1	t0, $f23
	mtc1	t0, $f24
	mtc1	t0, $f25
	mtc1	t0, $f26
	mtc1	t0, $f27
	mtc1	t0, $f28
	mtc1	t0, $f29
	mtc1	t0, $f30
	jr	ra
	 mtc1	t0, $f31
	END(init_fpu)
