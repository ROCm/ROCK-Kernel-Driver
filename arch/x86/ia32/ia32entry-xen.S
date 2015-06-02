/*
 * Compatibility mode system call entry point for x86-64. 
 * 		
 * Copyright 2000-2002 Andi Kleen, SuSE Labs.
 */		 

#include <asm/dwarf2.h>
#include <asm/calling.h>
#include <asm/asm-offsets.h>
#include <asm/current.h>
#include <asm/errno.h>
#include <asm/ia32_unistd.h>	
#include <asm/thread_info.h>	
#include <asm/segment.h>
#include <asm/irqflags.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <linux/linkage.h>
#include <linux/err.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_I386		(EM_386|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_LE	   0x40000000

	.section .entry.text, "ax"

	/* clobbers %rax */
	.macro  CLEAR_RREGS _r9=rax
	xorl 	%eax,%eax
	movq	%rax,R11(%rsp)
	movq	%rax,R10(%rsp)
	movq	%\_r9,R9(%rsp)
	movq	%rax,R8(%rsp)
	.endm

	/*
	 * Reload arg registers from stack in case ptrace changed them.
	 * We don't reload %eax because syscall_trace_enter() returned
	 * the %rax value we should see.  Instead, we just truncate that
	 * value to 32 bits again as we did on entry from user mode.
	 * If it's a new value set by user_regset during entry tracing,
	 * this matches the normal truncation of the user-mode value.
	 * If it's -1 to make us punt the syscall, then (u32)-1 is still
	 * an appropriately invalid value.
	 */
	.macro LOAD_ARGS32 _r9=0
	.if \_r9
	movl R9(%rsp),%r9d
	.endif
	movl RCX(%rsp),%ecx
	movl RDX(%rsp),%edx
	movl RSI(%rsp),%esi
	movl RDI(%rsp),%edi
	movl %eax,%eax			/* zero extension */
	.endm

	.macro CFI_STARTPROC32 simple
	CFI_STARTPROC	\simple
	CFI_UNDEFINED	r8
	CFI_UNDEFINED	r9
	CFI_UNDEFINED	r10
	CFI_UNDEFINED	r11
	CFI_UNDEFINED	r12
	CFI_UNDEFINED	r13
	CFI_UNDEFINED	r14
	CFI_UNDEFINED	r15
	.endm

#ifdef CONFIG_PARAVIRT
ENTRY(native_usergs_sysret32)
	swapgs
	sysretl
ENDPROC(native_usergs_sysret32)

ENTRY(native_irq_enable_sysexit)
	swapgs
	sti
	sysexit
ENDPROC(native_irq_enable_sysexit)
#endif

/*
 * 32bit SYSENTER instruction entry.
 *
 * SYSENTER loads ss, rsp, cs, and rip from previously programmed MSRs.
 * IF and VM in rflags are cleared (IOW: interrupts are off).
 * SYSENTER does not save anything on the stack,
 * and does not save old rip (!!!) and rflags.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  user stack
 * 0(%ebp) arg6
 *
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below. We set up a complete hardware stack frame to share code
 * with the int 0x80 path.
 */
ENTRY(ia32_sysenter_target)
	CFI_STARTPROC32	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,SS+8-RIP+16
	/*CFI_REL_OFFSET	ss,SS-RIP+16*/
	CFI_REL_OFFSET	rsp,RSP-RIP+16
	/*CFI_REL_OFFSET	rflags,EFLAGS-RIP+16*/
	/*CFI_REL_OFFSET	cs,CS-RIP+16*/
	CFI_REL_OFFSET	rip,RIP-RIP+16
	CFI_REL_OFFSET	r11,8
	CFI_REL_OFFSET	rcx,0
	movq	8(%rsp),%r11
	CFI_RESTORE	r11
	popq_cfi %rcx
	CFI_RESTORE	rcx

	/* Zero-extending 32-bit regs, do not remove */
	movl	%ebp, %ebp
	movl	%eax, %eax
	movl	ASM_THREAD_INFO(TI_sysenter_return, %rsp, 6*8), %r10d
	CFI_REGISTER rip,r10
	movl	$__USER32_DS,40(%rsp)
	movq	%rbp,32(%rsp)
	movl	$__USER32_CS,16(%rsp)
	movq	%r10,8(%rsp)

	/* Construct struct pt_regs on stack */
	movq	%rax,(%rsp)
	pushq_cfi_reg	rdi			/* pt_regs->di */
	pushq_cfi_reg	rsi			/* pt_regs->si */
	pushq_cfi_reg	rdx			/* pt_regs->dx */
	pushq_cfi_reg	rcx			/* pt_regs->cx */
	pushq_cfi_reg	rax			/* pt_regs->ax */
	cld
	sub	$(10*8),%rsp /* pt_regs->r8-11,bp,bx,r12-15 not saved */
	CFI_ADJUST_CFA_OFFSET 10*8

	/*
	 * no need to do an access_ok check here because rbp has been
	 * 32bit zero extended
	 */
	ASM_STAC
