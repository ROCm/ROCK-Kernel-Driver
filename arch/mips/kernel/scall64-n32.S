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
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/unistd.h>

/* This duplicates the definition from <linux/sched.h> */
#define PT_TRACESYS	0x00000002	/* tracing system calls */

/* This duplicates the definition from <asm/signal.h> */
#define SIGILL		4		/* Illegal instruction (ANSI).  */

#ifndef CONFIG_MIPS32_O32
/* No O32, so define handle_sys here */
#define handle_sysn32 handle_sys
#endif

	.align  5
NESTED(handle_sysn32, PT_SIZE, sp)
#ifndef CONFIG_MIPS32_O32
	.set	noat
	SAVE_SOME
	STI
	.set	at
#endif
	ld	t1, PT_EPC(sp)		# skip syscall on return

	subu	t0, v0, __NR_N32_Linux	# check syscall number
	sltiu	t0, t0,	__NR_N32_Linux_syscalls + 1
	daddiu	t1, 4			# skip to next instruction
	beqz	t0, not_n32_scall
	sd	t1, PT_EPC(sp)

	dsll	t0, v0, 3		# offset into table
	ld	t2, (sysn32_call_table - (__NR_N32_Linux * 8))(t0)

	sd	a3, PT_R26(sp)		# save a3 for syscall restarting

	LONG_L	t0, TI_FLAGS($28)	# syscall tracing enabled?
	bltz	t0, n32_syscall_trace_entry

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	negu	v0			# error
	sd	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

FEXPORT(n32_syscall_exit)
	local_irq_disable		# make sure need_resched and
					# signals dont change between
					# sampling and return
	LONG_L  a2, TI_FLAGS($28)	# current->work
	bnez	a2, n32_syscall_exit_work

	RESTORE_SOME
	RESTORE_SP_AND_RET

n32_syscall_exit_work:
	SAVE_STATIC
	j	syscall_exit_work

/* ------------------------------------------------------------------------ */

n32_syscall_trace_entry:
	SAVE_STATIC
	sd	t2,PT_R1(sp)
	jal	do_syscall_trace
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

	j	n32_syscall_exit

not_n32_scall:
	/* This is not an n32 compatibility syscall, pass it on to
	   the n64 syscall handlers.  */
	j	handle_sys64

	END(handle_sysn32)

