/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 2000, 2001 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 *
 * Hairy, the userspace application uses a different argument passing
 * convention than the kernel, so we have to translate things from o32
 * to ABI64 calling convention.  64-bit syscalls are also processed
 * here for now.
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/unistd.h>
#include <asm/sysmips.h>

	.align  5
NESTED(handle_sys, PT_SIZE, sp)
	.set	noat
	SAVE_SOME
	STI
	.set	at
	ld	t1, PT_EPC(sp)		# skip syscall on return

	subu	t0, v0, __NR_O32_Linux	# check syscall number
	sltiu	t0, t0, __NR_O32_Linux_syscalls + 1
	daddiu	t1, 4			# skip to next instruction
	beqz	t0, not_o32_scall
	sd	t1, PT_EPC(sp)
#if 0
 SAVE_ALL
 move a1, v0
 PRINT("Scall %ld\n")
 RESTORE_ALL
#endif

	sll	a0, a0, 0
	sll	a1, a1, 0
	sll	a2, a2, 0
	sll	a3, a3, 0

	/* XXX Put both in one cacheline, should save a bit. */
	dsll	t0, v0, 3		# offset into table
	ld	t2, (sys_call_table - (__NR_O32_Linux * 8))(t0)
	lbu	t3, (sys_narg_table - __NR_O32_Linux)(v0)

	subu	t0, t3, 5		# 5 or more arguments?
	sd	a3, PT_R26(sp)		# save a3 for syscall restarting
	bgez	t0, stackargs

stack_done:
	LONG_L	t0, TI_FLAGS($28)
					# syscall tracing enabled?
	bnez	t0, trace_a_syscall

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	negu	v0			# error
	sd	v0, PT_R0(sp)		# flag for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

FEXPORT(o32_syscall_exit)
	local_irq_disable		# make need_resched and
					# signals dont change between
					# sampling and return
	LONG_L	a2, TI_FLAGS($28)
	bnez	a2, o32_syscall_exit_work

	RESTORE_SOME
	RESTORE_SP_AND_RET

o32_syscall_exit_work:
	SAVE_STATIC
	j	syscall_exit_work

/* ------------------------------------------------------------------------ */

trace_a_syscall:
	SAVE_STATIC
	sd	a4, PT_R8(sp)
	sd	a5, PT_R9(sp)
	sd	a6, PT_R10(sp)
	sd	a7, PT_R11(sp)

	sd	t2,PT_R1(sp)
	jal	do_syscall_trace
	ld	t2,PT_R1(sp)

	ld	a0, PT_R4(sp)		# Restore argument registers
	ld	a1, PT_R5(sp)
	ld	a2, PT_R6(sp)
	ld	a3, PT_R7(sp)
	ld	a4, PT_R8(sp)
	ld	a5, PT_R9(sp)

	jalr	t2

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	negu	v0			# error
	sd	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

	j	syscall_exit

/* ------------------------------------------------------------------------ */

	/*
	 * More than four arguments.  Try to deal with it by copying the
	 * stack arguments from the user stack to the kernel stack.
	 * This Sucks (TM).
	 */
stackargs:
	ld	t0, PT_R29(sp)		# get old user stack pointer
	subu	t3, 4
	sll	t1, t3, 2		# stack valid?

	addu	t1, t0			# end address
	or	t0, t1
	bltz	t0, bad_stack		# -> sp is bad

	ld	t0, PT_R29(sp)		# get old user stack pointer
	PTR_LA	t1, 3f			# copy 1 to 2 arguments
	sll	t3, t3, 2
	subu	t1, t3
	jr	t1

	/* Ok, copy the args from the luser stack to the kernel stack */
	.set	push
	.set	noreorder
	.set	nomacro
1:	lw	a5, 20(t0)		# argument #6 from usp
2:	lw	a4, 16(t0)		# argument #5 from usp
3:	.set	pop

	j	stack_done		# go back

	.section __ex_table,"a"
	PTR	1b, bad_stack
	PTR	2b, bad_stack
	.previous

	/*
	 * The stackpointer for a call with more than 4 arguments is bad.
	 */
bad_stack:
	negu	v0			# error
	sd	v0, PT_R0(sp)
	sd	v0, PT_R2(sp)
	li	t0, 1			# set error flag
	sd	t0, PT_R7(sp)
	j	o32_syscall_exit

not_o32_scall:
	/*
	 * This is not an o32 compatibility syscall, pass it on
	 * to the 64-bit syscall handlers.
	 */
#ifdef CONFIG_MIPS32_N32
	j	handle_sysn32
#else
	j	handle_sys64
#endif

