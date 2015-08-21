/*
 *  Copyright (C) 1991,1992  Linus Torvalds
 *
 * entry_32.S contains the system-call and low-level fault and trap handling routines.
 *
 * Stack layout in 'syscall_exit':
 *	ptrace needs to have all registers on the stack.
 *	If the order here is changed, it needs to be
 *	updated in fork.c:copy_process(), signal.c:do_signal(),
 *	ptrace.c and ptrace.h
 *
 *	 0(%esp) - %ebx
 *	 4(%esp) - %ecx
 *	 8(%esp) - %edx
 *	 C(%esp) - %esi
 *	10(%esp) - %edi
 *	14(%esp) - %ebp
 *	18(%esp) - %eax
 *	1C(%esp) - %ds
 *	20(%esp) - %es
 *	24(%esp) - %fs
 *	28(%esp) - %gs		saved iff !CONFIG_X86_32_LAZY_GS
 *	2C(%esp) - orig_eax
 *	30(%esp) - %eip
 *	34(%esp) - %cs
 *	38(%esp) - %eflags
 *	3C(%esp) - %oldesp
 *	40(%esp) - %oldss
 */

#include <linux/linkage.h>
#include <linux/err.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/smp.h>
#include <asm/page_types.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/ftrace.h>
#include <asm/irq_vectors.h>
#include <asm/cpufeature.h>
#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <xen/interface/xen.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_I386		(EM_386|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_LE		0x40000000

#ifndef CONFIG_AUDITSYSCALL
# define sysenter_audit		syscall_trace_entry
# define sysexit_audit		syscall_exit_work
#endif

	.section .entry.text, "ax"

/*
 * We use macros for low-level operations which need to be overridden
 * for paravirtualization.  The following will never clobber any registers:
 *   INTERRUPT_RETURN (aka. "iret")
 *   GET_CR0_INTO_EAX (aka. "movl %cr0, %eax")
 *   ENABLE_INTERRUPTS_SYSEXIT (aka "sti; sysexit").
 *
 * For DISABLE_INTERRUPTS/ENABLE_INTERRUPTS (aka "cli"/"sti"), you must
 * specify what registers can be overwritten (CLBR_NONE, CLBR_EAX/EDX/ECX/ANY).
 * Allowing a register to be clobbered can shrink the paravirt replacement
 * enough to patch inline, increasing performance.
 */

/* Pseudo-eflags. */
NMI_MASK	= 0x80000000

#ifdef CONFIG_PREEMPT
# define preempt_stop(clobbers)	DISABLE_INTERRUPTS(clobbers); TRACE_IRQS_OFF
#else
# define preempt_stop(clobbers)
# define resume_kernel		restore_all
#endif

.macro TRACE_IRQS_IRET
#ifdef CONFIG_TRACE_IRQFLAGS
	testl	$X86_EFLAGS_IF, PT_EFLAGS(%esp)     # interrupts off?
	jz	1f
	TRACE_IRQS_ON
1:
#endif
.endm

/*
 * User gs save/restore
 *
 * %gs is used for userland TLS and kernel only uses it for stack
 * canary which is required to be at %gs:20 by gcc.  Read the comment
 * at the top of stackprotector.h for more info.
 *
 * Local labels 98 and 99 are used.
 */
#ifdef CONFIG_X86_32_LAZY_GS

 /* unfortunately push/pop can't be no-op */
.macro PUSH_GS
	pushl	$0
.endm
.macro POP_GS pop=0
	addl	$(4 + \pop), %esp
.endm
.macro POP_GS_EX
.endm

 /* all the rest are no-op */
.macro PTGS_TO_GS
.endm
.macro PTGS_TO_GS_EX
.endm
.macro GS_TO_REG reg
.endm
.macro REG_TO_PTGS reg
.endm
.macro SET_KERNEL_GS reg
.endm

#else	/* CONFIG_X86_32_LAZY_GS */

.macro PUSH_GS
	pushl	%gs
.endm

.macro POP_GS pop=0
98:	popl	%gs
  .if \pop <> 0
	add	$\pop, %esp
  .endif
.endm
.macro POP_GS_EX
.pushsection .fixup, "ax"
99:	movl	$0, (%esp)
	jmp	98b
.popsection
	_ASM_EXTABLE(98b, 99b)
.endm

.macro PTGS_TO_GS
98:	mov	PT_GS(%esp), %gs
.endm
.macro PTGS_TO_GS_EX
.pushsection .fixup, "ax"
99:	movl	$0, PT_GS(%esp)
	jmp	98b
.popsection
	_ASM_EXTABLE(98b, 99b)
.endm

.macro GS_TO_REG reg
	movl	%gs, \reg
.endm
.macro REG_TO_PTGS reg
	movl	\reg, PT_GS(%esp)
.endm
.macro SET_KERNEL_GS reg
	movl	$(__KERNEL_STACK_CANARY), \reg
	movl	\reg, %gs
.endm

#endif /* CONFIG_X86_32_LAZY_GS */

.macro SAVE_ALL
	cld
	PUSH_GS
	pushl	%fs
	pushl	%es
	pushl	%ds
	pushl	%eax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	movl	$(__USER_DS), %edx
	movl	%edx, %ds
	movl	%edx, %es
	movl	$(__KERNEL_PERCPU), %edx
	movl	%edx, %fs
	SET_KERNEL_GS %edx
.endm

.macro RESTORE_INT_REGS
	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp
	popl	%eax
.endm

.macro RESTORE_REGS pop=0
	RESTORE_INT_REGS
1:	popl	%ds
2:	popl	%es
3:	popl	%fs
	POP_GS \pop
.pushsection .fixup, "ax"
4:	movl	$0, (%esp)
	jmp	1b
5:	movl	$0, (%esp)
	jmp	2b
6:	movl	$0, (%esp)
	jmp	3b
.popsection
	_ASM_EXTABLE(1b, 4b)
	_ASM_EXTABLE(2b, 5b)
	_ASM_EXTABLE(3b, 6b)
	POP_GS_EX
.endm

