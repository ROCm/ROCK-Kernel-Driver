/*
 *  linux/arch/x86_64/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002  Andi Kleen SuSE Labs
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 * 
 *  $Id$
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
#include <asm/hw_irq.h>
#include <asm/errno.h>
#include <asm/dwarf2.h>

	.code64

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
	movq	%gs:pda_oldrsp,\tmp
	movq  	\tmp,RSP(%rsp)
	movq    $__USER_DS,SS(%rsp)
	movq    $__USER_CS,CS(%rsp)
	movq 	$-1,RCX(%rsp)
	movq	R11(%rsp),\tmp  /* get eflags */
	movq	\tmp,EFLAGS(%rsp)
	.endm

	.macro RESTORE_TOP_OF_STACK tmp,offset=0
	movq   RSP-\offset(%rsp),\tmp
	movq   \tmp,%gs:pda_oldrsp
	movq   EFLAGS-\offset(%rsp),\tmp
	movq   \tmp,R11-\offset(%rsp)
	.endm

	.macro FAKE_STACK_FRAME child_rip
	/* push in order ss, rsp, eflags, cs, rip */
	xorq %rax, %rax
	pushq %rax /* ss */
	pushq %rax /* rsp */
	pushq $(1<<9) /* eflags - interrupts on */
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
/* rdi:	prev */	
ENTRY(ret_from_fork)
	call schedule_tail
	GET_THREAD_INFO(%rcx)
	bt $TIF_SYSCALL_TRACE,threadinfo_flags(%rcx)
	jc rff_trace
rff_action:	
	RESTORE_REST
	testl $3,CS-ARGOFFSET(%rsp)	# from kernel_thread?
	je   int_ret_from_sys_call
	testl $_TIF_IA32,threadinfo_flags(%rcx)
	jnz  int_ret_from_sys_call
	RESTORE_TOP_OF_STACK %rdi,ARGOFFSET
	jmp ret_from_sys_call
rff_trace:
	movq %rsp,%rdi
	call syscall_trace
	GET_THREAD_INFO(%rcx)	
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
 * r10  arg3 	(--> moved to rcx for C)
 * r8   arg4
 * r9   arg5
 * r11  eflags for syscall/sysret, temporary for C
 * r12-r15,rbp,rbx saved by C code, not touched. 		
 * 
 * Interrupts are off on entry.
 * Only called from user space.
 *
 * XXX	if we had a free scratch register we could save the RSP into the stack frame
 *      and report it properly in ps. Unfortunately we haven't.
 */ 			 		

ENTRY(system_call)
	swapgs
	movq	%rsp,%gs:pda_oldrsp 
	movq	%gs:pda_kernelstack,%rsp
	sti					
	SAVE_ARGS 8,1
	movq  %rax,ORIG_RAX-ARGOFFSET(%rsp) 
	movq  %rcx,RIP-ARGOFFSET(%rsp)  
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
	.globl ret_from_sys_call
ret_from_sys_call:
	movl $_TIF_WORK_MASK,%edi
	/* edi:	flagmask */
sysret_check:		
	GET_THREAD_INFO(%rcx)
	cli
	movl threadinfo_flags(%rcx),%edx
	andl %edi,%edx
	jnz  sysret_careful 
	movq RIP-ARGOFFSET(%rsp),%rcx
	RESTORE_ARGS 0,-ARG_SKIP,1
	movq	%gs:pda_oldrsp,%rsp
	swapgs
	sysretq

	/* Handle reschedules */
	/* edx:	work, edi: workmask */	
sysret_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc sysret_signal
	sti
	pushq %rdi
	call schedule
	popq  %rdi
	jmp sysret_check

	/* Handle a signal */ 
