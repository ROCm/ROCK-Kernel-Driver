/*
 *  linux/arch/x86_64/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002  Andi Kleen SuSE Labs
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 *  Jun Nakajima <jun.nakajima@intel.com>
 *  Asit Mallick <asit.k.mallick@intel.com>
 *      Modified for Xen
 *
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * Some of this is documented in Documentation/x86/entry_64.txt
 *
 * A note on terminology:
 * - iret frame:	Architecture defined interrupt frame from SS to RIP
 *			at the top of the kernel process stack.
 *
 * Some macro usage:
 * - ENTRY/END:		Define functions in the symbol table.
 * - TRACE_IRQ_*:	Trace hardirq state for lock debugging.
 * - idtentry:		Define exception entry points.
 */
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include "calling.h"
#include <asm/asm-offsets.h>
#include <asm/msr.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/hw_irq.h>
#include <asm/page_types.h>
#include <asm/irqflags.h>
#include <asm/processor-flags.h>
#include <asm/percpu.h>
#include <asm/asm.h>
#include <asm/context_tracking.h>
#include <asm/smap.h>
#include <asm/pgtable_types.h>
#include <linux/err.h>
#include <xen/interface/xen.h>
#include <xen/interface/features.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_X86_64			(EM_X86_64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_64BIT			0x80000000
#define __AUDIT_ARCH_LE				0x40000000

.code64
.section .entry.text, "ax"

.macro TRACE_IRQS_IRETQ
#ifdef CONFIG_TRACE_IRQFLAGS
	bt	$9, EFLAGS(%rsp)		/* interrupts off? */
	jnc	1f
	TRACE_IRQS_ON
1:
#endif
.endm

/*
 * When dynamic function tracer is enabled it will add a breakpoint
 * to all locations that it is about to modify, sync CPUs, update
 * all the code, sync CPUs, then remove the breakpoints. In this time
 * if lockdep is enabled, it might jump back into the debug handler
 * outside the updating of the IST protection. (TRACE_IRQS_ON/OFF).
 *
 * We need to change the IDT table before calling TRACE_IRQS_ON/OFF to
 * make sure the stack pointer does not get reset back to the top
 * of the debug stack, and instead just reuses the current stack.
 */
#if defined(CONFIG_DYNAMIC_FTRACE) && defined(CONFIG_TRACE_IRQFLAGS)

.macro TRACE_IRQS_OFF_DEBUG
	call	debug_stack_set_zero
	TRACE_IRQS_OFF
	call	debug_stack_reset
.endm

.macro TRACE_IRQS_ON_DEBUG
	call	debug_stack_set_zero
	TRACE_IRQS_ON
	call	debug_stack_reset
.endm

.macro TRACE_IRQS_IRETQ_DEBUG
	bt	$9, EFLAGS(%rsp)		/* interrupts off? */
	jnc	1f
	TRACE_IRQS_ON_DEBUG
1:
.endm

#else
# define TRACE_IRQS_OFF_DEBUG			TRACE_IRQS_OFF
# define TRACE_IRQS_ON_DEBUG			TRACE_IRQS_ON
# define TRACE_IRQS_IRETQ_DEBUG			TRACE_IRQS_IRETQ
#endif

NMI_MASK = 0x80000000

	/*
	 * Must be consistent with the definition in arch-x86/xen-x86_64.h:
	 *     struct iret_context {
	 *        u64 rax, r11, rcx, flags, rip, cs, rflags, rsp, ss;
	 *     };
	 * with rax, r11, and rcx being taken care of in the hypercall stub.
	 */
	.macro HYPERVISOR_IRET flag
	.if \flag == 0	# return from syscall always uses the hypercall
	testb	$3, 1*8(%rsp)
	jnz	2f
	testl	$NMI_MASK, 2*8(%rsp)
	jnz	2f

	cmpb	$0, xen_features+XENFEAT_supervisor_mode_kernel(%rip)
	jne	1f

	/* Direct iret to kernel space. Correct CS and SS. */
	orl	$3, 1*8(%rsp)
	orl	$3, 4*8(%rsp)
1:	iretq
	.endif

