/*
 *  linux/arch/x86_64/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002  Andi Kleen SuSE Labs
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 * 
 *  $Id: entry.S,v 1.66 2001/11/11 17:47:47 ak Exp $		
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after an interrupt and after each system call.
 * 
 * Normal syscalls and interrupts don't save a full stack frame, this is 
 * only done for syscall tracing, signals or fork/exec et.al.
 * 
 * A note on terminology:	 
 * - top of stack: Architecture defined interrupt frame from SS to RIP 
 * at the top of the kernel process stack.	
 * - partial stack frame: partially saved registers upto R11.
 * - full stack frame: Like partial stack frame, but all register saved. 
 *	
 * TODO:	 
 * - schedule it carefully for the final hardware.
 */

#define ASSEMBLY 1
#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/smp.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/calling.h>
#include <asm/offset.h>
#include <asm/msr.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
	
#define RIP_SYMBOL_NAME(x) x(%rip)

	.code64

#define PDAREF(field) %gs:field	 		

#ifdef CONFIG_PREEMPT
#define preempt_stop cli
#else
#define preempt_stop
#define retint_kernel retint_restore_args
#endif	
	
/*
 * C code is not supposed to know about undefined top of stack. Every time 
 * a C function with an pt_regs argument is called from the SYSCALL based 
 * fast path FIXUP_TOP_OF_STACK is needed.
 * RESTORE_TOP_OF_STACK syncs the syscall state after any possible ptregs
 * manipulation.
 */        	
		
	/* %rsp:at FRAMEEND */ 
	.macro FIXUP_TOP_OF_STACK tmp
	movq	PDAREF(pda_oldrsp),\tmp
	movq  	\tmp,RSP(%rsp)
	movq    $__USER_DS,SS(%rsp)
	movq    $__USER_CS,CS(%rsp)
	movq	RCX(%rsp),\tmp  /* get return address */
	movq	\tmp,RIP(%rsp)
	movq	R11(%rsp),\tmp  /* get eflags */
	movq	\tmp,EFLAGS(%rsp)
	.endm

	.macro RESTORE_TOP_OF_STACK tmp,offset=0
	movq   RSP-\offset(%rsp),\tmp
	movq   \tmp,PDAREF(pda_oldrsp)
	movq   RIP-\offset(%rsp),\tmp
	movq   \tmp,RCX-\offset(%rsp)
	movq   EFLAGS-\offset(%rsp),\tmp
	movq   \tmp,R11-\offset(%rsp)
	.endm

	.macro FAKE_STACK_FRAME child_rip
	/* push in order ss, rsp, eflags, cs, rip */
	xorq %rax, %rax
	pushq %rax /* ss */
	pushq %rax /* rsp */
	pushq %rax /* eflags */
	pushq $__KERNEL_CS /* cs */
	pushq \child_rip /* rip */
	pushq	%rax /* orig rax */
	.endm

	.macro UNFAKE_STACK_FRAME
	addq $8*6, %rsp
	.endm

	
/*
 * A newly forked process directly context switches into this.
 */ 	
ENTRY(ret_from_fork)
	movq %rbx, %rdi
	call schedule_tail
	GET_THREAD_INFO(%rcx)
	bt $TIF_SYSCALL_TRACE,threadinfo_flags(%rcx)
	jc rff_trace
rff_action:	
	RESTORE_REST
	cmpq $__KERNEL_CS,CS-ARGOFFSET(%rsp)	# from kernel_thread?
	je   int_ret_from_sys_call
	testl $_TIF_IA32,threadinfo_flags(%rcx)
	jnz  int_ret_from_sys_call
	RESTORE_TOP_OF_STACK %rdi,ARGOFFSET
	jmp ret_from_sys_call
rff_trace:
	movq %rsp,%rdi
	call syscall_trace
	jmp rff_action

/*
 * System call entry. Upto 6 arguments in registers are supported.
 *
 * SYSCALL does not save anything on the stack and does not change the
 * stack pointer.
 */
		