ENTRY(ret_from_fork)
	pushl	%eax
	call	schedule_tail
	GET_THREAD_INFO(%ebp)
	popl	%eax
	pushl	$0x0202				# Reset kernel eflags
	popfl
	jmp	syscall_exit
END(ret_from_fork)

ENTRY(ret_from_kernel_thread)
	pushl	%eax
	call	schedule_tail
	GET_THREAD_INFO(%ebp)
	popl	%eax
	pushl	$0x0202				# Reset kernel eflags
	popfl
	movl	PT_EBP(%esp), %eax
	call	*PT_EBX(%esp)
	movl	$0, PT_EAX(%esp)
	jmp	syscall_exit
ENDPROC(ret_from_kernel_thread)

/*
 * Return to user mode is not as complex as all this looks,
 * but we want the default path for a system call return to
 * go as quickly as possible which is why some of this is
 * less clear than it otherwise should be.
 */

	# userspace resumption stub bypassing syscall exit tracing
	ALIGN
ret_from_exception:
	preempt_stop(CLBR_ANY)
ret_from_intr:
	GET_THREAD_INFO(%ebp)
#ifdef CONFIG_VM86
	movl	PT_EFLAGS(%esp), %eax		# mix EFLAGS and CS
	movb	PT_CS(%esp), %al
	andl	$(X86_EFLAGS_VM | SEGMENT_RPL_MASK), %eax
#else
	/*
	 * We can be coming here from child spawned by kernel_thread().
	 */
	movl	PT_CS(%esp), %eax
	andl	$SEGMENT_RPL_MASK, %eax
#endif
	cmpl	$USER_RPL, %eax
	jb	resume_kernel			# not returning to v8086 or userspace

ENTRY(resume_userspace)
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)		# make sure we don't miss an interrupt
						# setting need_resched or sigpending
						# between sampling and the iret
	TRACE_IRQS_OFF
	movl	TI_flags(%ebp), %ecx
	andl	$_TIF_WORK_MASK, %ecx		# is there any work to be done on
						# int/exception return?
	jne	work_pending
	jmp	restore_all
END(ret_from_exception)

#ifdef CONFIG_PREEMPT
ENTRY(resume_kernel)
	DISABLE_INTERRUPTS(CLBR_ANY)
need_resched:
	cmpl	$0, PER_CPU_VAR(__preempt_count)
	jnz	restore_all
	testl	$X86_EFLAGS_IF, PT_EFLAGS(%esp)	# interrupts off (exception path) ?
	jz	restore_all
	call	preempt_schedule_irq
	jmp	need_resched
END(resume_kernel)
#endif

/*
 * SYSENTER_RETURN points to after the SYSENTER instruction
 * in the vsyscall page.  See vsyscall-sysentry.S, which defines
 * the symbol.
 */

	# SYSENTER  call handler stub
ENTRY(entry_SYSENTER_32)
	movl	SYSENTER_stack_sp0(%esp), %esp
sysenter_past_esp:
	/*
	 * Interrupts are disabled here, but we can't trace it until
	 * enough kernel state to call TRACE_IRQS_OFF can be called - but
	 * we immediately enable interrupts at that point anyway.
	 */
	pushl	$__USER_DS
	pushl	%ebp
	pushfl
	orl	$X86_EFLAGS_IF, (%esp)
	pushl	$__USER_CS
	/*
	 * Push current_thread_info()->sysenter_return to the stack.
	 * A tiny bit of offset fixup is necessary: TI_sysenter_return
	 * is relative to thread_info, which is at the bottom of the
	 * kernel stack page.  4*4 means the 4 words pushed above;
	 * TOP_OF_KERNEL_STACK_PADDING takes us to the top of the stack;
	 * and THREAD_SIZE takes us to the bottom.
	 */
	pushl	((TI_sysenter_return) - THREAD_SIZE + TOP_OF_KERNEL_STACK_PADDING + 4*4)(%esp)

	pushl	%eax
	SAVE_ALL
	ENABLE_INTERRUPTS(CLBR_NONE)

/*
 * Load the potential sixth argument from user stack.
 * Careful about security.
 */
	cmpl	$__PAGE_OFFSET-3, %ebp
	jae	syscall_fault
	ASM_STAC
1:	movl	(%ebp), %ebp
	ASM_CLAC
	movl	%ebp, PT_EBP(%esp)
	_ASM_EXTABLE(1b, syscall_fault)

	GET_THREAD_INFO(%ebp)

	testl	$_TIF_WORK_SYSCALL_ENTRY, TI_flags(%ebp)
	jnz	sysenter_audit
sysenter_do_call:
	cmpl	$(NR_syscalls), %eax
	jae	sysenter_badsys
	call	*sys_call_table(, %eax, 4)
sysenter_after_call:
	movl	%eax, PT_EAX(%esp)
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	movl	TI_flags(%ebp), %ecx
	testl	$_TIF_ALLWORK_MASK, %ecx
	jnz	sysexit_audit
sysenter_exit:
/* if something modifies registers it must also disable sysexit */
	movl	PT_EIP(%esp), %edx
	movl	PT_OLDESP(%esp), %ecx
	xorl	%ebp, %ebp
#ifdef CONFIG_XEN_VCPU_INFO_PLACEMENT
	GET_VCPU_INFO
#endif
	TRACE_IRQS_ON
1:	mov	PT_FS(%esp), %fs
	PTGS_TO_GS
	ENABLE_INTERRUPTS_SYSEXIT

#ifdef CONFIG_AUDITSYSCALL
sysenter_audit:
	testl	$(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT), TI_flags(%ebp)
	jnz	syscall_trace_entry
	/* movl	PT_EAX(%esp), %eax already set, syscall number: 1st arg to audit */
	movl	PT_EBX(%esp), %edx		/* ebx/a0: 2nd arg to audit */
	/* movl	PT_ECX(%esp), %ecx already set, a1: 3nd arg to audit */
	pushl	PT_ESI(%esp)			/* a3: 5th arg */
	pushl	PT_EDX+4(%esp)			/* a2: 4th arg */
	call	__audit_syscall_entry
	popl	%ecx				/* get that remapped edx off the stack */
	popl	%ecx				/* get that remapped esi off the stack */
	movl	PT_EAX(%esp), %eax		/* reload syscall number */
	jmp	sysenter_do_call