2:	/* Slow iret via hypervisor. */
	andl 	$~NMI_MASK, 2*8(%rsp)
	pushq	$\flag & VGCF_in_syscall
	jmp	hypercall_page + (__HYPERVISOR_iret * 32)
	.endm

/*
 * 64-bit SYSCALL instruction entry. Up to 6 arguments in registers.
 *
 * 64-bit SYSCALL saves rip to rcx, clears rflags.RF, then saves rflags to r11,
 * then loads new ss, cs, and rip from previously programmed MSRs.
 * rflags gets masked by a value from another MSR (so CLD and CLAC
 * are not needed). SYSCALL does not save anything on the stack
 * and does not change rsp.
 *
 * Registers on entry:
 * rax  system call number
 * rcx  return address
 * r11  saved rflags (note: r11 is callee-clobbered register in C ABI)
 * rdi  arg0
 * rsi  arg1
 * rdx  arg2
 * r10  arg3 (needs to be moved to rcx to conform to C ABI)
 * r8   arg4
 * r9   arg5
 * (note: r12-r15, rbp, rbx are callee-preserved in C ABI)
 *
 * Only called from user space.
 *
 * When user can change pt_regs->foo always force IRET. That is because
 * it deals with uncanonical addresses better. SYSRET has trouble
 * with them due to bugs in both AMD and Intel CPUs.
 */

ENTRY(entry_SYSCALL_64)
	/*
	 * Interrupts are enabled on entry.
	 */
	/* Construct struct pt_regs on stack */
	movl	$__USER_DS, 6*8(%rsp)		/* pt_regs->ss */
	movl	$__USER_CS, 3*8(%rsp)		/* pt_regs->cs */
	movq	%rax, 8(%rsp)			/* pt_regs->orig_ax */
	movq	%rdi, 0(%rsp)			/* pt_regs->di */
	pushq	%rsi				/* pt_regs->si */
	pushq	%rdx				/* pt_regs->dx */
	pushq	%rcx				/* pt_regs->cx */
	pushq	$-ENOSYS			/* pt_regs->ax */
	pushq	%r8				/* pt_regs->r8 */
	pushq	%r9				/* pt_regs->r9 */
	pushq	%r10				/* pt_regs->r10 */
	pushq	%r11				/* pt_regs->r11 */
	sub	$(6*8), %rsp			/* pt_regs->bp, bx, r12-15 not saved */

	testl	$_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	tracesys
entry_SYSCALL_64_fastpath:
#if __SYSCALL_MASK == ~0
	cmpq	$__NR_syscall_max, %rax
#else
	andl	$__SYSCALL_MASK, %eax
	cmpl	$__NR_syscall_max, %eax
#endif
	ja	1f				/* return -ENOSYS (already in pt_regs->ax) */
	movq	%r10, %rcx
	call	*sys_call_table(, %rax, 8)
	movq	%rax, RAX(%rsp)
1:
/*
 * Syscall return path ending with SYSRET (fast path).
 * Has incompletely filled pt_regs.
 */
	LOCKDEP_SYS_EXIT
	/*
	 * We do not frame this tiny irq-off block with TRACE_IRQS_OFF/ON,
	 * it is too small to ever cause noticeable irq latency.
	 */
	DISABLE_INTERRUPTS(CLBR_NONE)

	/*
	 * We must check ti flags with interrupts (or at least preemption)
	 * off because we must *never* return to userspace without
	 * processing exit work that is enqueued if we're preempted here.
	 * In particular, returning to userspace with any of the one-shot
	 * flags (TIF_NOTIFY_RESUME, TIF_USER_RETURN_NOTIFY, etc) set is
	 * very bad.
	 */
	testl	$_TIF_ALLWORK_MASK, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	int_ret_from_sys_call_irqs_off	/* Go to the slow path */

	RESTORE_C_REGS_EXCEPT_RCX_R11
	REMOVE_PT_GPREGS_FROM_STACK 8
	xor	%ecx, %ecx
	xor	%r11, %r11
	HYPERVISOR_IRET VGCF_IN_SYSCALL

	/* Do syscall entry tracing */
