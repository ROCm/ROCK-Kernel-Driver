/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/config.h>
#include <asm/asm.h>
#include <linux/errno.h>
#include <asm/current.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/unistd.h>

/* This duplicates the definition from <linux/sched.h> */
#define PT_TRACESYS	0x00000002	/* tracing system calls */

/* This duplicates the definition from <asm/signal.h> */
#define SIGILL		4		/* Illegal instruction (ANSI).  */

#ifndef CONFIG_MIPS32_COMPAT
#define handle_sys64 handle_sys
#endif

	.align  5
NESTED(handle_sys64, PT_SIZE, sp)
/* When 32-bit compatibility is configured scall_o32.S already did this.  */
#ifndef CONFIG_MIPS32_COMPAT
	.set	noat
	SAVE_SOME
	STI
	.set	at
#endif
	ld	t1, PT_EPC(sp)		# skip syscall on return

	subu	t0, v0, __NR_Linux	# check syscall number
	sltiu	t0, t0,	__NR_Linux_syscalls + 1
	daddiu	t1, 4			# skip to next instruction
	beqz	t0, illegal_syscall
	sd	t1, PT_EPC(sp)

	dsll	t0, v0, 3		# offset into table
	ld	t2, (sys_call_table - (__NR_Linux * 8))(t0) # syscall routine

	sd	a3, PT_R26(sp)		# save a3 for syscall restarting

	ld	t0, TASK_PTRACE($28)	# syscall tracing enabled?
	andi	t0, PT_TRACESYS
	bnez	t0, trace_a_syscall

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	negu	v0			# error
	sd	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

ret_from_sys_call:
	mfc0	t0, CP0_STATUS
	xori	t0, t0, 1
	ori	t0, t0, 1
	mtc0	t0, CP0_STATUS

	ld	t2, TASK_NEED_RESCHED($28)
	bnez	t2, reschedule
	lw	v0, TASK_SIGPENDING($28)
	bnez	v0, signal_return

restore_all:
	RESTORE_SOME
	RESTORE_SP
	eret

/* Put this behind restore_all for the sake of the branch prediction.  */
signal_return:
	.type	signal_return, @function

	mfc0	t0, CP0_STATUS
	ori	t0, t0, 1
	mtc0	t0, CP0_STATUS

	move	a0, zero
	move	a1, sp
	jal	do_signal
	b	restore_all

reschedule:
	SAVE_STATIC
	jal	schedule
	b	ret_from_sys_call

/* ------------------------------------------------------------------------ */

trace_a_syscall:
	SAVE_STATIC
	sd	t2,PT_R1(sp)
	jal	syscall_trace
	ld	t2,PT_R1(sp)

	ld	a0, PT_R4(sp)		# Restore argument registers
	ld	a1, PT_R5(sp)
	ld	a2, PT_R6(sp)
	ld	a3, PT_R7(sp)
	jalr	t2

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	negu	v0			# error
	sd	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

	jal	syscall_trace
	j	ret_from_sys_call

illegal_syscall:
	/* This also isn't a 64-bit syscall, throw an error.  */
	li	v0, ENOSYS			# error
	sd	v0, PT_R2(sp)
	li	t0, 1				# set error flag
	sd	t0, PT_R7(sp)
	j	ret_from_sys_call
	END(handle_sys64)

