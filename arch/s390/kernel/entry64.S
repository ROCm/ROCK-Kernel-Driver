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
SP_R1        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 8
SP_R2        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 16
SP_R3        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 24
SP_R4        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 32
SP_R5        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 40
SP_R6        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 48
SP_R7        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 56
SP_R8        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 64
SP_R9        =  STACK_FRAME_OVERHEAD + __PT_GPRS + 72
SP_R10       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 80
SP_R11       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 88
SP_R12       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 96
SP_R13       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 104
SP_R14       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 112
SP_R15       =  STACK_FRAME_OVERHEAD + __PT_GPRS + 120
SP_ORIG_R2   =  STACK_FRAME_OVERHEAD + __PT_ORIG_GPR2
SP_ILC       =  STACK_FRAME_OVERHEAD + __PT_ILC
SP_TRAP      =  STACK_FRAME_OVERHEAD + __PT_TRAP
SP_SIZE      =  STACK_FRAME_OVERHEAD + __PT_SIZE

_TIF_WORK_SVC = (_TIF_SIGPENDING | _TIF_NEED_RESCHED | _TIF_RESTART_SVC)
_TIF_WORK_INT = (_TIF_SIGPENDING | _TIF_NEED_RESCHED)

/*
 * Register usage in interrupt handlers:
 *    R9  - pointer to current task structure
 *    R13 - pointer to literal pool
 *    R14 - return register for function calls
 *    R15 - kernel stack pointer
 */

        .macro  SAVE_ALL psworg,savearea,sync
        stmg    %r13,%r15,\savearea
	.if	\sync
        tm      \psworg+1,0x01           # test problem state bit
        jz      1f                       # skip stack setup save
	lg      %r15,__LC_KERNEL_STACK   # problem state -> load ksp
	.else
        tm      \psworg+1,0x01           # test problem state bit
	jnz	0f			 # from user -> load kernel stack
	lg	%r14,__LC_ASYNC_STACK	 # are we already on the async. stack ?
	slgr	%r14,%r15
	srag	%r14,%r14,14
	jz	1f
0:	lg      %r15,__LC_ASYNC_STACK    # load async stack
	.endif
1:      aghi    %r15,-SP_SIZE            # make room for registers & psw
	lghi	%r14,\psworg
	slgr	%r13,%r13
	icm	%r14,12,__LC_SVC_ILC
        stmg    %r0,%r12,SP_R0(%r15)     # store gprs 0-13 to kernel stack
        stg     %r2,SP_ORIG_R2(%r15)     # store original content of gpr 2
        mvc     SP_R13(24,%r15),\savearea # move r13, r14 and r15 to stack
        mvc     SP_PSW(16,%r15),\psworg  # move user PSW to stack
	st	%r14,SP_ILC(%r15)
	stg	%r13,0(%r15)
        .endm

	.macro	CLEANUP_SAVE_ALL psworg,savearea,sync
	lg	%r1,SP_PSW+8(%r15)
	cli	1(%r1),0xdf
	jne	2f
	mvc	\savearea(24),SP_R13(%r15)
2:	lg	%r1,\savearea+16
	.if	\sync
	tm	\psworg+1,0x01
	jz	1f
	lg	%r1,__LC_KERNEL_STACK
	.else
	tm	\psworg+1,0x01
	jnz	0f
	lg	%r0,__LC_ASYNC_STACK
	slgr	%r0,%r1
	srag	%r0,%r0,14
	jz	1f
0:	lg	%r1,__LC_ASYNC_STACK
	.endif
1:	aghi	%r1,-SP_SIZE
	stg	%r1,SP_R15(%r15)
	lghi	%r0,\psworg
	xc	SP_R13(8,%r15),SP_R13(%r15)
	icm	%r0,12,__LC_SVC_ILC
	stg	%r0,SP_R14(%r15)
	mvc	SP_R0(104,%r1),SP_R0(%r15)
	mvc	SP_ORIG_R2(8,%r1),SP_R2(%r15)
	mvc	SP_R13(24,%r1),\savearea
	mvc	SP_PSW(16,%r1),\psworg
	st	%r0,SP_ILC(%r1)
	xc	0(8,%r1),0(%r1)
	.endm

        .macro  RESTORE_ALL              # system exit macro
        mvc     __LC_RETURN_PSW(16),SP_PSW(%r15) # move user PSW to lowcore
        ni      __LC_RETURN_PSW+1,0xfd   # clear wait state bit
        lmg     %r0,%r15,SP_R0(%r15)     # load gprs 0-15 of user
        lpswe   __LC_RETURN_PSW          # back to caller
        .endm

	.macro	CLEANUP_RESTORE_ALL
	lg	%r1,SP_PSW+8(%r15)
	cli	0(%r1),0xb2
	jne	0f
	mvc	SP_PSW(16,%r15),__LC_RETURN_PSW
	j	1f