tracesys:
	movq	%rsp, %rdi
	movl	$AUDIT_ARCH_X86_64, %esi
	call	syscall_trace_enter_phase1
	test	%rax, %rax
	jnz	tracesys_phase2			/* if needed, run the slow path */
	RESTORE_C_REGS_EXCEPT_RAX		/* else restore clobbered regs */
	movq	ORIG_RAX(%rsp), %rax
	jmp	entry_SYSCALL_64_fastpath	/* and return to the fast path */

tracesys_phase2:
	SAVE_EXTRA_REGS
	movq	%rsp, %rdi
	movl	$AUDIT_ARCH_X86_64, %esi
	movq	%rax, %rdx
	call	syscall_trace_enter_phase2

	/*
	 * Reload registers from stack in case ptrace changed them.
	 * We don't reload %rax because syscall_trace_entry_phase2() returned
	 * the value it wants us to use in the table lookup.
	 */
	RESTORE_C_REGS_EXCEPT_RAX
	RESTORE_EXTRA_REGS
#if __SYSCALL_MASK == ~0
	cmpq	$__NR_syscall_max, %rax
#else
	andl	$__SYSCALL_MASK, %eax
	cmpl	$__NR_syscall_max, %eax
#endif
	ja	1f				/* return -ENOSYS (already in pt_regs->ax) */
	movq	%r10, %rcx			/* fixup for C */
	call	*sys_call_table(, %rax, 8)
	movq	%rax, RAX(%rsp)
1:
	/* Use IRET because user could have changed pt_regs->foo */

/*
 * Syscall return path ending with IRET.
 * Has correct iret frame.
 */
GLOBAL(int_ret_from_sys_call)
	DISABLE_INTERRUPTS(CLBR_NONE)
int_ret_from_sys_call_irqs_off: /* jumps come here from the irqs-off SYSRET path */
	TRACE_IRQS_OFF
	movl	$_TIF_ALLWORK_MASK, %edi
	/* edi:	mask to check */
GLOBAL(int_with_check)
	LOCKDEP_SYS_EXIT_IRQ
	GET_THREAD_INFO(%rcx)
	movl	TI_flags(%rcx), %edx
	andl	%edi, %edx
	jnz	int_careful
	andl	$~TS_COMPAT, TI_status(%rcx)
	jmp	restore_c_regs_and_iret

	/*
	 * Either reschedule or signal or syscall exit tracking needed.
	 * First do a reschedule test.
	 * edx:	work, edi: workmask
	 */
int_careful:
	bt	$TIF_NEED_RESCHED, %edx
	jnc	int_very_careful
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq	%rdi
	SCHEDULE_USER
	popq	%rdi
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp	int_with_check

	/* handle signals and tracing -- both require a full pt_regs */
int_very_careful:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_EXTRA_REGS
	/* Check for syscall exit trace */
	testl	$_TIF_WORK_SYSCALL_EXIT, %edx
	jz	int_signal
	pushq	%rdi
	leaq	8(%rsp), %rdi			/* &ptregs -> arg1 */
	call	syscall_trace_leave
	popq	%rdi
	andl	$~(_TIF_WORK_SYSCALL_EXIT|_TIF_SYSCALL_EMU), %edi
	jmp	int_restore_rest

int_signal:
	testl	$_TIF_DO_NOTIFY_MASK, %edx
	jz	1f
	movq	%rsp, %rdi			/* &ptregs -> arg1 */
	xorl	%esi, %esi			/* oldset -> arg2 */
	call	do_notify_resume
1:	movl	$_TIF_WORK_MASK, %edi
int_restore_rest:
	RESTORE_EXTRA_REGS
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp	int_with_check
END(entry_SYSCALL_64)


	.macro FORK_LIKE func
ENTRY(stub_\func)
	SAVE_EXTRA_REGS 8
	jmp	sys_\func
END(stub_\func)
	.endm

	FORK_LIKE  clone
	FORK_LIKE  fork
	FORK_LIKE  vfork

ENTRY(stub_execve)
	call	sys_execve
return_from_execve:
	testl	%eax, %eax
	jz	1f
	/* exec failed, can use fast SYSRET code path in this case */
	ret
