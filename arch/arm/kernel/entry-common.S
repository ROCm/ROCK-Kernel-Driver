/*
 *  linux/arch/arm/kernel/entry-common.S
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>

#define PT_TRACESYS	0x00000002

@ OS version number used in SWIs
@  RISC OS is 0
@  RISC iX is 8
@
#define OS_NUMBER	9

/*============================================================================
 * All exits to user mode from the kernel go through this code.
 */

#define S_OFF		8

		.macro	get_softirq, rd
#ifdef CONFIG_SMP
#error SMP not supported
#else
		ldr	\rd, __irq_stat
#endif
		.endm

		.globl	ret_from_sys_call

		.align	5
fast_syscall_return:
		str	r0, [sp, #S_R0 + S_OFF]	@ returned r0
slow_syscall_return:
		add	sp, sp, #S_OFF
ret_from_sys_call:				@ external entry
		get_softirq r0
		get_current_task r5
		ldmia	r0, {r0, r1}		@ softirq_active, softirq_mask
		mov	r4, #1			@ flag this as being syscall return
		tst	r0, r1
		blne	SYMBOL_NAME(do_softirq)
ret_with_reschedule:				@ external entry (r5 must be set) (__irq_usr)
		ldr	r0, [r5, #TSK_NEED_RESCHED]
		ldr	r1, [r5, #TSK_SIGPENDING]
		teq	r0, #0
		bne	ret_reschedule
		teq	r1, #0			@ check for signals
		blne	ret_signal
ret_from_all:	restore_user_regs		@ internal

ret_signal:	mov	r1, sp			@ internal
		mov	r2, r4
		b	SYMBOL_NAME(do_signal)	@ note the bl above sets lr

ret_reschedule:	adrsvc	al, lr, ret_with_reschedule	@ internal
		b	SYMBOL_NAME(schedule)

		.globl	ret_from_exception
ret_from_exception:				@ external entry
		get_softirq r0
		get_current_task r5
		ldmia	r0, {r0, r1}		@ softirq_active, softirq_mask
		mov	r4, #0
		tst	r0, r1
		ldr	r6, [sp, #S_PSR]
		blne	SYMBOL_NAME(do_softirq)
		tst	r6, #3			@ returning to user mode?
		beq	ret_with_reschedule
		b	ret_from_all

#include "calls.S"

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 */

/*
 * Create some aliases for some registers.  These should allow
 * us to have in theory up to 7 arguments to a function.
 */
scno		.req	r9			@ syscall number
tbl		.req	r8			@ syscall table pointer
tip		.req	r7			@ temporary IP

		.align	5
vector_swi:	save_user_regs
		mask_pc	lr, lr
		zero_fp
		ldr	scno, [lr, #-4]		@ get SWI instruction
		arm700_bug_check scno, ip
#ifdef CONFIG_ALIGNMENT_TRAP
		ldr	ip, .LCswi
		ldr	ip, [ip]
		mcr	p15, 0, ip, c1, c0
#endif
		enable_irqs ip

		str	r4, [sp, #-S_OFF]!	@ push fifth arg
		adrsvc	al, lr, fast_syscall_return

		bic	scno, scno, #0xff000000	@ mask off SWI op-code
		eor	scno, scno, #OS_NUMBER<<20	@ check OS number
		cmp	scno, #NR_syscalls	@ check upper syscall limit
		bcs	2f

		get_current_task ip
		ldr	ip, [ip, #TSK_PTRACE]	@ check for syscall tracing
		adr	tbl, SYMBOL_NAME(sys_call_table)
		tst	ip, #PT_TRACESYS
		ldreq	pc, [tbl, scno, lsl #2]	@ call sys routine

		ldr	tip, [sp, #S_IP + S_OFF]	@ save old IP
		mov	ip, #0
		str	ip, [sp, #S_IP + S_OFF]	@ trace entry [IP = 0]
		bl	SYMBOL_NAME(syscall_trace)
		str	tip, [sp, #S_IP + S_OFF]

		add	ip, sp, #S_OFF
		ldmia	ip, {r0 - r3}		@ have to reload r0 - r3
		mov	lr, pc
		ldr	pc, [tbl, scno, lsl #2]	@ call sys routine
		str	r0, [sp, #S_R0 + S_OFF]	@ returned r0

		mov	ip, #1
		str	ip, [sp, #S_IP + S_OFF]	@ trace exit [IP = 1]
		bl	SYMBOL_NAME(syscall_trace)
		str	tip, [sp, #S_IP + S_OFF]
		b	slow_syscall_return

2:		add	r1, sp, #S_OFF
		tst	scno, #0x00f00000	@ is it a Unix SWI?
		bne	3f
		subs	r0, scno, #(KSWI_SYS_BASE - KSWI_BASE)
		bcs	SYMBOL_NAME(arm_syscall)
		b	SYMBOL_NAME(sys_ni_syscall) @ not private func

3:		eor	r0, scno, #OS_NUMBER <<20	@ Put OS number back
		adrsvc	al, lr, slow_syscall_return
		b	SYMBOL_NAME(deferred)

		.align	5
		.type	__irq_stat, #object
__irq_stat:
		.word	SYMBOL_NAME(irq_stat)

		.type	sys_call_table, #object
ENTRY(sys_call_table)
#include "calls.S"

/*============================================================================
 * Special system call wrappers
 */
@ r0 = syscall number
@ r5 = syscall table
		.type	sys_syscall, #function
SYMBOL_NAME(sys_syscall):
		eor	scno, r0, #OS_NUMBER << 20
		cmp	scno, #NR_syscalls	@ check range
		add	ip, sp, #S_OFF
		stmleia	sp, {r5, r6}		@ shuffle args
		movle	r0, r1
		movle	r1, r2
		movle	r2, r3
		movle	r3, r4
		ldrle	pc, [tbl, scno, lsl #2]
		b	sys_ni_syscall

sys_fork_wrapper:
		add	r0, sp, #S_OFF
		b	SYMBOL_NAME(sys_fork)

sys_vfork_wrapper:
		add	r0, sp, #S_OFF
		b	SYMBOL_NAME(sys_vfork)

sys_execve_wrapper:
		add	r3, sp, #S_OFF
		b	SYMBOL_NAME(sys_execve)

sys_clone_wapper:
		add	r2, sp, #S_OFF
		b	SYMBOL_NAME(sys_clone)

sys_sigsuspend_wrapper:
		add	r3, sp, #S_OFF
		b	SYMBOL_NAME(sys_sigsuspend)

sys_rt_sigsuspend_wrapper:
		add	r2, sp, #S_OFF
		b	SYMBOL_NAME(sys_rt_sigsuspend)

sys_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		b	SYMBOL_NAME(sys_sigreturn)

sys_rt_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		b	SYMBOL_NAME(sys_rt_sigreturn)

sys_sigaltstack_wrapper:
		ldr	r2, [sp, #S_OFF + S_SP]
		b	do_sigaltstack

/*
 * Note: off_4k (r5) is always units of 4K.  If we can't do the requested
 * offset, we return EINVAL.
 */
sys_mmap2:
#if PAGE_SHIFT > 12
		tst	r5, #PGOFF_MASK
		moveq	r5, r5, lsr #PGOFF_SHIFT
		streq	r5, [sp, #4]
		beq	do_mmap2
		mov	r0, #-EINVAL
		RETINSTR(mov,pc, lr)
#else
		str	r5, [sp, #4]
		b	do_mmap2
#endif

		.data

ENTRY(fp_enter)
		.word	fpe_not_present
