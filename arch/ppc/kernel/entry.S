/*
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Rewritten by Cort Dougan (cort@fsmlabs.com) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@fsmlabs.com>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *  MPC8xx modifications Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *  Adaptations for iSeries Lpar by Mike Corrigan & Dave Boutcher
 *
 *  This file contains the system call entry code, context switch
 *  code, and exception/interrupt return code for PowerPC.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *	
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sys.h>
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include "asm-offsets.h"
#ifdef	CONFIG_PPC_ISERIES
#include "iSeries_asm.h"
#endif /* CONFIG_PPC_ISERIES */

#undef SHOW_SYSCALLS
#undef SHOW_SYSCALLS_TASK

#ifndef CONFIG_PPC_ISERIES	/* iSeries version is in iSeries_head.S */
/*
 * This code finishes saving the registers to the exception frame
 * and jumps to the appropriate handler for the exception, turning
 * on address translation.
 * Note that we rely on the caller having set cr0.eq iff the exception
 * occurred in kernel mode (i.e. MSR:PR = 0).
 */
	.globl	transfer_to_handler_full
transfer_to_handler_full:
	SAVE_NVGPRS(r11)
	/* fall through */

	.globl	transfer_to_handler
transfer_to_handler:
	stw	r2,GPR2(r11)
	stw	r12,_NIP(r11)
	stw	r9,_MSR(r11)
	mfctr	r12
	mfspr	r2,XER
	stw	r12,_CTR(r11)
	stw	r2,_XER(r11)
	mfspr	r12,SPRG3
	addi	r2,r12,-THREAD
	tovirt(r2,r2)			/* set r2 to current */
	beq	2f			/* if from user, fix up THREAD.regs */
	addi	r11,r1,STACK_FRAME_OVERHEAD
	stw	r11,PT_REGS(r12)
	b	3f
2:	/* if from kernel, check for stack overflow */
	lwz	r11,THREAD_INFO-THREAD(r12)
	cmplw	r1,r11			/* if r1 <= current->thread_info */
	ble-	stack_ovf		/* then the kernel stack overflowed */
3:
	mflr	r9
	lwz	r11,0(r9)		/* virtual address of handler */
	lwz	r9,4(r9)		/* where to go when done */
	FIX_SRR1(r10,r12)
	mtspr	SRR0,r11
	mtspr	SRR1,r10
	mtlr	r9
	SYNC
	RFI				/* jump to handler, enable MMU */

/*
 * On kernel stack overflow, load up an initial stack pointer
 * and call StackOverflow(regs), which should not return.
 */
