/* -*- mode: asm -*-
 *
 *  linux/arch/m68k/kernel/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 * Linux/m68k support by Hamish Macdonald
 *
 */

/*
 * entry.S  contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 */

/*
 * 12/03/96 Jes: Currently we only support m68k single-cpu systems, so
 *               all pointers that used to be 'current' are now entry
 *               number 0 in the 'current_set' list.
 */

#include <linux/config.h>
#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/entry.h>
#include <asm/errno.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/traps.h>
#include <asm/asm-offsets.h>

#define IMMED #
#define DBG_PUTC(x)	moveb IMMED x,0xfffff907; \
			moveb IMMED '\r',0xfffff907; \
			moveb IMMED '\n',0xfffff907

.globl system_call, buserr, trap
.globl resume, ret_from_exception
.globl ret_from_signal
.globl sys_call_table
.globl sys_fork, sys_clone, sys_vfork
.globl ret_from_interrupt, bad_interrupt
.globl inthandler1, inthandler2, inthandler3, inthandler4
.globl inthandler5, inthandler6, inthandler7

.text
ENTRY(buserr)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@- 		/* stack frame pointer argument*/
	bsrw	buserr_c
	addql	#4,%sp
	jra	ret_from_exception

ENTRY(trap)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@- 		/* stack frame pointer argument*/
	bsrw	trap_c
	addql	#4,%sp
	jra	ret_from_exception

ENTRY(reschedule)
	/* save top of frame*/
	pea     %sp@
	jbsr    set_esp0
	addql   #4,%sp

	pea	ret_from_exception
	jmp	schedule

	/* After a fork we jump here directly from resume,*/
	/* so that %d1 contains the previous task*/
	/* Theoretically only needed on SMP, but let's watch*/
	/* what happens in schedule_tail() in future...*/
ENTRY(ret_from_fork)
#ifdef CONFIG_SMP
	movel	%d1,%sp@-
	jsr	schedule_tail
	addql	#4,%sp
#endif
	jra	ret_from_exception

badsys:
	movel	#-ENOSYS,%sp@(PT_D0)
	jra	ret_from_exception

do_trace:
	movel	#-ENOSYS,%sp@(PT_D0)	/* needed for strace*/
	subql	#4,%sp
	SAVE_SWITCH_STACK
	jbsr	syscall_trace
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	movel	%sp@(PT_ORIG_D0),%d1
	movel	#-ENOSYS,%d0
	cmpl	#NR_syscalls,%d1
	jcc	1f
	lsl	#2,%d1
	lea	sys_call_table, %a0
	jbsr	%a0@(%d1)

1:	movel	%d0,%sp@(PT_D0)		/* save the return value*/
	subql	#4,%sp			/* dummy return address*/
	SAVE_SWITCH_STACK
	jbsr	syscall_trace

ret_from_signal:
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	jra	ret_from_exception

ENTRY(system_call)
	SAVE_ALL_SYS

	GET_CURRENT(%d1)
	/* save top of frame*/
	pea     %sp@
	jbsr    set_esp0
	addql   #4,%sp

	btst	#PF_TRACESYS_BIT,%a2@(TASK_FLAGS+PF_TRACESYS_OFF)
	jne	do_trace
	cmpl	#NR_syscalls,%d0
	jcc	badsys
	lsl	#2,%d0
	lea	sys_call_table,%a0
	movel	%a0@(%d0), %a0
	jbsr	%a0@
	movel	%d0,%sp@(PT_D0)		/* save the return value*/

ret_from_exception:
	btst	#5,%sp@(PT_SR)		/* check if returning to kernel*/
	jeq	Luser_return		/* if so, skip resched, signals*/

Lkernel_return:
	RESTORE_ALL

Luser_return:
	/* only allow interrupts when we are really the last one on the*/
	/* kernel stack, otherwise stack overflow can occur during*/
	/* heavy interupt load*/
	andw	#ALLOWINT,%sr

	movel	%sp,%d1			/* get thread_info pointer */
	andl	#0xffffe000,%d1
	movel	%d1,%a2
	move	%a2@(TI_FLAGS),%d1	/* thread_info->flags */
	andl	#_TIF_WORK_MASK,%d1
	jne	Lwork_to_do
	RESTORE_ALL

