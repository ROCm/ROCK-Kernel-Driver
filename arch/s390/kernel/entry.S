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
#include <asm/lowcore.h>
#include <asm/errno.h>
#define ASSEMBLY
#include <asm/smp.h>
#include <asm/s390-regs-common.h>


/*
 * stack layout for the system_call stack entry
 * Martin please don't modify these back to hard coded values 
 * You know how bad I'm at mental arithmetic DJB & it gives 
 * me grief when I modify the pt_regs
 */
SP_PTREGS    =  STACK_FRAME_OVERHEAD 
SP_PSW       =  SP_PTREGS
SP_R0        =  (SP_PSW+PSW_MASK_SIZE+PSW_ADDR_SIZE)
SP_R1        =  (SP_R0+GPR_SIZE)
SP_R2        =  (SP_R1+GPR_SIZE)
SP_R3        =  (SP_R2+GPR_SIZE)
SP_R4        =  (SP_R3+GPR_SIZE)
SP_R5        =  (SP_R4+GPR_SIZE)
SP_R6        =  (SP_R5+GPR_SIZE)
SP_R7        =  (SP_R6+GPR_SIZE)
SP_R8        =  (SP_R7+GPR_SIZE)
SP_R9        =  (SP_R8+GPR_SIZE)
SP_RA        =  (SP_R9+GPR_SIZE)
SP_RB        =  (SP_RA+GPR_SIZE)
SP_RC        =  (SP_RB+GPR_SIZE)
SP_RD        =  (SP_RC+GPR_SIZE)
SP_RE        =  (SP_RD+GPR_SIZE)
SP_RF        =  (SP_RE+GPR_SIZE)
SP_AREGS     =  (SP_RF+GPR_SIZE)
SP_ORIG_R2   =  (SP_AREGS+(NUM_ACRS*ACR_SIZE))
SP_TRAP      =  (SP_ORIG_R2+GPR_SIZE)
#if CONFIG_REMOTE_DEBUG
SP_CRREGS    =  (SP_TRAP+4)
/* fpu registers are saved & restored by the gdb stub itself */
SP_FPC       =  (SP_CRREGS+(NUM_CRS*CR_SIZE))
SP_FPRS      =  (SP_FPC+FPC_SIZE+FPC_PAD_SIZE)
/* SP_PGM_OLD_ILC etc are not part of pt_regs & they are not 
 defined in ptrace.h but space is needed for this too */
SP_PGM_OLD_ILC= (SP_FPRS+(NUM_FPRS*FPR_SIZE))
#else
SP_PGM_OLD_ILC= (SP_TRAP+4)
#endif
SP_SVC_STEP   = (SP_PGM_OLD_ILC+4) 
SP_SIZE      =  (SP_SVC_STEP+4)
/*
 * these defines are offsets into the thread_struct
 */
_TSS_PTREGS  = 0
_TSS_FPRS    = (_TSS_PTREGS+8)
_TSS_AR2     = (_TSS_FPRS+136)
_TSS_AR4     = (_TSS_AR2+4)
_TSS_KSP     = (_TSS_AR4+4)
_TSS_USERSEG = (_TSS_KSP+4)
_TSS_ERROR   = (_TSS_USERSEG+4)
_TSS_PROT    = (_TSS_ERROR+4)
_TSS_TRAP    = (_TSS_PROT+4)
_TSS_MM      = (_TSS_TRAP+4)
_TSS_PER     = (_TSS_MM+8)

/*
 * these are offsets into the task-struct.
 */
state        =  0
flags        =  4
sigpending   =  8
need_resched = 24
processor    = 60

/* PSW related defines */
disable      = 0xFC
enable       = 0x03
daton        = 0x04


#if 0
/* some code left lying around in case we need a
 *	printk for debugging purposes
 */
  sysc_printk:        .long  printk
  sysc_msg:	      .string "<2>r15 %X\n"
	.align 4
		
#	basr    %r13,0
	l       %r0,SP_PSW+4(%r15)
	sll     %r0,1
	chi     %r0,0
	jnz     sysc_dn
	l       %r9,sysc_printk-sysc_lit(%r13)
	la      %r2,sysc_msg-sysc_lit(%r13)
	lr      %r3,%r15
	basr    %r14,%r9
sysc_dn:
#endif

/*
 * Register usage in interrupt handlers:
 *    R9  - pointer to current task structure
 *    R13 - pointer to literal pool
 *    R14 - return register for function calls
 *    R15 - kernel stack pointer
 */