sysexit_audit:
	testl	$(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT), %ecx
	jnz	syscall_exit_work
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)
	movl	%eax, %edx			/* second arg, syscall return value */
	cmpl	$-MAX_ERRNO, %eax		/* is it an error ? */
	setbe %al				/* 1 if so, 0 if not */
	movzbl %al, %eax			/* zero-extend that */
	call	__audit_syscall_exit
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	movl	TI_flags(%ebp), %ecx
	testl	$(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT), %ecx
	jnz	syscall_exit_work
	movl	PT_EAX(%esp), %eax		/* reload syscall return value */
	jmp	sysenter_exit
#endif

.pushsection .fixup, "ax"
2:	movl	$0, PT_FS(%esp)
	jmp	1b
.popsection
	_ASM_EXTABLE(1b, 2b)
	PTGS_TO_GS_EX
ENDPROC(entry_SYSENTER_32)

	# pv sysenter call handler stub
ENTRY(entry_SYSENTER_PV32)
	ASM_CLAC
	movl	$__USER_DS, 16(%esp)
	movl	%ebp, 12(%esp)
	movl	$__USER_CS, 4(%esp)
	addl	$4, %esp
	/* +5*4 is SS:ESP,EFLAGS,CS:EIP. +8 is esp0 setting. */
	pushl	TI_sysenter_return-THREAD_SIZE+8+4*4(%esp)
/*
 * Load the potential sixth argument from user stack.
 * Careful about security.
 */
	cmpl	$__PAGE_OFFSET-3, %ebp
	jae	syscall_fault
	ASM_STAC
1:	movl	(%ebp), %ebp
	ASM_CLAC
	_ASM_EXTABLE(1b,syscall_fault)
	jmp	entry_INT80_32
ENDPROC(entry_SYSENTER_PV32)

	# system call handler stub
ENTRY(entry_INT80_32)
	ASM_CLAC
	pushl	%eax				# save orig_eax
	SAVE_ALL
	GET_THREAD_INFO(%ebp)
						# system call tracing in operation / emulation
	testl	$_TIF_WORK_SYSCALL_ENTRY, TI_flags(%ebp)
	jnz	syscall_trace_entry
	cmpl	$(NR_syscalls), %eax
	jae	syscall_badsys
syscall_call:
	call	*sys_call_table(, %eax, 4)
syscall_after_call:
	movl	%eax, PT_EAX(%esp)		# store the return value
syscall_exit:
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)		# make sure we don't miss an interrupt
						# setting need_resched or sigpending
						# between sampling and the iret
	TRACE_IRQS_OFF
	movl	TI_flags(%ebp), %ecx
	testl	$_TIF_ALLWORK_MASK, %ecx	# current->work
	jnz	syscall_exit_work

restore_all:
	TRACE_IRQS_IRET
restore_all_notrace:
#ifdef CONFIG_X86_ESPFIX32
	movl	PT_EFLAGS(%esp), %eax		# mix EFLAGS, SS and CS
	/*
	 * Warning: PT_OLDSS(%esp) contains the wrong/random values if we
	 * are returning to the kernel.
	 * See comments in process.c:copy_thread() for details.
	 */
	movb	PT_OLDSS(%esp), %ah
	movb	PT_CS(%esp), %al
	andl	$(X86_EFLAGS_VM | (SEGMENT_TI_MASK << 8) | SEGMENT_RPL_MASK), %eax
	cmpl	$((SEGMENT_LDT << 8) | USER_RPL), %eax
	je ldt_ss				# returning to user-space with LDT SS
#endif
restore_nocheck:
#ifdef CONFIG_XEN
	movl	PT_EFLAGS(%esp), %eax
	testl	$(X86_EFLAGS_VM|NMI_MASK), %eax
	jnz	hypervisor_iret
	shr	$9, %eax			# EAX[0] == IRET_EFLAGS.IF
	GET_VCPU_INFO
	andb	evtchn_upcall_mask(%esi),%al
	andb	$1, %al				# EAX[0] == IRET_EFLAGS.IF & event_mask
	jnz	restore_all_enable_events	#        != 0 => enable event delivery
#endif
	RESTORE_REGS 4				# skip orig_eax/error_code
irq_return:
	INTERRUPT_RETURN
.section .fixup, "ax"
ENTRY(iret_exc	)
	pushl	$0				# no error code
	pushl	$do_iret_error
	jmp	error_code
.previous
	_ASM_EXTABLE(irq_return, iret_exc)

#ifdef CONFIG_X86_ESPFIX32
ldt_ss:
#ifdef CONFIG_PARAVIRT
	/*
	 * The kernel can't run on a non-flat stack if paravirt mode
	 * is active.  Rather than try to fixup the high bits of
	 * ESP, bypass this code entirely.  This may break DOSemu
	 * and/or Wine support in a paravirt VM, although the option
	 * is still available to implement the setting of the high
	 * 16-bits in the INTERRUPT_RETURN paravirt-op.
	 */
	cmpl	$0, pv_info+PARAVIRT_enabled
	jne	restore_nocheck
#endif

/*
 * Setup and switch to ESPFIX stack
 *
 * We're returning to userspace with a 16 bit stack. The CPU will not
 * restore the high word of ESP for us on executing iret... This is an
 * "official" bug of all the x86-compatible CPUs, which we can work
 * around to make dosemu and wine happy. We do this by preloading the
 * high word of ESP with the high word of the userspace ESP while
 * compensating for the offset by changing to the ESPFIX segment with
 * a base address that matches for the difference.
 */
#define GDT_ESPFIX_SS PER_CPU_VAR(gdt_page) + (GDT_ENTRY_ESPFIX_SS * 8)
	mov	%esp, %edx			/* load kernel esp */
	mov	PT_OLDESP(%esp), %eax		/* load userspace esp */
	mov	%dx, %ax			/* eax: new kernel esp */
	sub	%eax, %edx			/* offset (low word is 0) */
	shr	$16, %edx
	mov	%dl, GDT_ESPFIX_SS + 4		/* bits 16..23 */
	mov	%dh, GDT_ESPFIX_SS + 7		/* bits 24..31 */
	pushl	$__ESPFIX_SS
	pushl	%eax				/* new kernel esp */
	/*
	 * Disable interrupts, but do not irqtrace this section: we
	 * will soon execute iret and the tracer was already set to
	 * the irqstate after the IRET:
	 */
	DISABLE_INTERRUPTS(CLBR_EAX)
	lss	(%esp), %esp			/* switch to espfix segment */
	jmp	restore_nocheck