Lwork_to_do:
	movel	%a2@(TI_FLAGS),%d1	/* thread_info->flags */
	btst	#TIF_NEED_RESCHED,%d1
	jne	reschedule

Lsignal_return:
	subql	#4,%sp			/* dummy return address*/
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	clrl	%sp@-
	bsrw	do_signal
	addql	#8,%sp
	RESTORE_SWITCH_STACK
	addql	#4,%sp
Lreturn:
	RESTORE_ALL

/*
** This is the main interrupt handler, responsible for calling process_int()
*/
inthandler1:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#65,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler2:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#66,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler3:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#67,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler4:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#68,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler5:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#69,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler6:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#70,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler7:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#71,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler8:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#72,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

timerhandler:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#0x40,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

serialhandler:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	#0x42,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler_wrap:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	%d0,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

inthandler:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addql	#1,local_irq_count	/*  put exception # in d0*/
/*	bfextu %sp@(PT_VECTOR){#4,#10},%d0 */
	movew	%sp@(PT_VECTOR), %d0
	and	#0x3ff, %d0

	movel	%sp,%sp@-
	movel	%d0,%sp@- 		/*  put vector # on stack*/
	jbsr	process_int		/*  process the IRQ*/
3:     	addql	#8,%sp			/*  pop parameters off stack*/
	bra	ret_from_interrupt

ret_from_interrupt:
	subql	#1,local_irq_count
	jeq	1f
2:
	RESTORE_ALL
1:
#if 1
/*	bfextu  %sp@(PT_SR){#5,#3},%d0 */	/* Check for nested interrupt.*/
	moveb	%sp@(PT_SR), %d0
	and	#7, %d0

#if MAX_NOINT_IPL > 0
	cmpiw	#MAX_NOINT_IPL,%d0
#endif
	jhi	2b
#endif
	/* check if we need to do software interrupts */

	movel	local_irq_count,%d0
	jeq	ret_from_exception

	pea	ret_from_exception
	jra	do_softirq


/* Handler for uninitialized and spurious interrupts */

bad_interrupt:
	addql	#1,num_spurious
	rte

ENTRY(sys_fork)
	SAVE_SWITCH_STACK	
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_fork
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_clone)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_clone
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_vfork)
	SAVE_SWITCH_STACK	
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_vfork
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_sigsuspend)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	do_sigsuspend
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_rt_sigsuspend)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	do_rt_sigsuspend
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_sigreturn)
	SAVE_SWITCH_STACK
	jbsr	do_sigreturn
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_rt_sigreturn)
	SAVE_SWITCH_STACK
	jbsr	do_rt_sigreturn
	RESTORE_SWITCH_STACK
	rts

resume:
	/*
	 * Beware - when entering resume, prev (the current task) is
	 * in a0, next (the new task) is in a1,so don't change these
	 * registers until their contents are no longer needed.
	 */

	/* save prev thread in d1 */
	movel	%a0,%d1

	/* save sr */
	movew	%sr,%a0@(TASK_THREAD+THREAD_SR)
#ifdef USE_SFC_DFC
	/* save fs (sfc,%dfc) (may be pointing to kernel memory) */
	movec	%sfc,%d0
	movew	%d0,%a0@(TASK_THREAD+THREAD_FS)
#endif

	/* save non-scratch registers on stack */
	SAVE_SWITCH_STACK

	/* save usp */
	/* it is better to use a movel here instead of a movew 8*) */
	movel	%usp,%a2
	movel	%a2,%a0@(TASK_THREAD+THREAD_USP)

	/* save current kernel stack pointer */
	movel	%sp,%a0@(TASK_THREAD+THREAD_KSP)

	/* restore the kernel stack pointer */
	movel	%a1@(TASK_THREAD+THREAD_KSP),%sp

	/* restore non-scratch registers */
	RESTORE_SWITCH_STACK

	/* restore user stack pointer */
	movel	%a1@(TASK_THREAD+THREAD_USP),%a0
	movel	%a0,%usp

#ifdef USE_SFC_DFC
	/* restore fs (sfc,%dfc) */
	movew	%a1@(TASK_THREAD+THREAD_FS),%a0
	movec	%a0,%sfc
	movec	%a0,%dfc
#endif
	/* restore status register */
	movew	%a1@(TASK_THREAD+THREAD_SR),%sr

	rts