#define SAVE_ALL(psworg) \
	st      %r15,__LC_SAVE_AREA   ; \
        tm      psworg+1,0x01         ; /* test problem state bit */ \
	jz	0f		      ; /* skip stack setup save */ \
	l	%r15,__LC_KERNEL_STACK ; /* problem state -> load ksp */ \
0:	ahi     %r15,-SP_SIZE         ; /* make room for registers & psw */ \
        srl     %r15,3                ; \
        sll     %r15,3                ; /* align stack pointer to 8 */ \
        stm     %r0,%r14,SP_R0(%r15)  ; /* store gprs 0-14 to kernel stack */ \
        st      %r2,SP_ORIG_R2(%r15)  ; /* store original content of gpr 2 */ \
        mvc     SP_RF(4,%r15),__LC_SAVE_AREA   ; /* move R15 to stack */ \
        stam    %a0,%a15,SP_AREGS(%r15) ; /* store access registers to kst. */ \
        mvc     SP_PSW(8,%r15),psworg ; /* move user PSW to stack */ \
        lhi     %r0,psworg            ; /* store trap indication */ \
        st      %r0,SP_TRAP(%r15)     ; \
        xc      0(4,%r15),0(%r15)     ; /* clear back chain */ \
        tm      psworg+1,0x01         ; /* kmod.c .wishes the set_fs & gs */ \
        jz      1f                    ; /* to work across syscalls */ \
        slr     %r0,%r0               ; \
        sar     %a2,%r0               ; /* set ac.reg. 2 to primary space */ \
        lhi     %r0,1                 ; \
        sar     %a4,%r0               ; /* set access reg. 4 to home space */ \
1:

#define RESTORE_ALL \
        mvc     __LC_RETURN_PSW(8,0),SP_PSW(%r15) ; /* move user PSW to lowcore */ \
        lam     %a0,%a15,SP_AREGS(%r15)   ; /* load the access registers */ \
        lm      %r0,%r15,SP_R0(%r15)      ; /* load gprs 0-15 of user */ \
        ni      __LC_RETURN_PSW+1(0),0xfd ; /* clear wait state bit */ \
        lpsw    __LC_RETURN_PSW             /* back to caller */

#define GET_CURRENT /* load pointer to task_struct to R9 */ \
        lhi     %r9,-8192             ; \
        nr      %r9,15


/*
 * Scheduler resume function, called by switch_to
 *  grp2 = (thread_struct *) prev->tss
 *  grp3 = (thread_struct *) next->tss
 * Returns:
 *  gpr2 = prev
 */
        .globl  resume
resume:
        l       %r4,_TSS_PTREGS(%r3)
        tm      SP_PSW-SP_PTREGS(%r4),0x40 # is the new process using per ?
        jz      RES_DN1                 # if not we're fine
        stctl   %r9,%r11,24(%r15)       # We are using per stuff
        clc     _TSS_PER(12,%r3),24(%r15)
        je      RES_DN1                 # we got away without bashing TLB's
        lctl    %c9,%c11,_TSS_PER(%r3)  # Nope we didn't
RES_DN1:
        stm     %r6,%r15,24(%r15)       # store resume registers of prev task
        st      %r15,_TSS_KSP(%r2) # store kernel stack ptr to prev->tss.ksp
        lhi     %r0,-8192
        nr      %r0,%r15
        l       %r15,_TSS_KSP(%r3) # load kernel stack ptr from next->tss.ksp
        lhi     %r1,8191
        or      %r1,%r15
        ahi     %r1,1
        st      %r1,__LC_KERNEL_STACK   # __LC_KERNEL_STACK = new kernel stack
        stam    %a2,%a2,_TSS_AR2(%r2)   # store kernel access reg. 2
        stam    %a4,%a4,_TSS_AR4(%r2)   # store kernel access reg. 4
        lam     %a2,%a2,_TSS_AR2(%r3)   # load kernel access reg. 2
        lam     %a4,%a4,_TSS_AR4(%r3)   # load kernel access reg. 4
        lr      %r2,%r0                 # return task_struct of last task
        lm      %r6,%r15,24(%r15)       # load resume registers of next task
        br      %r14

/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are executed with interrupts enabled.
 */

sysc_lit:
  sysc_do_signal:     .long  do_signal
  sysc_do_softirq:    .long  do_softirq
  sysc_schedule:      .long  schedule
  sysc_trace:         .long  syscall_trace