stack_ovf:
	SAVE_NVGPRS(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	tovirt(r2,r2)			/* set r2 to current */
	lis	r1,init_thread_union@ha
	addi	r1,r1,init_thread_union@l
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	lis	r9,StackOverflow@ha
	addi	r9,r9,StackOverflow@l
	li	r10,MSR_KERNEL
	FIX_SRR1(r10,r12)
	mtspr	SRR0,r9
	mtspr	SRR1,r10
	SYNC
	RFI
#endif /* CONFIG_PPC_ISERIES */

/*
 * Handle a system call.
 */
	.stabs	"arch/ppc/kernel/",N_SO,0,0,0f
	.stabs	"entry.S",N_SO,0,0,0f
0:

_GLOBAL(DoSyscall)
	stw	r0,THREAD+LAST_SYSCALL(r2)
	stw	r3,ORIG_GPR3(r1)
	li	r12,0
	stw	r12,RESULT(r1)
	lwz	r11,_CCR(r1)	/* Clear SO bit in CR */
	rlwinm	r11,r11,0,4,2
	stw	r11,_CCR(r1)
#ifdef SHOW_SYSCALLS
	bl	do_show_syscall
#endif /* SHOW_SYSCALLS */
	cmpli	0,r0,NR_syscalls
	bge-	66f
	rlwinm	r10,r1,0,0,18	/* current_thread_info() */
	lwz	r10,TI_FLAGS(r10)
	andi.	r10,r10,_TIF_SYSCALL_TRACE
	bne-	syscall_dotrace
syscall_dotrace_cont:
	lis	r10,sys_call_table@h
	ori	r10,r10,sys_call_table@l
	slwi	r0,r0,2
	lwzx	r10,r10,r0	/* Fetch system call handler [ptr] */
	mtlr	r10
	addi	r9,r1,STACK_FRAME_OVERHEAD
	blrl			/* Call handler */
	.globl	ret_from_syscall
ret_from_syscall:
#ifdef SHOW_SYSCALLS
	bl	do_show_syscall_exit
#endif
	mr	r6,r3
	li	r11,-_LAST_ERRNO
	cmpl	0,r3,r11
	blt+	30f
	neg	r3,r3
	cmpi	0,r3,ERESTARTNOHAND
	bne	22f
	li	r3,EINTR
22:	lwz	r10,_CCR(r1)	/* Set SO bit in CR */
	oris	r10,r10,0x1000
	stw	r10,_CCR(r1)

	/* disable interrupts so current_thread_info()->flags can't change */
30:	li	r10,MSR_KERNEL	/* doesn't include MSR_EE */
	SYNC
	MTMSRD(r10)
	rlwinm	r12,r1,0,0,18	/* current_thread_info() */
	lwz	r9,TI_FLAGS(r12)
	andi.	r0,r9,(_TIF_SYSCALL_TRACE|_TIF_SIGPENDING|_TIF_NEED_RESCHED)
	bne-	syscall_exit_work
syscall_exit_cont:
	stwcx.	r0,0,r1			/* to clear the reservation */
	lwz	r4,_LINK(r1)
	lwz	r5,_CCR(r1)
	mtlr	r4
	mtcr	r5
	lwz	r7,_NIP(r1)
	lwz	r8,_MSR(r1)
	FIX_SRR1(r8, r0)
	lwz	r2,GPR2(r1)
	lwz	r1,GPR1(r1)
	mtspr	SRR0,r7
	mtspr	SRR1,r8
	SYNC
	RFI

66:	li	r3,ENOSYS
	b	22b

	.globl	ret_from_fork
ret_from_fork:
	REST_NVGPRS(r1)
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
	bl	schedule_tail
#endif
	li	r3,0
	b	ret_from_syscall

/* Traced system call support */
syscall_dotrace:
	SAVE_NVGPRS(r1)
	li	r0,0xc00
	stw	r0,TRAP(r1)
	bl	do_syscall_trace
	lwz	r0,GPR0(r1)	/* Restore original registers */
	lwz	r3,GPR3(r1)
	lwz	r4,GPR4(r1)
	lwz	r5,GPR5(r1)
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)
	lwz	r8,GPR8(r1)
	REST_NVGPRS(r1)
	b	syscall_dotrace_cont

syscall_exit_work:
	stw	r6,RESULT(r1)	/* Save result */
	stw	r3,GPR3(r1)	/* Update return value */
	andi.	r0,r9,_TIF_SYSCALL_TRACE
	beq	5f
	stw	r6,GPR0(r1)	/* temporary gross hack to make strace work */
	ori	r10,r10,MSR_EE
	SYNC
	MTMSRD(r10)		/* re-enable interrupts */
	lwz	r4,TRAP(r1)
	andi.	r4,r4,1
	beq	4f
	SAVE_NVGPRS(r1)
	li	r4,0xc00
	stw	r4,TRAP(r1)
4:
	bl	do_syscall_trace
	REST_NVGPRS(r1)
2:
	lwz	r3,GPR3(r1)
	li	r10,MSR_KERNEL	/* doesn't include MSR_EE */
	SYNC
	MTMSRD(r10)		/* disable interrupts again */
	rlwinm	r12,r1,0,0,18	/* current_thread_info() */
	lwz	r9,TI_FLAGS(r12)
5:
	andi.	r0,r9,_TIF_NEED_RESCHED
	bne	1f
	lwz	r5,_MSR(r1)
	andi.	r5,r5,MSR_PR
	beq	syscall_exit_cont
	andi.	r0,r9,_TIF_SIGPENDING
	beq	syscall_exit_cont
	ori	r10,r10,MSR_EE
	SYNC
	MTMSRD(r10)		/* re-enable interrupts */
	b	syscall_do_signal
1:
	ori	r10,r10,MSR_EE
	SYNC
	MTMSRD(r10)		/* re-enable interrupts */
	bl	schedule
	b	2b

#ifdef SHOW_SYSCALLS
do_show_syscall:
#ifdef SHOW_SYSCALLS_TASK
	lis	r11,show_syscalls_task@ha
	lwz	r11,show_syscalls_task@l(r11)
	cmp	0,r2,r11
	bnelr