sysret_signal:
	sti
	testl $(_TIF_SIGPENDING|_TIF_NOTIFY_RESUME),%edx
	jz    1f

	/* Really a signal */
	/* edx:	work flags (arg3) */
	leaq do_notify_resume(%rip),%rax
	leaq -ARGOFFSET(%rsp),%rdi # &pt_regs -> arg1
	xorl %esi,%esi # oldset -> arg2
	call ptregscall_common
1:	movl $_TIF_NEED_RESCHED,%edi
	jmp sysret_check
	
	/* Do syscall tracing */
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
	call *sys_call_table(,%rax,8)
	movq %rax,RAX-ARGOFFSET(%rsp)
1:	SAVE_REST
	movq %rsp,%rdi
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
	cli
	testl $3,CS-ARGOFFSET(%rsp)
	je retint_restore_args
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	mask to check */
int_with_check:
	GET_THREAD_INFO(%rcx)
	movl threadinfo_flags(%rcx),%edx
	andl %edi,%edx
	jnz   int_careful
	jmp   retint_swapgs

	/* Either reschedule or signal or syscall exit tracking needed. */
	/* First do a reschedule test. */
	/* edx:	work, edi: workmask */
int_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc  int_very_careful
	sti
	pushq %rdi
	call schedule
	popq %rdi
	jmp int_with_check

	/* handle signals and tracing -- both require a full stack frame */
int_very_careful:
	sti
	SAVE_REST
	/* Check for syscall exit trace */	
	bt $TIF_SYSCALL_TRACE,%edx
	jnc int_signal
	pushq %rdi
	leaq 8(%rsp),%rdi	# &ptregs -> arg1	
	call syscall_trace
	popq %rdi
	btr  $TIF_SYSCALL_TRACE,%edi
	jmp int_restore_rest
	
int_signal:
	testl $(_TIF_NOTIFY_RESUME|_TIF_SIGPENDING),%edx
	jz 1f
	movq %rsp,%rdi		# &ptregs -> arg1
	xorl %esi,%esi		# oldset -> arg2
	call do_notify_resume
1:	movl $_TIF_NEED_RESCHED,%edi	
int_restore_rest:
	RESTORE_REST
	jmp int_with_check
		
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
	bt $TIF_IA32,threadinfo_flags(%rcx)
	jc exec_32bit
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
	.macro interrupt func
	cld
#ifdef CONFIG_X86_REMOTE_DEBUG
	SAVE_ALL	
	movq %rsp,%rdi
#else		
	SAVE_ARGS
	leaq -ARGOFFSET(%rsp),%rdi	# arg1 for handler
#endif	
	testl $3,CS(%rdi)
	je 1f
	swapgs	
1:	addl $1,%gs:pda_irqcount	# RED-PEN should check preempt count
	movq %gs:pda_irqstackptr,%rax
	cmoveq %rax,%rsp							
	pushq %rdi			# save old stack	
	call \func
	.endm

ENTRY(common_interrupt)
	interrupt do_IRQ
	/* 0(%rsp): oldrsp-ARGOFFSET */
ret_from_intr:		
	popq  %rdi
	cli	
	subl $1,%gs:pda_irqcount
	leaq ARGOFFSET(%rdi),%rsp
exit_intr:	 	
	GET_THREAD_INFO(%rcx)
	testl $3,CS-ARGOFFSET(%rsp)
	je retint_kernel
	
	/* Interrupt came from user space */
	/*
	 * Has a correct top of stack, but a partial stack frame
	 * %rcx: thread info. Interrupts off.
	 */		
retint_with_reschedule:
	movl $_TIF_WORK_MASK,%edi
retint_check:			
	movl threadinfo_flags(%rcx),%edx
	andl %edi,%edx
	jnz  retint_careful
retint_swapgs:	 	
	cli
	swapgs 
retint_restore_args:				
	cli
	RESTORE_ARGS 0,8,0						
iret_label:	
	iretq

	.section __ex_table,"a"
	.quad iret_label,bad_iret	
	.previous
	.section .fixup,"ax"
	/* force a signal here? this matches i386 behaviour */
	/* running with kernel gs */
