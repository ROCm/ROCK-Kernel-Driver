/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1998, 1999, 2002 by Ralf Baechle
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1994, 1995, 1996, by Andreas Busse
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000 MIPS Technologies, Inc.
 *    written by Carsten Langgaard, carstenl@mips.com
 */
#include <linux/config.h>
#include <asm/asm.h>
#include <asm/cachectl.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/offset.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

#include <asm/asmmacro.h>

#define ST_OFF (KERNEL_STACK_SIZE - 32 - PT_SIZE + PT_STATUS)

/*
 * FPU context is saved iff the process has used it's FPU in the current
 * time slice as indicated by TIF_USEDFPU.  In any case, the CU1 bit for user
 * space STATUS register should be 0, so that a process *always* starts its 
 * userland with FPU disabled after each context switch.
 *
 * FPU will be enabled as soon as the process accesses FPU again, through 
 * do_cpu() trap.
 */

/*
 * task_struct *r4xx0_resume(task_struct *prev, task_struct *next,
 *                           struct thread_info *next_ti)
 */
	.align	5
	LEAF(resume)
#ifndef CONFIG_CPU_HAS_LLSC
	sw      zero, ll_bit
#endif
	mfc0	t1, CP0_STATUS
	sw	t1, THREAD_STATUS(a0)
	CPU_SAVE_NONSCRATCH(a0)
	sw	ra, THREAD_REG31(a0)

	/* 
	 * check if we need to save FPU registers
	 */
	lw	t3, TASK_THREAD_INFO(a0)
	lw	t0, TI_FLAGS(t3)
	li	t1, _TIF_USEDFPU
	and	t2, t0, t1
	beqz	t2, 1f
	nor	t1, zero, t1

	and	t0, t0, t1
	sw	t0, TI_FLAGS(t3)

	/*
	 * clear saved user stack CU1 bit
	 */
	lw	t0, ST_OFF(t3)
	li	t1, ~ST0_CU1
	and	t0, t0, t1
	sw	t0, ST_OFF(t3)

	FPU_SAVE_DOUBLE(a0, t0)			# clobbers t0

1:
	/*
	 * The order of restoring the registers takes care of the race
	 * updating $28, $29 and kernelsp without disabling ints.
	 */
	move	$28, a2
	CPU_RESTORE_NONSCRATCH(a1)

	addiu	t0, $28, KERNEL_STACK_SIZE-32
#ifdef CONFIG_SMP
	mfc0	a3, CP0_CONTEXT
	la	t1, kernelsp
	srl	a3, 23
	sll	a3, 2
	addu	t1, a3, t1
	sw	t0, (t1)
#else
	sw	t0, kernelsp
#endif

	mfc0	t1, CP0_STATUS		/* Do we really need this? */
	li	a3, 0xff00
	and	t1, a3
	lw	a2, THREAD_STATUS(a1)
	nor	a3, $0, a3
	and	a2, a3
	or	a2, t1
	mtc0	a2, CP0_STATUS
	move	v0, a0
	jr	ra
	END(resume)

/*
 * Save a thread's fp context.
 */
LEAF(_save_fp)
	FPU_SAVE_DOUBLE(a0, t1)			# clobbers t1
	jr	ra
	END(_save_fp)

/*
 * Restore a thread's fp context.
 */
LEAF(_restore_fp)
	FPU_RESTORE_DOUBLE(a0, t1)		# clobbers t1
	jr	ra
	END(_restore_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter whether considered as single or as double
 * precision represents signaling NANS.
 *
 * We initialize fcr31 to rounding to nearest, no exceptions.
 */

#define FPU_DEFAULT  0x00000000

LEAF(_init_fpu)
	.set	mips3
	mfc0	t0, CP0_STATUS
	li	t1, ST0_CU1
	or	t0, t1
	mtc0	t0, CP0_STATUS
	FPU_ENABLE_HAZARD

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
	dmtc1	t0, $f30
	jr	ra
	END(_init_fpu)