#endif
	stw	r31,GPR31(r1)
	mflr	r31
	lis	r3,7f@ha
	addi	r3,r3,7f@l
	lwz	r4,GPR0(r1)
	lwz	r5,GPR3(r1)
	lwz	r6,GPR4(r1)
	lwz	r7,GPR5(r1)
	lwz	r8,GPR6(r1)
	lwz	r9,GPR7(r1)
	bl	printk
	lis	r3,77f@ha
	addi	r3,r3,77f@l
	lwz	r4,GPR8(r1)
	mr	r5,r2
	bl	printk
	lwz	r0,GPR0(r1)
	lwz	r3,GPR3(r1)
	lwz	r4,GPR4(r1)
	lwz	r5,GPR5(r1)
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)
	lwz	r8,GPR8(r1)
	mtlr	r31
	lwz	r31,GPR31(r1)
	blr

do_show_syscall_exit:
#ifdef SHOW_SYSCALLS_TASK
	lis	r11,show_syscalls_task@ha
	lwz	r11,show_syscalls_task@l(r11)
	cmp	0,r2,r11
	bnelr
#endif
	stw	r31,GPR31(r1)
	mflr	r31
	stw	r3,RESULT(r1)	/* Save result */
	mr	r4,r3
	lis	r3,79f@ha
	addi	r3,r3,79f@l
	bl	printk
	lwz	r3,RESULT(r1)
	mtlr	r31
	lwz	r31,GPR31(r1)
	blr

7:	.string	"syscall %d(%x, %x, %x, %x, %x, "
77:	.string	"%x), current=%p\n"
79:	.string	" -> %x\n"
	.align	2,0

#ifdef SHOW_SYSCALLS_TASK
	.data
	.globl	show_syscalls_task
show_syscalls_task:
	.long	-1
	.text
#endif
#endif /* SHOW_SYSCALLS */

/*
 * The sigsuspend and rt_sigsuspend system calls can call do_signal
 * and thus put the process into the stopped state where we might
 * want to examine its user state with ptrace.  Therefore we need
 * to save all the nonvolatile registers (r13 - r31) before calling
 * the C code.
 */
	.globl	ppc_sigsuspend
ppc_sigsuspend:
	SAVE_NVGPRS(r1)
	lwz	r0,TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,TRAP(r1)		/* register set saved */
	b	sys_sigsuspend

	.globl	ppc_rt_sigsuspend
ppc_rt_sigsuspend:
	SAVE_NVGPRS(r1)
	lwz	r0,TRAP(r1)
	rlwinm	r0,r0,0,0,30
	stw	r0,TRAP(r1)
	b	sys_rt_sigsuspend

	.globl	ppc_fork
ppc_fork:
	SAVE_NVGPRS(r1)
	lwz	r0,TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,TRAP(r1)		/* register set saved */
	b	sys_fork

	.globl	ppc_vfork
ppc_vfork:
	SAVE_NVGPRS(r1)
	lwz	r0,TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,TRAP(r1)		/* register set saved */
	b	sys_vfork

	.globl	ppc_clone
ppc_clone:
	SAVE_NVGPRS(r1)
	lwz	r0,TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,TRAP(r1)		/* register set saved */
	b	sys_clone

/*
 * This routine switches between two different tasks.  The process
 * state of one is saved on its kernel stack.  Then the state
 * of the other is restored from its kernel stack.  The memory
 * management hardware is updated to the second process's state.
 * Finally, we can return to the second process.
 * On entry, r3 points to the THREAD for the current task, r4
 * points to the THREAD for the new task.
 *
 * This routine is always called with interrupts disabled
 * (soft disabled for iSeries).
 *
 * Note: there are two ways to get to the "going out" portion
 * of this code; either by coming in via the entry (_switch)
 * or via "fork" which must set up an environment equivalent
 * to the "_switch" path.  If you change this , you'll have to
 * change the fork code also.
 *
 * The code which creates the new task context is in 'copy_thread'
 * in arch/ppc/kernel/process.c
 */	