1:	movl	(%rbp),%ebp
	_ASM_EXTABLE(1b,ia32_badarg)
	ASM_CLAC
	orl     $TS_COMPAT, ASM_THREAD_INFO(TI_status, %rsp, SIZEOF_PTREGS)
	testl   $_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz  sysenter_tracesys
	jmp .Lia32_check_call

#ifdef CONFIG_AUDITSYSCALL
	.macro auditsys_entry_common
	movl %esi,%r8d			/* 5th arg: 4th syscall arg */
	movl %ecx,%r9d			/*swap with edx*/
	movl %edx,%ecx			/* 4th arg: 3rd syscall arg */
	movl %r9d,%edx			/* 3rd arg: 2nd syscall arg */
	movl %ebx,%esi			/* 2nd arg: 1st syscall arg */
	movl %eax,%edi			/* 1st arg: syscall number */
	call __audit_syscall_entry
	movl RAX(%rsp),%eax	/* reload syscall number */
	cmpq $(IA32_NR_syscalls-1),%rax
	ja ia32_badsys
	movl %ebx,%edi			/* reload 1st syscall arg */
	movl RCX(%rsp),%esi	/* reload 2nd syscall arg */
	movl RDX(%rsp),%edx	/* reload 3rd syscall arg */
	movl RSI(%rsp),%ecx	/* reload 4th syscall arg */
	movl RDI(%rsp),%r8d	/* reload 5th syscall arg */
	.endm

sysenter_auditsys:
	auditsys_entry_common
	movl %ebp,%r9d			/* reload 6th syscall arg */
	jmp .Lia32_dispatch
#endif
	CFI_ENDPROC
ENDPROC(ia32_sysenter_target)

/*
 * 32bit SYSCALL instruction entry.
 *
 * 32bit SYSCALL saves rip to rcx, clears rflags.RF, then saves rflags to r11,
 * then loads new ss, cs, and rip from previously programmed MSRs.
 * rflags gets masked by a value from another MSR (so CLD and CLAC
 * are not needed). SYSCALL does not save anything on the stack
 * and does not change rsp.
 *
 * Note: rflags saving+masking-with-MSR happens only in Long mode
 * (in legacy 32bit mode, IF, RF and VM bits are cleared and that's it).
 * Don't get confused: rflags saving+masking depends on Long Mode Active bit
 * (EFER.LMA=1), NOT on bitness of userspace where SYSCALL executes
 * or target CS descriptor's L bit (SYSCALL does not read segment descriptors).
 *
 * Arguments:
 * eax  system call number
 * ecx  return address
 * ebx  arg1
 * ebp  arg2	(note: not saved in the stack frame, should not be touched)
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * esp  user stack
 * 0(%esp) arg6
 *
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below. We set up a complete hardware stack frame to share code
 * with the int 0x80 path.
 */