bad_iret:
	movq $-9999,%rdi	/* better code? */
	jmp do_exit			
	.previous	
	
	/* edi: workmask, edx: work */	
retint_careful:
	bt    $TIF_NEED_RESCHED,%edx
	jnc   retint_signal
	sti
	pushq %rdi
	call  schedule
	popq %rdi		
	GET_THREAD_INFO(%rcx)
	cli
	jmp retint_check
	
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
	cli
	movl $_TIF_NEED_RESCHED,%edi
	GET_THREAD_INFO(%rcx)	
	jmp retint_check

#ifdef CONFIG_PREEMPT
	/* Returning to kernel space. Check if we need preemption */
	/* rcx:	 threadinfo. interrupts off. */
	.p2align
retint_kernel:	
	cmpl $0,threadinfo_preempt_count(%rcx)
	jnz  retint_restore_args
	bt  $TIF_NEED_RESCHED,threadinfo_flags(%rcx)
	jnc  retint_restore_args
	bt   $9,EFLAGS-ARGOFFSET(%rsp)	/* interrupts off? */
	jc   retint_restore_args
	movl $PREEMPT_ACTIVE,threadinfo_preempt_count(%rcx)
	sti
	call schedule
	cli
	GET_THREAD_INFO(%rcx)
	movl $0,threadinfo_preempt_count(%rcx) 
	jmp exit_intr
#endif	
	
/*
 * APIC interrupts.
 */		
	.macro apicinterrupt num,func
	pushq $\num-256
	interrupt \func
	jmp ret_from_intr
	.endm

#ifdef CONFIG_SMP	
ENTRY(reschedule_interrupt)
	apicinterrupt RESCHEDULE_VECTOR,smp_reschedule_interrupt

ENTRY(invalidate_interrupt)
	apicinterrupt INVALIDATE_TLB_VECTOR,smp_invalidate_interrupt

ENTRY(call_function_interrupt)
	apicinterrupt CALL_FUNCTION_VECTOR,smp_call_function_interrupt
#endif

#ifdef CONFIG_X86_LOCAL_APIC	
ENTRY(apic_timer_interrupt)
	apicinterrupt LOCAL_TIMER_VECTOR,smp_apic_timer_interrupt

ENTRY(error_interrupt)
	apicinterrupt ERROR_APIC_VECTOR,smp_error_interrupt

ENTRY(spurious_interrupt)
	apicinterrupt SPURIOUS_APIC_VECTOR,smp_spurious_interrupt
#endif
				
/*
 * Exception entry points.
 */ 		
	.macro zeroentry sym
	pushq $0	/* push error code/oldrax */ 
	pushq %rax	/* push real oldrax to the rdi slot */ 
	leaq  \sym(%rip),%rax
	jmp error_entry
	.endm	

	.macro errorentry sym
	pushq %rax
	leaq  \sym(%rip),%rax
	jmp error_entry
	.endm

/*
 * Exception entry point. This expects an error code/orig_rax on the stack
 * and the exception handler in %rax.	
 */ 		  				
ENTRY(error_entry)
	/* rdi slot contains rax, oldrax contains error code */
	cld	
	subq  $14*8,%rsp
	movq %rsi,13*8(%rsp)
	movq 14*8(%rsp),%rsi	/* load rax from rdi slot */
	movq %rdx,12*8(%rsp)
	movq %rcx,11*8(%rsp)
	movq %rsi,10*8(%rsp)	/* store rax */ 
	movq %r8, 9*8(%rsp)
	movq %r9, 8*8(%rsp)
	movq %r10,7*8(%rsp)
	movq %r11,6*8(%rsp)
	movq %rbx,5*8(%rsp) 
	movq %rbp,4*8(%rsp) 
	movq %r12,3*8(%rsp) 
	movq %r13,2*8(%rsp) 
	movq %r14,1*8(%rsp) 
	movq %r15,(%rsp) 
	xorl %ebx,%ebx	
	testl $3,CS(%rsp)
	je  error_kernelspace
