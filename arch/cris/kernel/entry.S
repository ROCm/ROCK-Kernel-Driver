/* $Id: entry.S,v 1.11 2001/01/10 21:13:29 bjornw Exp $
 *
 *  linux/arch/cris/entry.S
 *
 *  Copyright (C) 2000 Axis Communications AB
 *
 *  Authors:	Bjorn Wesen (bjornw@axis.com)
 *
 *  $Log: entry.S,v $
 *  Revision 1.11  2001/01/10 21:13:29  bjornw
 *  SYMBOL_NAME is defined incorrectly for the compiler options we currently use
 *
 *  Revision 1.10  2000/12/18 23:47:56  bjornw
 *  * Added syscall trace support (ptrace), completely untested of course
 *  * Removed redundant check for NULL entries in syscall_table
 *
 *  Revision 1.9  2000/11/21 16:40:51  bjornw
 *  * New frame type used when an SBFS frame needs to be popped without
 *    actually restarting the instruction
 *  * Enable interrupts in signal_return (they did so in x86, I hope it's a good
 *    idea)
 *
 *  Revision 1.8  2000/11/17 16:53:35  bjornw
 *  Added detection of frame-type in Rexit, so that mmu_bus_fault can
 *  use ret_from_intr in the return-path to check for signals (like SEGV)
 *  and other foul things that might have occurred during the fault.
 *
 *  Revision 1.7  2000/10/06 15:04:28  bjornw
 *  Include mof in register savings
 *
 *  Revision 1.6  2000/09/12 16:02:44  bjornw
 *  Linux-2.4.0-test7 derived updates
 *
 *  Revision 1.5  2000/08/17 15:35:15  bjornw
 *  2.4.0-test6 changed local_irq_count and friends API
 *
 *  Revision 1.4  2000/08/02 13:59:30  bjornw
 *  Removed olduname and uname from the syscall list
 *
 *  Revision 1.3  2000/07/31 13:32:58  bjornw
 *  * Export ret_from_intr
 *  * _resume updated (prev/last tjohejsan)
 *  * timer_interrupt obsolete
 *  * SIGSEGV detection in mmu_bus_fault temporarily disabled
 *
 *
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * Stack layout in 'ret_from_system_call':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be 
 *	updated in fork.c:copy_process, signal.c:do_signal,
 *	ptrace.c and ptrace.h
 *
 */

#include <linux/linkage.h>
#include <linux/sys.h>
			
	;; functions exported from this file
	
	.globl _system_call
	.globl _ret_from_intr
	.globl _ret_from_sys_call
	.globl _resume
	.globl _multiple_interrupt
	.globl _hwbreakpoint
	.globl _IRQ1_interrupt
	.globl _timer_interrupt
	.globl _timer_shortcut
	.globl _spurious_interrupt
	.globl _hw_bp_trigs
	.globl _mmu_bus_fault
				
	.globl _sys_call_table
		
	;; syscall error codes
	
LENOSYS = 38

	;; offsets into the task_struct (found at sp aligned to THREAD_SIZE, 8192)
	;; linux/sched.h
	
LTASK_SIGPENDING  =  8
LTASK_NEEDRESCHED = 20
LTASK_PTRACE      = 24

	;; some pt_regs offsets (from ptrace.h)
	
LORIG_R10	= 4
LR13		= 8
LR12		= 12
LR11		= 16
LR10		= 20
LR1		= 56
LR0		= 60	
LDCCR		= 68
LSRP		= 72
LIRP		= 76

	;; below are various parts of system_call which are not in the fast-path
	
	;; handle software irqs
				
handle_softirq:
	push	r9
	jsr	_do_softirq		; call the C routine for softirq handling
	pop	r9

	;; fall-through
	
_ret_from_intr:
	;; check for resched only if we're going back to user-mode

	move	ccr, r0
	btstq	8, r0		; U-flag
	bpl	Rexit		; go back directly
	nop
	ba	ret_with_reschedule  ; go back but check schedule and signals first 
	nop
		
reschedule:
	;; keep r9 intact
	push	r9
	jsr	_schedule
	pop	r9
	ba	_ret_from_sys_call
	nop

	;; return but call do_signal first