/*
 * Register setup:	
 * rax  system call number
 * rdi  arg0
 * rcx  return address for syscall/sysret, C arg3 
 * rsi  arg1
 * rdx  arg2	
 * r10  arg4 	(--> moved to rcx for C, serves as TOS flag afterwards) 
 * r8   arg5
 * r9   arg6
 * r11  eflags for syscall/sysret, temporary for C
 * r12-r15,rbp,rbx saved by C code, not touched. 		
 * 
 * Interrupts are off on entry.
 * Only called from user space.
 *
 * XXX need to add a flag for thread_saved_pc/KSTK_*. 			
 */ 			 		

ENTRY(system_call)
	swapgs
	movq	%rsp,PDAREF(pda_oldrsp) 
	movq	PDAREF(pda_kernelstack),%rsp
	pushq %rax
	sti					
	SAVE_ARGS
	GET_THREAD_INFO(%rcx)
	bt    $TIF_SYSCALL_TRACE,threadinfo_flags(%rcx) 
	jc    tracesys
	cmpq $__NR_syscall_max,%rax
	ja badsys
	movq %r10,%rcx
	call *sys_call_table(,%rax,8)  # XXX:	 rip relative
	movq %rax,RAX-ARGOFFSET(%rsp)
/*
 * Syscall return path ending with SYSRET (fast path)
 * Has incomplete stack frame and undefined top of stack. 
 */		
ENTRY(ret_from_sys_call)	
	GET_THREAD_INFO(%rcx)
	cli
	movl threadinfo_flags(%rcx),%edx
	andl $_TIF_WORK_MASK,%edx	# tracesys has been already checked.
	jnz  sysret_careful 
sysret_restore_args:
	RESTORE_ARGS
	movq	PDAREF(pda_oldrsp),%rsp
	swapgs
	SYSRET64

sysret_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc 1f
	call schedule
	jmp ret_from_sys_call
1:	sti
	SAVE_REST
	FIXUP_TOP_OF_STACK %rax
	xorq %rsi,%rsi		# oldset
	movq %rsp,%rdi		# &ptregs	
	call do_notify_resume
	RESTORE_TOP_OF_STACK %rax
	RESTORE_REST
	jmp ret_from_sys_call
	
tracesys:			 
	SAVE_REST
	movq $-ENOSYS,RAX(%rsp)
	FIXUP_TOP_OF_STACK %rdi
	movq %rsp,%rdi
	call syscall_trace
	LOAD_ARGS ARGOFFSET  /* reload args from stack in case ptrace changed it */
	RESTORE_REST
	cmpq $__NR_syscall_max,%rax
	ja  1f
	movq %r10,%rcx	/* fixup for C */
	movl $1,%r10d	/* set TOS flag */ 
	call *sys_call_table(,%rax,8)
	movq %rax,RAX-ARGOFFSET(%rsp)
	SAVE_REST
1:	movq %rsp,%rdi
	call syscall_trace
	RESTORE_TOP_OF_STACK %rbx
	RESTORE_REST
	jmp ret_from_sys_call
		
badsys:
	movq $-ENOSYS,RAX-ARGOFFSET(%rsp)	
	jmp ret_from_sys_call

/* 
 * Syscall return path ending with IRET.
 * Has correct top of stack, but partial stack frame.
 */ 	
ENTRY(int_ret_from_sys_call)	
	cmpq $__KERNEL_CS,CS-ARGOFFSET(%rsp)	# in kernel syscall?
	je int_restore_args
	movl $_TIF_ALLWORK_MASK,%esi
int_with_reschedule:
	GET_THREAD_INFO(%rcx)
	cli
	movl threadinfo_flags(%rcx),%edx
	andl %esi,%edx
	jnz   int_careful
	swapgs
int_restore_args:		
	RESTORE_ARGS	
	addq $8,%rsp	# Remove oldrax
	iretq