error_swapgs:	
	swapgs
error_sti:	
	movq %rdi,RDI(%rsp) 	
	movq %rsp,%rdi
	movq ORIG_RAX(%rsp),%rsi	/* get error code */ 
	movq $-1,ORIG_RAX(%rsp)
	call *%rax
error_exit:		
	movl %ebx,%eax		
	RESTORE_REST
	cli
	GET_THREAD_INFO(%rcx)	
	testl %eax,%eax
	jne  retint_kernel
	movl  threadinfo_flags(%rcx),%edx
	movl  $_TIF_WORK_MASK,%edi
	andl  %edi,%edx
	jnz  retint_careful
	swapgs 
	RESTORE_ARGS 0,8,0						
	iretq

error_kernelspace:
	incl %ebx
       /* There are two places in the kernel that can potentially fault with
          usergs. Handle them here. The exception handlers after
	   iret run with kernel gs again, so don't set the user space flag.
	   B stepping K8s sometimes report an truncated RIP for IRET 
	   exceptions returning to compat mode. Check for these here too. */
	leaq iret_label(%rip),%rbp
	cmpq %rbp,RIP(%rsp) 
	je   error_swapgs
	movl %ebp,%ebp	/* zero extend */
	cmpq %rbp,RIP(%rsp) 
	je   error_swapgs
	cmpq $gs_change,RIP(%rsp)
        je   error_swapgs
	jmp  error_sti
	
       /* Reload gs selector with exception handling */
       /* edi:  new selector */ 
ENTRY(load_gs_index)
	pushf
	cli
        swapgs
gs_change:     
        movl %edi,%gs   
2:	mfence		/* workaround */
	swapgs
        popf
        ret
       
        .section __ex_table,"a"
        .align 8
        .quad gs_change,bad_gs
        .previous
        .section .fixup,"ax"
	/* running with kernelgs */
bad_gs: 
	swapgs			/* switch back to user gs */
	xorl %eax,%eax
        movl %eax,%gs
        jmp  2b
        .previous       
	
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
	orq  kernel_thread_flags(%rip),%rdi
	movq $-1, %rsi
	movq %rsp, %rdx

	xorl %r8d,%r8d
	xorl %r9d,%r9d
	
	# clone now
	call do_fork
	movq %rax,RAX(%rsp)
	xorl %edi,%edi

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
	pushq $-1	#error code
	SAVE_ALL
	movl  $1,%ebx
	testl $3,CS(%rsp)
	je 1f
	xorl %ebx,%ebx
	swapgs
1:	movq  %cr0,%rax
	leaq  math_state_restore(%rip),%rcx
	leaq  math_emulate(%rip),%rdx
	testl $0x4,%eax
	cmoveq %rcx,%rdx
	call  *%rdx
	jmp  error_exit

ENTRY(debug)
	zeroentry do_debug

ENTRY(nmi)
	pushq $-1
	SAVE_ALL
        /* NMI could happen inside the critical section of a swapgs,
           so it is needed to use this expensive way to check. */
        movl  $MSR_GS_BASE,%ecx
        rdmsr
        xorl  %ebx,%ebx
        testl %edx,%edx
        js    1f
	swapgs
	movl $1,%ebx
1:	movq %rsp,%rdi # regs -> arg1
	call do_nmi
	/* XXX: should do preemption checks here */
	cli
	testl %ebx,%ebx
	jz 2f
	swapgs
2:	RESTORE_ALL 8
	iretq
	
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
	zeroentry do_divide_error

ENTRY(spurious_interrupt_bug)
	zeroentry do_spurious_interrupt_bug

ENTRY(machine_check)
       zeroentry do_machine_check      

ENTRY(call_debug)
       zeroentry do_call_debug