ENTRY(ia32_cstar_target)
	CFI_STARTPROC32	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,SS+8-RIP+16
	/*CFI_REL_OFFSET	ss,SS-RIP+16*/
	CFI_REL_OFFSET	rsp,RSP-RIP+16
	/*CFI_REL_OFFSET	rflags,EFLAGS-RIP+16*/
	/*CFI_REL_OFFSET	cs,CS-RIP+16*/
	CFI_REL_OFFSET	rip,RIP-RIP+16
	movl	RSP-RIP+16(%rsp),%r8d

	/* Zero-extending 32-bit regs, do not remove */
	movl	%eax,%eax

	/* Construct struct pt_regs on stack */
	movl		$__USER32_DS,6*8(%rsp)	/* pt_regs->ss */
	movl		$__USER32_CS,3*8(%rsp)	/* pt_regs->cs */
	movq_cfi	rax,8			/* pt_regs->orig_ax */
	movq_cfi	rdi,0			/* pt_regs->di */
	pushq_cfi_reg	rsi			/* pt_regs->si */
	pushq_cfi_reg	rdx			/* pt_regs->dx */
	pushq_cfi_reg	rbp			/* pt_regs->cx */
	movl	%ebp,%ecx
	pushq_cfi	%rax			/* pt_regs->ax */
	sub	$(10*8),%rsp /* pt_regs->r8-11,bp,bx,r12-15 not saved */
	CFI_ADJUST_CFA_OFFSET 10*8

	/*
	 * no need to do an access_ok check here because r8 has been
	 * 32bit zero extended
	 */
	ASM_STAC
1:	movl	(%r8),%r9d
	_ASM_EXTABLE(1b,ia32_badarg)
	ASM_CLAC
	orl     $TS_COMPAT, ASM_THREAD_INFO(TI_status, %rsp, SIZEOF_PTREGS)
	testl   $_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz   cstar_tracesys
	cmpq $IA32_NR_syscalls-1,%rax
	ja  ia32_badsys
cstar_do_call:
	/* 32bit syscall -> 64bit C ABI argument conversion */
	movl	%edi,%r8d	/* arg5 */
	/* r9 already loaded */	/* arg6 */
	jmp	.Lia32_arg_fixup_common
	
#ifdef CONFIG_AUDITSYSCALL
cstar_auditsys:
	movl %r9d,R9(%rsp)	/* register to be clobbered by call */
	auditsys_entry_common
	movl R9(%rsp),%r9d	/* reload 6th syscall arg */
	jmp .Lia32_dispatch
#endif

cstar_tracesys:
#ifdef CONFIG_AUDITSYSCALL
	testl $(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT), ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jz cstar_auditsys
#endif
	xchgl %r9d,%ebp
	SAVE_EXTRA_REGS
	CLEAR_RREGS r9
	movq $-ENOSYS,RAX(%rsp)	/* ptrace can change this for a bad syscall */
	movq %rsp,%rdi        /* &pt_regs -> arg1 */
	call syscall_trace_enter
	LOAD_ARGS32 1	/* reload args from stack in case ptrace changed it */
	RESTORE_EXTRA_REGS
	xchgl %ebp,%r9d
	cmpq $(IA32_NR_syscalls-1),%rax
	ja int_ret_from_sys_call /* cstar_tracesys has set RAX(%rsp) */
	jmp cstar_do_call
END(ia32_cstar_target)
				
ia32_badarg:
	ASM_CLAC
	movq $-EFAULT,%rax
	jmp ia32_sysret
	CFI_ENDPROC

/*
 * Emulated IA32 system calls via int 0x80.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  arg6	(note: not saved in the stack frame, should not be touched)
 *
 * Notes:
 * Uses the same stack frame as the x86-64 version.
 * All registers except eax must be saved (but ptrace may violate that).
 * Arguments are zero extended. For system calls that want sign extension and
 * take long arguments a wrapper is needed. Most calls can just be called
 * directly.
 * Assumes it is only called from user space and entered with interrupts on.
 */