#ifdef CONFIG_SMP
  sysc_schedtail:     .long  schedule_tail
#endif
  sysc_clone:         .long  sys_clone
  sysc_fork:          .long  sys_fork
  sysc_vfork:         .long  sys_vfork
  sysc_sigreturn:     .long  sys_sigreturn
  sysc_rt_sigreturn:  .long  sys_rt_sigreturn
  sysc_execve:        .long  sys_execve
  sysc_sigsuspend:    .long  sys_sigsuspend
  sysc_rt_sigsuspend: .long  sys_rt_sigsuspend

	.globl  system_call
system_call:
        SAVE_ALL(0x20)
	XC      SP_SVC_STEP(4,%r15),SP_SVC_STEP(%r15)
pgm_system_call:	
        basr    %r13,0
        ahi     %r13,sysc_lit-.   # setup base pointer R13 to sysc_lit
        slr     %r8,%r8           # gpr 8 is call save (-> tracesys)
        ic      %r8,0x8B          # get svc number from lowcore
        stosm   24(%r15),0x03     # reenable interrupts
        GET_CURRENT               # load pointer to task_struct to R9
        sll     %r8,2
        l       %r8,sys_call_table-sysc_lit(8,%r13) # get address of system call
        tm      flags+3(%r9),0x20 # PF_TRACESYS
        jnz     sysc_tracesys
        basr    %r14,%r8          # call sys_xxxx
        st      %r2,SP_R2(%r15)   # store return value (change R2 on stack)
                                  # ATTENTION: check sys_execve_glue before
                                  # changing anything here !!

sysc_return:
        GET_CURRENT               # load pointer to task_struct to R9
        tm      SP_PSW+1(%r15),0x01 # returning to user ?
        jno     sysc_leave        # no-> skip bottom half, resched & signal
#
# check, if bottom-half has to be done
#
        l       %r0,__LC_IRQ_STAT     # get softirq_active
        n       %r0,__LC_IRQ_STAT+4   # and it with softirq_mask
        jnz     sysc_handle_bottom_half
#
# check, if reschedule is needed
#
sysc_return_bh:	
        icm     %r0,15,need_resched(%r9) # get need_resched from task_struct
        jnz     sysc_reschedule
        icm     %r0,15,sigpending(%r9)   # get sigpending from task_struct
        jnz     sysc_signal_return
sysc_leave:
	icm     %r0,15,SP_SVC_STEP(%r15)   # get sigpending from task_struct
        jnz     pgm_svcret
	stnsm   24(%r15),disable  # disable I/O and ext. interrupts
        RESTORE_ALL

#
# call do_signal before return
#
sysc_signal_return:     
        la      %r2,SP_PTREGS(%r15) # load pt_regs
        sr      %r3,%r3           # clear *oldset
        l       %r1,sysc_do_signal-sysc_lit(%r13)
	la      %r14,sysc_leave-sysc_lit(%r13)
        br      %r1               # return point is sysc_leave

#
# call trace before and after sys_call
#
sysc_tracesys:
        l       %r1,sysc_trace-sysc_lit(%r13)
	lhi     %r2,-ENOSYS
	st      %r2,SP_R2(%r15)   # give sysc_trace an -ENOSYS retval
        basr    %r14,%r1
	lm      %r3,%r6,SP_R3(%r15)
	l       %r2,SP_ORIG_R2(%r15)
        basr    %r14,%r8          # call sys_xxx
        st      %r2,SP_R2(%r15)   # store return value
        l       %r1,sysc_trace-sysc_lit(%r13)
	la      %r14,sysc_return-sysc_lit(%r13)
        br      %r1               # return point is sysc_return


#
# call do_softirq and return from syscall, if interrupt-level
# is zero
#
sysc_handle_bottom_half:        
        l       %r1,sysc_do_softirq-sysc_lit(%r13)
	la      %r14,sysc_return_bh-sysc_lit(%r13)
        br      %r1               # call do_softirq

#
# call schedule with sysc_return as return-address
#
sysc_reschedule:        
        l       %r1,sysc_schedule-sysc_lit(%r13)
	la      %r14,sysc_return-sysc_lit(%r13)
        br      %r1               # call scheduler, return to sysc_return

#
# a new process exits the kernel with ret_from_fork
#
        .globl  ret_from_fork
