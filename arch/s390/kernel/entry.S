/*
 *  arch/s390/kernel/entry.S
 *    S390 low-level entry points.
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Hartmut Penner (hp@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <linux/config.h>
#include <asm/cache.h>
#include <asm/lowcore.h>
#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/offsets.h>
#include <asm/unistd.h>

/*
 * Stack layout for the system_call stack entry.
 * The first few entries are identical to the user_regs_struct.
 */
SP_PTREGS    =  STACK_FRAME_OVERHEAD 
SP_PSW       =  STACK_FRAME_OVERHEAD + __PT_PSW
SP_R0        =  STACK_FRAME_OVERHEAD + __PT_GPRS
SP_R1        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 4
SP_R2        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 8
SP_R3        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 12
SP_R4        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 16
SP_R5        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 20
SP_R6        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 24
SP_R7        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 28
SP_R8        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 32
SP_R9        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 36
SP_R10       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 40
SP_R11       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 44
SP_R12       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 48
SP_R13       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 52
SP_R14       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 56
SP_R15       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 60
SP_ORIG_R2   =  STACK_FRAME_OVERHEAD + __PT_ORIG_GPR2
SP_ILC       =  STACK_FRAME_OVERHEAD + __PT_ILC
SP_TRAP      =  STACK_FRAME_OVERHEAD + __PT_TRAP
SP_SIZE      =  STACK_FRAME_OVERHEAD + __PT_SIZE

_TIF_WORK_SVC = (_TIF_SIGPENDING | _TIF_NEED_RESCHED | _TIF_RESTART_SVC)
_TIF_WORK_INT = (_TIF_SIGPENDING | _TIF_NEED_RESCHED)

#define BASED(name) name-system_call(%r13)

/*
 * Register usage in interrupt handlers:
 *    R9  - pointer to current task structure
 *    R13 - pointer to literal pool
 *    R14 - return register for function calls
 *    R15 - kernel stack pointer
 */

        .macro  SAVE_ALL_BASE psworg,savearea,sync
        stm     %r12,%r15,\savearea
	l	%r13,__LC_SVC_NEW_PSW+4   # load &system_call to %r13
	.endm

        .macro  CLEANUP_SAVE_ALL_BASE psworg,savearea,sync
	l	%r1,SP_PSW+4(%r15)
	cli	1(%r1),0xcf
	jne	0f
	mvc	\savearea(16),SP_R12(%r15)
0:	st	%r13,SP_R13(%r15)
	.endm

        .macro  SAVE_ALL psworg,savearea,sync
	.if	\sync
        tm      \psworg+1,0x01            # test problem state bit
        bz      BASED(1f)                 # skip stack setup save
        l       %r15,__LC_KERNEL_STACK    # problem state -> load ksp
	.else
        tm      \psworg+1,0x01            # test problem state bit
        bnz     BASED(0f)                 # from user -> load async stack
	l	%r14,__LC_ASYNC_STACK	  # are we already on the async stack ?
	slr     %r14,%r15
	sra	%r14,13
	be	BASED(1f)
0:	l	%r15,__LC_ASYNC_STACK
	.endif
1:      s       %r15,BASED(.Lc_spsize)    # make room for registers & psw
	l	%r14,BASED(.L\psworg)
	slr	%r12,%r12
	icm	%r14,12,__LC_SVC_ILC
        stm     %r0,%r11,SP_R0(%r15)      # store gprs 0-12 to kernel stack
        st      %r2,SP_ORIG_R2(%r15)      # store original content of gpr 2
        mvc     SP_R12(16,%r15),\savearea # move R13-R15 to stack
        mvc     SP_PSW(8,%r15),\psworg    # move user PSW to stack
	st	%r14,SP_ILC(%r15)
        st      %r12,0(%r15)              # clear back chain
        .endm

	.macro	CLEANUP_SAVE_ALL psworg,savearea,sync
	l	%r1,\savearea+12
	.if	\sync
	tm	\psworg+1,0x01
	bz	BASED(1f)
	l	%r1,__LC_KERNEL_STACK
	.else
	tm	\psworg+1,0x01
	bnz	BASED(0f)
	l	%r0,__LC_ASYNC_STACK
	slr	%r0,%r1
	sra	%r0,13
	bz	BASED(1f)
