/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Low level exception handling
 *
 * Copyright (C) 1994 - 2000, 2001 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

#define KU_USER 0x10

		.text
		.align	5
FEXPORT(ret_from_irq)
FEXPORT(ret_from_exception)
		ld	t0, PT_STATUS(sp)	# returning to kernel mode?
		andi	t0, t0, KU_USER
		beqz	t0, restore_all

FEXPORT(resume_userspace)
		mfc0	t0, CP0_STATUS		# make sure need_resched and
		ori	t0, t0, 1		# signals dont change between
		xori	t0, t0, 1		# sampling and return
		SSNOP; SSNOP; SSNOP

		LONG_L	a2, TI_FLAGS($28)
		andi	a2, _TIF_WORK_MASK	# current->work (ignoring
						# syscall_trace
		bnez	a2, work_pending
		j	restore_all

FEXPORT(ret_from_fork)
		jal	schedule_tail

FEXPORT(syscall_exit)
		mfc0	t0, CP0_STATUS		# make sure need_resched and
		ori	t0, t0, 1		# signals dont change between
		xori	t0, t0, 1		# sampling and return
		mtc0	t0, CP0_STATUS
		SSNOP; SSNOP; SSNOP

		LONG_L	a2, TI_FLAGS($28)	# current->work
		bnez	a2, syscall_exit_work

restore_all:
		.set	noat
		RESTORE_ALL
		eret
		.set	at

work_pending:
		bltz	a2, work_notifysig	# current->work.need_resched
						# test high 8 bits
work_resched:
		jal	schedule

		mfc0	t0, CP0_STATUS		# make sure need_resched and
		ori	t0, t0, 1		# signals dont change between
		xori	t0, t0, 1		# sampling and return
		mtc0	t0, CP0_STATUS
		SSNOP; SSNOP; SSNOP

		LONG_L	a2, TI_FLAGS($28)	# This also converts into
						# a union of four chars
		andi	a2, _TIF_WORK_MASK	# is there any work to be done
						# other than syscall tracing?
		beqz	a2, restore_all
		andi	t0, a2, _TIF_NEED_RESCHED
		bnez	t0, work_notifysig

work_notifysig:					# deal with pending signals and
						# notify-resume requests
		move	a0, sp
		li	a1, 0
		jal	do_notify_resume	# a2 already loaded
		j	restore_all

FEXPORT(syscall_exit_work)
		LONG_L	t0, TI_FLAGS($28)	# current->work.syscall_trace
		bgez	t0, work_pending
		mfc0	t0, CP0_STATUS		# sti
		ori	t0, t0, 1
		mtc0	t0, CP0_STATUS
		jal	do_syscall_trace
		b	resume_userspace

/*
 * Common spurious interrupt handler.
 */
		.text
		.align  5
LEAF(spurious_interrupt)
		/*
		 * Someone tried to fool us by sending an interrupt but we
		 * couldn't find a cause for it.
		 */
		lui     t1, %hi(irq_err_count)
1:		ll      t0, %lo(irq_err_count)(t1)
		addiu   t0, 1
		sc      t0, %lo(irq_err_count)(t1)
		beqz	t0, 1b
		j	ret_from_irq
		END(spurious_interrupt)