_GLOBAL(_switch)
	stwu	r1,-INT_FRAME_SIZE(r1)
	mflr	r0
	stw	r0,INT_FRAME_SIZE+4(r1)
	/* r3-r12 are caller saved -- Cort */
	SAVE_NVGPRS(r1)
	stw	r0,_NIP(r1)	/* Return to switch caller */
	mfmsr	r11
	li	r0,MSR_FP	/* Disable floating-point */
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	oris	r0,r0,MSR_VEC@h	/* Disable altivec */
	mfspr	r12,SPRN_VRSAVE	/* save vrsave register value */
	stw	r12,THREAD+THREAD_VRSAVE(r2)
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif /* CONFIG_ALTIVEC */
	and.	r0,r0,r11	/* FP or altivec enabled? */
	beq+	1f
	andc	r11,r11,r0
	MTMSRD(r11)
	isync
1:	stw	r11,_MSR(r1)
	mfcr	r10
	stw	r10,_CCR(r1)
	stw	r1,KSP(r3)	/* Set old stack pointer */

	tophys(r0,r4)
	CLR_TOP32(r0)
	mtspr	SPRG3,r0	/* Update current THREAD phys addr */
	lwz	r1,KSP(r4)	/* Load new stack pointer */
	/* save the old current 'last' for return value */
	mr	r3,r2
	addi	r2,r4,-THREAD	/* Update current */

#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	lwz	r0,THREAD+THREAD_VRSAVE(r2)
	mtspr	SPRN_VRSAVE,r0		/* if G4, restore VRSAVE reg */
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif /* CONFIG_ALTIVEC */

	lwz	r0,_CCR(r1)
	mtcrf	0xFF,r0
	/* r3-r12 are destroyed -- Cort */
	REST_NVGPRS(r1)

	lwz	r4,_NIP(r1)	/* Return to _switch caller in new task */
	mtlr	r4
	addi	r1,r1,INT_FRAME_SIZE
	blr

	.globl	sigreturn_exit
sigreturn_exit:
	subi	r1,r3,STACK_FRAME_OVERHEAD
	rlwinm	r12,r1,0,0,18	/* current_thread_info() */
	lwz	r9,TI_FLAGS(r12)
	andi.	r0,r9,_TIF_SYSCALL_TRACE
	bnel-	do_syscall_trace
	/* fall through */

	.globl	ret_from_except_full
ret_from_except_full:
	REST_NVGPRS(r1)
	/* fall through */

	.globl	ret_from_except
ret_from_except:
	/* Hard-disable interrupts so that current_thread_info()->flags
	 * can't change between when we test it and when we return
	 * from the interrupt. */
	li	r10,MSR_KERNEL	/* doesn't include EE */
	SYNC			/* Some chip revs have problems here... */
	MTMSRD(r10)		/* disable interrupts */

	lwz	r3,_MSR(r1)	/* Returning to user mode? */
	andi.	r3,r3,MSR_PR
	beq	resume_kernel

	/* Check current_thread_info()->flags */
	rlwinm	r9,r1,0,0,18
	lwz	r9,TI_FLAGS(r9)
	andi.	r0,r9,(_TIF_SIGPENDING|_TIF_NEED_RESCHED)
	bne	do_work

#ifdef CONFIG_PREEMPT
	b	restore

resume_kernel:
	rlwinm	r9,r1,0,0,18	/* check current_thread_info->preempt_count */
	lwz	r3,TI_PREEMPT(r9)
	cmpwi	0,r0,0		/* if non-zero, just restore regs and return */
	bne	restore
	lwz	r9,TI_FLAGS(r9)
	andi.	r0,r9,_TIF_NEED_RESCHED
	bne	do_work
#else
resume_kernel:
#endif /* CONFIG_PREEMPT */

	/* interrupts are hard-disabled at this point */
restore:
	lwz	r0,GPR0(r1)
	lwz	r2,GPR2(r1)
	REST_4GPRS(3, r1)
	REST_2GPRS(7, r1)

	lwz	r10,_XER(r1)
	lwz	r11,_CTR(r1)
	mtspr	XER,r10
	mtctr	r11

	PPC405_ERR77(0,r1)
	stwcx.	r0,0,r1			/* to clear the reservation */

#ifndef CONFIG_4xx
	lwz	r9,_MSR(r1)
	andi.	r10,r9,MSR_RI		/* check if this exception occurred */
	beql	nonrecoverable		/* at a bad place (MSR:RI = 0) */

	lwz	r10,_CCR(r1)
	lwz	r11,_LINK(r1)
	mtcrf	0xFF,r10
	mtlr	r11

	/*
	 * Once we put values in SRR0 and SRR1, we are in a state
	 * where exceptions are not recoverable, since taking an
	 * exception will trash SRR0 and SRR1.  Therefore we clear the
	 * MSR:RI bit to indicate this.  If we do take an exception,
	 * we can't return to the point of the exception but we
	 * can restart the exception exit path at the label
	 * exc_exit_restart below.  -- paulus
	 */
	li	r10,MSR_KERNEL & ~MSR_RI
	SYNC
	MTMSRD(r10)		/* clear the RI bit */
	.globl exc_exit_restart