1:
	/* must use IRET code path (pt_regs->cs may have changed) */
	addq	$8, %rsp
	ZERO_EXTRA_REGS
	movq	%rax, RAX(%rsp)
	jmp	int_ret_from_sys_call
END(stub_execve)
/*
 * Remaining execve stubs are only 7 bytes long.
 * ENTRY() often aligns to 16 bytes, which in this case has no benefits.
 */
	.align	8
GLOBAL(stub_execveat)
	call	sys_execveat
	jmp	return_from_execve
END(stub_execveat)

#if defined(CONFIG_X86_X32_ABI) || defined(CONFIG_IA32_EMULATION)
	.align	8
GLOBAL(stub_x32_execve)
GLOBAL(stub32_execve)
	call	compat_sys_execve
	jmp	return_from_execve
END(stub32_execve)
END(stub_x32_execve)
	.align	8
GLOBAL(stub_x32_execveat)
GLOBAL(stub32_execveat)
	call	compat_sys_execveat
	jmp	return_from_execve
END(stub32_execveat)
END(stub_x32_execveat)
#endif

/*
 * sigreturn is special because it needs to restore all registers on return.
 * This cannot be done with SYSRET, so use the IRET return path instead.
 */
ENTRY(stub_rt_sigreturn)
	/*
	 * SAVE_EXTRA_REGS result is not normally needed:
	 * sigreturn overwrites all pt_regs->GPREGS.
	 * But sigreturn can fail (!), and there is no easy way to detect that.
	 * To make sure RESTORE_EXTRA_REGS doesn't restore garbage on error,
	 * we SAVE_EXTRA_REGS here.
	 */
	SAVE_EXTRA_REGS 8
	call	sys_rt_sigreturn
return_from_stub:
	addq	$8, %rsp
	RESTORE_EXTRA_REGS
	movq	%rax, RAX(%rsp)
	jmp	int_ret_from_sys_call
END(stub_rt_sigreturn)

#ifdef CONFIG_X86_X32_ABI
ENTRY(stub_x32_rt_sigreturn)
	SAVE_EXTRA_REGS 8
	call	sys32_x32_rt_sigreturn
	jmp	return_from_stub
END(stub_x32_rt_sigreturn)
#endif

/*
 * A newly forked process directly context switches into this address.
 *
 * rdi: prev task we switched from
 */
ENTRY(ret_from_fork)

	LOCK ; btr $TIF_FORK, TI_flags(%r8)

	pushq	$0x0002
	popfq					/* reset kernel eflags */

	call	schedule_tail			/* rdi: 'prev' task parameter */

	RESTORE_EXTRA_REGS

	testb	$3, CS(%rsp)			/* from kernel_thread? */

	/*
	 * By the time we get here, we have no idea whether our pt_regs,
	 * ti flags, and ti status came from the 64-bit SYSCALL fast path,
	 * the slow path, or one of the 32-bit compat paths.
	 * Use IRET code path to return, since it can safely handle
	 * all of the above.
	 */
	jnz	int_ret_from_sys_call

	/*
	 * We came from kernel_thread
	 * Need to set the proper %ss (not NULL) for ring 3 iretq
	 */
	movl	$__KERNEL_DS, SS(%rsp)
	/* nb: we depend on RESTORE_EXTRA_REGS above */
	movq	%rbp, %rdi
	call	*%rbx
	movl	$0, RAX(%rsp)
	RESTORE_EXTRA_REGS
	jmp	int_ret_from_sys_call
END(ret_from_fork)

/*
 * Interrupt exit.
 */
	/* Interrupt came from user space */
retint_user:
	GET_THREAD_INFO(%rcx)

	/* %rcx: thread info. Interrupts are off. */
retint_with_reschedule:
	movl	$_TIF_WORK_MASK, %edi
retint_check:
	LOCKDEP_SYS_EXIT_IRQ
	movl	TI_flags(%rcx), %edx
	andl	%edi, %edx
	jnz	retint_careful
	jmp	restore_c_regs_and_iret

/* Returning to kernel space */
retint_kernel:
#ifdef CONFIG_PREEMPT
	/* Interrupts are off */
	/* Check if we need preemption */
	bt	$9, EFLAGS(%rsp)		/* were interrupts off? */
	jnc	1f