signal_return:
	ei			; we can get here from an interrupt
	move.d	r9,r10		; do_signals syscall/irq param
	moveq	0,r11		; oldset param - 0 in this case
	move.d	sp,r12		; another argument to do_signal (the regs param)
	jsr	_do_signal	; arch/cris/kernel/signal.c
	ba	Rexit
	nop
	
	;; The system_call is called by a BREAK instruction, which works like
	;; an interrupt call but it stores the return PC in BRP instead of IRP.
	;; Since we dont really want to have two epilogues (one for system calls
	;; and one for interrupts) we push the contents of BRP instead of IRP in the
	;; system call prologue, to make it look like an ordinary interrupt on the
	;; stackframe.
	;;
	;; Since we can't have system calls inside interrupts, it should not matter
	;; that we don't stack IRP.
	;; 
	;; In r1 we have the wanted syscall number. Arguments come in r10,r11,r12,r13,r0
	;;
	;; This function looks on the _surface_ like spaghetti programming, but it's
	;; really designed so that the fast-path does not force cache-loading of non-used
	;; instructions. Only the non-common cases cause the outlined code to run..

_system_call:
	;; stack-frame similar to the irq heads, which is reversed in ret_from_sys_call
	push	brp		; this is normally push irp
	push	srp
	push	dccr
	push	mof
	subq	14*4,sp		; make room for r0-r13
	movem	r13,[sp]	; push r0-r13
	push	r10		; push orig_r10
	clear.d [sp=sp-4]	; frametype == 0, normal stackframe
	
	move.d	r10,r2		; save for later
	
	movs.w	-LENOSYS,r10
	move.d	r10,[sp+LR10]	; put the default return value in r10 in the frame
	
	move.d	sp,r10
	jsr	_set_esp0	; save top of frame (clobbers r9...)

	;; check if this process is syscall-traced

	move.d	sp, r10
	and.d	-8192, r10		; THREAD_SIZE == 8192
	move.d	[r10+LTASK_PTRACE],r10
	btstq	2, r10	                ; PT_TRACESYS
	bmi	tracesys
	nop	

	;; check for sanity in the requested syscall number
	
	cmpu.w	NR_syscalls,r1	
	bcc	_ret_from_sys_call
	lslq	2,r1		;  multiply by 4, in the delay slot

	;; read the system call vector into r1
	
	move.d	[r1+_sys_call_table],r1	

	;; the parameter carrying registers r11, r12 and 13 are intact - restore r10.
	;; the fifth parameter (if any) was in r0, and we need to put it on the stack

	push	r0
	move.d	r2,r10
	
	jsr	r1		; actually call the corresponding system call
	addq	4,sp		; pop the r0 parameter
	move.d	r10,[sp+LR10]	; save the return value

	moveq	1,r9		; "parameter" to ret_from_sys_call to show it was a sys call
	
	;; fall through into ret_from_sys_call to return
	
_ret_from_sys_call:
	;; r9 is a parameter - if 1, we came from a syscall, if 0, from an irq
		
	;; check if any bottom halves need service
	
	move.d	[_irq_stat],r0	       ; softirq_active
	and.d	[_irq_stat+4],r0       ; softirq_mask
	bne	handle_softirq
	nop

ret_with_reschedule:
	;; first get the current task-struct pointer (see top for defs)

	move.d	sp, r0
	and.d	-8192, r0	; THREAD_SIZE == 8192
	
	;; see if we want to reschedule into another process
	
	test.d	[r0+LTASK_NEEDRESCHED]
	bne	reschedule
	nop

	;; see if we need to run signal checks (important that r9 is intact here)

	test.d	[r0+LTASK_SIGPENDING]
	bne	signal_return
	nop
	
Rexit:
	;; this epilogue MUST match the prologues in multiple_interrupt, irq.h and ptregs.h
	pop	r10		; frametype
	bne	RBFexit		; was not CRIS_FRAME_NORMAL, handle otherwise
	addq	4,sp		; skip orig_r10, in delayslot
	movem	[sp+],r13	; registers r0-r13
	pop	mof		; multiply overflow register 
	pop	dccr		; condition codes
	pop	srp		; subroutine return pointer
	jmpu	[sp+]		; return by popping irp and jumping there
	;; jmpu takes the U-flag into account to see if we return to
	;; user-mode or kernel mode.