exc_exit_restart:
	lwz	r9,_MSR(r1)
	lwz	r12,_NIP(r1)
	FIX_SRR1(r9,r10)
	mtspr	SRR0,r12
	mtspr	SRR1,r9
	REST_4GPRS(9, r1)
	lwz	r1,GPR1(r1)
	.globl exc_exit_restart_end
exc_exit_restart_end:
	SYNC
	RFI

#else /* CONFIG_4xx */
	/*
	 * This is a bit different on 4xx because 4xx doesn't have
	 * the RI bit in the MSR, and because we have critical
	 * exceptions, for which we need to restore SRR0 and SRR1
	 * and then use SRR2/SRR3 to return from the exception.
	 * The TLB miss handler checks if we have interrupted
	 * the exception exit path and restarts it if so.
	 */
	lwz	r10,TRAP(r1)		/* check for critical exception */
	lwz	r11,_LINK(r1)
	andi.	r10,r10,2
	mtlr	r11
	lwz	r10,_CCR(r1)
	bne	crit_exc_exit
	mtcrf	0xff,r10
	REST_2GPRS(9, r1)
	.globl exc_exit_restart
exc_exit_restart:
	lwz	r11,_NIP(r1)
	lwz	r12,_MSR(r1)
exc_exit_start:
	mtspr	SRR0,r11
	mtspr	SRR1,r12
	REST_2GPRS(11, r1)
	lwz	r1,GPR1(r1)
	.globl exc_exit_restart_end
exc_exit_restart_end:
	PPC405_ERR77_SYNC
	rfi
	b	.			/* prevent prefetch past rfi */
crit_exc_exit:
	mtcrf	0xff,r10
	/* avoid any possible TLB misses here by turning off MSR.DR, we
	 * assume the instructions here are mapped by a pinned TLB entry */
	li	r10,MSR_IR
	mtmsr	r10
	isync
	tophys(r1, r1)
	lwz	r9,_DEAR(r1)
	lwz	r10,_ESR(r1)
	mtspr	SPRN_DEAR,r9
	mtspr	SPRN_ESR,r10
	lwz	r11,_NIP(r1)
	lwz	r12,_MSR(r1)
	mtspr	SRR2,r11
	mtspr	SRR3,r12
	lwz	r9,GPR9(r1)
	lwz	r12,GPR12(r1)
	lwz	r10,crit_sprg0@l(0)
	mtspr	SPRN_SPRG0,r10
	lwz	r10,crit_sprg1@l(0)
	mtspr	SPRN_SPRG1,r10
	lwz	r10,crit_sprg4@l(0)
	mtspr	SPRN_SPRG4,r10
	lwz	r10,crit_sprg5@l(0)
	mtspr	SPRN_SPRG5,r10
	lwz	r10,crit_sprg6@l(0)
	mtspr	SPRN_SPRG6,r10
	lwz	r10,crit_sprg7@l(0)
	mtspr	SPRN_SPRG7,r10
	lwz	r10,crit_srr0@l(0)
	mtspr	SRR0,r10
	lwz	r10,crit_srr1@l(0)
	mtspr	SRR1,r10
	lwz	r10,crit_pid@l(0)
	mtspr	SPRN_PID,r10
	lwz	r10,crit_r10@l(0)
	lwz	r11,crit_r11@l(0)
	lwz	r1,GPR1(r1)
	PPC405_ERR77_SYNC
	rfci
	b	.		/* prevent prefetch past rfci */
#endif /* CONFIG_4xx */

recheck:
	li	r10,MSR_KERNEL
	SYNC
	MTMSRD(r10)		/* disable interrupts */
	rlwinm	r9,r1,0,0,18
	lwz	r9,TI_FLAGS(r9)