ret_from_fork:  
        basr    %r13,0
        ahi     %r13,sysc_lit-.   # setup base pointer R13 to $SYSCDAT
        GET_CURRENT               # load pointer to task_struct to R9
        stosm   24(%r15),0x03     # reenable interrupts
        sr      %r0,%r0           # child returns 0
        st      %r0,SP_R2(%r15)   # store return value (change R2 on stack)
#ifdef CONFIG_SMP
        l       %r1,sysc_schedtail-sysc_lit(%r13)
	la      %r14,sysc_return-sysc_lit(%r13)
        br      %r1               # call schedule_tail, return to sysc_return
#else
        j       sysc_return
#endif

#
# clone, fork, vfork, exec and sigreturn need glue,
# because they all expect pt_regs as parameter,
# but are called with different parameter.
# return-address is set up above
#
sys_clone_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,sysc_clone-sysc_lit(%r13)
        br      %r1                   # branch to sys_clone

sys_fork_glue:  
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,sysc_fork-sysc_lit(%r13)
        br      %r1                   # branch to sys_fork

sys_vfork_glue: 
        la      %r2,SP_PTREGS(%r15)    # load pt_regs
        l       %r1,sysc_vfork-sysc_lit(%r13)
        br      %r1                   # branch to sys_vfork

sys_execve_glue:        
        la      %r2,SP_PTREGS(%r15)   # load pt_regs
        l       %r1,sysc_execve-sysc_lit(%r13)
	lr      %r12,%r14             # save return address
        basr    %r14,%r1              # call sys_execve
        ltr     %r2,%r2               # check if execve failed
        bnz     0(%r12)               # it did fail -> store result in gpr2
        b       4(%r12)               # SKIP ST 2,SP_R2(15) after BASR 14,8
                                      # in system_call/sysc_tracesys

sys_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        l       %r1,sysc_sigreturn-sysc_lit(%r13)
        br      %r1                   # branch to sys_sigreturn

sys_rt_sigreturn_glue:     
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as parameter
        l       %r1,sysc_rt_sigreturn-sysc_lit(%r13)
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
        l       %r1,sysc_sigsuspend-sysc_lit(%r13)
	la      %r14,4(%r14)          # skip store of return value
        br      %r1                   # branch to sys_sigsuspend

sys_rt_sigsuspend_glue: 
        lr      %r4,%r3               # move sigsetsize parameter
        lr      %r3,%r2               # move unewset parameter
        la      %r2,SP_PTREGS(%r15)   # load pt_regs as first parameter
        l       %r1,sysc_rt_sigsuspend-sysc_lit(%r13)
	la      %r14,4(%r14)          # skip store of return value
        br      %r1                   # branch to sys_rt_sigsuspend

	.globl  sys_call_table	