0:	lg	%r1,SP_R15(%r15)
	mvc	SP_PSW(16,%r15),SP_PSW(%r1)
	mvc	SP_R0(128,%r15),SP_R0(%r1)
1:
	.endm

        .macro  GET_THREAD_INFO
	lg	%r9,__LC_THREAD_INFO     # load pointer to thread_info struct
        .endm

	.macro	CHECK_CRITICAL
        tm      SP_PSW+1(%r15),0x01      # test problem state bit
	jnz	0f			 # from user -> not critical
	larl	%r1,.Lcritical_start
	clc	SP_PSW+8(8,%r15),8(%r1)  # compare ip with __critical_end
	jnl	0f
	clc	SP_PSW+8(8,%r15),0(%r1)  # compare ip with __critical_start
	jl	0f
	brasl	%r14,cleanup_critical
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
	tm	__THREAD_per+4(%r3),0xe8 # is the new process using per ?
	jz	__switch_to_noper		# if not we're fine
        stctg   %c9,%c11,48(%r15)       # We are using per stuff
        clc     __THREAD_per(24,%r3),48(%r15)
        je      __switch_to_noper            # we got away without bashing TLB's
        lctlg   %c9,%c11,__THREAD_per(%r3)	# Nope we didn't
__switch_to_noper:
        stmg    %r6,%r15,48(%r15)       # store __switch_to registers of prev task
	stg	%r15,__THREAD_ksp(%r2)	# store kernel stack to prev->tss.ksp
	lg	%r15,__THREAD_ksp(%r3)	# load kernel stack from next->tss.ksp
        lmg     %r6,%r15,48(%r15)       # load __switch_to registers of next task
	stg	%r3,__LC_CURRENT	# __LC_CURRENT = current task struct
	lg	%r3,__THREAD_info(%r3)  # load thread_info from task struct
	stg	%r3,__LC_THREAD_INFO
	aghi	%r3,16384
	stg	%r3,__LC_KERNEL_STACK	# __LC_KERNEL_STACK = new kernel stack
	br	%r14

/*
 * do_softirq calling function. We want to run the softirq functions on the
 * asynchronous interrupt stack.
 */
	.global do_call_softirq
do_call_softirq:
	stnsm	48(%r15),0xfc
	stmg	%r12,%r15,56(%r15)
	lgr	%r12,%r15
	lg	%r0,__LC_ASYNC_STACK
	slgr    %r0,%r15
	srag	%r0,%r0,14
	je	0f
	lg	%r15,__LC_ASYNC_STACK
0:	aghi	%r15,-STACK_FRAME_OVERHEAD
	stg	%r12,0(%r15)		# store back chain
	brasl	%r14,do_softirq
	lmg	%r12,%r15,56(%r12)
	ssm	48(%r15)
	br	%r14

__critical_start:
/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are executed with interrupts enabled.
 */

	.globl  system_call
system_call:
        SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	llgh    %r7,__LC_SVC_INT_CODE # get svc number from lowcore
sysc_enter:
        GET_THREAD_INFO           # load pointer to task_struct to R9
        slag    %r7,%r7,2         # *4 and test for svc 0
	jnz	sysc_do_restart
	# svc 0: system call number in %r1
	lghi	%r0,NR_syscalls
	clr	%r1,%r0
	jnl	sysc_do_restart
	lgfr	%r7,%r1           # clear high word in r1
	slag    %r7,%r7,2         # svc 0: system call number in %r1
sysc_do_restart:
	larl    %r10,sys_call_table
#ifdef CONFIG_S390_SUPPORT
        tm      SP_PSW+3(%r15),0x01  # are we running in 31 bit mode ?
        jo      sysc_noemu
	larl    %r10,sys_call_table_emu  # use 31 bit emulation system calls