#ifdef CONFIG_PREEMPT
	lwz	r0,_MSR(r1)
	li	r11,_TIF_NEED_RESCHED
	/* move MSR_PR bit down to TIF_SIGPENDING (0x4) bit */
	rlwimi	r11,r0,18+TIF_SIGPENDING,31-TIF_SIGPENDING,31-TIF_SIGPENDING
	and.	r0,r9,r11
#else /* CONFIG_PREEMPT */
	andi.	r0,r9,(_TIF_SIGPENDING|_TIF_NEED_RESCHED)
#endif /* CONFIG_PREEMPT */
	beq	restore
do_work:
	ori	r10,r10,MSR_EE
	SYNC
	MTMSRD(r10)		/* hard-enable interrupts */
	andi.	r0,r9,_TIF_NEED_RESCHED
	beq	1f
	bl	schedule
	b	recheck
1:
syscall_do_signal:
	/* save r13-r31 in the exception frame, if not already done */
	lwz	r3,TRAP(r1)
	andi.	r0,r3,1
	beq	2f
	SAVE_NVGPRS(r1)
	rlwinm	r3,r3,0,0,30
	stw	r3,TRAP(r1)
2:	li	r3,0
	addi	r4,r1,STACK_FRAME_OVERHEAD
	bl	do_signal
	REST_NVGPRS(r1)
	b	recheck

/*
 * We come here when we are at the end of handling an exception
 * that occurred at a place where taking an exception will lose
 * state information, such as the contents of SRR0 and SRR1.
 */
nonrecoverable:
	lis	r10,exc_exit_restart_end@ha
	addi	r10,r10,exc_exit_restart_end@l
	cmplw	r12,r10
	bge	3f
	lis	r11,exc_exit_restart@ha
	addi	r11,r11,exc_exit_restart@l
	cmplw	r12,r11
	blt	3f
	lis	r10,ee_restarts@ha
	lwz	r12,ee_restarts@l(r10)
	addi	r12,r12,1
	stw	r12,ee_restarts@l(r10)
	mr	r12,r11		/* restart at exc_exit_restart */
	blr
3:	/* OK, we can't recover, kill this process */
	/* but the 601 doesn't implement the RI bit, so assume it's OK */
BEGIN_FTR_SECTION
	blr
END_FTR_SECTION_IFSET(CPU_FTR_601)
	lwz	r3,TRAP(r1)
	andi.	r0,r3,1
	beq	4f
	SAVE_NVGPRS(r1)
	rlwinm	r3,r3,0,0,30
	stw	r3,TRAP(r1)
4:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	nonrecoverable_exception
	/* shouldn't return */
	b	4b

	.comm	ee_restarts,4

/*
 * PROM code for specific machines follows.  Put it 
 * here so it's easy to add arch-specific sections later.
 * -- Cort
 */
#if defined(CONFIG_ALL_PPC)
/*
 * On CHRP, the Run-Time Abstraction Services (RTAS) have to be
 * called with the MMU off.
 */
_GLOBAL(enter_rtas)
	stwu	r1,-INT_FRAME_SIZE(r1)
	mflr	r0
	stw	r0,INT_FRAME_SIZE+4(r1)
	lis	r4,rtas_data@ha
	lwz	r4,rtas_data@l(r4)
	lis	r6,1f@ha	/* physical return address for rtas */
	addi	r6,r6,1f@l
	tophys(r6,r6)
	tophys(r7,r1)
	lis	r8,rtas_entry@ha
	lwz	r8,rtas_entry@l(r8)
	mfmsr	r9
	stw	r9,8(r1)
	li	r0,MSR_KERNEL
	SYNC			/* disable interrupts so SRR0/1 */
	MTMSRD(r0)		/* don't get trashed */
	li	r9,MSR_
	mtlr	r6
	CLR_TOP32(r7)
	mtspr	SPRG2,r7
	mtspr	SRR0,r8
	mtspr	SRR1,r9
	RFI
1:	tophys(r9,r1)
	lwz	r8,INT_FRAME_SIZE+4(r9)	/* get return address */
	lwz	r9,8(r9)	/* original msr value */
	FIX_SRR1(r9,r0)
	addi	r1,r1,INT_FRAME_SIZE
	li	r0,0
	mtspr	SPRG2,r0
	mtspr	SRR0,r8
	mtspr	SRR1,r9
	RFI			/* return to caller */

	.globl	machine_check_in_rtas
machine_check_in_rtas:
	twi	31,0,0
	/* XXX load up BATs and panic */

#endif /* CONFIG_ALL_PPC */