int_careful:
	sti
	bt $TIF_NEED_RESCHED,%edx
	jnc  int_very_careful
	call schedule
	movl $_TIF_ALLWORK_MASK,%esi
	jmp int_with_reschedule
int_very_careful:
	SAVE_REST
	leaq syscall_trace(%rip),%rbp
	leaq do_notify_resume(%rip),%rbx
	bt $TIF_SYSCALL_TRACE,%edx
	cmovcq %rbp,%rbx
	xorq %rsi,%rsi		# oldset -> arg2 
	movq %rsp,%rdi		# &ptregs -> arg1
	call *%rbx
	RESTORE_REST
	movl $_TIF_WORK_MASK,%esi
	jmp int_with_reschedule	
		
/* 
 * Certain special system calls that need to save a complete full stack frame.
 */ 								
	
	.macro PTREGSCALL label,func
	.globl \label
\label:
	leaq	\func(%rip),%rax
	jmp	ptregscall_common
	.endm

	PTREGSCALL stub_clone, sys_clone
	PTREGSCALL stub_fork, sys_fork
	PTREGSCALL stub_vfork, sys_vfork
	PTREGSCALL stub_rt_sigsuspend, sys_rt_sigsuspend
	PTREGSCALL stub_sigaltstack, sys_sigaltstack
	PTREGSCALL stub_iopl, sys_iopl

ENTRY(ptregscall_common)
	popq %r11
	SAVE_REST
	movq %r11, %r15
	FIXUP_TOP_OF_STACK %r11
	call *%rax
	RESTORE_TOP_OF_STACK %r11
	movq %r15, %r11
	RESTORE_REST
	pushq %r11
	ret
	
ENTRY(stub_execve)
	popq %r11
	SAVE_REST
	movq %r11, %r15
	FIXUP_TOP_OF_STACK %r11
	call sys_execve
	GET_THREAD_INFO(%rcx)
	testl $_TIF_IA32,threadinfo_flags(%rcx)
	jnz exec_32bit
	RESTORE_TOP_OF_STACK %r11
	movq %r15, %r11
	RESTORE_REST
	push %r11
	ret

exec_32bit:
	movq %rax,RAX(%rsp)
	RESTORE_REST
	jmp int_ret_from_sys_call
	
/*
 * sigreturn is special because it needs to restore all registers on return.
 * This cannot be done with SYSRET, so use the IRET return path instead.
 */                
ENTRY(stub_rt_sigreturn)
	addq $8, %rsp		
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	call sys_rt_sigreturn
	movq %rax,RAX(%rsp) # fixme, this could be done at the higher layer
	RESTORE_REST
	jmp int_ret_from_sys_call

/* 
 * Interrupt entry/exit.
 *
 * Interrupt entry points save only callee clobbered registers in fast path.
 *	
 * Entry runs with interrupts off.	
 */ 

/* 0(%rsp): interrupt number */ 
ENTRY(common_interrupt)
	cmpq $__KERNEL_CS,16(%rsp)
	je   1f
	swapgs
1:	cld
	SAVE_ARGS
#ifdef CONFIG_PREEMPT
	GET_THREAD_INFO(%rdx)
	incl threadinfo_preempt_count(%rdx)
#endif		
	leaq -ARGOFFSET(%rsp),%rdi	# arg1 for handler
	addl $1,PDAREF(pda_irqcount)	# XXX: should be merged with irq.c irqcount
	movq PDAREF(pda_irqstackptr),%rax
	cmoveq %rax,%rsp							
	pushq %rdi			# save old stack	
	call do_IRQ
	/* 0(%rsp): oldrsp-ARGOFFSET */
	.globl ret_from_intr
ret_from_intr:		
	popq  %rdi
	cli	
	subl $1,PDAREF(pda_irqcount)
	leaq ARGOFFSET(%rdi),%rsp
exit_intr:	 	
	GET_THREAD_INFO(%rcx)
#ifdef CONFIG_PREEMPT	
	decl threadinfo_preempt_count(%rcx)