sys_call_table:
        .long  sys_ni_syscall            /* 0 */
        .long  sys_exit
        .long  sys_fork_glue
        .long  sys_read
        .long  sys_write
        .long  sys_open                  /* 5 */
        .long  sys_close
        .long  sys_ni_syscall           /* old waitpid syscall holder */
        .long  sys_creat
        .long  sys_link
        .long  sys_unlink                /* 10 */
        .long  sys_execve_glue
        .long  sys_chdir
        .long  sys_time
        .long  sys_mknod
        .long  sys_chmod                /* 15 */
        .long  sys_lchown16
        .long  sys_ni_syscall           /* old break syscall holder */
        .long  sys_ni_syscall           /* old stat syscall holder */
        .long  sys_lseek
        .long  sys_getpid               /* 20 */
        .long  sys_mount
        .long  sys_oldumount
        .long  sys_setuid16
        .long  sys_getuid16
        .long  sys_stime                /* 25 */
        .long  sys_ptrace
        .long  sys_alarm
        .long  sys_ni_syscall           /* old fstat syscall holder */
        .long  sys_pause
        .long  sys_utime                /* 30 */
        .long  sys_ni_syscall           /* old stty syscall holder */
        .long  sys_ni_syscall           /* old gtty syscall holder */
        .long  sys_access
        .long  sys_nice
        .long  sys_ni_syscall           /* 35 */  /* old ftime syscall holder */
        .long  sys_sync
        .long  sys_kill
        .long  sys_rename
        .long  sys_mkdir
        .long  sys_rmdir                /* 40 */
        .long  sys_dup
        .long  sys_pipe
        .long  sys_times
        .long  sys_ni_syscall           /* old prof syscall holder */
        .long  sys_brk                  /* 45 */
        .long  sys_setgid16
        .long  sys_getgid16
        .long  sys_signal
        .long  sys_geteuid16
        .long  sys_getegid16            /* 50 */
        .long  sys_acct
        .long  sys_umount
        .long  sys_ni_syscall           /* old lock syscall holder */
        .long  sys_ioctl
        .long  sys_fcntl                /* 55 */
        .long  sys_ni_syscall           /* old mpx syscall holder */
        .long  sys_setpgid
        .long  sys_ni_syscall           /* old ulimit syscall holder */
        .long  sys_ni_syscall           /* old uname syscall holder */
        .long  sys_umask                /* 60 */
        .long  sys_chroot
        .long  sys_ustat
        .long  sys_dup2
        .long  sys_getppid
        .long  sys_getpgrp              /* 65 */
        .long  sys_setsid
        .long  sys_sigaction
        .long  sys_ni_syscall           /* old sgetmask syscall holder */
        .long  sys_ni_syscall           /* old ssetmask syscall holder */
        .long  sys_setreuid16           /* 70 */
        .long  sys_setregid16
        .long  sys_sigsuspend_glue
        .long  sys_sigpending
        .long  sys_sethostname
        .long  sys_setrlimit            /* 75 */
        .long  sys_getrlimit
        .long  sys_getrusage
        .long  sys_gettimeofday
        .long  sys_settimeofday
        .long  sys_getgroups16          /* 80 */
        .long  sys_setgroups16
        .long  sys_ni_syscall           /* old select syscall holder */
        .long  sys_symlink
        .long  sys_ni_syscall           /* old lstat syscall holder */
        .long  sys_readlink             /* 85 */
        .long  sys_uselib
        .long  sys_swapon
        .long  sys_reboot
        .long  sys_ni_syscall           /* old readdir syscall holder */
        .long  old_mmap                 /* 90 */
        .long  sys_munmap
        .long  sys_truncate
        .long  sys_ftruncate
        .long  sys_fchmod
        .long  sys_fchown16              /* 95 */
        .long  sys_getpriority
        .long  sys_setpriority
        .long  sys_ni_syscall            /* old profil syscall holder */
        .long  sys_statfs
        .long  sys_fstatfs               /* 100 */
        .long  sys_ioperm
        .long  sys_socketcall
        .long  sys_syslog
        .long  sys_setitimer
        .long  sys_getitimer             /* 105 */
        .long  sys_newstat
        .long  sys_newlstat
        .long  sys_newfstat
        .long  sys_ni_syscall            /* old uname syscall holder */
        .long  sys_ni_syscall            /* 110 */ /* iopl for i386 */
        .long  sys_vhangup
        .long  sys_ni_syscall            /* old "idle" system call */
        .long  sys_ni_syscall            /* vm86old for i386 */
        .long  sys_wait4
        .long  sys_swapoff               /* 115 */
        .long  sys_sysinfo
        .long  sys_ipc
        .long  sys_fsync
        .long  sys_sigreturn_glue
        .long  sys_clone_glue            /* 120 */
        .long  sys_setdomainname
        .long  sys_newuname
        .long  sys_ni_syscall            /* modify_ldt for i386 */
        .long  sys_adjtimex
        .long  sys_mprotect              /* 125 */
        .long  sys_sigprocmask
        .long  sys_create_module
        .long  sys_init_module
        .long  sys_delete_module
        .long  sys_get_kernel_syms       /* 130 */
        .long  sys_quotactl
        .long  sys_getpgid
        .long  sys_fchdir
        .long  sys_bdflush
        .long  sys_sysfs                 /* 135 */
        .long  sys_personality
        .long  sys_ni_syscall            /* for afs_syscall */
        .long  sys_setfsuid16
        .long  sys_setfsgid16
        .long  sys_llseek                /* 140 */
        .long  sys_getdents
        .long  sys_select
        .long  sys_flock
        .long  sys_msync
        .long  sys_readv                 /* 145 */
        .long  sys_writev
        .long  sys_getsid
        .long  sys_fdatasync
        .long  sys_sysctl
        .long  sys_mlock                 /* 150 */
        .long  sys_munlock
        .long  sys_mlockall
        .long  sys_munlockall
        .long  sys_sched_setparam
        .long  sys_sched_getparam        /* 155 */
        .long  sys_sched_setscheduler
        .long  sys_sched_getscheduler
        .long  sys_sched_yield
        .long  sys_sched_get_priority_max
        .long  sys_sched_get_priority_min  /* 160 */
        .long  sys_sched_rr_get_interval
        .long  sys_nanosleep
        .long  sys_mremap
        .long  sys_setresuid16
        .long  sys_getresuid16           /* 165 */
        .long  sys_ni_syscall            /* for vm86 */
        .long  sys_query_module
        .long  sys_poll
        .long  sys_nfsservctl
        .long  sys_setresgid16           /* 170 */
        .long  sys_getresgid16
        .long  sys_prctl
        .long  sys_rt_sigreturn_glue
        .long  sys_rt_sigaction
        .long  sys_rt_sigprocmask        /* 175 */
        .long  sys_rt_sigpending
        .long  sys_rt_sigtimedwait
        .long  sys_rt_sigqueueinfo
        .long  sys_rt_sigsuspend_glue
        .long  sys_pread                 /* 180 */
        .long  sys_pwrite
        .long  sys_chown16
        .long  sys_getcwd
        .long  sys_capget
        .long  sys_capset                /* 185 */
        .long  sys_sigaltstack
        .long  sys_sendfile
        .long  sys_ni_syscall            /* streams1 */
        .long  sys_ni_syscall            /* streams2 */
        .long  sys_vfork_glue            /* 190 */
        .long  sys_getrlimit
	.long  sys_ni_syscall  /* FIXME: problem with sys_mmap2: 6 parms */
        .long  sys_truncate64
        .long  sys_ftruncate64
        .long  sys_stat64                /* 195 */
        .long  sys_lstat64
        .long  sys_fstat64
	.long  sys_lchown
	.long  sys_getuid
	.long  sys_getgid		 /* 200 */
	.long  sys_geteuid
	.long  sys_getegid
	.long  sys_setreuid
	.long  sys_setregid
	.long  sys_getgroups             /* 205 */
	.long  sys_setgroups
	.long  sys_fchown
	.long  sys_setresuid
	.long  sys_getresuid
	.long  sys_setresgid             /* 210 */
	.long  sys_getresgid
	.long  sys_chown
	.long  sys_setuid
	.long  sys_setgid
	.long  sys_setfsuid              /* 215 */
	.long  sys_setfsgid
        .long  sys_pivot_root
        .long  sys_mincore
        .long  sys_madvise
	.long  sys_getdents64		 /* 220 */
	.rept  255-220
	.long  sys_ni_syscall
	.endr