0:	l	%r1,__LC_ASYNC_STACK
	.endif
1:	s	%r1,BASED(.Lc_spsize)
	st	%r1,SP_R15(%r15)
	l	%r0,BASED(.L\psworg)
	xc	SP_R12(4,%r15),SP_R12(%r15)
	icm	%r0,12,__LC_SVC_ILC
	st	%r0,SP_R14(%r15)
	mvc	SP_R0(48,%r1),SP_R0(%r15)
	mvc	SP_ORIG_R2(4,%r1),SP_R2(%r15)
	mvc	SP_R12(16,%r1),\savearea
	mvc	SP_PSW(8,%r1),\psworg
	st	%r0,SP_ILC(%r1)
	xc	0(4,%r1),0(%r1)
	.endm

        .macro  RESTORE_ALL               # system exit macro
        mvc     __LC_RETURN_PSW(8),SP_PSW(%r15)  # move user PSW to lowcore
        ni      __LC_RETURN_PSW+1,0xfd    # clear wait state bit
        lm      %r0,%r15,SP_R0(%r15)      # load gprs 0-15 of user
        lpsw    __LC_RETURN_PSW           # back to caller
        .endm

	.macro	CLEANUP_RESTORE_ALL
	l	%r1,SP_PSW+4(%r15)
	cli	0(%r1),0x82
	jne	0f
	mvc	SP_PSW(8,%r15),__LC_RETURN_PSW
	j	1f
0:	l	%r1,SP_R15(%r15)
	mvc	SP_PSW(8,%r15),SP_PSW(%r1)
	mvc	SP_R0(64,%r15),SP_R0(%r1)
1:
	.endm

        .macro  GET_THREAD_INFO
	l	%r9,__LC_THREAD_INFO
        .endm

	.macro	CHECK_CRITICAL
        tm      SP_PSW+1(%r15),0x01      # test problem state bit
	bnz	BASED(0f)		 # from user -> not critical
	clc	SP_PSW+4(4,%r15),BASED(.Lcritical_end)
	jnl	0f
	clc	SP_PSW+4(4,%r15),BASED(.Lcritical_start)
	jl	0f
	l	%r1,BASED(.Lcleanup_critical)
	basr	%r14,%r1
0:
	.endm

/*
 * Scheduler resume function, called by switch_to
 *  gpr2 = (task_struct *) prev
 *  gpr3 = (task_struct *) next
 * Returns:
 *  gpr2 = prev
 */
        .globl  __switch_to
__switch_to:
        basr    %r1,0
__switch_to_base:
	tm	__THREAD_per(%r3),0xe8		# new process is using per ?
	bz	__switch_to_noper-__switch_to_base(%r1)	# if not we're fine
        stctl   %c9,%c11,24(%r15)		# We are using per stuff
        clc     __THREAD_per(12,%r3),24(%r15)
        be      __switch_to_noper-__switch_to_base(%r1)	# we got away w/o bashing TLB's
        lctl    %c9,%c11,__THREAD_per(%r3)	# Nope we didn't
__switch_to_noper:
        stm     %r6,%r15,24(%r15)       # store __switch_to registers of prev task
	st	%r15,__THREAD_ksp(%r2)	# store kernel stack to prev->tss.ksp
	l	%r15,__THREAD_ksp(%r3)	# load kernel stack from next->tss.ksp
	lm	%r6,%r15,24(%r15)	# load __switch_to registers of next task
	st	%r3,__LC_CURRENT	# __LC_CURRENT = current task struct
	l	%r3,__THREAD_info(%r3)  # load thread_info from task struct
	st	%r3,__LC_THREAD_INFO
	ahi	%r3,8192
	st	%r3,__LC_KERNEL_STACK	# __LC_KERNEL_STACK = new kernel stack
	br	%r14