0:	cmpl	$0, PER_CPU_VAR(__preempt_count)
	jnz	1f
	call	preempt_schedule_irq
	jmp	0b
1:
#endif

/*
 * At this label, code paths which return to kernel and to user,
 * which come from interrupts/exception and from syscalls, merge.
 */
restore_c_regs_and_iret:
	movl	EFLAGS(%rsp), %eax
	shr	$9, %eax			/* EAX[0] == IRET_EFLAGS.IF */
	GET_VCPU_INFO
	andb	evtchn_upcall_mask(%rsi), %al
	andb	$1, %al				/* EAX[0] == IRET_EFLAGS.IF & event_mask */
	jnz	restore_all_enable_events	/*        != 0 => enable event delivery */
.Lrestore_c_regs_and_iret:
	/*
	 * The iretq could re-enable interrupts:
	 */
	TRACE_IRQS_IRETQ
	RESTORE_C_REGS
	REMOVE_PT_GPREGS_FROM_STACK 8
	HYPERVISOR_IRET 0

	/* edi: workmask, edx: work */
retint_careful:
	bt	$TIF_NEED_RESCHED, %edx
	jnc	retint_signal
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq	%rdi
	SCHEDULE_USER
	popq	%rdi
	GET_THREAD_INFO(%rcx)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp	retint_check

retint_signal:
	testl	$_TIF_DO_NOTIFY_MASK, %edx
	jz	restore_c_regs_and_iret
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_EXTRA_REGS
	movq	$-1, ORIG_RAX(%rsp)
	xorl	%esi, %esi			/* oldset */
	movq	%rsp, %rdi			/* &pt_regs */
	call	do_notify_resume
	RESTORE_EXTRA_REGS
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)
	jmp	retint_with_reschedule

END(retint_check)

/*
 * Exception entry points.
 */
#define CPU_TSS_IST(x) PER_CPU_VAR(cpu_tss) + (TSS_ist + ((x) - 1) * 8)

.macro idtentry sym do_sym has_error_code:req paranoid=0 shift_ist=-1
ENTRY(\sym)
	/* Sanity check */
	.if \shift_ist != -1 && \paranoid == 0
	.error "using shift_ist requires paranoid=1"
	.endif

	ASM_CLAC
	movq	0(%rsp), %rcx
	movq	8(%rsp), %r11

	.if \has_error_code
	ALLOC_PT_GPREGS_ON_STACK -2*8
	.else
	movq	$-1, 8(%rsp)			/* ORIG_RAX: no syscall to restart */
	ALLOC_PT_GPREGS_ON_STACK -1*8
	.endif

	.if \paranoid
	.if \paranoid == 1
	testb	$3, CS(%rsp)			/* If coming from userspace, switch stacks */
	jnz	1f
	.endif
	call	paranoid_entry
	.else
	call	error_entry
	.endif
	/* returned flag: ebx=0: need swapgs on exit, ebx=1: don't need it */

	.if \paranoid
	.if \shift_ist != -1
	TRACE_IRQS_OFF_DEBUG			/* reload IDT in case of recursion */
	.else
	TRACE_IRQS_OFF
	.endif
	.endif

	movq	%rsp, %rdi			/* pt_regs pointer */

	.if \has_error_code
	movq	ORIG_RAX(%rsp), %rsi		/* get error code */
	movq	$-1, ORIG_RAX(%rsp)		/* no syscall to restart */
	.else
	xorl	%esi, %esi			/* no error code */
	.endif

	.if \shift_ist != -1
	subq	$EXCEPTION_STKSZ, CPU_TSS_IST(\shift_ist)
	.endif

	call	\do_sym

	.if \shift_ist != -1
	addq	$EXCEPTION_STKSZ, CPU_TSS_IST(\shift_ist)
	.endif

	/* these procedures expect "no swapgs" flag in ebx */
	.if \paranoid
	jmp	paranoid_exit
	.else
	jmp	error_exit
	.endif

	.if \paranoid == 1
	/*
	 * Paranoid entry from userspace.  Switch stacks and treat it
	 * as a normal entry.  This means that paranoid handlers
	 * run in real process context if user_mode(regs).
	 */