illegal_syscall:
	/* This also isn't a 64-bit syscall, throw an error.  */
	li	v0, ENOSYS		# error
	sd	v0, PT_R2(sp)
	li	t0, 1			# set error flag
	sd	t0, PT_R7(sp)
	j	o32_syscall_exit
	END(handle_sys)

LEAF(sys32_syscall)
	ld	t0, PT_R29(sp)		# user sp

	sltu	v0, a0, __NR_O32_Linux + __NR_O32_Linux_syscalls + 1
	beqz	v0, enosys

	dsll	v0, a0, 3
	dla	v1, sys32_syscall
	ld	t2, (sys_call_table - (__NR_O32_Linux * 8))(v0)
	lbu	t3, (sys_narg_table - __NR_O32_Linux)(a0)

	li	v0, -EINVAL
	beq	t2, v1, out		# do not recurse

	beqz	t2, enosys		# null function pointer?

	andi	v0, t0, 0x3		# unaligned stack pointer?
	bnez	v0, sigsegv

	daddiu	v0, t0, 16		# v0 = usp + 16
	daddu	t1, v0, 12		# 3 32-bit arguments
	ld	v1, TI_ADDR_LIMIT($28)
	or	v0, v0, t1
	and	v1, v1, v0
	bnez	v1, efault

	move	a0, a1			# shift argument registers
	move	a1, a2
	move	a2, a3

1:	lw	a3, 16(t0)
2:	lw	t3, 20(t0)
3:	lw	t1, 24(t0)

	.section __ex_table,"a"
	PTR	1b, efault
	PTR	2b, efault
	PTR	3b, efault
	.previous

	sw	t3, 16(sp)		# put into new stackframe
	sw	t1, 20(sp)

	bnez	t1, 1f			# zero arguments?
	daddu	a0, sp, 32		# then pass sp in a0
1:

	sw	t3, 16(sp)
	sw	v1, 20(sp)
	jr	t2
	/* Unreached */

enosys:	li	v0, -ENOSYS
	b	out

sigsegv:
	li	a0, _SIGSEGV
	move	a1, $28
	jal	force_sig
	/* Fall through */

efault:	li	v0, -EFAULT