#endif
	cmpq $__KERNEL_CS,CS-ARGOFFSET(%rsp)
	je retint_kernel
	
	/* Interrupt came from user space */
	/*
	 * Shared return path for exceptions and interrupts that came from user space.
	 * Has a correct top of stack, but a partial stack frame
	 * %rcx: thread info. Interrupts off.
	 */		
retint_with_reschedule:
	testl $_TIF_WORK_MASK,threadinfo_flags(%rcx)
	jnz  retint_careful
retint_swapgs:	 	
	swapgs 
retint_restore_args:				
	RESTORE_ARGS						
	addq $8,%rsp
	iretq

retint_careful:
	movl  threadinfo_flags(%rcx),%edx
	bt    $TIF_NEED_RESCHED,%edx
	jnc   retint_signal
	sti
	call  schedule
retint_next_try:		
	GET_THREAD_INFO(%rcx)
	cli
	jmp retint_with_reschedule
retint_signal:
	testl $(_TIF_SIGPENDING|_TIF_NOTIFY_RESUME),%edx
	jz    retint_swapgs
	sti
	SAVE_REST
	movq $-1,ORIG_RAX(%rsp) 			
	xorq %rsi,%rsi		# oldset
	movq %rsp,%rdi		# &pt_regs
	call do_notify_resume
	RESTORE_REST
	jmp retint_next_try

#ifdef CONFIG_PREEMPT
	/* Returning to kernel space. Check if we need preemption */
	/* rcx:	 threadinfo. interrupts off. */
	.p2align
retint_kernel:	
	cmpl $0,threadinfo_preempt_count(%rcx)
	jnz  retint_restore_args
	bt  $TIF_NEED_RESCHED,threadinfo_flags(%rcx)
	jnc  retint_restore_args
	movl PDAREF(pda___local_bh_count),%eax
	addl PDAREF(pda___local_irq_count),%eax
	jnz  retint_restore_args
	incl threadinfo_preempt_count(%rcx)
	sti
	call preempt_schedule
	cli
	jmp exit_intr
#endif	
	
/*
 * Exception entry points.
 */ 		
	.macro zeroentry sym
	pushq $0	/* push error code/oldrax */ 
	pushq %rax	/* push real oldrax to the rdi slot */ 
	leaq  RIP_SYMBOL_NAME(\sym),%rax
	jmp error_entry
	.endm	

	.macro errorentry sym
	pushq %rax
	leaq  RIP_SYMBOL_NAME(\sym),%rax
	jmp error_entry
	.endm

/*
 * Exception entry point. This expects an error code/orig_rax on the stack
 * and the exception handler in %rax.	
 */ 		  				
 	ALIGN
error_entry:
	cmpq $__KERNEL_CS,24(%rsp)	
	je  error_kernelspace	
	swapgs
error_kernelspace:			
	sti
	/* rdi slot contains rax, oldrax contains error code */
	pushq %rsi
	movq  8(%rsp),%rsi	/* load rax */
	pushq %rdx
	pushq %rcx
	pushq %rsi	/* store rax */ 
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	cld	
	SAVE_REST
	movq %rdi,RDI(%rsp) 	
	movq %rsp,%rdi
	movq ORIG_RAX(%rsp),%rsi	/* get error code */ 
	movq $-1,ORIG_RAX(%rsp)
	call *%rax
error_exit:		
	RESTORE_REST
	cli
	GET_THREAD_INFO(%rcx)	
	cmpq $__KERNEL_CS,CS-ARGOFFSET(%rsp)
	je retint_kernel
	jmp retint_with_reschedule

/*
 * Create a kernel thread.
 *
 * C extern interface:
 *	extern long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
 *
 * asm input arguments:
 *	rdi: fn, rsi: arg, rdx: flags
 */
