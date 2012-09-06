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
#include <linux/linkage.h>
#include <linux/err.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_I386		(EM_386|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_LE	   0x40000000

	.section .entry.text, "ax"

	.macro IA32_ARG_FIXUP noebp=0
	movl	%edi,%r8d
	.if \noebp
	jmp	.Lia32_common
	.else
	movl	%ebp,%r9d
.Lia32_common:
	.endif
	xchg	%ecx,%esi
	movl	%ebx,%edi
	movl	%edx,%edx	/* zero extension */
	.endm 

	/* clobbers %eax */	
	.macro  CLEAR_RREGS offset=0, _r9=rax
	xorl 	%eax,%eax
	movq	%rax,\offset+R11(%rsp)
	movq	%rax,\offset+R10(%rsp)
	movq	%\_r9,\offset+R9(%rsp)
	movq	%rax,\offset+R8(%rsp)
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
	.macro LOAD_ARGS32 offset, _r9=0
	.if \_r9
	movl \offset+16(%rsp),%r9d
	.endif
	movl \offset+40(%rsp),%ecx
	movl \offset+48(%rsp),%edx
	movl \offset+56(%rsp),%esi
	movl \offset+64(%rsp),%edi
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
 * Arguments:
 * %eax	System call number.
 * %ebx Arg1
 * %ecx Arg2
 * %edx Arg3
 * %esi Arg4
 * %edi Arg5
 * %ebp user stack
 * 0(%ebp) Arg6	
 * 	
 * Interrupts on.
 *	
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below.	Set up a complete hardware stack frame to share code
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
 	movl	%ebp,%ebp		/* zero extension */
	movl	%eax,%eax
	movl	TI_sysenter_return+THREAD_INFO(%rsp,8*6-KERNEL_STACK_OFFSET),%r10d
	movl	$__USER32_DS,40(%rsp)
	movq	%rbp,32(%rsp)
	movl	$__USER32_CS,16(%rsp)
	movq	%r10,8(%rsp)
	movq	%rax,(%rsp)
	cld
	SAVE_ARGS 0,1,0
 	/* no need to do an access_ok check here because rbp has been
 	   32bit zero extended */ 
1:	movl	(%rbp),%ebp
	_ASM_EXTABLE(1b,ia32_badarg)
	orl     $TS_COMPAT,TI_status+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	testl   $_TIF_WORK_SYSCALL_ENTRY,TI_flags+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	jnz  sysenter_tracesys
	jmp .Lia32_check_call

#ifdef CONFIG_AUDITSYSCALL
	.macro auditsys_entry_common
	movl %esi,%r9d			/* 6th arg: 4th syscall arg */
	movl %edx,%r8d			/* 5th arg: 3rd syscall arg */
	/* (already in %ecx)		   4th arg: 2nd syscall arg */
	movl %ebx,%edx			/* 3rd arg: 1st syscall arg */
	movl %eax,%esi			/* 2nd arg: syscall number */
	movl $AUDIT_ARCH_I386,%edi	/* 1st arg: audit arch */
	call __audit_syscall_entry
	movl RAX-ARGOFFSET(%rsp),%eax	/* reload syscall number */
	cmpq $(IA32_NR_syscalls-1),%rax
	ja ia32_badsys
	movl %ebx,%edi			/* reload 1st syscall arg */
	movl RCX-ARGOFFSET(%rsp),%esi	/* reload 2nd syscall arg */
	movl RDX-ARGOFFSET(%rsp),%edx	/* reload 3rd syscall arg */
	movl RSI-ARGOFFSET(%rsp),%ecx	/* reload 4th syscall arg */
	movl RDI-ARGOFFSET(%rsp),%r8d	/* reload 5th syscall arg */
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
 * Arguments:
 * %eax	System call number.
 * %ebx Arg1
 * %ecx return EIP 
 * %edx Arg3
 * %esi Arg4
 * %edi Arg5
 * %ebp Arg2    [note: not saved in the stack frame, should not be touched]
 * %esp user stack 
 * 0(%esp) Arg6
 * 	
 * Interrupts on.
 *	
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below.	Set up a complete hardware stack frame to share code
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
	movl 	%eax,%eax	/* zero extension */
	movl	RSP-RIP+16(%rsp),%r8d
	SAVE_ARGS -8,0,0
	movq	%rax,ORIG_RAX-ARGOFFSET(%rsp)
	movq	%rbp,RCX-ARGOFFSET(%rsp) /* this lies slightly to ptrace */
	movl	%ebp,%ecx
	movl	$__USER32_CS,CS-ARGOFFSET(%rsp)
	movl	$__USER32_DS,SS-ARGOFFSET(%rsp)
	/* no need to do an access_ok check here because r8 has been
	   32bit zero extended */ 
	/* hardware stack frame is complete now */	
1:	movl	(%r8),%r9d
	_ASM_EXTABLE(1b,ia32_badarg)
	orl     $TS_COMPAT,TI_status+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	testl   $_TIF_WORK_SYSCALL_ENTRY,TI_flags+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	jnz   cstar_tracesys
	cmpq $IA32_NR_syscalls-1,%rax
	ja  ia32_badsys
cstar_do_call:
	IA32_ARG_FIXUP 1
	
#ifdef CONFIG_AUDITSYSCALL
cstar_auditsys:
	movl %r9d,R9-ARGOFFSET(%rsp)	/* register to be clobbered by call */
	auditsys_entry_common
	movl R9-ARGOFFSET(%rsp),%r9d	/* reload 6th syscall arg */
	jmp .Lia32_dispatch
#endif

cstar_tracesys:
#ifdef CONFIG_AUDITSYSCALL
	testl $(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT),TI_flags+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	jz cstar_auditsys