1:
	call	error_entry


	movq	%rsp, %rdi			/* pt_regs pointer */
	call	sync_regs
	movq	%rax, %rsp			/* switch stack */

	movq	%rsp, %rdi			/* pt_regs pointer */

	.if \has_error_code
	movq	ORIG_RAX(%rsp), %rsi		/* get error code */
	movq	$-1, ORIG_RAX(%rsp)		/* no syscall to restart */
	.else
	xorl	%esi, %esi			/* no error code */
	.endif

	call	\do_sym

	jmp	error_exit			/* %ebx: no swapgs flag */
	.endif
END(\sym)
.endm

#ifdef CONFIG_TRACING
.macro trace_idtentry sym do_sym has_error_code:req
idtentry trace_\sym trace_\do_sym has_error_code=\has_error_code
idtentry \sym \do_sym has_error_code=\has_error_code
.endm
#else
.macro trace_idtentry sym do_sym has_error_code:req
idtentry \sym \do_sym has_error_code=\has_error_code
.endm
#endif

/*
 * Copied from arch/xen/i386/kernel/entry.S
 */               
# A note on the "critical region" in our callback handler.
# We want to avoid stacking callback handlers due to events occurring
# during handling of the last event. To do this, we keep events disabled
# until we've done all processing. HOWEVER, we must enable events before
# popping the stack frame (can't be done atomically) and so it would still
# be possible to get enough handler activations to overflow the stack.
# Although unlikely, bugs of that kind are hard to track down, so we'd
# like to avoid the possibility.
# So, on entry to the handler we detect whether we interrupted an
# existing activation in its critical region -- if so, we pop the current
# activation and restart the handler using the previous one.
ENTRY(do_hypervisor_callback)   # do_hypervisor_callback(struct *pt_regs)
# Since we don't modify %rdi, evtchn_do_upall(struct *pt_regs) will
# see the correct pointer to the pt_regs
	movq	%rdi, %rsp		# we don't return, adjust the stack frame
11:	incl	PER_CPU_VAR(irq_count)
	movq	%rsp, %rbp
	cmovzq	PER_CPU_VAR(irq_stack_ptr), %rsp
	pushq	%rbp			# backlink for old unwinder
	call	evtchn_do_upcall
	popq 	%rsp
	decl	PER_CPU_VAR(irq_count)
#ifndef CONFIG_PREEMPT
	test	%al, %al
	jz	error_exit
	cmpl	$0, PER_CPU_VAR(__preempt_count)
	jnz	error_exit
	bt	$9, EFLAGS(%rsp)
	jnc	error_exit
	movb	$0, PER_CPU_VAR(privcmd_hcall)
	call	preempt_schedule_irq
	movb	$1, PER_CPU_VAR(privcmd_hcall)
#endif
	jmp	error_exit
END(do_hypervisor_callback)

        ALIGN
restore_all_enable_events:  
	TRACE_IRQS_ON
	__ENABLE_INTERRUPTS

.Lscrit: /**** START OF CRITICAL REGION ****/
	__TEST_PENDING
	jz	.Lrestore_c_regs_and_iret

	__DISABLE_INTERRUPTS
.Lecrit: /**** END OF CRITICAL REGION ****/
	SAVE_EXTRA_REGS
        movq	%rsp, %rdi		# set the argument again
	jmp	11b
# At this point, unlike on x86-32, we don't do the fixup to simplify the 
# code and the stack frame is more complex on x86-64.
# When the kernel is interrupted in the critical section, the kernel 
# will do IRET in that case, and everything will be restored at that point, 
# i.e. it just resumes from the next instruction interrupted with the same context. 