/*
 * Program check handler routine
 */

pgm_lit:
  pgm_handle_per:  .long  handle_per_exception
  pgm_jump_table:  .long  pgm_check_table
  pgm_sysc_ret:    .long  sysc_return
  pgm_sysc_lit:    .long  sysc_lit
  pgm_do_signal:   .long  do_signal

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
        jz      pgm_sv                   # skip if not
        tm      __LC_PGM_OLD_PSW,0x40    # test if per event recording is on
        jnz     pgm_sv                   # skip if it is
# ok its one of the special cases, now we need to find out which one
        clc     __LC_PGM_OLD_PSW(8),__LC_SVC_NEW_PSW
        je      pgm_svcper
# no interesting special case, ignore PER event
        lpsw    0x28
# it was a single stepped SVC that is causing all the trouble
pgm_svcper:
        SAVE_ALL(0x20)
        mvi     SP_SVC_STEP(%r15),1 # make SP_SVC_STEP nonzero
        mvc     SP_PGM_OLD_ILC(4,%r15),__LC_PGM_ILC # save program check information
        j       pgm_system_call              # now do the svc
pgm_svcret:
        lh      %r7,SP_PGM_OLD_ILC(%r15)     # get ilc from stack
        lhi     %r0,0x28
        st      %r0,SP_TRAP(%r15) # set new trap indicator
        xc      SP_SVC_STEP(4,%r15),SP_SVC_STEP(%r15)
        basr    %r13,0
        ahi     %r13,pgm_lit-.    # setup base pointer
        j       pgm_no_sv
pgm_sv:
        SAVE_ALL(0x28)
        XC      SP_SVC_STEP(4,%r15),SP_SVC_STEP(%r15)
        basr    %r13,0
        ahi     %r13,pgm_lit-.    # setup base pointer R13 to $PGMDAT
        lh      %r7,__LC_PGM_ILC  # load instruction length