ENTRY(ia32_syscall)
	CFI_STARTPROC32	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,5*8+16
	/*CFI_REL_OFFSET	ss,4*8+16 */
	CFI_REL_OFFSET	rsp,3*8+16
	/*CFI_REL_OFFSET	rflags,2*8+16 */
	/*CFI_REL_OFFSET	cs,1*8+16 */
	CFI_REL_OFFSET	rip,0*8+16
	CFI_REL_OFFSET	r11,8
	CFI_REL_OFFSET	rcx,0
	movq 8(%rsp),%r11
	CFI_RESTORE	r11
	popq_cfi %rcx
	CFI_RESTORE	rcx

	/* Zero-extending 32-bit regs, do not remove */
	movl	%eax,%eax

	/* Construct struct pt_regs on stack (iret frame is already on stack) */
	movq		%rax,(%rsp)		/* pt_regs->orig_ax */
	pushq_cfi_reg	rdi			/* pt_regs->di */
	pushq_cfi_reg	rsi			/* pt_regs->si */
	pushq_cfi_reg	rdx			/* pt_regs->dx */
	pushq_cfi_reg	rcx			/* pt_regs->cx */
	pushq_cfi_reg	rax			/* pt_regs->ax */
	cld
	sub	$(10*8),%rsp /* pt_regs->r8-11,bp,bx,r12-15 not saved */
	CFI_ADJUST_CFA_OFFSET 10*8

	orl $TS_COMPAT, ASM_THREAD_INFO(TI_status, %rsp, SIZEOF_PTREGS)
	testl $_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz ia32_tracesys
.Lia32_check_call:
	cmpq $(IA32_NR_syscalls-1),%rax
	ja ia32_badsys
ia32_do_call:
	/* 32bit syscall -> 64bit C ABI argument conversion */
	movl %edi,%r8d	/* arg5 */
	movl %ebp,%r9d	/* arg6 */
.Lia32_arg_fixup_common:
	xchg %ecx,%esi	/* rsi:arg2, rcx:arg4 */
	movl %ebx,%edi	/* arg1 */
	movl %edx,%edx	/* arg3 (zero extension) */
.Lia32_dispatch:
	call *ia32_sys_call_table(,%rax,8) # xxx: rip relative
ia32_sysret:
	movq %rax,RAX(%rsp)
	CLEAR_RREGS
	jmp int_ret_from_sys_call

sysenter_tracesys:
#ifdef CONFIG_AUDITSYSCALL
	testl	$(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT), ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jz sysenter_auditsys
#endif
ia32_tracesys:
	SAVE_EXTRA_REGS
	CLEAR_RREGS
	movq $-ENOSYS,RAX(%rsp)	/* ptrace can change this for a bad syscall */
	movq %rsp,%rdi        /* &pt_regs -> arg1 */
	call syscall_trace_enter
	LOAD_ARGS32	/* reload args from stack in case ptrace changed it */
	RESTORE_EXTRA_REGS
	cmpq $(IA32_NR_syscalls-1),%rax
	ja  int_ret_from_sys_call	/* ia32_tracesys has set RAX(%rsp) */
	jmp ia32_do_call
END(ia32_syscall)

ia32_badsys:
	movq $0,ORIG_RAX(%rsp)
	movq $-ENOSYS,%rax
	jmp ia32_sysret

	CFI_ENDPROC
	
	.macro PTREGSCALL label, func
	ALIGN
GLOBAL(\label)
	leaq \func(%rip),%rax
	jmp  ia32_ptregs_common	
	.endm

	CFI_STARTPROC32

	PTREGSCALL stub32_rt_sigreturn, sys32_rt_sigreturn
	PTREGSCALL stub32_sigreturn, sys32_sigreturn
	PTREGSCALL stub32_fork, sys_fork
	PTREGSCALL stub32_vfork, sys_vfork

	ALIGN
GLOBAL(stub32_clone)
	leaq sys_clone(%rip),%rax
	mov	%r8, %rcx
	jmp  ia32_ptregs_common

	ALIGN
ia32_ptregs_common:
	CFI_ENDPROC
	CFI_STARTPROC32	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,SIZEOF_PTREGS
	CFI_REL_OFFSET	rax,RAX
	CFI_REL_OFFSET	rcx,RCX
	CFI_REL_OFFSET	rdx,RDX
	CFI_REL_OFFSET	rsi,RSI
	CFI_REL_OFFSET	rdi,RDI
	CFI_REL_OFFSET	rip,RIP
/*	CFI_REL_OFFSET	cs,CS*/
/*	CFI_REL_OFFSET	rflags,EFLAGS*/
	CFI_REL_OFFSET	rsp,RSP
/*	CFI_REL_OFFSET	ss,SS*/
	SAVE_EXTRA_REGS 8
	call *%rax
	RESTORE_EXTRA_REGS 8
	ret
	CFI_ENDPROC
END(ia32_ptregs_common)