# Hypervisor uses this for application faults while it executes.
# We get here for two reasons:
#  1. Fault while reloading DS, ES, FS or GS
#  2. Fault while executing IRET
# Category 1 we do not need to fix up as Xen has already reloaded all segment
# registers that could be reloaded and zeroed the others.
# Category 2 we fix up by killing the current process. We cannot use the
# normal Linux return path in this case because if we use the IRET hypercall
# to pop the stack frame we end up in an infinite loop of failsafe callbacks.
# We distinguish between categories by comparing each saved segment register
# with its current contents: any discrepancy means we in category 1.
ENTRY(failsafe_callback)
	ASM_CLAC
	movw	%ds, %cx
	cmpw	%cx, 0x10(%rsp)
	jne	1f
	movw	%es, %cx
	cmpw	%cx, 0x18(%rsp)
	jne	1f
	movw	%fs, %cx
	cmpw	%cx, 0x20(%rsp)
	jne	1f
	movw	%gs, %cx
	cmpw	%cx, 0x28(%rsp)
	jne	1f
	/* All segments match their saved values => Category 2 (Bad IRET). */
	movq	0(%rsp), %rcx
	movq	8(%rsp), %r11
	addq	$0x30, %rsp
	movq	$11, %rdi		/* SIGSEGV */
	jmp	do_exit
1:	/* Segment mismatch => Category 1 (Bad segment). Retry the IRET. */
	movq	0(%rsp), %rcx
	movq	8(%rsp), %r11
	addq	$0x30, %rsp
	pushq	$-1
	ALLOC_PT_GPREGS_ON_STACK
	SAVE_C_REGS
	SAVE_EXTRA_REGS
	jmp	error_exit

idtentry divide_error			do_divide_error			has_error_code=0
idtentry overflow			do_overflow			has_error_code=0
idtentry bounds				do_bounds			has_error_code=0
idtentry invalid_op			do_invalid_op			has_error_code=0
idtentry device_not_available		do_device_not_available		has_error_code=0
idtentry hypervisor_callback		do_hypervisor_callback		has_error_code=0
idtentry coprocessor_segment_overrun	do_coprocessor_segment_overrun	has_error_code=0
idtentry invalid_TSS			do_invalid_TSS			has_error_code=1
idtentry segment_not_present		do_segment_not_present		has_error_code=1
idtentry coprocessor_error		do_coprocessor_error		has_error_code=0
idtentry alignment_check		do_alignment_check		has_error_code=1
idtentry simd_coprocessor_error		do_simd_coprocessor_error	has_error_code=0

/* Call softirq on interrupt stack. Interrupts are off. */
ENTRY(do_softirq_own_stack)
	pushq	%rbp
	mov	%rsp, %rbp
	incl	PER_CPU_VAR(irq_count)
	cmove	PER_CPU_VAR(irq_stack_ptr), %rsp
	push	%rbp				/* frame pointer backlink */
	call	__do_softirq
	leaveq
	decl	PER_CPU_VAR(irq_count)
	ret
END(do_softirq_own_stack)

idtentry debug			do_debug		has_error_code=0
idtentry nmi			do_nmi_callback		has_error_code=0
idtentry int3			do_int3			has_error_code=0
idtentry stack_segment		do_stack_segment	has_error_code=1

idtentry general_protection	do_general_protection	has_error_code=1
trace_idtentry page_fault	do_page_fault		has_error_code=1

#ifdef CONFIG_KVM_GUEST
idtentry async_page_fault	do_async_page_fault	has_error_code=1
#endif

#ifdef CONFIG_X86_MCE
idtentry machine_check					has_error_code=0	do_sym=*machine_check_vector(%rip)
#endif

#ifndef CONFIG_XEN
/*
 * Save all registers in pt_regs, and switch gs if needed.
 * Use slow, but surefire "are we in kernel?" check.
 * Return: ebx=0: need swapgs on exit, ebx=1: otherwise
 */
ENTRY(paranoid_entry)
	cld
	SAVE_C_REGS 8
	SAVE_EXTRA_REGS 8
	movl	$1, %ebx
	movl	$MSR_GS_BASE, %ecx
	rdmsr
	testl	%edx, %edx
	js	1f				/* negative -> in kernel */
	SWAPGS
	xorl	%ebx, %ebx
1:	ret
END(paranoid_entry)

/*
 * "Paranoid" exit path from exception stack.  This is invoked
 * only on return from non-NMI IST interrupts that came
 * from kernel space.
 *
 * We may be returning to very strange contexts (e.g. very early
 * in syscall entry), so checking for preemption here would
 * be complicated.  Fortunately, we there's no good reason
 * to try to handle preemption here.
 *
 * On entry, ebx is "no swapgs" flag (1: don't need swapgs, 0: need it)
 */