ENTRY(kernel_thread)
	FAKE_STACK_FRAME $child_rip
	SAVE_ALL

	# rdi: flags, rsi: usp, rdx: will be &pt_regs
	movq %rdx,%rdi
	orq  $CLONE_VM, %rdi

	movq $-1, %rsi

	movq %rsp, %rdx

	# clone now
	call do_fork
	# save retval on the stack so it's popped before `ret`
	movq %rax, RAX(%rsp)

	/*
	 * It isn't worth to check for reschedule here,
	 * so internally to the x86_64 port you can rely on kernel_thread()
	 * not to reschedule the child before returning, this avoids the need
	 * of hacks for example to fork off the per-CPU idle tasks.
         * [Hopefully no generic code relies on the reschedule -AK]	
	 */
	RESTORE_ALL
	UNFAKE_STACK_FRAME
	ret
	
child_rip:
	/*
	 * Here we are in the child and the registers are set as they were
	 * at kernel_thread() invocation in the parent.
	 */
	movq %rdi, %rax
	movq %rsi, %rdi
	call *%rax
	# exit
	xorq %rdi, %rdi
	call do_exit

/*
 * execve(). This function needs to use IRET, not SYSRET, to set up all state properly.
 *
 * C extern interface:
 *	 extern long execve(char *name, char **argv, char **envp)
 *
 * asm input arguments:
 *	rdi: name, rsi: argv, rdx: envp
 *
 * We want to fallback into:
 *	extern long sys_execve(char *name, char **argv,char **envp, struct pt_regs regs)
 *
 * do_sys_execve asm fallback arguments:
 *	rdi: name, rsi: argv, rdx: envp, fake frame on the stack
 */
ENTRY(execve)
	FAKE_STACK_FRAME $0
	SAVE_ALL	
	call sys_execve
	movq %rax, RAX(%rsp)	
	RESTORE_REST
	testq %rax,%rax
	je int_ret_from_sys_call
	RESTORE_ARGS
	UNFAKE_STACK_FRAME
	ret

ENTRY(page_fault)
	errorentry do_page_fault

ENTRY(coprocessor_error)
	zeroentry do_coprocessor_error

ENTRY(simd_coprocessor_error)
	zeroentry do_simd_coprocessor_error	


ENTRY(device_not_available)
	cmpq $0,(%rsp)
	jl  1f
	swapgs
1:	pushq $-1	
	SAVE_ALL
	movq  %cr0,%rax
	leaq  math_state_restore(%rip),%rcx
	leaq  math_emulate(%rip),%rbx
	testl $0x4,%eax
	cmoveq %rcx,%rbx
	preempt_stop
	call  *%rbx
	jmp  error_exit

ENTRY(debug)
	zeroentry do_debug

	/* XXX checkme */ 
ENTRY(nmi)
	cmpq $0,(%rsp)
	jl 1f
	swapgs
1:	pushq $-1
	SAVE_ALL
	movq %rsp,%rdi
	call do_nmi
	RESTORE_ALL
	addq $8,%rsp
	cmpq $0,(%rsp)
	jl 2f
	swapgs
2:	iretq
	
ENTRY(int3)
	zeroentry do_int3	

ENTRY(overflow)
	zeroentry do_overflow

ENTRY(bounds)
	zeroentry do_bounds

ENTRY(invalid_op)
	zeroentry do_invalid_op	

ENTRY(coprocessor_segment_overrun)
	zeroentry do_coprocessor_segment_overrun

ENTRY(reserved)
	zeroentry do_reserved

ENTRY(double_fault)
	errorentry do_double_fault	

ENTRY(invalid_TSS)
	errorentry do_invalid_TSS

ENTRY(segment_not_present)
	errorentry do_segment_not_present

ENTRY(stack_segment)
	errorentry do_stack_segment

ENTRY(general_protection)
	errorentry do_general_protection

ENTRY(alignment_check)
	errorentry do_alignment_check

ENTRY(divide_error)
	errorentry do_divide_error

ENTRY(spurious_interrupt_bug)
	zeroentry do_spurious_interrupt_bug

ENTRY(__bad_intr)
	pushq $-1
	SAVE_ALL
	call  bad_intr
	RESTORE_ALL
	addq $8,%rsp
	iretq
