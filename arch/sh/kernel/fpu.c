/* $Id: fpu.c,v 1.29 2000/03/22 13:42:10 gniibe Exp $
 *
 * linux/arch/sh/kernel/fpu.c
 *
 * Save/restore floating point context for signal handlers.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 *
 * FIXME! These routines can be optimized in big endian case.
 */

#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/processor.h>
#include <asm/io.h>

void
save_fpu(struct task_struct *tsk)
{
	asm volatile("sts.l	$fpul, @-%0\n\t"
		     "sts.l	$fpscr, @-%0\n\t"
		     "lds	%1, $fpscr\n\t"
		     "frchg\n\t"
		     "fmov.s	$fr15, @-%0\n\t"
		     "fmov.s	$fr14, @-%0\n\t"
		     "fmov.s	$fr13, @-%0\n\t"
		     "fmov.s	$fr12, @-%0\n\t"
		     "fmov.s	$fr11, @-%0\n\t"
		     "fmov.s	$fr10, @-%0\n\t"
		     "fmov.s	$fr9, @-%0\n\t"
		     "fmov.s	$fr8, @-%0\n\t"
		     "fmov.s	$fr7, @-%0\n\t"
		     "fmov.s	$fr6, @-%0\n\t"
		     "fmov.s	$fr5, @-%0\n\t"
		     "fmov.s	$fr4, @-%0\n\t"
		     "fmov.s	$fr3, @-%0\n\t"
		     "fmov.s	$fr2, @-%0\n\t"
		     "fmov.s	$fr1, @-%0\n\t"
		     "fmov.s	$fr0, @-%0\n\t"
		     "frchg\n\t"
		     "fmov.s	$fr15, @-%0\n\t"
		     "fmov.s	$fr14, @-%0\n\t"
		     "fmov.s	$fr13, @-%0\n\t"
		     "fmov.s	$fr12, @-%0\n\t"
		     "fmov.s	$fr11, @-%0\n\t"
		     "fmov.s	$fr10, @-%0\n\t"
		     "fmov.s	$fr9, @-%0\n\t"
		     "fmov.s	$fr8, @-%0\n\t"
		     "fmov.s	$fr7, @-%0\n\t"
		     "fmov.s	$fr6, @-%0\n\t"
		     "fmov.s	$fr5, @-%0\n\t"
		     "fmov.s	$fr4, @-%0\n\t"
		     "fmov.s	$fr3, @-%0\n\t"
		     "fmov.s	$fr2, @-%0\n\t"
		     "fmov.s	$fr1, @-%0\n\t"
		     "fmov.s	$fr0, @-%0"
		     : /* no output */
		     : "r" ((char *)(&tsk->thread.fpu.hard.status)),
		       "r" (FPSCR_INIT)
		     : "memory");

	tsk->flags &= ~PF_USEDFPU;
	release_fpu();
}

static void
restore_fpu(struct task_struct *tsk)
{
	asm volatile("lds	%1, $fpscr\n\t"
		     "fmov.s	@%0+, $fr0\n\t"
		     "fmov.s	@%0+, $fr1\n\t"
		     "fmov.s	@%0+, $fr2\n\t"
		     "fmov.s	@%0+, $fr3\n\t"
		     "fmov.s	@%0+, $fr4\n\t"
		     "fmov.s	@%0+, $fr5\n\t"
		     "fmov.s	@%0+, $fr6\n\t"
		     "fmov.s	@%0+, $fr7\n\t"
		     "fmov.s	@%0+, $fr8\n\t"
		     "fmov.s	@%0+, $fr9\n\t"
		     "fmov.s	@%0+, $fr10\n\t"
		     "fmov.s	@%0+, $fr11\n\t"
		     "fmov.s	@%0+, $fr12\n\t"
		     "fmov.s	@%0+, $fr13\n\t"
		     "fmov.s	@%0+, $fr14\n\t"
		     "fmov.s	@%0+, $fr15\n\t"
		     "frchg\n\t"
		     "fmov.s	@%0+, $fr0\n\t"
		     "fmov.s	@%0+, $fr1\n\t"
		     "fmov.s	@%0+, $fr2\n\t"
		     "fmov.s	@%0+, $fr3\n\t"
		     "fmov.s	@%0+, $fr4\n\t"
		     "fmov.s	@%0+, $fr5\n\t"
		     "fmov.s	@%0+, $fr6\n\t"
		     "fmov.s	@%0+, $fr7\n\t"
		     "fmov.s	@%0+, $fr8\n\t"
		     "fmov.s	@%0+, $fr9\n\t"
		     "fmov.s	@%0+, $fr10\n\t"
		     "fmov.s	@%0+, $fr11\n\t"
		     "fmov.s	@%0+, $fr12\n\t"
		     "fmov.s	@%0+, $fr13\n\t"
		     "fmov.s	@%0+, $fr14\n\t"
		     "fmov.s	@%0+, $fr15\n\t"
		     "frchg\n\t"
		     "lds.l	@%0+, $fpscr\n\t"
		     "lds.l	@%0+, $fpul\n\t"
		     : /* no output */
		     : "r" (&tsk->thread.fpu), "r" (FPSCR_INIT)
		     : "memory");
}

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using
 * has the property that no matter wether considered as single or as
 * double precission represents signaling NANS.  
 */

