/*
 * This file contains the light-weight system call handlers (fsyscall-handlers).
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 * 	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/asmmacro.h>
#include <asm/errno.h>
#include <asm/offsets.h>
#include <asm/thread_info.h>

ENTRY(fsys_ni_syscall)
	mov r8=ENOSYS
	mov r10=-1
	br.ret.sptk.many b6
END(fsys_ni_syscall)

ENTRY(fsys_getpid)
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;
	ld4 r9=[r9]
	add r8=IA64_TASK_TGID_OFFSET,r16
	;;
	and r9=TIF_ALLWORK_MASK,r9
	ld4 r8=[r8]
	;;
	cmp.ne p8,p0=0,r9
(p8)	br.spnt.many fsys_fallback_syscall
	br.ret.sptk.many b6
END(fsys_getpid)

	.rodata
	.align 8
	.globl fsyscall_table
fsyscall_table:
	data8 fsys_ni_syscall
	data8 fsys_fallback_syscall	// exit			// 1025
	data8 fsys_fallback_syscall	// read
	data8 fsys_fallback_syscall	// write
	data8 fsys_fallback_syscall	// open
	data8 fsys_fallback_syscall	// close
	data8 fsys_fallback_syscall	// creat		// 1030
	data8 fsys_fallback_syscall	// link
	data8 fsys_fallback_syscall	// unlink
	data8 fsys_fallback_syscall	// execve
	data8 fsys_fallback_syscall	// chdir
	data8 fsys_fallback_syscall	// fchdir		// 1035
	data8 fsys_fallback_syscall	// utimes
	data8 fsys_fallback_syscall	// mknod
	data8 fsys_fallback_syscall	// chmod
	data8 fsys_fallback_syscall	// chown
	data8 fsys_fallback_syscall	// lseek		// 1040
	data8 fsys_getpid
	data8 fsys_fallback_syscall	// getppid
	data8 fsys_fallback_syscall	// mount
	data8 fsys_fallback_syscall	// umount
	data8 fsys_fallback_syscall	// setuid		// 1045
	data8 fsys_fallback_syscall	// getuid
	data8 fsys_fallback_syscall	// geteuid
	data8 fsys_fallback_syscall	// ptrace
	data8 fsys_fallback_syscall	// access
	data8 fsys_fallback_syscall	// sync			// 1050
	data8 fsys_fallback_syscall	// fsync
	data8 fsys_fallback_syscall	// fdatasync
	data8 fsys_fallback_syscall	// kill
	data8 fsys_fallback_syscall	// rename
	data8 fsys_fallback_syscall	// mkdir		// 1055
	data8 fsys_fallback_syscall	// rmdir
	data8 fsys_fallback_syscall	// dup
	data8 fsys_fallback_syscall	// pipe
	data8 fsys_fallback_syscall	// times
	data8 fsys_fallback_syscall	// brk			// 1060
	data8 fsys_fallback_syscall	// setgid
	data8 fsys_fallback_syscall	// getgid
	data8 fsys_fallback_syscall	// getegid
	data8 fsys_fallback_syscall	// acct
	data8 fsys_fallback_syscall	// ioctl		// 1065
	data8 fsys_fallback_syscall	// fcntl
	data8 fsys_fallback_syscall	// umask
	data8 fsys_fallback_syscall	// chroot
	data8 fsys_fallback_syscall	// ustat
	data8 fsys_fallback_syscall	// dup2			// 1070
	data8 fsys_fallback_syscall	// setreuid
	data8 fsys_fallback_syscall	// setregid
	data8 fsys_fallback_syscall	// getresuid
	data8 fsys_fallback_syscall	// setresuid
	data8 fsys_fallback_syscall	// getresgid		// 1075
	data8 fsys_fallback_syscall	// setresgid
	data8 fsys_fallback_syscall	// getgroups
	data8 fsys_fallback_syscall	// setgroups
	data8 fsys_fallback_syscall	// getpgid
	data8 fsys_fallback_syscall	// setpgid		// 1080
	data8 fsys_fallback_syscall	// setsid
	data8 fsys_fallback_syscall	// getsid
	data8 fsys_fallback_syscall	// sethostname
	data8 fsys_fallback_syscall	// setrlimit
	data8 fsys_fallback_syscall	// getrlimit		// 1085
	data8 fsys_fallback_syscall	// getrusage
	data8 fsys_fallback_syscall	// gettimeofday
	data8 fsys_fallback_syscall	// settimeofday
	data8 fsys_fallback_syscall	// select
	data8 fsys_fallback_syscall	// poll			// 1090
	data8 fsys_fallback_syscall	// symlink
	data8 fsys_fallback_syscall	// readlink
	data8 fsys_fallback_syscall	// uselib
	data8 fsys_fallback_syscall	// swapon
	data8 fsys_fallback_syscall	// swapoff		// 1095
	data8 fsys_fallback_syscall	// reboot
	data8 fsys_fallback_syscall	// truncate
	data8 fsys_fallback_syscall	// ftruncate
	data8 fsys_fallback_syscall	// fchmod
	data8 fsys_fallback_syscall	// fchown		// 1100
	data8 fsys_fallback_syscall	// getpriority
	data8 fsys_fallback_syscall	// setpriority
	data8 fsys_fallback_syscall	// statfs
	data8 fsys_fallback_syscall	// fstatfs
	data8 fsys_fallback_syscall	// gettid		// 1105
	data8 fsys_fallback_syscall	// semget
	data8 fsys_fallback_syscall	// semop
	data8 fsys_fallback_syscall	// semctl
	data8 fsys_fallback_syscall	// msgget
	data8 fsys_fallback_syscall	// msgsnd		// 1110
	data8 fsys_fallback_syscall	// msgrcv
	data8 fsys_fallback_syscall	// msgctl
	data8 fsys_fallback_syscall	// shmget
	data8 fsys_fallback_syscall	// shmat
	data8 fsys_fallback_syscall	// shmdt		// 1115
	data8 fsys_fallback_syscall	// shmctl
	data8 fsys_fallback_syscall	// syslog
	data8 fsys_fallback_syscall	// setitimer
	data8 fsys_fallback_syscall	// getitimer
	data8 fsys_fallback_syscall		 		// 1120
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall	// vhangup
	data8 fsys_fallback_syscall	// lchown
	data8 fsys_fallback_syscall	// remap_file_pages	// 1125
	data8 fsys_fallback_syscall	// wait4
	data8 fsys_fallback_syscall	// sysinfo
	data8 fsys_fallback_syscall	// clone
	data8 fsys_fallback_syscall	// setdomainname
	data8 fsys_fallback_syscall	// newuname		// 1130
	data8 fsys_fallback_syscall	// adjtimex
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall	// init_module
	data8 fsys_fallback_syscall	// delete_module
	data8 fsys_fallback_syscall				// 1135
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall	// quotactl
	data8 fsys_fallback_syscall	// bdflush
	data8 fsys_fallback_syscall	// sysfs
	data8 fsys_fallback_syscall	// personality		// 1140
	data8 fsys_fallback_syscall	// afs_syscall
	data8 fsys_fallback_syscall	// setfsuid
	data8 fsys_fallback_syscall	// setfsgid
	data8 fsys_fallback_syscall	// getdents
	data8 fsys_fallback_syscall	// flock		// 1145
	data8 fsys_fallback_syscall	// readv
	data8 fsys_fallback_syscall	// writev
	data8 fsys_fallback_syscall	// pread64
	data8 fsys_fallback_syscall	// pwrite64
	data8 fsys_fallback_syscall	// sysctl		// 1150
	data8 fsys_fallback_syscall	// mmap
	data8 fsys_fallback_syscall	// munmap
	data8 fsys_fallback_syscall	// mlock
	data8 fsys_fallback_syscall	// mlockall
	data8 fsys_fallback_syscall	// mprotect		// 1155
	data8 fsys_fallback_syscall	// mremap
	data8 fsys_fallback_syscall	// msync
	data8 fsys_fallback_syscall	// munlock
	data8 fsys_fallback_syscall	// munlockall
	data8 fsys_fallback_syscall	// sched_getparam	// 1160
	data8 fsys_fallback_syscall	// sched_setparam
	data8 fsys_fallback_syscall	// sched_getscheduler
	data8 fsys_fallback_syscall	// sched_setscheduler
	data8 fsys_fallback_syscall	// sched_yield
	data8 fsys_fallback_syscall	// sched_get_priority_max	// 1165
	data8 fsys_fallback_syscall	// sched_get_priority_min
	data8 fsys_fallback_syscall	// sched_rr_get_interval
	data8 fsys_fallback_syscall	// nanosleep
	data8 fsys_fallback_syscall	// nfsservctl
	data8 fsys_fallback_syscall	// prctl		// 1170
	data8 fsys_fallback_syscall	// getpagesize
	data8 fsys_fallback_syscall	// mmap2
	data8 fsys_fallback_syscall	// pciconfig_read
	data8 fsys_fallback_syscall	// pciconfig_write
	data8 fsys_fallback_syscall	// perfmonctl		// 1175
	data8 fsys_fallback_syscall	// sigaltstack
	data8 fsys_fallback_syscall	// rt_sigaction
	data8 fsys_fallback_syscall	// rt_sigpending
	data8 fsys_fallback_syscall	// rt_sigprocmask
	data8 fsys_fallback_syscall	// rt_sigqueueinfo	// 1180
	data8 fsys_fallback_syscall	// rt_sigreturn
	data8 fsys_fallback_syscall	// rt_sigsuspend
	data8 fsys_fallback_syscall	// rt_sigtimedwait
	data8 fsys_fallback_syscall	// getcwd
	data8 fsys_fallback_syscall	// capget		// 1185
	data8 fsys_fallback_syscall	// capset
	data8 fsys_fallback_syscall	// sendfile
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall	// socket		// 1190
	data8 fsys_fallback_syscall	// bind
	data8 fsys_fallback_syscall	// connect
	data8 fsys_fallback_syscall	// listen
	data8 fsys_fallback_syscall	// accept
	data8 fsys_fallback_syscall	// getsockname		// 1195
	data8 fsys_fallback_syscall	// getpeername
	data8 fsys_fallback_syscall	// socketpair
	data8 fsys_fallback_syscall	// send
	data8 fsys_fallback_syscall	// sendto
	data8 fsys_fallback_syscall	// recv			// 1200
	data8 fsys_fallback_syscall	// recvfrom
	data8 fsys_fallback_syscall	// shutdown
	data8 fsys_fallback_syscall	// setsockopt
	data8 fsys_fallback_syscall	// getsockopt
	data8 fsys_fallback_syscall	// sendmsg		// 1205
	data8 fsys_fallback_syscall	// recvmsg
	data8 fsys_fallback_syscall	// pivot_root
	data8 fsys_fallback_syscall	// mincore
	data8 fsys_fallback_syscall	// madvise
	data8 fsys_fallback_syscall	// newstat		// 1210
	data8 fsys_fallback_syscall	// newlstat
	data8 fsys_fallback_syscall	// newfstat
	data8 fsys_fallback_syscall	// clone2
	data8 fsys_fallback_syscall	// getdents64
	data8 fsys_fallback_syscall	// getunwind		// 1215
	data8 fsys_fallback_syscall	// readahead
	data8 fsys_fallback_syscall	// setxattr
	data8 fsys_fallback_syscall	// lsetxattr
	data8 fsys_fallback_syscall	// fsetxattr
	data8 fsys_fallback_syscall	// getxattr		// 1220
	data8 fsys_fallback_syscall	// lgetxattr
	data8 fsys_fallback_syscall	// fgetxattr
	data8 fsys_fallback_syscall	// listxattr
	data8 fsys_fallback_syscall	// llistxattr
	data8 fsys_fallback_syscall	// flistxattr		// 1225
	data8 fsys_fallback_syscall	// removexattr
	data8 fsys_fallback_syscall	// lremovexattr
	data8 fsys_fallback_syscall	// fremovexattr
	data8 fsys_fallback_syscall	// tkill
	data8 fsys_fallback_syscall	// futex		// 1230
	data8 fsys_fallback_syscall	// sched_setaffinity
	data8 fsys_fallback_syscall	// sched_getaffinity
	data8 fsys_fallback_syscall	// set_tid_address
	data8 fsys_fallback_syscall	// alloc_hugepages
	data8 fsys_fallback_syscall	// free_hugepages	// 1235
	data8 fsys_fallback_syscall	// exit_group
	data8 fsys_fallback_syscall	// lookup_dcookie
	data8 fsys_fallback_syscall	// io_setup
	data8 fsys_fallback_syscall	// io_destroy
	data8 fsys_fallback_syscall	// io_getevents		// 1240
	data8 fsys_fallback_syscall	// io_submit
	data8 fsys_fallback_syscall	// io_cancel
	data8 fsys_fallback_syscall	// epoll_create
	data8 fsys_fallback_syscall	// epoll_ctl
	data8 fsys_fallback_syscall	// epoll_wait		// 1245
	data8 fsys_fallback_syscall	// restart_syscall
	data8 fsys_fallback_syscall	// semtimedop
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall				// 1250
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall				// 1255
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall				// 1260
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall				// 1265
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall				// 1270
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall				// 1275
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
	data8 fsys_fallback_syscall