pgm_no_sv:
        lh      %r8,__LC_PGM_INT_CODE  # N.B. saved int code used later KEEP it
        stosm   24(%r15),0x03     # reenable interrupts
        lr      %r3,%r8
        lhi     %r0,0x7f
        nr      %r3,%r0           # clear per-event-bit
        je      pgm_dn            # none of Martins exceptions occured bypass
        l       %r9,pgm_jump_table-pgm_lit(%r13)
        sll     %r3,2
        l       %r9,0(%r3,%r9)    # load address of handler routine
        la      %r2,SP_PTREGS(%r15) # address of register-save area
        srl     %r3,2
        chi     %r3,0x4           # protection-exception ?
        jne     pgm_go            # if not,
        l       %r5,SP_PSW+4(15)  # load psw addr
        sr      %r5,%r7           # substract ilc from psw
        st      %r5,SP_PSW+4(15)  # store corrected psw addr
pgm_go: basr    %r14,%r9          # branch to interrupt-handler
pgm_dn: lhi     %r0,0x80
        nr      %r8,%r0           # check for per exception
        je      pgm_return
        la      %r2,SP_PTREGS(15) # address of register-save area
        l       %r9,pgm_handle_per-pgm_lit(%r13) # load adr. of per handler
        l       %r14,pgm_sysc_ret-pgm_lit(%r13)  # load adr. of system return
        l       %r13,pgm_sysc_lit-pgm_lit(%r13)
        br      %r9               # branch to handle_per_exception
#
# the backend code is the same as for sys-call
#
pgm_return:
        l       %r14,pgm_sysc_ret-pgm_lit(%r13)
        l       %r13,pgm_sysc_lit-pgm_lit(%r13)
        br      %r14

/*
 * IO interrupt handler routine
 */

io_lit:
  io_do_IRQ:        .long do_IRQ
  io_schedule:	    .long schedule
  io_do_signal:     .long do_signal
  io_do_softirq:    .long  do_softirq

        .globl io_int_handler
io_int_handler:
        SAVE_ALL(0x38)
        basr    %r13,0
        ahi     %r13,io_lit-.     # setup base pointer R13 to $IODAT
        la      %r2,SP_PTREGS(%r15) # address of register-save area
        sr      %r3,%r3
        icm     %r3,%r3,__LC_SUBCHANNEL_NR # load subchannel nr & extend to int
        l       %r4,__LC_IO_INT_PARM       # load interuption parm
        l       %r9,io_do_IRQ-io_lit(%r13) # load address of do_IRQ
        basr    %r14,%r9          # branch to standard irq handler

io_return:
        GET_CURRENT               # load pointer to task_struct to R9
        tm      SP_PSW+1(%r15),0x01 # returning to user ?
        jz      io_leave          # no-> skip resched & signal
        stosm   24(%r15),0x03     # reenable interrupts
#
# check, if bottom-half has to be done
#
        l       %r0,__LC_IRQ_STAT     # get softirq_active
        n       %r0,__LC_IRQ_STAT+4   # and it with softirq_mask
        jnz     io_handle_bottom_half
io_return_bh:	
#
# check, if reschedule is needed
#
        icm     %r0,15,need_resched(%r9) # get need_resched from task_struct
        jnz     io_reschedule
        icm     %r0,15,sigpending(%r9)   # get sigpending from task_struct
        jnz     io_signal_return
io_leave:
        stnsm   24(%r15),disable  # disable I/O and ext. interrupts
        RESTORE_ALL

#
# call do_softirq and return from syscall, if interrupt-level
# is zero
#
io_handle_bottom_half:        
        l       %r1,io_do_softirq-io_lit(%r13)
	la      %r14,io_return_bh-io_lit(%r13)
        br      %r1               # call do_softirq

#
# call schedule with io_return as return-address
#
io_reschedule:        
        l       %r1,io_schedule-io_lit(%r13)
	la      %r14,io_return-io_lit(%r13)
        br      %r1               # call scheduler, return to io_return

#
# call do_signal before return
#
io_signal_return:     
        la      %r2,SP_PTREGS(%r15) # load pt_regs
        sr      %r3,%r3           # clear *oldset
        l       %r1,io_do_signal-io_lit(%r13)
	la      %r14,io_leave-io_lit(%r13)
        br      %r1               # return point is io_leave

/*
 * External interrupt handler routine
 */

ext_lit:
  ext_timer_int:     .long  do_timer_interrupt
#ifdef CONFIG_SMP
  ext_call_int:      .long  do_ext_call_interrupt