out:	jr	ra
	END(sys32_syscall)

	.macro	syscalltable
	sys	sys32_syscall	0			/* 4000 */
	sys	sys_exit	1
	sys	sys_fork	0
	sys	sys_read	3
	sys	sys_write	3
	sys	sys_open	3			/* 4005 */
	sys	sys_close	1
	sys	sys_waitpid	3
	sys	sys_creat	2
	sys	sys_link	2
	sys	sys_unlink	1			/* 4010 */
	sys	sys32_execve	0
	sys	sys_chdir	1
	sys	sys_time	1
	sys	sys_mknod	3
	sys	sys_chmod	2			/* 4015 */
	sys	sys_lchown	3
	sys	sys_ni_syscall	0
	sys	sys_ni_syscall	0			/* was sys_stat */
	sys	sys_lseek	3
	sys	sys_getpid	0			/* 4020 */
	sys	sys_mount	5
	sys	sys_oldumount	1
	sys	sys_setuid	1
	sys	sys_getuid	0
	sys	sys_stime	1			/* 4025 */
	sys	sys32_ptrace	4
	sys	sys_alarm	1
	sys	sys_ni_syscall	0			/* was sys_fstat */
	sys	sys_pause	0
	sys	compat_sys_utime	2			/* 4030 */
	sys	sys_ni_syscall	0
	sys	sys_ni_syscall	0
	sys	sys_access	2
	sys	sys_nice	1
	sys	sys_ni_syscall	0			/* 4035 */
	sys	sys_sync	0
	sys	sys_kill	2
	sys	sys_rename	2
	sys	sys_mkdir	2
	sys	sys_rmdir	1			/* 4040 */
	sys	sys_dup		1
	sys	sys_pipe	0
	sys	compat_sys_times	1
	sys	sys_ni_syscall	0
	sys	sys_brk		1			/* 4045 */
	sys	sys_setgid	1
	sys	sys_getgid	0
	sys	sys_ni_syscall	0	/* was signal	2 */
	sys	sys_geteuid	0
	sys	sys_getegid	0			/* 4050 */
	sys	sys_acct	0
	sys	sys_umount	2
	sys	sys_ni_syscall	0
	sys	compat_sys_ioctl	3
	sys	compat_sys_fcntl	3		/* 4055 */
	sys	sys_ni_syscall	2
	sys	sys_setpgid	2
	sys	sys_ni_syscall, 0
	sys	sys_olduname	1
	sys	sys_umask	1			/* 4060 */
	sys	sys_chroot	1
	sys	sys_ustat	2
	sys	sys_dup2	2
	sys	sys_getppid	0
	sys	sys_getpgrp	0			/* 4065 */
	sys	sys_setsid	0
	sys	sys32_sigaction	3
	sys	sys_sgetmask	0
	sys	sys_ssetmask	1
	sys	sys_setreuid	2			/* 4070 */
	sys	sys_setregid	2
	sys	sys32_sigsuspend	0
	sys	compat_sys_sigpending	1
	sys	sys_sethostname	2
	sys	compat_sys_setrlimit	2		/* 4075 */
	sys	compat_sys_getrlimit	2
	sys	compat_sys_getrusage	2
	sys	sys32_gettimeofday 2
	sys	sys32_settimeofday 2
	sys	sys_getgroups	2			/* 4080 */
	sys	sys_setgroups	2
	sys	sys_ni_syscall	0			/* old_select */
	sys	sys_symlink	2
	sys	sys_ni_syscall	0			/* was sys_lstat */
	sys	sys_readlink	3			/* 4085 */
	sys	sys_uselib	1
	sys	sys_swapon	2
	sys	sys_reboot	3
	sys	sys32_readdir	3
	sys	old_mmap	6			/* 4090 */
	sys	sys_munmap	2
	sys	sys_truncate	2
	sys	sys_ftruncate	2
	sys	sys_fchmod	2
	sys	sys_fchown	3			/* 4095 */
	sys	sys_getpriority	2
	sys	sys_setpriority	3
	sys	sys_ni_syscall	0
	sys	compat_sys_statfs	2
	sys	compat_sys_fstatfs	2		/* 4100 */
	sys	sys_ni_syscall		0		/* sys_ioperm */
	sys	sys_socketcall		2
	sys	sys_syslog		3
	sys	compat_sys_setitimer	3
	sys	compat_sys_getitimer	2	/* 4105 */
	sys	compat_sys_newstat	2
	sys	compat_sys_newlstat	2
	sys	compat_sys_newfstat	2
	sys	sys_uname		1
	sys	sys_ni_syscall		0	/* sys_ioperm  *//* 4110 */
	sys	sys_vhangup		0
	sys	sys_ni_syscall		0	/* was sys_idle	 */
	sys	sys_ni_syscall		0	/* sys_vm86 */
	sys	sys32_wait4		4
	sys	sys_swapoff		1	/* 4115 */
	sys	sys32_sysinfo		1
	sys	sys32_ipc		6
	sys	sys_fsync	1
	sys	sys32_sigreturn	0
	sys	sys_clone	0			/* 4120 */
	sys	sys_setdomainname 2
	sys	sys32_newuname	1
	sys	sys_ni_syscall	0	/* sys_modify_ldt */
	sys	sys32_adjtimex	1
	sys	sys_mprotect	3			/* 4125 */
	sys	compat_sys_sigprocmask	3
	sys	sys_ni_syscall	0			/* was creat_module */
	sys	sys_init_module	5
	sys	sys_delete_module 1
	sys	sys_ni_syscall	0		/* 4130, get_kernel_syms */
	sys	sys_quotactl	0
	sys	sys_getpgid	1
	sys	sys_fchdir	1
	sys	sys_bdflush	2
	sys	sys_sysfs	3			/* 4135 */
	sys	sys32_personality	1
	sys	sys_ni_syscall	0 /* for afs_syscall */
	sys	sys_setfsuid	1
	sys	sys_setfsgid	1
	sys	sys32_llseek	5			/* 4140 */
	sys	sys32_getdents	3
	sys	sys32_select	5
	sys	sys_flock	2
	sys	sys_msync	3
	sys	sys32_readv	3			/* 4145 */
	sys	sys32_writev	3
	sys	sys_cacheflush	3
	sys	sys_cachectl	3
	sys	sys_sysmips	4
	sys	sys_ni_syscall	0			/* 4150 */
	sys	sys_getsid	1
	sys	sys_fdatasync	0
	sys	sys32_sysctl	1
	sys	sys_mlock	2
	sys	sys_munlock	2			/* 4155 */
	sys	sys_mlockall	1
	sys	sys_munlockall	0
	sys	sys_sched_setparam 2
	sys	sys_sched_getparam 2
	sys	sys_sched_setscheduler 3		/* 4160 */
	sys	sys_sched_getscheduler 1
	sys	sys_sched_yield	0
	sys	sys_sched_get_priority_max 1
	sys	sys_sched_get_priority_min 1
	sys	sys32_sched_rr_get_interval 2		/* 4165 */
	sys	compat_sys_nanosleep	2
	sys	sys_mremap	4
	sys	sys_accept	3
	sys	sys_bind	3
	sys	sys_connect	3			/* 4170 */
	sys	sys_getpeername	3
	sys	sys_getsockname	3
	sys	sys_getsockopt	5
	sys	sys_listen	2
	sys	sys_recv	4			/* 4175 */
	sys	sys_recvfrom	6
	sys	compat_sys_recvmsg	3
	sys	sys_send	4
	sys	compat_sys_sendmsg	3
	sys	sys_sendto	6			/* 4180 */
	sys	compat_sys_setsockopt	5
	sys	sys_shutdown	2
	sys	sys_socket	3
	sys	sys_socketpair	4
	sys	sys_setresuid	3			/* 4185 */
	sys	sys_getresuid	3
	sys	sys_ni_syscall	0			/* was query_module */
	sys	sys_poll	3
	sys	sys_nfsservctl	3
	sys	sys_setresgid	3			/* 4190 */
	sys	sys_getresgid	3
	sys	sys_prctl	5
	sys	sys32_rt_sigreturn 0
	sys	sys32_rt_sigaction 4
	sys	sys32_rt_sigprocmask 4			/* 4195 */
	sys	sys32_rt_sigpending 2
	sys	sys32_rt_sigtimedwait 4
	sys	sys32_rt_sigqueueinfo 3
	sys	sys32_rt_sigsuspend 0
	sys	sys32_pread	6			/* 4200 */
	sys	sys32_pwrite	6
	sys	sys_chown	3
	sys	sys_getcwd	2
	sys	sys_capget	2
	sys	sys_capset	2			/* 4205 */
	sys	sys32_sigaltstack	0
	sys	sys32_sendfile	4
	sys	sys_ni_syscall	0
	sys	sys_ni_syscall	0
	sys	sys32_mmap2	6			/* 4210 */
	sys	sys32_truncate64	4
	sys	sys32_ftruncate64	4
	sys	sys_newstat	2
	sys	sys_newlstat	2
	sys	sys_newfstat	2			/* 4215 */
	sys	sys_pivot_root	2
	sys	sys_mincore	3
	sys	sys_madvise	3
	sys	sys_getdents64	3
	sys	compat_sys_fcntl64	3		/* 4220 */
	sys	sys_ni_syscall	0
	sys	sys_gettid	0
	sys	sys32_readahead	5
	sys	sys_setxattr	5
	sys	sys_lsetxattr	5			/* 4225 */
	sys	sys_fsetxattr	5
	sys	sys_getxattr	4
	sys	sys_lgetxattr	4
	sys	sys_fgetxattr	4
	sys	sys_listxattr	3			/* 4230 */
	sys	sys_llistxattr	3
	sys	sys_flistxattr	3
	sys	sys_removexattr	2
	sys	sys_lremovexattr	2
	sys	sys_fremovexattr	2		/* 4235 */
	sys	sys_tkill		2
	sys	sys_sendfile64		5
	sys	compat_sys_futex	5
	sys	sys32_sched_setaffinity	3
	sys	sys32_sched_getaffinity	3		/* 4240 */
	sys	sys_io_setup		2
	sys	sys_io_destroy		1
	sys	sys_io_getevents	5
	sys	sys_io_submit		3
	sys	sys_io_cancel		3		/* 4245 */
	sys	sys_exit_group		1
	sys	sys_lookup_dcookie	3
	sys	sys_epoll_create	1
	sys	sys_epoll_ctl		4
	sys	sys_epoll_wait		3		/* 4250 */
	sys	sys_remap_file_pages	5
	sys	sys_set_tid_address	1
	sys	sys_restart_syscall	0
	sys	sys_fadvise64		6
	sys	sys_statfs64		3		/* 4255 */
	sys	sys_fstatfs64		2
	sys	sys_timer_create	3
	sys	sys_timer_settime	4
	sys	sys_timer_gettime	2
	sys	sys_timer_getoverrun	1		/* 4260 */
	sys	sys_timer_delete	1
	sys	sys_clock_settime	2
	sys	sys_clock_gettime	2
	sys	sys_clock_getres	2
	sys	sys_clock_nanosleep	4		/* 4265 */
	sys	sys_tgkill		3
	sys	compat_sys_utimes	2

	.endm

	.macro	sys function, nargs
	PTR	\function
	.endm

	.align	3
sys_call_table:
	syscalltable

	.macro	sys function, nargs
	.byte	\nargs
	.endm

sys_narg_table:
	syscalltable
