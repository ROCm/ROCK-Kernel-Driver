/* $Id: entry.S,v 1.5 2000/02/23 00:41:00 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Low level exception handling
 *
 * Copyright (C) 1994 - 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics
 */
#include <linux/config.h>

#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>

		.text
		.set	noreorder
		.align 4
FEXPORT(ret_from_fork)
		jal	schedule_tail
		 move	a0, v0					# prev
		j	ret_from_sys_call
		 nop
FEXPORT(handle_softirq)
		jal	do_softirq
		 nop
		b	9f
		 nop

reschedule:	jal	schedule 
		 nop

FEXPORT(ret_from_sys_call)
FEXPORT(ret_from_irq)
		la	t1, irq_stat		# softirq_active
#ifdef CONFIG_SMP
		lwu	t0, TASK_PROCESSOR($28)
		dsll	t0, t0, 5
		daddu	t1, t0
#endif
		lw	t0, 0 (t1)		# softirq_active
		lw	t1, 4 (t1)		# softirq_mask.  unused delay slot
		and	t0, t1
		bnez	t0, handle_softirq
9:		 ld	t0, PT_STATUS(sp)	# returning to kernel mode?

		andi	t1, t0, 0x10
		beqz	t1, return		# -> yes
		 ld	t1, TASK_NEED_RESCHED($28)
		bnez	t1, reschedule
		lw	v0, TASK_SIGPENDING($28)
		 move	a0, zero
		beqz	v0, return
		 nop
		jal	do_signal
		 move	a1, sp

FEXPORT(return)	.set	noat
		RESTORE_ALL
		eret
		.set	at

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
		lui     t1,%hi(spurious_count)
		lw      t0,%lo(spurious_count)(t1)
		addiu   t0,1
		j	ret_from_irq
		 sw      t0,%lo(spurious_count)(t1)
		END(spurious_interrupt)