#endif
	xchgl %r9d,%ebp
	SAVE_REST
	CLEAR_RREGS 0, r9
	movq $-ENOSYS,RAX(%rsp)	/* ptrace can change this for a bad syscall */
	movq %rsp,%rdi        /* &pt_regs -> arg1 */
	call syscall_trace_enter
	LOAD_ARGS32 ARGOFFSET, 1  /* reload args from stack in case ptrace changed it */
	RESTORE_REST
	xchgl %ebp,%r9d
	cmpq $(IA32_NR_syscalls-1),%rax
	ja int_ret_from_sys_call /* cstar_tracesys has set RAX(%rsp) */
	jmp cstar_do_call
END(ia32_cstar_target)
				
ia32_badarg:
	movq $-EFAULT,%rax
	jmp ia32_sysret
	CFI_ENDPROC

/* 
 * Emulated IA32 system calls via int 0x80. 
 *
 * Arguments:	 
 * %eax	System call number.
 * %ebx Arg1
 * %ecx Arg2
 * %edx Arg3
 * %esi Arg4
 * %edi Arg5
 * %ebp Arg6    [note: not saved in the stack frame, should not be touched]
 *
 * Notes:
 * Uses the same stack frame as the x86-64 version.	
 * All registers except %eax must be saved (but ptrace may violate that)
 * Arguments are zero extended. For system calls that want sign extension and
 * take long arguments a wrapper is needed. Most calls can just be called
 * directly.
 * Assumes it is only called from user space and entered with interrupts on.
 */ 				

ENTRY(ia32_syscall)
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
	movq 8(%rsp),%r11
	CFI_RESTORE	r11
	popq_cfi %rcx
	CFI_RESTORE	rcx
	movl %eax,%eax
	movq %rax,(%rsp)
	cld
	/* note the registers are not zero extended to the sf.
	   this could be a problem. */
	SAVE_ARGS 0,1,0
	orl $TS_COMPAT,TI_status+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	testl $_TIF_WORK_SYSCALL_ENTRY,TI_flags+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	jnz ia32_tracesys
.Lia32_check_call:
	cmpq $(IA32_NR_syscalls-1),%rax
	ja ia32_badsys
ia32_do_call:
	IA32_ARG_FIXUP
.Lia32_dispatch:
	call *ia32_sys_call_table(,%rax,8) # xxx: rip relative
ia32_sysret:
	movq %rax,RAX-ARGOFFSET(%rsp)
	CLEAR_RREGS -ARGOFFSET
	jmp int_ret_from_sys_call 

sysenter_tracesys:
#ifdef CONFIG_AUDITSYSCALL
	testl $(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT),TI_flags+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	jz sysenter_auditsys
#endif
ia32_tracesys:			 
	SAVE_REST
	CLEAR_RREGS
	movq $-ENOSYS,RAX(%rsp)	/* ptrace can change this for a bad syscall */
	movq %rsp,%rdi        /* &pt_regs -> arg1 */
	call syscall_trace_enter
	LOAD_ARGS32 ARGOFFSET  /* reload args from stack in case ptrace changed it */
	RESTORE_REST
	cmpq $(IA32_NR_syscalls-1),%rax
	ja  int_ret_from_sys_call	/* ia32_tracesys has set RAX(%rsp) */
	jmp ia32_do_call
END(ia32_syscall)

ia32_badsys:
	movq $0,ORIG_RAX-ARGOFFSET(%rsp)
	movq $-ENOSYS,%rax
	jmp ia32_sysret

	CFI_ENDPROC
	
	.macro PTREGSCALL label, func, arg
	ALIGN
GLOBAL(\label)
	leaq \func(%rip),%rax
	leaq -ARGOFFSET+8(%rsp),\arg	/* 8 for return address */
	jmp  ia32_ptregs_common	
	.endm

	CFI_STARTPROC32

	PTREGSCALL stub32_rt_sigreturn, sys32_rt_sigreturn, %rdi
	PTREGSCALL stub32_sigreturn, sys32_sigreturn, %rdi
	PTREGSCALL stub32_sigaltstack, sys32_sigaltstack, %rdx
	PTREGSCALL stub32_execve, sys32_execve, %rcx
	PTREGSCALL stub32_fork, sys_fork, %rdi
	PTREGSCALL stub32_clone, sys32_clone, %rdx
	PTREGSCALL stub32_vfork, sys_vfork, %rdi
	PTREGSCALL stub32_iopl, sys_iopl, %rsi

	ALIGN
ia32_ptregs_common:
	popq %r11
	CFI_ENDPROC
	CFI_STARTPROC32	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,SS+8-ARGOFFSET
	CFI_REL_OFFSET	rax,RAX-ARGOFFSET
	CFI_REL_OFFSET	rcx,RCX-ARGOFFSET
	CFI_REL_OFFSET	rdx,RDX-ARGOFFSET
	CFI_REL_OFFSET	rsi,RSI-ARGOFFSET
	CFI_REL_OFFSET	rdi,RDI-ARGOFFSET
	CFI_REL_OFFSET	rip,RIP-ARGOFFSET
/*	CFI_REL_OFFSET	cs,CS-ARGOFFSET*/
/*	CFI_REL_OFFSET	rflags,EFLAGS-ARGOFFSET*/
	CFI_REL_OFFSET	rsp,RSP-ARGOFFSET
/*	CFI_REL_OFFSET	ss,SS-ARGOFFSET*/
	SAVE_REST
	call *%rax
	RESTORE_REST
	jmp  ia32_sysret	/* misbalances the return cache */
	CFI_ENDPROC
END(ia32_ptregs_common)