#endif
#ifdef CONFIG_XEN
        ALIGN
restore_all_enable_events:
	TRACE_IRQS_ON
	__ENABLE_INTERRUPTS
.Lscrit: /**** START OF CRITICAL REGION ****/
	__TEST_PENDING
	jnz	14f				# process more events if necessary...
	RESTORE_REGS 4
1:	INTERRUPT_RETURN
	_ASM_EXTABLE(1b,iret_exc)
14:	__DISABLE_INTERRUPTS
.Lecrit: /**** END OF CRITICAL REGION ****/
	TRACE_IRQS_OFF
	jmp	.Ldo_upcall

hypervisor_iret:
	andl	$~NMI_MASK, PT_EFLAGS(%esp)
	RESTORE_REGS 4
	jmp	hypercall_page + (__HYPERVISOR_iret * 32)
#endif
ENDPROC(entry_INT80_32)

	# perform work that needs to be done immediately before resumption
	ALIGN
work_pending:
	testb	$_TIF_NEED_RESCHED, %cl
	jz	work_notifysig
work_resched:
	call	schedule
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)		# make sure we don't miss an interrupt
						# setting need_resched or sigpending
						# between sampling and the iret
	TRACE_IRQS_OFF
	movl	TI_flags(%ebp), %ecx
	andl	$_TIF_WORK_MASK, %ecx		# is there any work to be done other
						# than syscall tracing?
	jz	restore_all
	testb	$_TIF_NEED_RESCHED, %cl
	jnz	work_resched

work_notifysig:					# deal with pending signals and
						# notify-resume requests
#ifdef CONFIG_VM86
	testl	$X86_EFLAGS_VM, PT_EFLAGS(%esp)
	movl	%esp, %eax
	jnz	work_notifysig_v86		# returning to kernel-space or
						# vm86-space
1:
#else
	movl	%esp, %eax
#endif
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	movb	PT_CS(%esp), %bl
	andb	$SEGMENT_RPL_MASK, %bl
	cmpb	$USER_RPL, %bl
	jb	resume_kernel
	xorl	%edx, %edx
	call	do_notify_resume
	jmp	resume_userspace

#ifdef CONFIG_VM86
	ALIGN
work_notifysig_v86:
	pushl	%ecx				# save ti_flags for do_notify_resume
	call	save_v86_state			# %eax contains pt_regs pointer
	popl	%ecx
	movl	%eax, %esp
	jmp	1b
#endif
END(work_pending)

	# perform syscall exit tracing
	ALIGN
syscall_trace_entry:
	movl	$-ENOSYS, PT_EAX(%esp)
	movl	%esp, %eax
	call	syscall_trace_enter
	/* What it returned is what we'll actually use.  */
	cmpl	$(NR_syscalls), %eax
	jnae	syscall_call
	jmp	syscall_exit
END(syscall_trace_entry)

	# perform syscall exit tracing
	ALIGN
syscall_exit_work:
	testl	$_TIF_WORK_SYSCALL_EXIT, %ecx
	jz	work_pending
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)		# could let syscall_trace_leave() call
						# schedule() instead
	movl	%esp, %eax
	call	syscall_trace_leave
	jmp	resume_userspace
END(syscall_exit_work)

syscall_fault:
	ASM_CLAC
	GET_THREAD_INFO(%ebp)
	movl	$-EFAULT, PT_EAX(%esp)
	jmp	resume_userspace
END(syscall_fault)

syscall_badsys:
	movl	$-ENOSYS, %eax
	jmp	syscall_after_call
END(syscall_badsys)

sysenter_badsys:
	movl	$-ENOSYS, %eax
	jmp	sysenter_after_call
END(sysenter_badsys)

#ifndef CONFIG_XEN
.macro FIXUP_ESPFIX_STACK
/*
 * Switch back for ESPFIX stack to the normal zerobased stack
 *
 * We can't call C functions using the ESPFIX stack. This code reads
 * the high word of the segment base from the GDT and swiches to the
 * normal stack and adjusts ESP with the matching offset.
 */
#ifdef CONFIG_X86_ESPFIX32
	/* fixup the stack */
	mov	GDT_ESPFIX_SS + 4, %al /* bits 16..23 */
	mov	GDT_ESPFIX_SS + 7, %ah /* bits 24..31 */
	shl	$16, %eax
	addl	%esp, %eax			/* the adjusted stack pointer */
	pushl	$__KERNEL_DS
	pushl	%eax
	lss	(%esp), %esp			/* switch to the normal stack segment */
#endif
.endm
.macro UNWIND_ESPFIX_STACK
#ifdef CONFIG_X86_ESPFIX32
	movl	%ss, %eax
	/* see if on espfix stack */
	cmpw	$__ESPFIX_SS, %ax
	jne	27f
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movl	%eax, %es
	/* switch to normal stack */
	FIXUP_ESPFIX_STACK
27:
#endif
.endm

/*
 * Build the entry stubs with some assembler magic.
 * We pack 1 stub into every 8-byte block.
 */
	.align 8
ENTRY(irq_entries_start)
    vector=FIRST_EXTERNAL_VECTOR
    .rept (FIRST_SYSTEM_VECTOR - FIRST_EXTERNAL_VECTOR)
	pushl	$(~vector+0x80)			/* Note: always in signed byte range */
    vector=vector+1
	jmp	common_interrupt
	.align	8
    .endr
END(irq_entries_start)

/*
 * the CPU automatically disables interrupts when executing an IRQ vector,
 * so IRQ-flags tracing has to follow that:
 */
	.p2align CONFIG_X86_L1_CACHE_SHIFT