RBFexit:
	cmpq	2, r10		; was it CRIS_FRAME_FIXUP ?
	beq	2f
	movem	[sp+],r13	; registers r0-r13, in delay slot
	pop	mof		; multiply overflow register 
	pop	dccr		; condition codes
	pop	srp		; subroutine return pointer
	rbf	[sp+]		; return by popping the CPU status

2:	pop	mof		; multiply overflow register
	pop	dccr		; condition codes
	pop	srp		; subroutine return pointer
	;; now we have a 4-word SBFS frame which we do not want to restore
	;; using RBF since we have made a fixup. instead we would like to
	;; just get the PC value to restart it with, and skip the rest of
	;; the frame.
	pop	irp		; fixup location will be here
	pop	p8		; null pop
	pop	p8		; null pop
	reti			; return to IRP, taking U-flag into account
	pop	p8		; null pop in delayslot


tracesys:
	;; this first invocation of syscall_trace _requires_ that
	;; LR10 in the frame contains -LENOSYS (as is set in the beginning
	;; of system_call

	jsr	_syscall_trace

	;; now we should more or less do the same things as in the system_call
	;; but since our argument regs got clobbered during syscall_trace and
	;; because syscall_trace might want to alter them, we need to reload them
	;; from the stack-frame as we use them.

	;; check for sanity in the requested syscall number

	move.d	[sp+LR1], r1
	movs.w	-LENOSYS, r10
	cmpu.w	NR_syscalls,r1	
	bcc	1f
	lslq	2,r1		;  multiply by 4, in the delay slot

	;; read the system call vector entry into r1
	
	move.d	[r1+_sys_call_table],r1

	;; restore r10, r11, r12, r13 and r0 into the needed registers

	move.d	[sp+LORIG_R10], r10  ; LR10 is already filled with -LENOSYS
	move.d	[sp+LR11],      r11
	move.d	[sp+LR12],      r12
	move.d	[sp+LR13],      r13
	move.d	[sp+LR0],       r0

	;; the fifth parameter needs to be put on the stack for the system
	;; call to find it

	push	r0
	jsr	r1		; actually call the system-call
	addq	4,sp		; pop the r0 parameter

1:	move.d	r10,[sp+LR10]	; save the return value

	;; second call of syscall_trace, to let it grab the results
		
	jsr	_syscall_trace

	moveq	1,r9		; "parameter" to ret_from_sys_call to show it was a sys call
	ba	_ret_from_sys_call
	nop
		
	;; from asm/processor.h, the thread_struct
	
LTHREAD_KSP	= 0
LTHREAD_USP	= 4
LTHREAD_ESP0	= 8
LTHREAD_DCCR	= 12
	
	;; _resume performs the actual task-switching, by switching stack pointers
	;; input arguments: r10 = prev, r11 = next, r12 = thread offset in task struct
	;; returns old current in r10
	;;
	;; TODO:  see the i386 version. The switch_to which calls resume in our version
	;;        could really be an inline asm of this.

_resume:	
	push	srp		         ; we keep the old/new PC on the stack 
	add.d	r12, r10		 ; r10 = current tasks tss
	move	dccr, [r10+LTHREAD_DCCR] ; save irq enable state
	di

	move	usp, [r10+LTHREAD_USP]	 ; save user-mode stackpointer
	
	subq	10*4, sp
	movem	r9, [sp]		 ; save non-scratch registers
	
	move.d	sp, [r10+LTHREAD_KSP]	 ; save the kernel stack pointer for the old task
	move.d	sp, r10			 ; return last running task in r10
	and.d   -8192, r10	         ; get task ptr from stackpointer
	add.d	r12, r11		 ; find the new tasks tss
	move.d	[r11+LTHREAD_KSP], sp	 ; switch into the new stackframe by restoring kernel sp

	movem	[sp+], r9		 ; restore non-scratch registers

	move	[r11+LTHREAD_USP], usp	 ; restore user-mode stackpointer
	
	move	[r11+LTHREAD_DCCR], dccr ; restore irq enable status
	jump	[sp+]		         ; restore PC

	;; This is the MMU bus fault handler.
	;; It needs to stack the CPU status and overall is different
	;; from the other interrupt handlers.

_mmu_bus_fault:	
	sbfs	[sp=sp-16]	; push the internal CPU status
	;; the first longword in the sbfs frame was the interrupted PC
	;; which fits nicely with the "IRP" slot in pt_regs normally used to
	;; contain the return address. used by Oops to print kernel errors..
	push	srp		; make a stackframe similar to pt_regs
	push	dccr
	push	mof
	di
	subq	14*4, sp
	movem	r13, [sp]
	push	r10		; dummy orig_r10
	moveq	1, r10
	push	r10		; frametype == 1, BUSFAULT frame type

	moveq	0, r9		; busfault is equivalent to an irq
		
	move.d	sp, r10		; pt_regs argument to handle_mmu_bus_fault
		
	jsr	_handle_mmu_bus_fault  ; in arch/cris/mm/fault.c

	;; now we need to return through the normal path, we cannot just
	;; do the RBFexit since we might have killed off the running
	;; process due to a SEGV, scheduled due to a page blocking or
	;; whatever.

	ba	_ret_from_intr
	nop
		
	;; special handlers for breakpoint and NMI
#if 0			
_hwbreakpoint:
	push	dccr
	di
	push	r10
	push	r11
	push	r12
	push	r13
	clearf	b
	move	brp,r11
	move.d	[_hw_bp_msg],r10
	jsr	_printk
	setf	b
	pop	r13
	pop	r12
	pop	r11
	pop	r10
	pop	dccr
	retb
	nop
#else
_hwbreakpoint:
	push	dccr
	di
#if 1
	push	r10
	push	r11
	move.d	[_hw_bp_trig_ptr],r10
	move.d	[r10],r11
	cmp.d	42,r11
	beq	nobp
	nop
	move	brp,r11
	move.d	r11,[r10+]
	move.d	r10,[_hw_bp_trig_ptr]
nobp:	pop	r11
	pop	r10
#endif
	pop	dccr
	retb
	nop
#endif
	
_IRQ1_interrupt:
_spurious_interrupt:	
	di
	move.b	4,r0
	move.b	r0,[0xb0000030]
basse2:	ba	basse2
	nop

	;; this handles the case when multiple interrupts arrive at the same time
	;; we jump to the first set interrupt bit in a priority fashion
	;; the hardware will call the unserved interrupts after the handler finishes
	
_multiple_interrupt:
	;; this prologue MUST match the one in irq.h and the struct in ptregs.h!!!
	push	irp
	push	srp
	push	dccr
	push	mof
	di
	subq	14*4,sp
	movem	r13,[sp]
	push	r10		; push orig_r10
	clear.d [sp=sp-4]	; frametype == 0, normal frame
	
	move.d	_irq_shortcuts + 8,r1
	moveq	2,r2		; first bit we care about is the timer0 irq
	move.d	[0xb00000d8],r0	; read the irq bits that triggered the multiple irq
multloop:	
	btst	r2,r0		; check for the irq given by bit r2
	bmi	do_shortcut	; actually do the shortcut
	nop
	addq	1,r2		; next vector bit - remember this is in the delay slot!
	addq	4,r1		; next vector
	cmpq	26,r2
	bne	multloop	; process all irq's up to and including number 25
	nop
	
	;; strange, we didn't get any set vector bits.. oh well, just return
	
	ba	Rexit
	nop

do_shortcut:
	test.d	[r1]
	beq	Rexit
	nop
	jump	[r1]		; jump to the irq handlers shortcut

	
	.data

_hw_bp_trigs:
	.space 64*4
_hw_bp_trig_ptr:
	.dword _hw_bp_trigs

/* linux/linkage.h got it wrong for this compiler currently */
	
#undef SYMBOL_NAME
#define SYMBOL_NAME(X) _/**/X
		
_sys_call_table:	
	.long SYMBOL_NAME(sys_ni_syscall)	/* 0  -  old "setup()" system call*/
	.long SYMBOL_NAME(sys_exit)
	.long SYMBOL_NAME(sys_fork)
	.long SYMBOL_NAME(sys_read)
	.long SYMBOL_NAME(sys_write)
	.long SYMBOL_NAME(sys_open)		/* 5 */
	.long SYMBOL_NAME(sys_close)
	.long SYMBOL_NAME(sys_waitpid)
	.long SYMBOL_NAME(sys_creat)
	.long SYMBOL_NAME(sys_link)
	.long SYMBOL_NAME(sys_unlink)		/* 10 */
	.long SYMBOL_NAME(sys_execve)
	.long SYMBOL_NAME(sys_chdir)
	.long SYMBOL_NAME(sys_time)
	.long SYMBOL_NAME(sys_mknod)
	.long SYMBOL_NAME(sys_chmod)		/* 15 */
	.long SYMBOL_NAME(sys_lchown16)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old break syscall holder */
	.long SYMBOL_NAME(sys_stat)
	.long SYMBOL_NAME(sys_lseek)
	.long SYMBOL_NAME(sys_getpid)		/* 20 */
	.long SYMBOL_NAME(sys_mount)
	.long SYMBOL_NAME(sys_oldumount)
	.long SYMBOL_NAME(sys_setuid16)
	.long SYMBOL_NAME(sys_getuid16)
	.long SYMBOL_NAME(sys_stime)		/* 25 */
	.long SYMBOL_NAME(sys_ptrace)
	.long SYMBOL_NAME(sys_alarm)
	.long SYMBOL_NAME(sys_fstat)
	.long SYMBOL_NAME(sys_pause)
	.long SYMBOL_NAME(sys_utime)		/* 30 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old stty syscall holder */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old gtty syscall holder */
	.long SYMBOL_NAME(sys_access)
	.long SYMBOL_NAME(sys_nice)
	.long SYMBOL_NAME(sys_ni_syscall)	/* 35  old ftime syscall holder */
	.long SYMBOL_NAME(sys_sync)
	.long SYMBOL_NAME(sys_kill)
	.long SYMBOL_NAME(sys_rename)
	.long SYMBOL_NAME(sys_mkdir)
	.long SYMBOL_NAME(sys_rmdir)		/* 40 */
	.long SYMBOL_NAME(sys_dup)
	.long SYMBOL_NAME(sys_pipe)
	.long SYMBOL_NAME(sys_times)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old prof syscall holder */
	.long SYMBOL_NAME(sys_brk)		/* 45 */
	.long SYMBOL_NAME(sys_setgid16)
	.long SYMBOL_NAME(sys_getgid16)
	.long SYMBOL_NAME(sys_signal)
	.long SYMBOL_NAME(sys_geteuid16)
	.long SYMBOL_NAME(sys_getegid16)	/* 50 */
	.long SYMBOL_NAME(sys_acct)
	.long SYMBOL_NAME(sys_umount)		/* recycled never used phys() */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old lock syscall holder */
	.long SYMBOL_NAME(sys_ioctl)
	.long SYMBOL_NAME(sys_fcntl)		/* 55 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old mpx syscall holder */
	.long SYMBOL_NAME(sys_setpgid)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old ulimit syscall holder */
	.long SYMBOL_NAME(sys_ni_syscall)       /* old sys_olduname holder */
	.long SYMBOL_NAME(sys_umask)		/* 60 */
	.long SYMBOL_NAME(sys_chroot)
	.long SYMBOL_NAME(sys_ustat)
	.long SYMBOL_NAME(sys_dup2)
	.long SYMBOL_NAME(sys_getppid)
	.long SYMBOL_NAME(sys_getpgrp)		/* 65 */
	.long SYMBOL_NAME(sys_setsid)
	.long SYMBOL_NAME(sys_sigaction)
	.long SYMBOL_NAME(sys_sgetmask)
	.long SYMBOL_NAME(sys_ssetmask)
	.long SYMBOL_NAME(sys_setreuid16)	/* 70 */
	.long SYMBOL_NAME(sys_setregid16)
	.long SYMBOL_NAME(sys_sigsuspend)
	.long SYMBOL_NAME(sys_sigpending)
	.long SYMBOL_NAME(sys_sethostname)
	.long SYMBOL_NAME(sys_setrlimit)	/* 75 */
	.long SYMBOL_NAME(sys_old_getrlimit)
	.long SYMBOL_NAME(sys_getrusage)
	.long SYMBOL_NAME(sys_gettimeofday)
	.long SYMBOL_NAME(sys_settimeofday)
	.long SYMBOL_NAME(sys_getgroups16)	/* 80 */
	.long SYMBOL_NAME(sys_setgroups16)
	.long SYMBOL_NAME(sys_select)           /* was old_select in Linux/E100 */
	.long SYMBOL_NAME(sys_symlink)
	.long SYMBOL_NAME(sys_lstat)
	.long SYMBOL_NAME(sys_readlink)		/* 85 */
	.long SYMBOL_NAME(sys_uselib)
	.long SYMBOL_NAME(sys_swapon)
	.long SYMBOL_NAME(sys_reboot)
	.long SYMBOL_NAME(old_readdir)
	.long SYMBOL_NAME(old_mmap)		/* 90 */
	.long SYMBOL_NAME(sys_munmap)
	.long SYMBOL_NAME(sys_truncate)
	.long SYMBOL_NAME(sys_ftruncate)
	.long SYMBOL_NAME(sys_fchmod)
	.long SYMBOL_NAME(sys_fchown16)		/* 95 */
	.long SYMBOL_NAME(sys_getpriority)
	.long SYMBOL_NAME(sys_setpriority)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old profil syscall holder */
	.long SYMBOL_NAME(sys_statfs)
	.long SYMBOL_NAME(sys_fstatfs)		/* 100 */
	.long SYMBOL_NAME(sys_ni_syscall)       /* sys_ioperm in i386 */
	.long SYMBOL_NAME(sys_socketcall)
	.long SYMBOL_NAME(sys_syslog)
	.long SYMBOL_NAME(sys_setitimer)
	.long SYMBOL_NAME(sys_getitimer)	/* 105 */
	.long SYMBOL_NAME(sys_newstat)
	.long SYMBOL_NAME(sys_newlstat)
	.long SYMBOL_NAME(sys_newfstat)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old sys_uname holder */
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_iopl in i386 */
	.long SYMBOL_NAME(sys_vhangup)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old "idle" system call */
	.long SYMBOL_NAME(sys_ni_syscall)	/* vm86old in i386 */
	.long SYMBOL_NAME(sys_wait4)
	.long SYMBOL_NAME(sys_swapoff)		/* 115 */
	.long SYMBOL_NAME(sys_sysinfo)
	.long SYMBOL_NAME(sys_ipc)
	.long SYMBOL_NAME(sys_fsync)
	.long SYMBOL_NAME(sys_sigreturn)
	.long SYMBOL_NAME(sys_clone)		/* 120 */
	.long SYMBOL_NAME(sys_setdomainname)
	.long SYMBOL_NAME(sys_newuname)
	.long SYMBOL_NAME(sys_ni_syscall)	/* TODO sys_modify_ldt - do something ?*/
	.long SYMBOL_NAME(sys_adjtimex)
	.long SYMBOL_NAME(sys_mprotect)		/* 125 */
	.long SYMBOL_NAME(sys_sigprocmask)
	.long SYMBOL_NAME(sys_create_module)
	.long SYMBOL_NAME(sys_init_module)
	.long SYMBOL_NAME(sys_delete_module)
	.long SYMBOL_NAME(sys_get_kernel_syms)	/* 130 */
	.long SYMBOL_NAME(sys_quotactl)
	.long SYMBOL_NAME(sys_getpgid)
	.long SYMBOL_NAME(sys_fchdir)
	.long SYMBOL_NAME(sys_bdflush)
	.long SYMBOL_NAME(sys_sysfs)		/* 135 */
	.long SYMBOL_NAME(sys_personality)
	.long SYMBOL_NAME(sys_ni_syscall)	/* for afs_syscall */
	.long SYMBOL_NAME(sys_setfsuid16)
	.long SYMBOL_NAME(sys_setfsgid16)
	.long SYMBOL_NAME(sys_llseek)		/* 140 */
	.long SYMBOL_NAME(sys_getdents)
	.long SYMBOL_NAME(sys_select)
	.long SYMBOL_NAME(sys_flock)
	.long SYMBOL_NAME(sys_msync)
	.long SYMBOL_NAME(sys_readv)		/* 145 */
	.long SYMBOL_NAME(sys_writev)
	.long SYMBOL_NAME(sys_getsid)
	.long SYMBOL_NAME(sys_fdatasync)
	.long SYMBOL_NAME(sys_sysctl)
	.long SYMBOL_NAME(sys_mlock)		/* 150 */
	.long SYMBOL_NAME(sys_munlock)
	.long SYMBOL_NAME(sys_mlockall)
	.long SYMBOL_NAME(sys_munlockall)
	.long SYMBOL_NAME(sys_sched_setparam)
	.long SYMBOL_NAME(sys_sched_getparam)   /* 155 */
	.long SYMBOL_NAME(sys_sched_setscheduler)
	.long SYMBOL_NAME(sys_sched_getscheduler)
	.long SYMBOL_NAME(sys_sched_yield)
	.long SYMBOL_NAME(sys_sched_get_priority_max)
	.long SYMBOL_NAME(sys_sched_get_priority_min)  /* 160 */
	.long SYMBOL_NAME(sys_sched_rr_get_interval)
	.long SYMBOL_NAME(sys_nanosleep)
	.long SYMBOL_NAME(sys_mremap)
	.long SYMBOL_NAME(sys_setresuid16)
	.long SYMBOL_NAME(sys_getresuid16)	/* 165 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_vm86 */
	.long SYMBOL_NAME(sys_query_module)
	.long SYMBOL_NAME(sys_poll)
	.long SYMBOL_NAME(sys_nfsservctl)
	.long SYMBOL_NAME(sys_setresgid16)	/* 170 */
	.long SYMBOL_NAME(sys_getresgid16)
	.long SYMBOL_NAME(sys_prctl)
	.long SYMBOL_NAME(sys_rt_sigreturn)
	.long SYMBOL_NAME(sys_rt_sigaction)
	.long SYMBOL_NAME(sys_rt_sigprocmask)	/* 175 */
	.long SYMBOL_NAME(sys_rt_sigpending)
	.long SYMBOL_NAME(sys_rt_sigtimedwait)
	.long SYMBOL_NAME(sys_rt_sigqueueinfo)
	.long SYMBOL_NAME(sys_rt_sigsuspend)
	.long SYMBOL_NAME(sys_pread)		/* 180 */
	.long SYMBOL_NAME(sys_pwrite)
	.long SYMBOL_NAME(sys_chown16)
	.long SYMBOL_NAME(sys_getcwd)
	.long SYMBOL_NAME(sys_capget)
	.long SYMBOL_NAME(sys_capset)           /* 185 */
	.long SYMBOL_NAME(sys_sigaltstack)
	.long SYMBOL_NAME(sys_sendfile)
	.long SYMBOL_NAME(sys_ni_syscall)	/* streams1 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* streams2 */
	.long SYMBOL_NAME(sys_vfork)            /* 190 */
	.long SYMBOL_NAME(sys_getrlimit)
	.long SYMBOL_NAME(sys_mmap2)
	.long SYMBOL_NAME(sys_truncate64)
	.long SYMBOL_NAME(sys_ftruncate64)
	.long SYMBOL_NAME(sys_stat64)		/* 195 */
	.long SYMBOL_NAME(sys_lstat64)
	.long SYMBOL_NAME(sys_fstat64)
	.long SYMBOL_NAME(sys_lchown)
	.long SYMBOL_NAME(sys_getuid)
	.long SYMBOL_NAME(sys_getgid)		/* 200 */
	.long SYMBOL_NAME(sys_geteuid)
	.long SYMBOL_NAME(sys_getegid)
	.long SYMBOL_NAME(sys_setreuid)
	.long SYMBOL_NAME(sys_setregid)
	.long SYMBOL_NAME(sys_getgroups)	/* 205 */
	.long SYMBOL_NAME(sys_setgroups)
	.long SYMBOL_NAME(sys_fchown)
	.long SYMBOL_NAME(sys_setresuid)
	.long SYMBOL_NAME(sys_getresuid)
	.long SYMBOL_NAME(sys_setresgid)	/* 210 */
	.long SYMBOL_NAME(sys_getresgid)
	.long SYMBOL_NAME(sys_chown)
	.long SYMBOL_NAME(sys_setuid)
	.long SYMBOL_NAME(sys_setgid)
	.long SYMBOL_NAME(sys_setfsuid)		/* 215 */
	.long SYMBOL_NAME(sys_setfsgid)
	.long SYMBOL_NAME(sys_pivot_root)
	.long SYMBOL_NAME(sys_mincore)
	.long SYMBOL_NAME(sys_madvise)
	.long SYMBOL_NAME(sys_getdents64)       /* 220 */

        /*
         * NOTE!! This doesn't have to be exact - we just have
         * to make sure we have _enough_ of the "sys_ni_syscall"
         * entries. Don't panic if you notice that this hasn't
         * been shrunk every time we add a new system call.
         */

	;; TODO: this needs to actually generate sys_ni_syscall entires
	;; since we now have removed the check for NULL entries in this
	;; table in system_call!
	
	.space (NR_syscalls-220)*4
	