ENTRY(paranoid_exit)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF_DEBUG
	testl	%ebx, %ebx			/* swapgs needed? */
	jnz	paranoid_exit_no_swapgs
	TRACE_IRQS_IRETQ
	SWAPGS_UNSAFE_STACK
	jmp	paranoid_exit_restore
paranoid_exit_no_swapgs:
	TRACE_IRQS_IRETQ_DEBUG
paranoid_exit_restore:
	RESTORE_EXTRA_REGS
	RESTORE_C_REGS
	REMOVE_PT_GPREGS_FROM_STACK 8
	INTERRUPT_RETURN
END(paranoid_exit)
#endif

/*
 * Save all registers in pt_regs, and switch gs if needed.
 * Return: EBX=0: came from user mode; EBX=1: otherwise
 */
ENTRY(error_entry)
	cld
	SAVE_C_REGS 8
	SAVE_EXTRA_REGS 8
#ifndef CONFIG_XEN
	xorl	%ebx, %ebx
	testb	$3, CS+8(%rsp)
	jz	error_kernelspace

	/* We entered from user mode */
	SWAPGS

error_entry_done:
#endif
	TRACE_IRQS_OFF
	ret

#ifndef CONFIG_XEN
	/*
	 * There are two places in the kernel that can potentially fault with
	 * usergs. Handle them here.  B stepping K8s sometimes report a
	 * truncated RIP for IRET exceptions returning to compat mode. Check
	 * for these here too.
	 */
error_kernelspace:
	incl	%ebx
	leaq	native_irq_return_iret(%rip), %rcx
	cmpq	%rcx, RIP+8(%rsp)
	je	error_bad_iret
	movl	%ecx, %eax			/* zero extend */
	cmpq	%rax, RIP+8(%rsp)
	je	bstep_iret
	cmpq	$gs_change, RIP+8(%rsp)
	jne	error_entry_done

	/*
	 * hack: gs_change can fail with user gsbase.  If this happens, fix up
	 * gsbase and proceed.  We'll fix up the exception and land in
	 * gs_change's error handler with kernel gsbase.
	 */
	SWAPGS
	jmp	error_entry_done

bstep_iret:
	/* Fix truncated RIP */
	movq	%rcx, RIP+8(%rsp)
	/* fall through */

error_bad_iret:
	/*
	 * We came from an IRET to user mode, so we have user gsbase.
	 * Switch to kernel gsbase:
	 */
	SWAPGS

	/*
	 * Pretend that the exception came from user mode: set up pt_regs
	 * as if we faulted immediately after IRET and clear EBX so that
	 * error_exit knows that we will be returning to user mode.
	 */
	mov	%rsp, %rdi
	call	fixup_bad_iret
	mov	%rax, %rsp
	decl	%ebx
	jmp	error_entry_done
#endif
END(error_entry)


ENTRY(error_exit)
	RESTORE_EXTRA_REGS
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testb	$3, CS(%rsp)
	jz	retint_kernel
	jmp	retint_user
END(error_exit)

#define extern #
#include <asm-generic/percpu.h>

.pushsection PER_CPU_BASE_SECTION, "aw", @progbits
in_NMI:	.byte	0
.popsection

do_nmi_callback:
	addq	$8, %rsp
	orb	$1, PER_CPU_VAR(in_NMI)
	js	1f
0:
	movb	$0x80, PER_CPU_VAR(in_NMI)
	call	do_nmi
	movl	$0x80, %eax
	cmpxchgb %ah, PER_CPU_VAR(in_NMI)
	jne 	0b
	orl	$NMI_MASK, EFLAGS(%rsp)
1:
	RESTORE_EXTRA_REGS
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)
	jmp	restore_c_regs_and_iret
END(do_nmi_callback)

#ifndef CONFIG_IA32_EMULATION
ENTRY(ignore_sysret)
	popq	%rcx
	popq	%r11
	mov	$-ENOSYS, %eax
	HYPERVISOR_IRET VGCF_i387_valid
END(ignore_sysret)
#endif