EXPORT(sysn32_call_table)
	PTR	sys_read			/* 6000 */
	PTR	sys_write
	PTR	sys_open
	PTR	sys_close
	PTR	sys_newstat
	PTR	sys_newfstat			/* 6005 */
	PTR	sys_newlstat
	PTR	sys_poll
	PTR	sys_lseek
	PTR	sys_mmap2
	PTR	sys_mprotect			/* 6010 */
	PTR	sys_munmap
	PTR	sys_brk
	PTR	sys32_rt_sigaction
	PTR	sys32_rt_sigprocmask
	PTR	compat_sys_ioctl		/* 6015 */
	PTR	sys_pread64
	PTR	sys_pwrite64
	PTR	sys32_readv
	PTR	sys32_writev
	PTR	sys_access			/* 6020 */
	PTR	sys_pipe
	PTR	sys32_select
	PTR	sys_sched_yield
	PTR	sys_mremap
	PTR	sys_msync			/* 6025 */
	PTR	sys_mincore
	PTR	sys_madvise
	PTR	sys_shmget
	PTR	sys_shmat
	PTR	sys_shmctl			/* 6030 */
	PTR	sys_dup
	PTR	sys_dup2
	PTR	sys_pause
	PTR	compat_sys_nanosleep
	PTR	compat_sys_getitimer		/* 6035 */
	PTR	compat_sys_setitimer
	PTR	sys_alarm
	PTR	sys_getpid
	PTR	sys32_sendfile
	PTR	sys_socket			/* 6040 */
	PTR	sys_connect
	PTR	sys_accept
	PTR	sys_sendto
	PTR	sys_recvfrom
	PTR	compat_sys_sendmsg		/* 6045 */
	PTR	compat_sys_recvmsg
	PTR	sys_shutdown
	PTR	sys_bind
	PTR	sys_listen
	PTR	sys_getsockname			/* 6050 */
	PTR	sys_getpeername
	PTR	sys_socketpair
	PTR	compat_sys_setsockopt
	PTR	sys_getsockopt
	PTR	sys_clone			/* 6055 */
	PTR	sys_fork
	PTR	sys32_execve
	PTR	sys_exit
	PTR	sys32_wait4
	PTR	sys_kill			/* 6060 */
	PTR	sys32_newuname
	PTR	sys_semget
	PTR	sys_semop
	PTR	sys_semctl
	PTR	sys_shmdt			/* 6065 */
	PTR	sys_msgget
	PTR	sys_msgsnd
	PTR	sys_msgrcv
	PTR	sys_msgctl
	PTR	compat_sys_fcntl		/* 6070 */
	PTR	sys_flock
	PTR	sys_fsync
	PTR	sys_fdatasync
	PTR	sys_truncate
	PTR	sys_ftruncate			/* 6075 */
	PTR	sys32_getdents
	PTR	sys_getcwd
	PTR	sys_chdir
	PTR	sys_fchdir
	PTR	sys_rename			/* 6080 */
	PTR	sys_mkdir
	PTR	sys_rmdir
	PTR	sys_creat
	PTR	sys_link
	PTR	sys_unlink			/* 6085 */
	PTR	sys_symlink
	PTR	sys_readlink
	PTR	sys_chmod
	PTR	sys_fchmod
	PTR	sys_chown			/* 6090 */
	PTR	sys_fchown
	PTR	sys_lchown
	PTR	sys_umask
	PTR	sys32_gettimeofday
	PTR	compat_sys_getrlimit		/* 6095 */
	PTR	compat_sys_getrusage
	PTR	sys32_sysinfo
	PTR	compat_sys_times
	PTR	sys_ptrace
	PTR	sys_getuid			/* 6100 */
	PTR	sys_syslog
	PTR	sys_getgid
	PTR	sys_setuid
	PTR	sys_setgid
	PTR	sys_geteuid			/* 6105 */
	PTR	sys_getegid
	PTR	sys_setpgid
	PTR	sys_getppid
	PTR	sys_getpgrp
	PTR	sys_setsid			/* 6110 */
	PTR	sys_setreuid
	PTR	sys_setregid
	PTR	sys_getgroups
	PTR	sys_setgroups
	PTR	sys_setresuid			/* 6115 */
	PTR	sys_getresuid
	PTR	sys_setresgid
	PTR	sys_getresgid
	PTR	sys_getpgid
	PTR	sys_setfsuid			/* 6120 */
	PTR	sys_setfsgid
	PTR	sys_getsid
	PTR	sys_capget
	PTR	sys_capset
	PTR	sys32_rt_sigpending		/* 6125 */
	PTR	sys32_rt_sigtimedwait
	PTR	sys32_rt_sigqueueinfo
	PTR	sys32_rt_sigsuspend
	PTR	sys32_sigaltstack
	PTR	compat_sys_utime		/* 6130 */
	PTR	sys_mknod
	PTR	sys32_personality
	PTR	sys_ustat
	PTR	compat_sys_statfs
	PTR	compat_sys_fstatfs		/* 6135 */
	PTR	sys_sysfs
	PTR	sys_getpriority
	PTR	sys_setpriority
	PTR	sys_sched_setparam
	PTR	sys_sched_getparam		/* 6140 */
	PTR	sys_sched_setscheduler
	PTR	sys_sched_getscheduler
	PTR	sys_sched_get_priority_max
	PTR	sys_sched_get_priority_min
	PTR	sys32_sched_rr_get_interval	/* 6145 */
	PTR	sys_mlock
	PTR	sys_munlock
	PTR	sys_mlockall
	PTR	sys_munlockall
	PTR	sys_vhangup			/* 6150 */
	PTR	sys_pivot_root
	PTR	sys32_sysctl
	PTR	sys_prctl
	PTR	sys32_adjtimex
	PTR	compat_sys_setrlimit		/* 6155 */
	PTR	sys_chroot
	PTR	sys_sync
	PTR	sys_acct
	PTR	sys32_settimeofday
	PTR	sys_mount			/* 6160 */
	PTR	sys_umount
	PTR	sys_swapon
	PTR	sys_swapoff
	PTR	sys_reboot
	PTR	sys_sethostname			/* 6165 */
	PTR	sys_setdomainname
	PTR	sys_ni_syscall			/* was create_module */
	PTR	sys_init_module
	PTR	sys_delete_module
	PTR	sys_ni_syscall			/* 6170, was get_kernel_syms */
	PTR	sys_ni_syscall			/* was query_module */
	PTR	sys_quotactl
	PTR	sys_nfsservctl
	PTR	sys_ni_syscall			/* res. for getpmsg */
	PTR	sys_ni_syscall			/* 6175  for putpmsg */
	PTR	sys_ni_syscall			/* res. for afs_syscall */
	PTR	sys_ni_syscall			/* res. for security */
	PTR	sys_gettid
	PTR	sys32_readahead
	PTR	sys_setxattr			/* 6180 */
	PTR	sys_lsetxattr
	PTR	sys_fsetxattr
	PTR	sys_getxattr
	PTR	sys_lgetxattr
	PTR	sys_fgetxattr			/* 6185 */
	PTR	sys_listxattr
	PTR	sys_llistxattr
	PTR	sys_flistxattr
	PTR	sys_removexattr
	PTR	sys_lremovexattr		/* 6190 */
	PTR	sys_fremovexattr
	PTR	sys_tkill
	PTR	sys_time
	PTR	compat_sys_futex
	PTR	sys32_sched_setaffinity		/* 6195 */
	PTR	sys32_sched_getaffinity
	PTR	sys_cacheflush
	PTR	sys_cachectl
	PTR	sys_sysmips
	PTR	sys_io_setup			/* 6200 */
	PTR	sys_io_destroy
	PTR	sys_io_getevents
	PTR	sys_io_submit
	PTR	sys_io_cancel
	PTR	sys_exit_group			/* 6205 */
	PTR	sys_lookup_dcookie
	PTR	sys_epoll_create
	PTR	sys_epoll_ctl
	PTR	sys_epoll_wait
	PTR	sys_remap_file_pages		/* 6210 */
	PTR	sysn32_rt_sigreturn
	PTR	sys_fcntl
	PTR	sys_set_tid_address
	PTR	sys_restart_syscall
	PTR	sys_semtimedop			/* 6215 */
	PTR	sys_fadvise64
	PTR	sys_statfs64
	PTR	sys_fstatfs64
	PTR	sys_sendfile64
	PTR	sys_timer_create		/* 6220 */
	PTR	sys_timer_settime
	PTR	sys_timer_gettime
	PTR	sys_timer_getoverrun
	PTR	sys_timer_delete
	PTR	sys_clock_settime		/* 6225 */
	PTR	sys_clock_gettime
	PTR	sys_clock_getres
	PTR	sys_clock_nanosleep
	PTR	sys_tgkill
	PTR	compat_sys_utimes		/* 6230 */