/*
 * do_softirq calling function. We want to run the softirq functions on the
 * asynchronous interrupt stack.
 */
	.global do_call_softirq
do_call_softirq:
	stnsm	24(%r15),0xfc
	stm	%r12,%r15,28(%r15)
	lr	%r12,%r15
        basr    %r13,0
do_call_base:
	l	%r0,__LC_ASYNC_STACK
	slr     %r0,%r15
	sra	%r0,13
	be	0f-do_call_base(%r13)
	l	%r15,__LC_ASYNC_STACK
0:	sl	%r15,.Lc_overhead-do_call_base(%r13)
        st	%r12,0(%r15)	# store backchain
	l	%r1,.Ldo_softirq-do_call_base(%r13)
	basr	%r14,%r1
	lm	%r12,%r15,28(%r12)
	ssm	24(%r15)
	br	%r14
	
__critical_start:
/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are executed with interrupts enabled.
 */

	.globl  system_call
system_call:
	SAVE_ALL_BASE __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
        SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	lh	%r7,0x8a	  # get svc number from lowcore
sysc_enter:
        GET_THREAD_INFO           # load pointer to task_struct to R9
	sla	%r7,2             # *4 and test for svc 0
	bnz	BASED(sysc_do_restart)  # svc number > 0
	# svc 0: system call number in %r1
	cl	%r1,BASED(.Lnr_syscalls)
	bnl	BASED(sysc_do_restart)
	lr	%r7,%r1           # copy svc number to %r7
	sla	%r7,2             # *4