sys_call_table:
	PTR	sys_syscall				/* 5000 */
	PTR	sys_exit
	PTR	sys_fork
	PTR	sys_read
	PTR	sys_write
	PTR	sys_open				/* 5005 */
	PTR	sys_close
	PTR	sys_waitpid
	PTR	sys_creat
	PTR	sys_link
	PTR	sys_unlink				/* 5010 */
	PTR	sys_execve
	PTR	sys_chdir
	PTR	sys_time
	PTR	sys_mknod
	PTR	sys_chmod				/* 5015 */
	PTR	sys_lchown
	PTR	sys_ni_syscall
	PTR	sys_stat
	PTR	sys_lseek
	PTR	sys_getpid				/* 5020 */
	PTR	sys_mount
	PTR	sys_oldumount
	PTR	sys_setuid
	PTR	sys_getuid
	PTR	sys_stime				/* 5025 */
	PTR	sys_ni_syscall		/* ptrace */
	PTR	sys_alarm
	PTR	sys_fstat
	PTR	sys_pause
	PTR	sys_utime				/* 5030 */
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall
	PTR	sys_access
	PTR	sys_nice
	PTR	sys_ni_syscall				/* 5035 */
	PTR	sys_sync
	PTR	sys_kill
	PTR	sys_rename
	PTR	sys_mkdir
	PTR	sys_rmdir				/* 5040 */
	PTR	sys_dup
	PTR	sys_pipe
	PTR	sys_times
	PTR	sys_ni_syscall
	PTR	sys_brk					/* 5045 */
	PTR	sys_setgid
	PTR	sys_getgid
	PTR	sys_ni_syscall		/* was signal	2 */
	PTR	sys_geteuid
	PTR	sys_getegid				/* 5050 */
	PTR	sys_acct
	PTR	sys_umount
	PTR	sys_ni_syscall
	PTR	sys_ioctl
	PTR	sys_fcntl				/* 5055 */
	PTR	sys_ni_syscall
	PTR	sys_setpgid
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall
	PTR	sys_umask				/* 5060 */
	PTR	sys_chroot
	PTR	sys_ustat
	PTR	sys_dup2
	PTR	sys_getppid
	PTR	sys_getpgrp				/* 5065 */
	PTR	sys_setsid
	PTR	sys_sigaction
	PTR	sys_sgetmask
	PTR	sys_ssetmask
	PTR	sys_setreuid				/* 5070 */
	PTR	sys_setregid
	PTR	sys_sigsuspend
	PTR	sys_sigpending
	PTR	sys_sethostname
	PTR	sys_setrlimit				/* 5075 */
	PTR	sys_getrlimit
	PTR	sys_getrusage
	PTR	sys_gettimeofday
	PTR	sys_settimeofday
	PTR	sys_getgroups				/* 5080 */
	PTR	sys_setgroups
	PTR	sys_ni_syscall			/* old_select */
	PTR	sys_symlink
	PTR	sys_lstat
	PTR	sys_readlink				/* 5085 */
	PTR	sys_uselib
	PTR	sys_swapon
	PTR	sys_reboot
	PTR	sys_ni_syscall			/* old_readdir */
	PTR	sys_mmap				/* 5090 */
	PTR	sys_munmap
	PTR	sys_truncate
	PTR	sys_ftruncate
	PTR	sys_fchmod
	PTR	sys_fchown				/* 5095 */
	PTR	sys_getpriority
	PTR	sys_setpriority
	PTR	sys_ni_syscall
	PTR	sys_statfs
	PTR	sys_fstatfs				/* 5100 */
	PTR	sys_ni_syscall		/* sys_ioperm */
	PTR	sys_socketcall
	PTR	sys_syslog
	PTR	sys_setitimer
	PTR	sys_getitimer				/* 5105 */
	PTR	sys_newstat
	PTR	sys_newlstat
	PTR	sys_newfstat
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall		/* sys_ioperm  *//* 5110 */
	PTR	sys_vhangup
	PTR	sys_ni_syscall		/* was sys_idle	 */
	PTR	sys_ni_syscall		/* sys_vm86 */
	PTR	sys_wait4
	PTR	sys_swapoff				/* 5115 */
	PTR	sys_sysinfo
	PTR	sys_ipc
	PTR	sys_fsync
	PTR	sys_sigreturn
	PTR	sys_clone				/* 5120 */
	PTR	sys_setdomainname
	PTR	sys_newuname
	PTR	sys_ni_syscall		/* sys_modify_ldt */
	PTR	sys_adjtimex
	PTR	sys_mprotect				/* 5125 */
	PTR	sys_sigprocmask
	PTR	sys_create_module
	PTR	sys_init_module
	PTR	sys_delete_module
	PTR	sys_get_kernel_syms 			/* 5130 */
	PTR	sys_quotactl
	PTR	sys_getpgid
	PTR	sys_fchdir
	PTR	sys_bdflush
	PTR	sys_sysfs				/* 5135 */
	PTR	sys_personality
	PTR	sys_ni_syscall		/* for afs_syscall */
	PTR	sys_setfsuid
	PTR	sys_setfsgid
	PTR	sys_llseek				/* 5140 */
	PTR	sys_getdents
	PTR	sys_select
	PTR	sys_flock
	PTR	sys_msync
	PTR	sys_readv				/* 5145 */
	PTR	sys_writev
	PTR	sys_cacheflush
	PTR	sys_cachectl
	PTR	sys_sysmips
	PTR	sys_ni_syscall				/* 5150 */
	PTR	sys_getsid
	PTR	sys_fdatasync
	PTR	sys_sysctl
	PTR	sys_mlock
	PTR	sys_munlock				/* 5155 */
	PTR	sys_mlockall
	PTR	sys_munlockall
	PTR	sys_sched_setparam
	PTR	sys_sched_getparam
	PTR	sys_sched_setscheduler			/* 5160 */
	PTR	sys_sched_getscheduler
	PTR	sys_sched_yield
	PTR	sys_sched_get_priority_max
	PTR	sys_sched_get_priority_min
	PTR	sys_sched_rr_get_interval		/* 5165 */
	PTR	sys_nanosleep
	PTR	sys_mremap
	PTR	sys_accept
	PTR	sys_bind
	PTR	sys_connect				/* 5170 */
	PTR	sys_getpeername
	PTR	sys_getsockname
	PTR	sys_getsockopt
	PTR	sys_listen
	PTR	sys_recv				/* 5175 */
	PTR	sys_recvfrom
	PTR	sys_recvmsg
	PTR	sys_send
	PTR	sys_sendmsg
	PTR	sys_sendto				/* 5180 */
	PTR	sys_setsockopt
	PTR	sys_shutdown
	PTR	sys_socket
	PTR	sys_socketpair
	PTR	sys_setresuid				/* 5185 */
	PTR	sys_getresuid
	PTR	sys_query_module
	PTR	sys_poll
	PTR	sys_nfsservctl
	PTR	sys_setresgid				/* 5190 */
	PTR	sys_getresgid
	PTR	sys_prctl
	PTR	sys_rt_sigreturn
	PTR	sys_rt_sigaction
	PTR	sys_rt_sigprocmask 			/* 5195 */
	PTR	sys_rt_sigpending
	PTR	sys_rt_sigtimedwait
	PTR	sys_rt_sigqueueinfo
	PTR	sys_rt_sigsuspend
	PTR	sys_pread				/* 5200 */
	PTR	sys_pwrite
	PTR	sys_chown
	PTR	sys_getcwd
	PTR	sys_capget
	PTR	sys_capset				/* 5205 */
	PTR	sys_sigaltstack
	PTR	sys_sendfile
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall
	PTR	sys_pivot_root				/* 5210 */
	PTR	sys_mincore
	PTR	sys_madvise
	PTR	sys_getdents64
