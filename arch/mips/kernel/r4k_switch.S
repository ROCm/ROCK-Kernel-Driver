/* $Id: r4k_switch.S,v 1.9 1999/08/18 23:37:44 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1994, 1995, 1996, by Andreas Busse
 * Copyright (C) 1999 Silicon Graphics, Inc.
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

	.set	mips3

/*
 * task_struct *r4xx0_resume(task_struct *prev, task_struct *next)
 */
	.set	noreorder
	.align	5
	LEAF(resume)
	mfc0	t1, CP0_STATUS
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

	lw	t1, ST_OFF(a0)			# last thread looses fpu
	and	t1, t3
	sw	t1, ST_OFF(a0)


	FPU_SAVE_DOUBLE(a0, t1)			# clobbers t1
2:

	.set	reorder
	FPU_RESTORE_DOUBLE($28, t0)		# clobbers t0
	jr	ra
	END(lazy_fpu_switch)

/*
 * Save a thread's fp context.
 */
LEAF(save_fp)
	FPU_SAVE_DOUBLE(a0, t1)			# clobbers t1
	jr	ra
	END(save_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter whether considered as single or as double
 * precision represents signaling NANS.
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

	dmtc1	t0, $f0
	dmtc1	t0, $f2
	dmtc1	t0, $f4
	dmtc1	t0, $f6
	dmtc1	t0, $f8
	dmtc1	t0, $f10
	dmtc1	t0, $f12
	dmtc1	t0, $f14
	dmtc1	t0, $f16
	dmtc1	t0, $f18
	dmtc1	t0, $f20
	dmtc1	t0, $f22
	dmtc1	t0, $f24
	dmtc1	t0, $f26
	dmtc1	t0, $f28
	.set	noreorder
	jr	ra
	 dmtc1	t0, $f30
	.set	reorder
	END(init_fpu)