void fpu_init(void)
{
	asm volatile("lds	%0, $fpul\n\t"
		     "lds	%1, $fpscr\n\t"
		     "fsts	$fpul, $fr0\n\t"
		     "fsts	$fpul, $fr1\n\t"
		     "fsts	$fpul, $fr2\n\t"
		     "fsts	$fpul, $fr3\n\t"
		     "fsts	$fpul, $fr4\n\t"
		     "fsts	$fpul, $fr5\n\t"
		     "fsts	$fpul, $fr6\n\t"
		     "fsts	$fpul, $fr7\n\t"
		     "fsts	$fpul, $fr8\n\t"
		     "fsts	$fpul, $fr9\n\t"
		     "fsts	$fpul, $fr10\n\t"
		     "fsts	$fpul, $fr11\n\t"
		     "fsts	$fpul, $fr12\n\t"
		     "fsts	$fpul, $fr13\n\t"
		     "fsts	$fpul, $fr14\n\t"
		     "fsts	$fpul, $fr15\n\t"
		     "frchg\n\t"
		     "fsts	$fpul, $fr0\n\t"
		     "fsts	$fpul, $fr1\n\t"
		     "fsts	$fpul, $fr2\n\t"
		     "fsts	$fpul, $fr3\n\t"
		     "fsts	$fpul, $fr4\n\t"
		     "fsts	$fpul, $fr5\n\t"
		     "fsts	$fpul, $fr6\n\t"
		     "fsts	$fpul, $fr7\n\t"
		     "fsts	$fpul, $fr8\n\t"
		     "fsts	$fpul, $fr9\n\t"
		     "fsts	$fpul, $fr10\n\t"
		     "fsts	$fpul, $fr11\n\t"
		     "fsts	$fpul, $fr12\n\t"
		     "fsts	$fpul, $fr13\n\t"
		     "fsts	$fpul, $fr14\n\t"
		     "fsts	$fpul, $fr15\n\t"
		     "frchg"
		     : /* no output */
		     : "r" (0), "r" (FPSCR_INIT));
}

asmlinkage void
do_fpu_error(unsigned long r4, unsigned long r5, unsigned long r6, unsigned long r7,
	     struct pt_regs regs)
{
	struct task_struct *tsk = current;

	regs.pc += 2;

	grab_fpu();
	save_fpu(tsk);
	tsk->thread.trap_no = 11;
	tsk->thread.error_code = 0;
	force_sig(SIGFPE, tsk);
}

asmlinkage void
do_fpu_state_restore(unsigned long r4, unsigned long r5, unsigned long r6,
		     unsigned long r7, struct pt_regs regs)
{
	struct task_struct *tsk = current;

	if (!user_mode(&regs)) {
		if (tsk != &init_task) {
			unlazy_fpu(tsk);
		}
		tsk = &init_task;
		if (tsk->flags & PF_USEDFPU) {
			/*
			 * This weird situation can be occurred.
			 *
			 * There's race condition in __cli:
			 *
			 *   (1) $SR --> register
			 *   (2) Set IMASK of register
			 *   (3) $SR <-- register
			 *
			 * Between (1) and (2), or (2) and (3) getting
			 * interrupt, and interrupt handler (or
			 * softirq) may use FPU.
			 *
			 * Then, SR.FD is overwritten by (3).
			 *
			 * This results init_task.PF_USEDFPU is on,
			 * with SR.FD == 1.
			 *
			 */
			release_fpu();
			return;
		}
	}

	grab_fpu();
	if (tsk->used_math) {
		/* Using the FPU again.  */
		restore_fpu(tsk);
	} else	{
		/* First time FPU user.  */
		fpu_init();
		tsk->used_math = 1;
	}
	tsk->flags |= PF_USEDFPU;
	release_fpu();
}

/*
 * Change current FD flag to set FD flag back to exception
 */
asmlinkage void
fpu_prepare_fd(unsigned long sr, unsigned long r5, unsigned long r6,
	       unsigned long r7, struct pt_regs regs)
{
	__cli();
	if (!user_mode(&regs)) {
		if (init_task.flags & PF_USEDFPU)
			grab_fpu();
		else {
			if (!(sr & SR_FD)) {
				BUG();
				release_fpu();
			}
		}
		return;
	}

	if (sr & SR_FD) { /* Kernel doesn't grab FPU */
		if (current->flags & PF_USEDFPU)
			grab_fpu();
		else {
			if (init_task.flags & PF_USEDFPU) {
				/*
				 * This weird situation can be occurred.
				 * See the comment in do_fpu_state_restore.
				 */
				grab_fpu();
				save_fpu(&init_task);
			}
		}
	} else {
		if (init_task.flags & PF_USEDFPU)
			save_fpu(&init_task);
		else {
			BUG();
			release_fpu();
		}
	}
}

/* Short cut for the FPU exception */
asmlinkage void
enable_fpu_in_danger(void)
{
	struct task_struct *tsk = current;

	if (tsk != &init_task)
		unlazy_fpu(tsk);

	tsk = &init_task;
	if (tsk->used_math) {
		/* Using the FPU again.  */
		restore_fpu(tsk);
	} else	{
		/* First time FPU user.  */
		fpu_init();
		tsk->used_math = 1;
	}
	tsk->flags |= PF_USEDFPU;
}