common_interrupt:
	ASM_CLAC
	addl	$-0x80, (%esp)			/* Adjust vector into the [-256, -1] range */
	SAVE_ALL
	TRACE_IRQS_OFF
	movl	%esp, %eax
	call	do_IRQ
	jmp	ret_from_intr
ENDPROC(common_interrupt)

#define BUILD_INTERRUPT3(name, nr, fn)	\
ENTRY(name)				\
	ASM_CLAC;			\
	pushl	$~(nr);			\
	SAVE_ALL;			\
	TRACE_IRQS_OFF			\
	movl	%esp, %eax;		\
	call	fn;			\
	jmp	ret_from_intr;		\
ENDPROC(name)


#ifdef CONFIG_TRACING
# define TRACE_BUILD_INTERRUPT(name, nr)	BUILD_INTERRUPT3(trace_##name, nr, smp_trace_##name)
#else
# define TRACE_BUILD_INTERRUPT(name, nr)
#endif

#define BUILD_INTERRUPT(name, nr)		\
	BUILD_INTERRUPT3(name, nr, smp_##name);	\
	TRACE_BUILD_INTERRUPT(name, nr)

/* The include is where all of the SMP etc. interrupts come from */
#include <asm/entry_arch.h>

#else
#define UNWIND_ESPFIX_STACK

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
#
# The sysexit critical region is slightly different. sysexit
# atomically removes the entire stack frame. If we interrupt in the
# critical region we know that the entire frame is present and correct
# so we can simply throw away the new one.
ENTRY(hypervisor_callback)
	ASM_CLAC
	pushl	$-1
	SAVE_ALL
	movl	PT_CS(%esp), %ecx
	movl	PT_EIP(%esp), %eax
	andl	$SEGMENT_RPL_MASK, %ecx
	cmpl	$USER_RPL, %ecx
	jae	.Ldo_upcall
	cmpl	$.Lscrit, %eax
	jb	0f
	cmpl	$.Lecrit, %eax
	jb	critical_region_fixup
0:
#ifdef CONFIG_XEN_SUPERVISOR_MODE_KERNEL
	cmpl	$.Lsysexit_scrit, %eax
	jb	.Ldo_upcall
	cmpl	$.Lsysexit_ecrit, %eax
	ja	.Ldo_upcall
	addl	$PT_OLDESP, %esp		# Remove eflags...ebx from stack frame.
#endif
.Ldo_upcall:
	pushl	%esp
	call	evtchn_do_upcall
	add	$4, %esp
#ifndef CONFIG_PREEMPT
	test	%al, %al
	jz	ret_from_intr
	cmpl	$0, PER_CPU_VAR(__preempt_count)
	jnz	ret_from_intr
	testl	$X86_EFLAGS_IF, PT_EFLAGS(%esp)
	jz	ret_from_intr
	movb	$0, PER_CPU_VAR(privcmd_hcall)
	call	preempt_schedule_irq
	movb	$1, PER_CPU_VAR(privcmd_hcall)
#endif
	jmp	ret_from_intr

# [How we do the fixup]. We want to merge the current stack frame with the
# just-interrupted frame. How we do this depends on where in the critical
# region the interrupted handler was executing, and so how many saved
# registers are in each frame. We do this quickly using the lookup table
# 'critical_fixup_table'. For each byte offset in the critical region, it
# provides the number of bytes which have already been popped from the
# interrupted stack frame.
critical_region_fixup:
	movsbl	critical_fixup_table-.Lscrit(%eax),%ecx # %ecx contains num slots popped
	testl	%ecx, %ecx
	leal	(%esp,%ecx,4), %esi		# %esi points at end of src region
	leal	PT_OLDESP(%esp), %edi		# %edi points at end of dst region
	jle	17f				# skip loop if nothing to copy
16:	subl	$4, %esi			# pre-decrementing copy loop
	subl	$4, %edi
	movl	(%esi), %eax
	movl	%eax, (%edi)
	loop	16b
17:	movl	%edi, %esp			# final %edi is top of merged stack
	jmp	.Ldo_upcall

.section .rodata,"a"
critical_fixup_table:
	.rept __SIZEOF_TEST_PENDING
	.byte -1
	.endr
	.byte -1,-1			# jnz  14f
	.byte 0				# pop  %ebx
	.byte 1				# pop  %ecx
	.byte 2				# pop  %edx
	.byte 3				# pop  %esi
	.byte 4				# pop  %edi
	.byte 5				# pop  %ebp
	.byte 6				# pop  %eax
	.byte 7				# pop  %ds
	.byte 8				# pop  %es
	.byte 9,9			# pop  %fs
#ifndef CONFIG_X86_32_LAZY_GS
	.byte 10,10			# pop  %gs
	.byte 11,11,11			# add  $4,%esp
#else
	.byte 10,10,10			# add  $8,%esp
#endif
	.byte 12			# iret
	.rept __SIZEOF_DISABLE_INTERRUPTS
	.byte -1
	.endr
.previous

# Hypervisor uses this for application faults while it executes.
# We get here for two reasons:
#  1. Fault while reloading DS, ES, FS or GS
#  2. Fault while executing IRET
# Category 1 we fix up by reattempting the load, and zeroing the segment
# register if the load fails.
# Category 2 we fix up by jumping to do_iret_error. We cannot use the
# normal Linux return path in this case because if we use the IRET hypercall
# to pop the stack frame we end up in an infinite loop of failsafe callbacks.
# We distinguish between categories by maintaining a status value in EAX.
ENTRY(failsafe_callback)
	ASM_CLAC
	pushl	%eax
	movl	$1, %eax
1:	mov	4(%esp), %ds
2:	mov	8(%esp), %es
3:	mov	12(%esp), %fs
4:	mov	16(%esp), %gs
	testl	%eax, %eax
	popl	%eax
	leal	16(%esp), %esp
	jnz	iret_exc		# EAX != 0 => Category 2 (Bad IRET)
	pushl	$-1			# EAX == 0 => Category 1 (Bad segment)
	SAVE_ALL
	jmp	ret_from_exception
.section .fixup,"ax"
6:	xorl	%eax, %eax
	movl	%eax, 4(%esp)
	jmp	1b
7:	xorl	%eax, %eax
	movl	%eax, 8(%esp)
	jmp	2b
8:	xorl	%eax, %eax
	movl	%eax, 12(%esp)
	jmp	3b
9:	xorl	%eax, %eax
	movl	%eax, 16(%esp)
	jmp	4b
.previous
	_ASM_EXTABLE(1b,6b)
	_ASM_EXTABLE(2b,7b)
	_ASM_EXTABLE(3b,8b)
	_ASM_EXTABLE(4b,9b)
#endif

ENTRY(coprocessor_error)
	ASM_CLAC
	pushl	$0
	pushl	$do_coprocessor_error
	jmp	error_code
END(coprocessor_error)

ENTRY(simd_coprocessor_error)
	ASM_CLAC
	pushl	$0
#ifdef CONFIG_X86_INVD_BUG
	/* AMD 486 bug: invd from userspace calls exception 19 instead of #GP */
	ALTERNATIVE "pushl	$do_general_protection",	\
		    "pushl	$do_simd_coprocessor_error",	\
		    X86_FEATURE_XMM
#else
	pushl	$do_simd_coprocessor_error
#endif
	jmp	error_code
END(simd_coprocessor_error)

ENTRY(device_not_available)
	ASM_CLAC
	pushl	$-1				# mark this as an int
	pushl	$do_device_not_available
	jmp	error_code
END(device_not_available)

#ifdef CONFIG_PARAVIRT
ENTRY(native_iret)
	iret
	_ASM_EXTABLE(native_iret, iret_exc)
END(native_iret)

ENTRY(native_irq_enable_sysexit)
	sti
	sysexit
END(native_irq_enable_sysexit)
#endif

ENTRY(overflow)
	ASM_CLAC
	pushl	$0
	pushl	$do_overflow
	jmp	error_code
END(overflow)

ENTRY(bounds)
	ASM_CLAC
	pushl	$0
	pushl	$do_bounds
	jmp	error_code
END(bounds)

ENTRY(invalid_op)
	ASM_CLAC
	pushl	$0
	pushl	$do_invalid_op
	jmp	error_code
END(invalid_op)

ENTRY(coprocessor_segment_overrun)
	ASM_CLAC
	pushl	$0
	pushl	$do_coprocessor_segment_overrun
	jmp	error_code
END(coprocessor_segment_overrun)

ENTRY(invalid_TSS)
	ASM_CLAC
	pushl	$do_invalid_TSS
	jmp	error_code
END(invalid_TSS)

ENTRY(segment_not_present)
	ASM_CLAC
	pushl	$do_segment_not_present
	jmp	error_code
END(segment_not_present)

ENTRY(stack_segment)
	ASM_CLAC
	pushl	$do_stack_segment
	jmp	error_code
END(stack_segment)

ENTRY(alignment_check)
	ASM_CLAC
	pushl	$do_alignment_check
	jmp	error_code
END(alignment_check)

ENTRY(divide_error)
	ASM_CLAC
	pushl	$0				# no error code
	pushl	$do_divide_error
	jmp	error_code
END(divide_error)

#ifdef CONFIG_X86_MCE
ENTRY(machine_check)
	ASM_CLAC
	pushl	$0
	pushl	machine_check_vector
	jmp	error_code
END(machine_check)
#endif

#ifndef CONFIG_XEN
ENTRY(spurious_interrupt_bug)
	ASM_CLAC
	pushl	$0
	pushl	$do_spurious_interrupt_bug
	jmp	error_code
END(spurious_interrupt_bug)
#endif /* !CONFIG_XEN */

ENTRY(fixup_4gb_segment)
	pushl	$do_fixup_4gb_segment
	jmp error_code
END(fixup_4gb_segment)

#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE

ENTRY(mcount)
	ret
END(mcount)

ENTRY(ftrace_caller)
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	pushl	$0				/* Pass NULL as regs pointer */
	movl	4*4(%esp), %eax
	movl	0x4(%ebp), %edx
	movl	function_trace_op, %ecx
	subl	$MCOUNT_INSN_SIZE, %eax

.globl ftrace_call
ftrace_call:
	call	ftrace_stub

	addl	$4, %esp			/* skip NULL pointer */
	popl	%edx
	popl	%ecx
	popl	%eax
ftrace_ret:
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
.globl ftrace_graph_call
ftrace_graph_call:
	jmp	ftrace_stub
#endif

.globl ftrace_stub
ftrace_stub:
	ret
END(ftrace_caller)

ENTRY(ftrace_regs_caller)
	pushf	/* push flags before compare (in cs location) */

	/*
	 * i386 does not save SS and ESP when coming from kernel.
	 * Instead, to get sp, &regs->sp is used (see ptrace.h).
	 * Unfortunately, that means eflags must be at the same location
	 * as the current return ip is. We move the return ip into the
	 * ip location, and move flags into the return ip location.
	 */
	pushl	4(%esp)				/* save return ip into ip slot */

	pushl	$0				/* Load 0 into orig_ax */
	pushl	%gs
	pushl	%fs
	pushl	%es
	pushl	%ds
	pushl	%eax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx

	movl	13*4(%esp), %eax		/* Get the saved flags */
	movl	%eax, 14*4(%esp)		/* Move saved flags into regs->flags location */
						/* clobbering return ip */
	movl	$__KERNEL_CS, 13*4(%esp)

	movl	12*4(%esp), %eax		/* Load ip (1st parameter) */
	subl	$MCOUNT_INSN_SIZE, %eax		/* Adjust ip */
	movl	0x4(%ebp), %edx			/* Load parent ip (2nd parameter) */
	movl	function_trace_op, %ecx		/* Save ftrace_pos in 3rd parameter */
	pushl	%esp				/* Save pt_regs as 4th parameter */

GLOBAL(ftrace_regs_call)
	call	ftrace_stub

	addl	$4, %esp			/* Skip pt_regs */
	movl	14*4(%esp), %eax		/* Move flags back into cs */
	movl	%eax, 13*4(%esp)		/* Needed to keep addl	from modifying flags */
	movl	12*4(%esp), %eax		/* Get return ip from regs->ip */
	movl	%eax, 14*4(%esp)		/* Put return ip back for ret */

	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp
	popl	%eax
	popl	%ds
	popl	%es
	popl	%fs
	popl	%gs
	addl	$8, %esp			/* Skip orig_ax and ip */
	popf					/* Pop flags at end (no addl to corrupt flags) */
	jmp	ftrace_ret

	popf
	jmp	ftrace_stub
#else /* ! CONFIG_DYNAMIC_FTRACE */

ENTRY(mcount)
	cmpl	$__PAGE_OFFSET, %esp
	jb	ftrace_stub			/* Paging not enabled yet? */

	cmpl	$ftrace_stub, ftrace_trace_function
	jnz	trace
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpl	$ftrace_stub, ftrace_graph_return
	jnz	ftrace_graph_caller

	cmpl	$ftrace_graph_entry_stub, ftrace_graph_entry
	jnz	ftrace_graph_caller
#endif
.globl ftrace_stub
ftrace_stub:
	ret

	/* taken from glibc */
trace:
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	movl	0xc(%esp), %eax
	movl	0x4(%ebp), %edx
	subl	$MCOUNT_INSN_SIZE, %eax

	call	*ftrace_trace_function

	popl	%edx
	popl	%ecx
	popl	%eax
	jmp	ftrace_stub
END(mcount)
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	movl	0xc(%esp), %eax
	lea	0x4(%ebp), %edx
	movl	(%ebp), %ecx
	subl	$MCOUNT_INSN_SIZE, %eax
	call	prepare_ftrace_return
	popl	%edx
	popl	%ecx
	popl	%eax
	ret
END(ftrace_graph_caller)

.globl return_to_handler
return_to_handler:
	pushl	%eax
	pushl	%edx
	movl	%ebp, %eax
	call	ftrace_return_to_handler
	movl	%eax, %ecx
	popl	%edx
	popl	%eax
	jmp	*%ecx
#endif

#ifdef TIF_CSTAR
	# pv syscall call handler stub
ENTRY(entry_SYSCALL_PV32)
	ASM_CLAC
	movl	$__USER_DS, 16(%esp)
	movl	%ebp, %ecx
	movl	$__USER_CS, 4(%esp)
	movl	12(%esp), %ebp
	pushl	%eax				# save orig_eax
/*
 * Load the potential sixth argument from user stack.
 * Careful about security.
 */
	cmpl	$__PAGE_OFFSET-4, %ebp
	ja	cstar_fault
	ASM_STAC
1:	movl	(%ebp), %ebp
	ASM_CLAC
	_ASM_EXTABLE(1b,cstar_fault)
	SAVE_ALL
	GET_THREAD_INFO(%ebp)
	testl	$_TIF_WORK_SYSCALL_ENTRY, TI_flags(%ebp)
	jnz	cstar_trace_entry
	cmpl	$NR_syscalls, %eax
	jae	cstar_badsys
.Lcstar_call:
	btl	%eax, cstar_special
	jc	.Lcstar_special
	call	*cstar_call_table(,%eax,4)
.Lcstar_after_call:
	movl	%eax, PT_EAX(%esp)		# store the return value
.Lcstar_exit:
	movl	PT_ECX(%esp), %ecx
	movl	%ecx, PT_EBP(%esp)		# put user EBP back in place
	jmp	syscall_exit
.Lcstar_special:
	movl	PT_ECX(%esp), %ecx
	movl	%ecx,PT_EBP(%esp)		# put user EBP back in place
	jmp	syscall_call
GLOBAL(cstar_set_tif)
	movl	$cstar_clear_tif, (%esp)	# replace return address
	LOCK_PREFIX
	orl	$_TIF_CSTAR, TI_flags(%ebp)
	jmp	*sys_call_table(,%eax,4)
cstar_clear_tif:
	LOCK_PREFIX
	andl	$~_TIF_CSTAR, TI_flags(%ebp)
	jmp	.Lcstar_after_call
cstar_trace_entry:
	movl	$-ENOSYS, PT_EAX(%esp)
	cmpl	$NR_syscalls, %eax
	jae	1f
	btl	%eax, cstar_special
	jc	.Lcstar_trace_special
1:	movl	%esp, %eax
	LOCK_PREFIX
	orl	$_TIF_CSTAR, TI_flags(%ebp)
	call	syscall_trace_enter
	LOCK_PREFIX
	andl	$~_TIF_CSTAR, TI_flags(%ebp)
	/* What it returned is what we'll actually use.  */
	cmpl	$NR_syscalls, %eax
	jb	.Lcstar_call
	jmp	.Lcstar_exit
.Lcstar_trace_special:
	movl	PT_ECX(%esp), %ecx
	movl	%esp, %eax
	movl	%ecx, PT_EBP(%esp)		# put user EBP back in place
	call	syscall_trace_enter
	/* What it returned is what we'll actually use.  */
	cmpl	$NR_syscalls, %eax
	jb	syscall_call
	jmp	syscall_exit
cstar_badsys:
	movl	$-ENOSYS, %eax
	jmp	.Lcstar_after_call
cstar_fault:
	ASM_CLAC
	movl	$-EFAULT, %eax
	SAVE_ALL
	GET_THREAD_INFO(%ebp)
	movl	PT_ECX(%esp), %ecx
	movl	%ecx, PT_EBP(%esp)		# put user EBP back in place
	jmp	resume_userspace
ENDPROC(entry_SYSCALL_PV32)

ENTRY(cstar_ret_from_fork)
	movl	PT_ECX(%esp), %ecx
	GET_THREAD_INFO(%ebp)
	movl	%ecx, PT_EBP(%esp)		# put user EBP back in place
	LOCK_PREFIX
	andl	$~_TIF_CSTAR, TI_flags(%ebp)
	jmp	ret_from_fork
END(cstar_ret_from_fork)

#include <asm/unistd.h>
.pushsection .rodata,"a"
.balign 4
cstar_special:
nr=0
mask=0
.rept NR_syscalls+31
 .irp n, __NR_sigreturn, __NR_rt_sigreturn
  .if nr == \n
   mask = mask | (1 << (\n & 31))
  .endif
 .endr
 nr = nr + 1
 .if (nr & 31) == 0
  .long mask
  mask = 0
 .endif
.endr
.popsection
#endif /* TIF_CSTAR */

#ifdef CONFIG_TRACING
ENTRY(trace_page_fault)
	ASM_CLAC
	pushl	$trace_do_page_fault
	jmp	error_code
END(trace_page_fault)
#endif

ENTRY(page_fault)
	ASM_CLAC
	pushl	$do_page_fault
	ALIGN
error_code:
	/* the function address is in %gs's slot on the stack */
	pushl	%fs
	pushl	%es
	pushl	%ds
	pushl	%eax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	cld
	movl	$(__KERNEL_PERCPU), %ecx
	movl	%ecx, %fs
	UNWIND_ESPFIX_STACK
	GS_TO_REG %ecx
	movl	PT_GS(%esp), %edi		# get the function address
	movl	PT_ORIG_EAX(%esp), %edx		# get the error code
	movl	$-1, PT_ORIG_EAX(%esp)		# no syscall to restart
	REG_TO_PTGS %ecx
	SET_KERNEL_GS %ecx
	movl	$(__USER_DS), %ecx
	movl	%ecx, %ds
	movl	%ecx, %es
	TRACE_IRQS_OFF
	movl	%esp, %eax			# pt_regs pointer
	call	*%edi
	jmp	ret_from_exception
END(page_fault)

#ifndef CONFIG_XEN
/*
 * Debug traps and NMI can happen at the one SYSENTER instruction
 * that sets up the real kernel stack. Check here, since we can't
 * allow the wrong stack to be used.
 *
 * "TSS_sysenter_sp0+12" is because the NMI/debug handler will have
 * already pushed 3 words if it hits on the sysenter instruction:
 * eflags, cs and eip.
 *
 * We just load the right stack, and push the three (known) values
 * by hand onto the new stack - while updating the return eip past
 * the instruction that would have done it for sysenter.
 */
.macro FIX_STACK offset ok label
	cmpw	$__KERNEL_CS, 4(%esp)
	jne	\ok
\label:
	movl	TSS_sysenter_sp0 + \offset(%esp), %esp
	pushfl
	pushl	$__KERNEL_CS
	pushl	$sysenter_past_esp
.endm
#endif /* CONFIG_XEN */

ENTRY(debug)
	ASM_CLAC
#ifndef CONFIG_XEN
	cmpl	$entry_SYSENTER_32, (%esp)
	jne	debug_stack_correct
	FIX_STACK 12, debug_stack_correct, debug_esp_fix_insn
debug_stack_correct:
#endif /* !CONFIG_XEN */
	pushl	$-1				# mark this as an int
	SAVE_ALL
	TRACE_IRQS_OFF
	xorl	%edx, %edx			# error code 0
	movl	%esp, %eax			# pt_regs pointer
	call	do_debug
	jmp	ret_from_exception
END(debug)

/*
 * NMI is doubly nasty. It can happen _while_ we're handling
 * a debug fault, and the debug fault hasn't yet been able to
 * clear up the stack. So we first check whether we got  an
 * NMI on the sysenter entry path, but after that we need to
 * check whether we got an NMI on the debug path where the debug
 * fault happened on the sysenter path.
 */
ENTRY(nmi)
	ASM_CLAC
#ifdef CONFIG_X86_ESPFIX32
	pushl	%eax
	movl	%ss, %eax
	cmpw	$__ESPFIX_SS, %ax
	popl	%eax
	je	nmi_espfix_stack
#endif
#ifndef CONFIG_XEN
	cmpl	$entry_SYSENTER_32, (%esp)
	je	nmi_stack_fixup
	pushl	%eax
	movl	%esp, %eax
	/*
	 * Do not access memory above the end of our stack page,
	 * it might not exist.
	 */
	andl	$(THREAD_SIZE-1), %eax
	cmpl	$(THREAD_SIZE-20), %eax
	popl	%eax
	jae	nmi_stack_correct
	cmpl	$entry_SYSENTER_32, 12(%esp)
	je	nmi_debug_stack_check
nmi_stack_correct:
	pushl	%eax
	SAVE_ALL
	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer
	call	do_nmi
	jmp	restore_all_notrace

nmi_stack_fixup:
	FIX_STACK 12, nmi_stack_correct, 1
	jmp	nmi_stack_correct

nmi_debug_stack_check:
	cmpw	$__KERNEL_CS, 16(%esp)
	jne	nmi_stack_correct
	cmpl	$debug, (%esp)
	jb	nmi_stack_correct
	cmpl	$debug_esp_fix_insn, (%esp)
	ja	nmi_stack_correct
	FIX_STACK 24, nmi_stack_correct, 1
	jmp	nmi_stack_correct

#ifdef CONFIG_X86_ESPFIX32
nmi_espfix_stack:
	/*
	 * create the pointer to lss back
	 */
	pushl	%ss
	pushl	%esp
	addl	$4, (%esp)
	/* copy the iret frame of 12 bytes */
	.rept 3
	pushl	16(%esp)
	.endr
	pushl	%eax
	SAVE_ALL
	FIXUP_ESPFIX_STACK			# %eax == %esp
	xorl	%edx, %edx			# zero error code
	call	do_nmi
	RESTORE_REGS
	lss	12+4(%esp), %esp		# back to espfix stack
	jmp	irq_return
#endif
#else /* CONFIG_XEN */
 	pushl	%eax
	SAVE_ALL
	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer
	call	do_nmi
	orl	$NMI_MASK, PT_EFLAGS(%esp)
	jmp	restore_all
#endif
END(nmi)

ENTRY(int3)
	ASM_CLAC
	pushl	$-1				# mark this as an int
	SAVE_ALL
	TRACE_IRQS_OFF
	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer
	call	do_int3
	jmp	ret_from_exception
END(int3)

ENTRY(general_protection)
	pushl	$do_general_protection
	jmp	error_code
END(general_protection)

#ifdef CONFIG_KVM_GUEST
ENTRY(async_page_fault)
	ASM_CLAC
	pushl	$do_async_page_fault
	jmp	error_code
END(async_page_fault)
#endif