#endif
#ifdef CONFIG_HWC
  ext_hwc_int:      .long  do_hwc_interrupt
#endif
#ifdef CONFIG_MDISK
  ext_mdisk_int:     .long  do_mdisk_interrupt
#endif
#ifdef CONFIG_IUCV
  ext_iucv_int:      .long  do_iucv_interrupt
#endif
  ext_io_lit:	     .long  io_lit
  ext_io_return:     .long  io_return

        .globl  ext_int_handler
ext_int_handler:
        SAVE_ALL(0x18)
        basr    %r13,0
        ahi     %r13,ext_lit-.    # setup base pointer R13 to $EXTDAT
        la      %r2,SP_PTREGS(%r15)    # address of register-save area
        lh      %r3,__LC_EXT_INT_CODE  # error code
#ifdef CONFIG_SMP
	chi     %r3,0x1202        # EXTERNAL_CALL
	jne     ext_no_extcall
	l       %r9,ext_call_int-ext_lit(%r13) # load ext_call_interrupt
	l       %r14,ext_io_return-ext_lit(%r13)
        l       %r13,ext_io_lit-ext_lit(%r13)
	br      %r9               # branch to ext call handler
ext_no_extcall:
#endif
        chi     %r3,0x1004        # CPU_TIMER
        jne     ext_no_timer
        l       %r9,ext_timer_int-ext_lit(%r13) # load timer_interrupt
	l       %r14,ext_io_return-ext_lit(%r13)
        l       %r13,ext_io_lit-ext_lit(%r13)
	br      %r9               # branch to ext call handler
ext_no_timer:   
#ifdef CONFIG_HWC
        chi     %r3,0x2401        # HWC interrupt
        jne     ext_no_hwc
        l       %r9,ext_hwc_int-ext_lit(%r13) # load addr. of hwc routine
	l       %r14,ext_io_return-ext_lit(%r13)
        l       %r13,ext_io_lit-ext_lit(%r13)
	br      %r9               # branch to ext call handler
ext_no_hwc:    
#endif
#ifdef CONFIG_MDISK
        chi     %r3,0x2603        # diag 250 (VM) interrupt
        jne     ext_no_mdisk
        l       %r9,ext_mdisk_int-ext_lit(%r13)
	l       %r14,ext_io_return-ext_lit(%r13)
        l       %r13,ext_io_lit-ext_lit(%r13)
	br      %r9               # branch to ext call handler
ext_no_mdisk:   
#endif
#ifdef CONFIG_IUCV
        chi     %r3,0x4000        # diag 250 (VM) interrupt
        jne     ext_no_iucv
        l       %r9,ext_iucv_int-ext_lit(%r13)
	l       %r14,ext_io_return-ext_lit(%r13)
        l       %r13,ext_io_lit-ext_lit(%r13)
	br      %r9               # branch to ext call handler
ext_no_iucv:    
#endif

	l       %r14,ext_io_return-ext_lit(%r13)
        l       %r13,ext_io_lit-ext_lit(%r13)
	br      %r14              # use backend code of io_int_handler

/*
 * Machine check handler routines
 */
mcck_lit:
  mcck_crw_pending: .long  do_crw_pending


        .globl mcck_int_handler
mcck_int_handler:
        SAVE_ALL(0x30)
        basr    %r13,0
        ahi     %r13,mcck_lit-.   # setup base pointer R13 to $MCCKDAT
	tm      __LC_MCCK_CODE+1,0x40
	jno     mcck_no_crw
	l       %r1,mcck_crw_pending-mcck_lit(%r13)
	basr    %r14,%r1	  # call do_crw_pending
mcck_no_crw:
mcck_return:
        RESTORE_ALL

#ifdef CONFIG_SMP
/*
 * Restart interruption handler, kick starter for additional CPUs
 */
        .globl restart_int_handler
restart_int_handler:
        l       %r15,__LC_KERNEL_STACK # load ksp
        lctl    %c0,%c15,__LC_CREGS_SAVE_AREA # get new ctl regs
        lam     %a0,%a15,__LC_AREGS_SAVE_AREA
        stosm   0(%r15),daton          # now we can turn dat on
        lm      %r6,%r15,24(%r15)      # load registers from clone
        bras    %r14,restart_go
        .long   start_secondary
restart_go:
        l       %r14,0(%r14)
        br      %r14                   # branch to start_secondary
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