sysc_do_restart:
	tm	__TI_flags+3(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        l       %r8,sys_call_table-system_call(%r7,%r13) # get system call addr.
        bo      BASED(sysc_tracesys)
        basr    %r14,%r8          # call sys_xxxx
        st      %r2,SP_R2(%r15)   # store return value (change R2 on stack)
                                  # ATTENTION: check sys_execve_glue before
                                  # changing anything here !!

sysc_return:
	tm	SP_PSW+1(%r15),0x01	# returning to user ?
	bno	BASED(sysc_leave)
	tm	__TI_flags+3(%r9),_TIF_WORK_SVC
	bnz	BASED(sysc_work)  # there is work to do (signals etc.)
sysc_leave:
        RESTORE_ALL

#
# recheck if there is more work to do
#
sysc_work_loop:
        GET_THREAD_INFO           # load pointer to task_struct to R9
	tm	__TI_flags+3(%r9),_TIF_WORK_SVC
	bz	BASED(sysc_leave)      # there is no work to do
#
# One of the work bits is on. Find out which one.
# Checked are: _TIF_SIGPENDING and _TIF_NEED_RESCHED
#
sysc_work:
	tm	__TI_flags+3(%r9),_TIF_NEED_RESCHED
	bo	BASED(sysc_reschedule)
	tm	__TI_flags+3(%r9),_TIF_SIGPENDING
	bo	BASED(sysc_sigpending)
	tm	__TI_flags+3(%r9),_TIF_RESTART_SVC
	bo	BASED(sysc_restart)
	b	BASED(sysc_leave)

#
# _TIF_NEED_RESCHED is set, call schedule
#	
sysc_reschedule:        
        l       %r1,BASED(.Lschedule)
	la      %r14,BASED(sysc_work_loop)
	br      %r1		       # call scheduler

#
# _TIF_SIGPENDING is set, call do_signal
#
sysc_sigpending:     
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        sr      %r3,%r3                # clear *oldset
        l       %r1,BASED(.Ldo_signal)
	basr	%r14,%r1               # call do_signal
	tm	__TI_flags+3(%r9),_TIF_RESTART_SVC
	bo	BASED(sysc_restart)
	b	BASED(sysc_leave)      # out of here, do NOT recheck

#
# _TIF_RESTART_SVC is set, set up registers and restart svc
#
sysc_restart:
	ni	__TI_flags+3(%r9),255-_TIF_RESTART_SVC # clear TIF_RESTART_SVC
	l	%r7,SP_R2(%r15)        # load new svc number
	sla	%r7,2
	mvc	SP_R2(4,%r15),SP_ORIG_R2(%r15) # restore first argument
	lm	%r2,%r6,SP_R2(%r15)    # load svc arguments
	b	BASED(sysc_do_restart) # restart svc

__critical_end:

#
# call trace before and after sys_call
#
sysc_tracesys:
        l       %r1,BASED(.Ltrace)
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,0
	srl	%r7,2
	st	%r7,SP_R2(%r15)
	basr	%r14,%r1
	clc	SP_R2(4,%r15),BASED(.Lnr_syscalls)
	bnl	BASED(sysc_tracenogo)
	l	%r7,SP_R2(%r15)        # strace might have changed the 
	sll	%r7,2                  #  system call
	l	%r8,sys_call_table-system_call(%r7,%r13)
sysc_tracego:
	lm	%r3,%r6,SP_R3(%r15)
	l	%r2,SP_ORIG_R2(%r15)
	basr	%r14,%r8          # call sys_xxx
	st	%r2,SP_R2(%r15)   # store return value
sysc_tracenogo:
	tm	__TI_flags+3(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        bno     BASED(sysc_return)
	l	%r1,BASED(.Ltrace)
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,1
	la	%r14,BASED(sysc_return)
	br	%r1

#
# a new process exits the kernel with ret_from_fork
#
        .globl  ret_from_fork
ret_from_fork:
	l	%r13,__LC_SVC_NEW_PSW+4
        GET_THREAD_INFO           # load pointer to task_struct to R9
        l       %r1,BASED(.Lschedtail)
	basr    %r14,%r1
        stosm   24(%r15),0x03     # reenable interrupts
	b	BASED(sysc_return)

#
# clone, fork, vfork, exec and sigreturn need glue,
# because they all expect pt_regs as parameter,
# but are called with different parameter.
# return-address is set up above
#
sys_clone_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,BASED(.Lclone)
        br      %r1                   # branch to sys_clone

sys_fork_glue:  
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,BASED(.Lfork)
        br      %r1                   # branch to sys_fork

sys_vfork_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,BASED(.Lvfork)
        br      %r1                   # branch to sys_vfork

sys_execve_glue:        
        la      %r2,SP_PTREGS(%r15)   # load pt_regs
        l       %r1,BASED(.Lexecve)
	lr      %r12,%r14             # save return address
        basr    %r14,%r1              # call sys_execve
        ltr     %r2,%r2               # check if execve failed
        bnz     0(%r12)               # it did fail -> store result in gpr2
        b       4(%r12)               # SKIP ST 2,SP_R2(15) after BASR 14,8
                                      # in system_call/sysc_tracesys

sys_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        l       %r1,BASED(.Lsigreturn)
        br      %r1                   # branch to sys_sigreturn

sys_rt_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        l       %r1,BASED(.Lrt_sigreturn)
        br      %r1                   # branch to sys_sigreturn

#
# sigsuspend and rt_sigsuspend need pt_regs as an additional
# parameter and they have to skip the store of %r2 into the
# user register %r2 because the return value was set in 
# sigsuspend and rt_sigsuspend already and must not be overwritten!
#

sys_sigsuspend_glue:    
        lr      %r5,%r4               # move mask back
        lr      %r4,%r3               # move history1 parameter
        lr      %r3,%r2               # move history0 parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
        l       %r1,BASED(.Lsigsuspend)
	la      %r14,4(%r14)          # skip store of return value
        br      %r1                   # branch to sys_sigsuspend

sys_rt_sigsuspend_glue: 
        lr      %r4,%r3               # move sigsetsize parameter
        lr      %r3,%r2               # move unewset parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
        l       %r1,BASED(.Lrt_sigsuspend)
	la      %r14,4(%r14)          # skip store of return value
        br      %r1                   # branch to sys_rt_sigsuspend

sys_sigaltstack_glue:
        la      %r4,SP_PTREGS(%r15)   # load pt_regs as parameter
        l       %r1,BASED(.Lsigaltstack)
        br      %r1                   # branch to sys_sigreturn


/*
 * Program check handler routine
 */

        .globl  pgm_check_handler
pgm_check_handler:
/*
 * First we need to check for a special case:
 * Single stepping an instruction that disables the PER event mask will
 * cause a PER event AFTER the mask has been set. Example: SVC or LPSW.
 * For a single stepped SVC the program check handler gets control after
 * the SVC new PSW has been loaded. But we want to execute the SVC first and
 * then handle the PER event. Therefore we update the SVC old PSW to point
 * to the pgm_check_handler and branch to the SVC handler after we checked
 * if we have to load the kernel stack register.
 * For every other possible cause for PER event without the PER mask set
 * we just ignore the PER event (FIXME: is there anything we have to do
 * for LPSW?).
 */
	SAVE_ALL_BASE __LC_PGM_OLD_PSW,__LC_SAVE_AREA,1
        tm      __LC_PGM_INT_CODE+1,0x80 # check whether we got a per exception
        bnz     BASED(pgm_per)           # got per exception -> special case
	SAVE_ALL __LC_PGM_OLD_PSW,__LC_SAVE_AREA,1
        l       %r3,__LC_PGM_ILC         # load program interruption code
	la	%r8,0x7f
        l       %r7,BASED(.Ljump_table)
	nr	%r8,%r3
        sll     %r8,2
	GET_THREAD_INFO
        l       %r7,0(%r8,%r7)		 # load address of handler routine
        la      %r2,SP_PTREGS(%r15)	 # address of register-save area
	la      %r14,BASED(sysc_return)
	br      %r7			 # branch to interrupt-handler

#
# handle per exception
#
pgm_per:
        tm      __LC_PGM_OLD_PSW,0x40    # test if per event recording is on
        bnz     BASED(pgm_per_std)       # ok, normal per event from user space
# ok its one of the special cases, now we need to find out which one
        clc     __LC_PGM_OLD_PSW(8),__LC_SVC_NEW_PSW
        be      BASED(pgm_svcper)
# no interesting special case, ignore PER event
        lm      %r13,%r15,__LC_SAVE_AREA
	lpsw    0x28

#
# Normal per exception
#
pgm_per_std:
	SAVE_ALL __LC_PGM_OLD_PSW,__LC_SAVE_AREA,1
	GET_THREAD_INFO
	l	%r1,__TI_task(%r9)
	mvc	__THREAD_per+__PER_atmid(2,%r1),__LC_PER_ATMID
	mvc	__THREAD_per+__PER_address(4,%r1),__LC_PER_ADDRESS
	mvc	__THREAD_per+__PER_access_id(1,%r1),__LC_PER_ACCESS_ID
	la	%r4,0x7f
	l	%r3,__LC_PGM_ILC	 # load program interruption code
        nr      %r4,%r3                  # clear per-event-bit and ilc
        be      BASED(pgm_per_only)      # only per or per+check ?
        l       %r1,BASED(.Ljump_table)
        sll     %r4,2
        l       %r1,0(%r4,%r1)		 # load address of handler routine
        la      %r2,SP_PTREGS(%r15)	 # address of register-save area
	basr    %r14,%r1		 # branch to interrupt-handler
pgm_per_only:
	la      %r2,SP_PTREGS(15)	 # address of register-save area
        l       %r1,BASED(.Lhandle_per)  # load adr. of per handler
        la      %r14,BASED(sysc_return)  # load adr. of system return
        br      %r1			 # branch to do_debugger_trap

#
# it was a single stepped SVC that is causing all the trouble
#
pgm_svcper:
	SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	lh	%r7,0x8a	  # get svc number from lowcore
        GET_THREAD_INFO           # load pointer to task_struct to R9
	l	%r1,__TI_task(%r9)
	mvc	__THREAD_per+__PER_atmid(2,%r1),__LC_PER_ATMID
	mvc	__THREAD_per+__PER_address(4,%r1),__LC_PER_ADDRESS
	mvc	__THREAD_per+__PER_access_id(1,%r1),__LC_PER_ACCESS_ID
        stosm   24(%r15),0x03     # reenable interrupts
        sla     %r7,2             # *4 and test for svc 0
	bnz	BASED(pgm_svcstd) # svc number > 0 ?
	# svc 0: system call number in %r1
	cl	%r1,BASED(.Lnr_syscalls)
	bnl	BASED(pgm_svcstd)
	lr	%r7,%r1           # copy svc number to %r7
	sla	%r7,2             # *4
pgm_svcstd:
	tm	__TI_flags+3(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        l       %r8,sys_call_table-system_call(%r7,%r13) # get system call addr.
        bo      BASED(pgm_tracesys)
        basr    %r14,%r8          # call sys_xxxx
        st      %r2,SP_R2(%r15)   # store return value (change R2 on stack)
                                  # ATTENTION: check sys_execve_glue before
                                  # changing anything here !!

pgm_svcret:
	tm	__TI_flags+3(%r9),_TIF_SIGPENDING
	bno	BASED(pgm_svcper_nosig)
	la	%r2,SP_PTREGS(%r15) # load pt_regs
	sr	%r3,%r3		  # clear *oldset
	l	%r1,BASED(.Ldo_signal)
	basr	%r14,%r1	  # call do_signal
	
pgm_svcper_nosig:
        mvi     SP_TRAP+3(%r15),0x28     # set trap indication to pgm check
	la      %r2,SP_PTREGS(15)        # address of register-save area
        l       %r1,BASED(.Lhandle_per)  # load adr. of per handler
        la      %r14,BASED(sysc_return)  # load adr. of system return
        br      %r1                      # branch to do_debugger_trap
#
# call trace before and after sys_call
#
pgm_tracesys:
        l       %r1,BASED(.Ltrace)
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,0
	srl	%r7,2
	st	%r7,SP_R2(%r15)
        basr    %r14,%r1
	clc	SP_R2(4,%r15),BASED(.Lnr_syscalls)
	bnl	BASED(pgm_svc_nogo)
	l	%r7,SP_R2(%r15)   # strace changed the syscall
	sll     %r7,2
	l	%r8,sys_call_table-system_call(%r7,%r13)
pgm_svc_go:
	lm      %r3,%r6,SP_R3(%r15)
	l       %r2,SP_ORIG_R2(%r15)
        basr    %r14,%r8          # call sys_xxx
        st      %r2,SP_R2(%r15)   # store return value
pgm_svc_nogo:
	tm	__TI_flags+3(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        bno     BASED(pgm_svcret)
        l       %r1,BASED(.Ltrace)
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,1
	la	%r14,BASED(pgm_svcret)
        br      %r1

/*
 * IO interrupt handler routine
 */

        .globl io_int_handler
io_int_handler:
	SAVE_ALL_BASE __LC_IO_OLD_PSW,__LC_SAVE_AREA+16,0
        SAVE_ALL __LC_IO_OLD_PSW,__LC_SAVE_AREA+16,0
	stck	__LC_INT_CLOCK
	CHECK_CRITICAL
        GET_THREAD_INFO           # load pointer to task_struct to R9
        l       %r1,BASED(.Ldo_IRQ)        # load address of do_IRQ
        la      %r2,SP_PTREGS(%r15) # address of register-save area
        basr    %r14,%r1          # branch to standard irq handler

io_return:
        tm      SP_PSW+1(%r15),0x01    # returning to user ?
#ifdef CONFIG_PREEMPT
	bno     BASED(io_preempt)      # no -> check for preemptive scheduling
#else
        bno     BASED(io_leave)        # no-> skip resched & signal
#endif
	tm	__TI_flags+3(%r9),_TIF_WORK_INT
	bnz	BASED(io_work)         # there is work to do (signals etc.)
io_leave:
        RESTORE_ALL

#ifdef CONFIG_PREEMPT
io_preempt:
	icm	%r0,15,__TI_precount(%r9)
	bnz     BASED(io_leave)
	l	%r1,SP_R15(%r15)
	s	%r1,BASED(.Lc_spsize)
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
        xc      0(4,%r1),0(%r1)        # clear back chain
	lr	%r15,%r1
io_resume_loop:
	tm	__TI_flags+3(%r9),_TIF_NEED_RESCHED
	bno	BASED(io_leave)
	mvc     __TI_precount(4,%r9),BASED(.Lc_pactive)
        stosm   24(%r15),0x03          # reenable interrupts
        l       %r1,BASED(.Lschedule)
	basr	%r14,%r1	       # call schedule
        stnsm   24(%r15),0xfc          # disable I/O and ext. interrupts
        GET_THREAD_INFO                # load pointer to task_struct to R9
	xc      __TI_precount(4,%r9),__TI_precount(%r9)
	b	BASED(io_resume_loop)
#endif

#
# switch to kernel stack, then check the TIF bits
#
io_work:
	l	%r1,__LC_KERNEL_STACK
	s	%r1,BASED(.Lc_spsize)
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
        xc      0(4,%r1),0(%r1)        # clear back chain
	lr	%r15,%r1
#
# One of the work bits is on. Find out which one.
# Checked are: _TIF_SIGPENDING and _TIF_NEED_RESCHED
#
io_work_loop:
	tm	__TI_flags+3(%r9),_TIF_NEED_RESCHED
	bo	BASED(io_reschedule)
	tm	__TI_flags+3(%r9),_TIF_SIGPENDING
	bo	BASED(io_sigpending)
	b	BASED(io_leave)

#
# _TIF_NEED_RESCHED is set, call schedule
#	
io_reschedule:        
        l       %r1,BASED(.Lschedule)
        stosm   24(%r15),0x03          # reenable interrupts
	basr    %r14,%r1	       # call scheduler
        stnsm   24(%r15),0xfc          # disable I/O and ext. interrupts
        GET_THREAD_INFO                # load pointer to task_struct to R9
	tm	__TI_flags+3(%r9),_TIF_WORK_INT
	bz	BASED(io_leave)        # there is no work to do
	b	BASED(io_work_loop)

#
# _TIF_SIGPENDING is set, call do_signal
#
io_sigpending:     
        stosm   24(%r15),0x03          # reenable interrupts
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        sr      %r3,%r3                # clear *oldset
        l       %r1,BASED(.Ldo_signal)
	basr    %r14,%r1	       # call do_signal
        stnsm   24(%r15),0xfc          # disable I/O and ext. interrupts
	b	BASED(io_leave)        # out of here, do NOT recheck

/*
 * External interrupt handler routine
 */

        .globl  ext_int_handler
ext_int_handler:
	SAVE_ALL_BASE __LC_EXT_OLD_PSW,__LC_SAVE_AREA+16,0
        SAVE_ALL __LC_EXT_OLD_PSW,__LC_SAVE_AREA+16,0
	stck	__LC_INT_CLOCK
	CHECK_CRITICAL
        GET_THREAD_INFO                # load pointer to task_struct to R9
	la	%r2,SP_PTREGS(%r15)    # address of register-save area
	lh	%r3,__LC_EXT_INT_CODE  # get interruption code
	l	%r1,BASED(.Ldo_extint)
	basr	%r14,%r1
	b	BASED(io_return)

/*
 * Machine check handler routines
 */

        .globl mcck_int_handler
mcck_int_handler:
	SAVE_ALL_BASE __LC_MCK_OLD_PSW,__LC_SAVE_AREA+32,0
        SAVE_ALL __LC_MCK_OLD_PSW,__LC_SAVE_AREA+32,0
	l       %r1,BASED(.Ls390_mcck)
	basr    %r14,%r1	  # call machine check handler
mcck_return:
        RESTORE_ALL

#ifdef CONFIG_SMP
/*
 * Restart interruption handler, kick starter for additional CPUs
 */
        .globl restart_int_handler
restart_int_handler:
        l       %r15,__LC_SAVE_AREA+60 # load ksp
        lctl    %c0,%c15,__LC_CREGS_SAVE_AREA # get new ctl regs
        lam     %a0,%a15,__LC_AREGS_SAVE_AREA
        stosm   0(%r15),0x04           # now we can turn dat on
        lm      %r6,%r15,24(%r15)      # load registers from clone
        basr    %r14,0
        l       %r14,restart_addr-.(%r14)
        br      %r14                   # branch to start_secondary
restart_addr:
        .long   start_secondary
#else
/*
 * If we do not run with SMP enabled, let the new CPU crash ...
 */
        .globl restart_int_handler
restart_int_handler:
        basr    %r1,0
restart_base:
        lpsw    restart_crash-restart_base(%r1)
        .align 8
restart_crash:
        .long  0x000a0000,0x00000000
restart_go:
#endif

cleanup_table:
	.long	system_call, sysc_enter, cleanup_sysc_enter
	.long	sysc_return, sysc_leave, cleanup_sysc_return
	.long	sysc_leave, sysc_work_loop, cleanup_sysc_leave
	.long	sysc_work_loop, sysc_reschedule, cleanup_sysc_return
cleanup_table_entries=(.-cleanup_table) / 12

cleanup_critical:
	lhi	%r0,cleanup_table_entries
	la	%r1,BASED(cleanup_table)
	l	%r2,SP_PSW+4(%r15)
	la	%r2,0(%r2)
cleanup_loop:
	cl	%r2,0(%r1)
	bl	BASED(cleanup_cont)
	cl	%r2,4(%r1)
	bl	BASED(cleanup_found)
cleanup_cont:
	la	%r1,12(%r1)
	bct	%r0,BASED(cleanup_loop)
	br	%r14
cleanup_found:
	l	%r1,8(%r1)
	br	%r1

cleanup_sysc_enter:
	CLEANUP_SAVE_ALL_BASE __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	CLEANUP_SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	lh	%r0,0x8a
	st	%r0,SP_R7(%r15)
	la	%r1,BASED(sysc_enter)
	o	%r1,BASED(.Lamode)
	st	%r1,SP_PSW+4(%r15)
	br	%r14

cleanup_sysc_return:
	la	%r1,BASED(sysc_return)
	o	%r1,BASED(.Lamode)
	st	%r1,SP_PSW+4(%r15)
	br	%r14

cleanup_sysc_leave:
	CLEANUP_RESTORE_ALL
	br	%r14

/*
 * Integer constants
 */
               .align 4
.Lc_spsize:    .long  SP_SIZE
.Lc_overhead:  .long  STACK_FRAME_OVERHEAD
.Lc_pactive:   .long  PREEMPT_ACTIVE
.Lnr_syscalls: .long  NR_syscalls
.L0x018:       .long  0x018
.L0x020:       .long  0x020
.L0x028:       .long  0x028
.L0x030:       .long  0x030
.L0x038:       .long  0x038
.Lamode:       .long  0x80000000

/*
 * Symbol constants
 */
.Ls390_mcck:   .long  s390_do_machine_check
.Ldo_IRQ:      .long  do_IRQ
.Ldo_extint:   .long  do_extint
.Ldo_signal:   .long  do_signal
.Ldo_softirq:  .long  do_softirq
.Lhandle_per:  .long  do_debugger_trap
.Ljump_table:  .long  pgm_check_table
.Lschedule:    .long  schedule
.Lclone:       .long  sys_clone
.Lexecve:      .long  sys_execve
.Lfork:        .long  sys_fork
.Lrt_sigreturn:.long  sys_rt_sigreturn
.Lrt_sigsuspend:
               .long  sys_rt_sigsuspend
.Lsigreturn:   .long  sys_sigreturn
.Lsigsuspend:  .long  sys_sigsuspend
.Lsigaltstack: .long  sys_sigaltstack
.Ltrace:       .long  syscall_trace
.Lvfork:       .long  sys_vfork
.Lschedtail:   .long  schedule_tail

.Lcritical_start:
               .long  __critical_start + 0x80000000
.Lcritical_end:
               .long  __critical_end + 0x80000000
.Lcleanup_critical:
               .long  cleanup_critical

#define SYSCALL(esa,esame,emu)	.long esa
	.globl  sys_call_table
sys_call_table:
#include "syscalls.S"
#undef SYSCALL