sysc_noemu:
#endif
	tm	__TI_flags+7(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        lgf     %r8,0(%r7,%r10)   # load address of system call routine
        jnz     sysc_tracesys
        basr    %r14,%r8          # call sys_xxxx
        stg     %r2,SP_R2(%r15)   # store return value (change R2 on stack)
                                  # ATTENTION: check sys_execve_glue before
                                  # changing anything here !!

sysc_return:
        tm      SP_PSW+1(%r15),0x01    # returning to user ?
        jno     sysc_leave
	tm	__TI_flags+7(%r9),_TIF_WORK_SVC
	jnz	sysc_work         # there is work to do (signals etc.)
sysc_leave:
        RESTORE_ALL

#
# recheck if there is more work to do
#
sysc_work_loop:
        GET_THREAD_INFO           # load pointer to task_struct to R9
	tm	__TI_flags+7(%r9),_TIF_WORK_SVC
	jz	sysc_leave        # there is no work to do
#
# One of the work bits is on. Find out which one.
# Checked are: _TIF_SIGPENDING and _TIF_NEED_RESCHED
#
sysc_work:
	tm	__TI_flags+7(%r9),_TIF_NEED_RESCHED
	jo	sysc_reschedule
	tm	__TI_flags+7(%r9),_TIF_SIGPENDING
	jo	sysc_sigpending
	tm	__TI_flags+7(%r9),_TIF_RESTART_SVC
	jo	sysc_restart
	j	sysc_leave

#
# _TIF_NEED_RESCHED is set, call schedule
#	
sysc_reschedule:        
	larl    %r14,sysc_work_loop
        jg      schedule            # return point is sysc_return

#
# _TIF_SIGPENDING is set, call do_signal
#
sysc_sigpending:     
        la      %r2,SP_PTREGS(%r15) # load pt_regs
        sgr     %r3,%r3           # clear *oldset
	brasl	%r14,do_signal    # call do_signal
	tm	__TI_flags+7(%r9),_TIF_RESTART_SVC
	jo	sysc_restart
	j	sysc_leave        # out of here, do NOT recheck

#
# _TIF_RESTART_SVC is set, set up registers and restart svc
#
sysc_restart:
	ni	__TI_flags+7(%r9),255-_TIF_RESTART_SVC # clear TIF_RESTART_SVC
	lg	%r7,SP_R2(%r15)        # load new svc number
        slag    %r7,%r7,2              # *4
	mvc	SP_R2(8,%r15),SP_ORIG_R2(%r15) # restore first argument
	lmg	%r2,%r6,SP_R2(%r15)    # load svc arguments
	j	sysc_do_restart        # restart svc

__critical_end:

#
# call syscall_trace before and after system call
# special linkage: %r12 contains the return address for trace_svc
#
sysc_tracesys:
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,0
	srl	%r7,2
	stg     %r7,SP_R2(%r15)
        brasl   %r14,syscall_trace
	lghi	%r0,NR_syscalls
	clg	%r0,SP_R2(%r15)
	jnh	sysc_tracenogo
	lg	%r7,SP_R2(%r15)   # strace might have changed the
	sll     %r7,2             #  system call
	lgf	%r8,0(%r7,%r10)
sysc_tracego:
	lmg     %r3,%r6,SP_R3(%r15)
	lg      %r2,SP_ORIG_R2(%r15)
        basr    %r14,%r8            # call sys_xxx
        stg     %r2,SP_R2(%r15)     # store return value
sysc_tracenogo:
	tm	__TI_flags+7(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        jz      sysc_return
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,1
	larl	%r14,sysc_return    # return point is sysc_return
	jg	syscall_trace

#
# a new process exits the kernel with ret_from_fork
#
        .globl  ret_from_fork
ret_from_fork:  
        GET_THREAD_INFO           # load pointer to task_struct to R9
        brasl   %r14,schedule_tail
        stosm   24(%r15),0x03     # reenable interrupts
	j	sysc_return

#
# clone, fork, vfork, exec and sigreturn need glue,
# because they all expect pt_regs as parameter,
# but are called with different parameter.
# return-address is set up above
#
sys_clone_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        jg      sys_clone              # branch to sys_clone

#ifdef CONFIG_S390_SUPPORT
sys32_clone_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        jg      sys32_clone            # branch to sys32_clone
#endif

sys_fork_glue:  
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        jg      sys_fork               # branch to sys_fork

sys_vfork_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        jg      sys_vfork              # branch to sys_vfork

sys_execve_glue:        
        la      %r2,SP_PTREGS(%r15)   # load pt_regs
	lgr     %r12,%r14             # save return address
        brasl   %r14,sys_execve       # call sys_execve
        ltgr    %r2,%r2               # check if execve failed
        bnz     0(%r12)               # it did fail -> store result in gpr2
        b       6(%r12)               # SKIP STG 2,SP_R2(15) in
                                      # system_call/sysc_tracesys
#ifdef CONFIG_S390_SUPPORT
sys32_execve_glue:        
        la      %r2,SP_PTREGS(%r15)   # load pt_regs
	lgr     %r12,%r14             # save return address
        brasl   %r14,sys32_execve     # call sys32_execve
        ltgr    %r2,%r2               # check if execve failed
        bnz     0(%r12)               # it did fail -> store result in gpr2
        b       6(%r12)               # SKIP STG 2,SP_R2(15) in
                                      # system_call/sysc_tracesys
#endif

sys_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        jg      sys_sigreturn         # branch to sys_sigreturn

#ifdef CONFIG_S390_SUPPORT
sys32_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        jg      sys32_sigreturn       # branch to sys32_sigreturn
#endif

sys_rt_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        jg      sys_rt_sigreturn      # branch to sys_sigreturn

#ifdef CONFIG_S390_SUPPORT
sys32_rt_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        jg      sys32_rt_sigreturn    # branch to sys32_sigreturn
#endif

#
# sigsuspend and rt_sigsuspend need pt_regs as an additional
# parameter and they have to skip the store of %r2 into the
# user register %r2 because the return value was set in 
# sigsuspend and rt_sigsuspend already and must not be overwritten!
#

sys_sigsuspend_glue:    
        lgr     %r5,%r4               # move mask back
        lgr     %r4,%r3               # move history1 parameter
        lgr     %r3,%r2               # move history0 parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
	la      %r14,6(%r14)          # skip store of return value
        jg      sys_sigsuspend        # branch to sys_sigsuspend

#ifdef CONFIG_S390_SUPPORT
sys32_sigsuspend_glue:    
	llgfr	%r4,%r4               # unsigned long			
        lgr     %r5,%r4               # move mask back
	lgfr	%r3,%r3               # int			
        lgr     %r4,%r3               # move history1 parameter
	lgfr	%r2,%r2               # int			
        lgr     %r3,%r2               # move history0 parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
	la      %r14,6(%r14)          # skip store of return value
        jg      sys32_sigsuspend      # branch to sys32_sigsuspend
#endif

sys_rt_sigsuspend_glue: 
        lgr     %r4,%r3               # move sigsetsize parameter
        lgr     %r3,%r2               # move unewset parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
	la      %r14,6(%r14)          # skip store of return value
        jg      sys_rt_sigsuspend     # branch to sys_rt_sigsuspend

#ifdef CONFIG_S390_SUPPORT
sys32_rt_sigsuspend_glue: 
	llgfr	%r3,%r3               # size_t			
        lgr     %r4,%r3               # move sigsetsize parameter
	llgtr	%r2,%r2               # sigset_emu31_t *
        lgr     %r3,%r2               # move unewset parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
	la      %r14,6(%r14)          # skip store of return value
        jg      sys32_rt_sigsuspend   # branch to sys32_rt_sigsuspend
#endif

sys_sigaltstack_glue:
        la      %r4,SP_PTREGS(%r15)   # load pt_regs as parameter
        jg      sys_sigaltstack       # branch to sys_sigreturn

#ifdef CONFIG_S390_SUPPORT
sys32_sigaltstack_glue:
        la      %r4,SP_PTREGS(%r15)   # load pt_regs as parameter
        jg      sys32_sigaltstack_wrapper # branch to sys_sigreturn
#endif

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
        tm      __LC_PGM_INT_CODE+1,0x80 # check whether we got a per exception
        jnz     pgm_per                  # got per exception -> special case
	SAVE_ALL __LC_PGM_OLD_PSW,__LC_SAVE_AREA,1
	lgf     %r3,__LC_PGM_ILC	 # load program interruption code
	lghi	%r8,0x7f
	ngr	%r8,%r3
        sll     %r8,3
	GET_THREAD_INFO
        larl    %r1,pgm_check_table
        lg      %r1,0(%r8,%r1)		 # load address of handler routine
        la      %r2,SP_PTREGS(%r15)	 # address of register-save area
	larl	%r14,sysc_return
        br      %r1			 # branch to interrupt-handler

#
# handle per exception
#
pgm_per:
        tm      __LC_PGM_OLD_PSW,0x40    # test if per event recording is on
        jnz     pgm_per_std              # ok, normal per event from user space
# ok its one of the special cases, now we need to find out which one
        clc     __LC_PGM_OLD_PSW(16),__LC_SVC_NEW_PSW
        je      pgm_svcper
# no interesting special case, ignore PER event
	lpswe   __LC_PGM_OLD_PSW

#
# Normal per exception
#
pgm_per_std:
	SAVE_ALL __LC_PGM_OLD_PSW,__LC_SAVE_AREA,1
	GET_THREAD_INFO
	lg	%r1,__TI_task(%r9)
	mvc	__THREAD_per+__PER_atmid(2,%r1),__LC_PER_ATMID
	mvc	__THREAD_per+__PER_address(8,%r1),__LC_PER_ADDRESS
	mvc	__THREAD_per+__PER_access_id(1,%r1),__LC_PER_ACCESS_ID
	lghi    %r4,0x7f
	lgf     %r3,__LC_PGM_ILC	 # load program interruption code
        nr      %r4,%r3			 # clear per-event-bit and ilc
        je      pgm_per_only		 # only per of per+check ?
        sll     %r4,3
        larl    %r1,pgm_check_table
        lg      %r1,0(%r4,%r1)		 # load address of handler routine
        la      %r2,SP_PTREGS(%r15)	 # address of register-save area
        basr    %r14,%r1		 # branch to interrupt-handler
pgm_per_only:
        la      %r2,SP_PTREGS(15)	 # address of register-save area
        larl    %r14,sysc_return	 # load adr. of system return
        jg      do_debugger_trap

#
# it was a single stepped SVC that is causing all the trouble
#
pgm_svcper:
	SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	llgh    %r7,__LC_SVC_INT_CODE # get svc number from lowcore
        GET_THREAD_INFO           # load pointer to task_struct to R9
	lg	%r1,__TI_task(%r9)
	mvc	__THREAD_per+__PER_atmid(2,%r1),__LC_PER_ATMID
	mvc	__THREAD_per+__PER_address(8,%r1),__LC_PER_ADDRESS
	mvc	__THREAD_per+__PER_access_id(1,%r1),__LC_PER_ACCESS_ID
	stosm   48(%r15),0x03     # reenable interrupts
	slag	%r7,%r7,2         # *4 and test for svc 0
	jnz	pgm_svcstd
	# svc 0: system call number in %r1
	lghi	%r0,NR_syscalls
	clr	%r1,%r0
	slag	%r7,%r1,2
pgm_svcstd:
	larl    %r10,sys_call_table
#ifdef CONFIG_S390_SUPPORT
        tm      SP_PSW+3(%r15),0x01  # are we running in 31 bit mode ?
        jo      pgm_svcper_noemu
	larl    %r10,sys_call_table_emu # use 31 bit emulation system calls
pgm_svcper_noemu:
#endif
	tm	__TI_flags+7(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        lgf     %r8,0(%r7,%r10)   # load address of system call routine
        jnz     pgm_tracesys
        basr    %r14,%r8          # call sys_xxxx
        stg     %r2,SP_R2(%r15)   # store return value (change R2 on stack)
                                  # ATTENTION: check sys_execve_glue before
                                  # changing anything here !!

pgm_svcret:
	tm	__TI_flags+7(%r9),_TIF_SIGPENDING
	jno	pgm_svcper_nosig
        la      %r2,SP_PTREGS(%r15) # load pt_regs
        sgr     %r3,%r3             # clear *oldset
	brasl	%r14,do_signal
	
pgm_svcper_nosig:
	lhi     %r0,__LC_PGM_OLD_PSW     # set trap indication back to pgm_chk
	st      %r0,SP_TRAP(%r15)
        la      %r2,SP_PTREGS(15) # address of register-save area
        larl    %r14,sysc_return  # load adr. of system return
        jg      do_debugger_trap
#
# call trace before and after sys_call
#
pgm_tracesys:
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,0
	srlg	%r7,%r7,2
	stg	%r7,SP_R2(%r15)
        brasl   %r14,syscall_trace
	lghi	%r0,NR_syscalls
	clg	%r0,SP_R2(%r15)
	jnh	pgm_svc_nogo
	lg      %r7,SP_R2(%r15)
	sllg    %r7,%r7,2           # strace wants to change the syscall
	lgf	%r8,0(%r7,%r10)
pgm_svc_go:
	lmg     %r3,%r6,SP_R3(%r15)
	lg      %r2,SP_ORIG_R2(%r15)
        basr    %r14,%r8            # call sys_xxx
        stg     %r2,SP_R2(%r15)     # store return value
pgm_svc_nogo:
	tm	__TI_flags+7(%r9),(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT)
        jz      pgm_svcret
	la	%r2,SP_PTREGS(%r15)    # load pt_regs
	la	%r3,1
	larl	%r14,pgm_svcret     # return point is sysc_return
	jg	syscall_trace

/*
 * IO interrupt handler routine
 */
        .globl io_int_handler
io_int_handler:
        SAVE_ALL __LC_IO_OLD_PSW,__LC_SAVE_AREA+32,0
	stck	__LC_INT_CLOCK
	CHECK_CRITICAL
        GET_THREAD_INFO                # load pointer to task_struct to R9
        la      %r2,SP_PTREGS(%r15)    # address of register-save area
	brasl   %r14,do_IRQ            # call standard irq handler

io_return:
        tm      SP_PSW+1(%r15),0x01    # returning to user ?
#ifdef CONFIG_PREEMPT
	jno     io_preempt             # no -> check for preemptive scheduling
#else
        jno     io_leave               # no-> skip resched & signal
#endif
	tm	__TI_flags+7(%r9),_TIF_WORK_INT
	jnz	io_work                # there is work to do (signals etc.)
io_leave:
        RESTORE_ALL

#ifdef CONFIG_PREEMPT
io_preempt:
	icm	%r0,15,__TI_precount(%r9)	
	jnz     io_leave
	# switch to kernel stack
	lg	%r1,SP_R15(%r15)
	aghi	%r1,-SP_SIZE
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
        xc      0(8,%r1),0(%r1)        # clear back chain
	lgr	%r15,%r1
io_resume_loop:
	tm	__TI_flags+7(%r9),_TIF_NEED_RESCHED
	jno	io_leave
	larl    %r1,.Lc_pactive
	mvc     __TI_precount(4,%r9),0(%r1)
        stosm   48(%r15),0x03          # reenable interrupts
	brasl   %r14,schedule          # call schedule
        stnsm   48(%r15),0xfc          # disable I/O and ext. interrupts
        GET_THREAD_INFO                # load pointer to task_struct to R9
	xc      __TI_precount(4,%r9),__TI_precount(%r9)
	j	io_resume_loop
#endif

#
# switch to kernel stack, then check TIF bits
#
io_work:
	lg	%r1,__LC_KERNEL_STACK
	aghi	%r1,-SP_SIZE
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
        xc      0(8,%r1),0(%r1)        # clear back chain
	lgr	%r15,%r1
#
# One of the work bits is on. Find out which one.
# Checked are: _TIF_SIGPENDING and _TIF_NEED_RESCHED
#
io_work_loop:
	tm	__TI_flags+7(%r9),_TIF_NEED_RESCHED
	jo	io_reschedule
	tm	__TI_flags+7(%r9),_TIF_SIGPENDING
	jo	io_sigpending
	j	io_leave

#
# _TIF_NEED_RESCHED is set, call schedule
#	
io_reschedule:        
        stosm   48(%r15),0x03       # reenable interrupts
        brasl   %r14,schedule       # call scheduler
        stnsm   48(%r15),0xfc       # disable I/O and ext. interrupts
        GET_THREAD_INFO             # load pointer to task_struct to R9
	tm	__TI_flags+7(%r9),_TIF_WORK_INT
	jz	io_leave               # there is no work to do
	j	io_work_loop

#
# _TIF_SIGPENDING is set, call do_signal
#
io_sigpending:     
        stosm   48(%r15),0x03       # reenable interrupts
        la      %r2,SP_PTREGS(%r15) # load pt_regs
        slgr    %r3,%r3             # clear *oldset
	brasl	%r14,do_signal      # call do_signal
        stnsm   48(%r15),0xfc       # disable I/O and ext. interrupts
	j	sysc_leave          # out of here, do NOT recheck

/*
 * External interrupt handler routine
 */
        .globl  ext_int_handler
ext_int_handler:
        SAVE_ALL __LC_EXT_OLD_PSW,__LC_SAVE_AREA+32,0
	CHECK_CRITICAL
        GET_THREAD_INFO                # load pointer to task_struct to R9
	stck	__LC_INT_CLOCK
	la	%r2,SP_PTREGS(%r15)    # address of register-save area
	llgh	%r3,__LC_EXT_INT_CODE  # get interruption code
	brasl   %r14,do_extint
	j	io_return

/*
 * Machine check handler routines
 */
        .globl mcck_int_handler
mcck_int_handler:
        SAVE_ALL __LC_MCK_OLD_PSW,__LC_SAVE_AREA+64,0
	brasl   %r14,s390_do_machine_check
mcck_return:
        RESTORE_ALL

#ifdef CONFIG_SMP
/*
 * Restart interruption handler, kick starter for additional CPUs
 */
        .globl restart_int_handler
restart_int_handler:
        lg      %r15,__LC_SAVE_AREA+120 # load ksp
        lghi    %r10,__LC_CREGS_SAVE_AREA
        lctlg   %c0,%c15,0(%r10) # get new ctl regs
        lghi    %r10,__LC_AREGS_SAVE_AREA
        lam     %a0,%a15,0(%r10)
        stosm   0(%r15),0x04           # now we can turn dat on
        lmg     %r6,%r15,48(%r15)      # load registers from clone
	jg      start_secondary
#else
/*
 * If we do not run with SMP enabled, let the new CPU crash ...
 */
        .globl restart_int_handler
restart_int_handler:
        basr    %r1,0
restart_base:
        lpswe   restart_crash-restart_base(%r1)
        .align 8
restart_crash:
        .long  0x000a0000,0x00000000,0x00000000,0x00000000
restart_go:
#endif

cleanup_table:
	.quad	system_call, sysc_enter, cleanup_sysc_enter
	.quad	sysc_return, sysc_leave, cleanup_sysc_return
	.quad	sysc_leave, sysc_work_loop, cleanup_sysc_leave
	.quad	sysc_work_loop, sysc_reschedule, cleanup_sysc_return
cleanup_table_entries=(.-cleanup_table) / 24

cleanup_critical:
	lghi	%r0,cleanup_table_entries
	larl	%r1,cleanup_table
	lg	%r2,SP_PSW+8(%r15)
cleanup_loop:
	clg	%r2,0(%r1)
	jl	cleanup_cont
	clg	%r2,8(%r1)
	jl	cleanup_found
cleanup_cont:
	la	%r1,24(%r1)
	brct	%r0,cleanup_loop
	br	%r14
cleanup_found:
	lg	%r1,16(%r1)
	br	%r1

cleanup_sysc_enter:
	CLEANUP_SAVE_ALL __LC_SVC_OLD_PSW,__LC_SAVE_AREA,1
	llgh	%r0,0x8a
	stg	%r0,SP_R7(%r15)
	larl	%r1,sysc_enter
	stg	%r1,SP_PSW+8(%r15)
	br	%r14

cleanup_sysc_return:
	larl	%r1,sysc_return
	stg	%r1,SP_PSW+8(%r15)
	br	%r14

cleanup_sysc_leave:
	CLEANUP_RESTORE_ALL
	br	%r14

/*
 * Integer constants
 */
               .align 4
.Lconst:
.Lc_pactive:   .long  PREEMPT_ACTIVE
.Lcritical_start:
               .quad  __critical_start
.Lcritical_end:
               .quad  __critical_end

#define SYSCALL(esa,esame,emu)	.long esame
	.globl  sys_call_table
sys_call_table:
#include "syscalls.S"
#undef SYSCALL

#ifdef CONFIG_S390_SUPPORT

#define SYSCALL(esa,esame,emu)	.long emu
	.globl  sys_call_table_emu
sys_call_table_emu:
#include "syscalls.S"
#undef SYSCALL
#endif
