/*
 * This file contains the light-weight system call handlers (fsyscall-handlers).
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 * 	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 18-Feb-03 louisk	Implement fsys_gettimeofday().
 * 28-Feb-03 davidm	Fixed several bugs in fsys_gettimeofday().  Tuned it some more,
 *			probably broke it along the way... ;-)
 */

#include <asm/asmmacro.h>
#include <asm/errno.h>
#include <asm/offsets.h>
#include <asm/percpu.h>
#include <asm/thread_info.h>
#include <asm/sal.h>

/*
 * See Documentation/ia64/fsys.txt for details on fsyscalls.
 *
 * On entry to an fsyscall handler:
 *   r10	= 0 (i.e., defaults to "successful syscall return")
 *   r11	= saved ar.pfs (a user-level value)
 *   r15	= system call number
 *   r16	= "current" task pointer (in normal kernel-mode, this is in r13)
 *   r32-r39	= system call arguments
 *   b6		= return address (a user-level value)
 *   ar.pfs	= previous frame-state (a user-level value)
 *   PSR.be	= cleared to zero (i.e., little-endian byte order is in effect)
 *   all other registers may contain values passed in from user-mode
 *
 * On return from an fsyscall handler:
 *   r11	= saved ar.pfs (as passed into the fsyscall handler)
 *   r15	= system call number (as passed into the fsyscall handler)
 *   r32-r39	= system call arguments (as passed into the fsyscall handler)
 *   b6		= return address (as passed into the fsyscall handler)
 *   ar.pfs	= previous frame-state (as passed into the fsyscall handler)
 */

ENTRY(fsys_ni_syscall)
	mov r8=ENOSYS
	mov r10=-1
	MCKINLEY_E9_WORKAROUND
	br.ret.sptk.many b6
END(fsys_ni_syscall)

ENTRY(fsys_getpid)
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;
	ld4 r9=[r9]
	add r8=IA64_TASK_TGID_OFFSET,r16
	;;
	and r9=TIF_ALLWORK_MASK,r9
	ld4 r8=[r8]				// r8 = current->tgid
	;;
	cmp.ne p8,p0=0,r9
(p8)	br.spnt.many fsys_fallback_syscall
	MCKINLEY_E9_WORKAROUND
	br.ret.sptk.many b6
END(fsys_getpid)

ENTRY(fsys_getppid)
	add r17=IA64_TASK_GROUP_LEADER_OFFSET,r16
	;;
	ld8 r17=[r17]				// r17 = current->group_leader
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;

	ld4 r9=[r9]
	add r17=IA64_TASK_REAL_PARENT_OFFSET,r17 // r17 = &current->group_leader->real_parent
	;;
	and r9=TIF_ALLWORK_MASK,r9

1:	ld8 r18=[r17]				// r18 = current->group_leader->real_parent
	;;
	cmp.ne p8,p0=0,r9
	add r8=IA64_TASK_TGID_OFFSET,r18	// r8 = &current->group_leader->real_parent->tgid
	;;

	/*
	 * The .acq is needed to ensure that the read of tgid has returned its data before
	 * we re-check "real_parent".
	 */
	ld4.acq r8=[r8]				// r8 = current->group_leader->real_parent->tgid
#ifdef CONFIG_SMP
	/*
	 * Re-read current->group_leader->real_parent.
	 */
	ld8 r19=[r17]				// r19 = current->group_leader->real_parent
(p8)	br.spnt.many fsys_fallback_syscall
	;;
	cmp.ne p6,p0=r18,r19			// did real_parent change?
	mov r19=0			// i must not leak kernel bits...
(p6)	br.cond.spnt.few 1b			// yes -> redo the read of tgid and the check
	;;
	mov r17=0			// i must not leak kernel bits...
	mov r18=0			// i must not leak kernel bits...
#else
	mov r17=0			// i must not leak kernel bits...
	mov r18=0			// i must not leak kernel bits...
	mov r19=0			// i must not leak kernel bits...
#endif
	MCKINLEY_E9_WORKAROUND
	br.ret.sptk.many b6
END(fsys_getppid)

ENTRY(fsys_set_tid_address)
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;
	ld4 r9=[r9]
	tnat.z p6,p7=r32		// check argument register for being NaT
	;;
	and r9=TIF_ALLWORK_MASK,r9
	add r8=IA64_TASK_PID_OFFSET,r16
	add r18=IA64_TASK_CLEAR_CHILD_TID_OFFSET,r16
	;;
	ld4 r8=[r8]
	cmp.ne p8,p0=0,r9
	mov r17=-1
	;;
(p6)	st8 [r18]=r32
(p7)	st8 [r18]=r17
(p8)	br.spnt.many fsys_fallback_syscall
	;;
	mov r17=0			// i must not leak kernel bits...
	mov r18=0			// i must not leak kernel bits...
	MCKINLEY_E9_WORKAROUND
	br.ret.sptk.many b6
END(fsys_set_tid_address)

/*
 * Note 1: This routine uses floating-point registers, but only with registers that
 *	   operate on integers.  Because of that, we don't need to set ar.fpsr to the
 *	   kernel default value.
 *
 * Note 2: For now, we will assume that all CPUs run at the same clock-frequency.
 *	   If that wasn't the case, we would have to disable preemption (e.g.,
 *	   by disabling interrupts) between reading the ITC and reading
 *	   local_cpu_data->nsec_per_cyc.
 *
 * Note 3: On platforms where the ITC-drift bit is set in the SAL feature vector,
 *	   we ought to either skip the ITC-based interpolation or run an ntp-like
 *	   daemon to keep the ITCs from drifting too far apart.
 */

ENTRY(fsys_gettimeofday)
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	movl r3=THIS_CPU(cpu_info)

	mov.m r31=ar.itc		// put time stamp into r31 (ITC) == now		(35 cyc)
#ifdef CONFIG_SMP
	movl r10=__per_cpu_offset
	movl r2=sal_platform_features
	;;

	ld8 r2=[r2]
	movl r19=xtime			// xtime is a timespec struct

	ld8 r10=[r10]			// r10 <- __per_cpu_offset[0]
	movl r21=cpu_info__per_cpu
	;;
	add r10=r21, r10		// r10 <- &cpu_data(time_keeper_id)
	tbit.nz p8,p0 = r2, IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT_BIT
(p8)	br.spnt.many fsys_fallback_syscall
#else
	;;
	mov r10=r3
	movl r19=xtime			// xtime is a timespec struct
#endif
	ld4 r9=[r9]
	movl r17=xtime_lock
	;;

	// r32, r33 should contain the 2 args of gettimeofday
	adds r21=IA64_CPUINFO_ITM_NEXT_OFFSET, r10
	mov r2=-1
	tnat.nz p6,p7=r32		// guard against NaT args
	;;

	adds r10=IA64_CPUINFO_ITM_DELTA_OFFSET, r10
(p7)	tnat.nz p6,p0=r33
(p6)	br.cond.spnt.few .fail

	adds r8=IA64_CPUINFO_NSEC_PER_CYC_OFFSET, r3
	movl r24=2361183241434822607	// for division hack (only for / 1000)
	;;

	ldf8 f7=[r10]			// f7 now contains itm_delta
	setf.sig f11=r2
	adds r10=8, r32

	adds r20=IA64_TIMESPEC_TV_NSEC_OFFSET, r19	// r20 = &xtime->tv_nsec
	movl r26=jiffies

	setf.sig f9=r24			// f9 is used for division hack
	movl r27=wall_jiffies

	and r9=TIF_ALLWORK_MASK,r9
	movl r25=last_nsec_offset
	;;

	/*
	 * Verify that we have permission to write to struct timeval.  Note:
	 * Another thread might unmap the mapping before we actually get
	 * to store the result.  That's OK as long as the stores are also
	 * protect by EX().
	 */
EX(.fail, probe.w.fault r32, 3)		// this must come _after_ NaT-check
EX(.fail, probe.w.fault r10, 3)		// this must come _after_ NaT-check
	nop 0

	ldf8 f10=[r8]			// f10 <- local_cpu_data->nsec_per_cyc value
	cmp.ne p8, p0=0, r9
(p8)	br.spnt.many fsys_fallback_syscall
	;;
.retry:	// *** seq = read_seqbegin(&xtime_lock); ***
	ld4.acq r23=[r17]		// since &xtime_lock == &xtime_lock->sequence
	ld8 r14=[r25]			// r14 (old) = last_nsec_offset

	ld8 r28=[r26]			// r28 = jiffies
	ld8 r29=[r27]			// r29 = wall_jiffies
	;;

	ldf8 f8=[r21]			// f8 now contains itm_next
	sub r28=r29, r28, 1		// r28 now contains "-(lost + 1)"
	tbit.nz p9, p10=r23, 0		// p9 <- is_odd(r23), p10 <- is_even(r23)
	;;

	ld8 r2=[r19]			// r2 = sec = xtime.tv_sec
	ld8 r29=[r20]			// r29 = nsec = xtime.tv_nsec

	setf.sig f6=r28			// f6 <- -(lost + 1)				(6 cyc)
	;;

	mf
	xma.l f8=f6, f7, f8	// f8 (last_tick) <- -(lost + 1)*itm_delta + itm_next	(5 cyc)
	nop 0

	setf.sig f12=r31		// f12 <- ITC					(6 cyc)
	// *** if (unlikely(read_seqretry(&xtime_lock, seq))) continue; ***
	ld4 r24=[r17]			// r24 = xtime_lock->sequence (re-read)
	nop 0
	;;

	mov r31=ar.itc			// re-read ITC in case we .retry		(35 cyc)
	xma.l f8=f11, f8, f12	// f8 (elapsed_cycles) <- (-1*last_tick + now) = (now - last_tick)
	nop 0
	;;

	getf.sig r18=f8			// r18 <- (now - last_tick)
	xmpy.l f8=f8, f10		// f8 <- elapsed_cycles*nsec_per_cyc (5 cyc)
	add r3=r29, r14			// r3 = (nsec + old)
	;;

	cmp.lt p7, p8=r18, r0		// if now < last_tick, set p7 = 1, p8 = 0
	getf.sig r18=f8			// r18 = elapsed_cycles*nsec_per_cyc		(6 cyc)
	nop 0
	;;

(p10)	cmp.ne p9, p0=r23, r24		// if xtime_lock->sequence != seq, set p9
	shr.u r18=r18, IA64_NSEC_PER_CYC_SHIFT	// r18 <- offset
(p9)	br.spnt.many .retry
	;;

	mov ar.ccv=r14			// ar.ccv = old					(1 cyc)
	cmp.leu p7, p8=r18, r14		// if (offset <= old), set p7 = 1, p8 = 0
	;;

(p8)	cmpxchg8.rel r24=[r25], r18, ar.ccv	// compare-and-exchange (atomic!)
(p8)	add r3=r29, r18			// r3 = (nsec + offset)
	;;
	shr.u r3=r3, 3			// initiate dividing r3 by 1000
	;;
	setf.sig f8=r3			//						(6 cyc)
	mov r10=1000000			// r10 = 1000000
	;;
(p8)	cmp.ne.unc p9, p0=r24, r14
	xmpy.hu f6=f8, f9		//						(5 cyc)
(p9)	br.spnt.many .retry
	;;

	getf.sig r3=f6			//						(6 cyc)
	;;
	shr.u r3=r3, 4			// end of division, r3 is divided by 1000 (=usec)
	;;

1:	cmp.geu p7, p0=r3, r10		// while (usec >= 1000000)
	;;
(p7)	sub r3=r3, r10			// usec -= 1000000
(p7)	adds r2=1, r2			// ++sec
(p7)	br.spnt.many 1b

	// finally: r2 = sec, r3 = usec
EX(.fail, st8 [r32]=r2)
	adds r9=8, r32
	mov r8=r0			// success
	;;
EX(.fail, st8 [r9]=r3)			// store them in the timeval struct
	mov r10=0
	MCKINLEY_E9_WORKAROUND
	br.ret.sptk.many b6		// return to caller
	/*
	 * Note: We are NOT clearing the scratch registers here.  Since the only things
	 *	 in those registers are time-related variables and some addresses (which
	 *	 can be obtained from System.map), none of this should be security-sensitive
	 *	 and we should be fine.
	 */

.fail:	adds r8=EINVAL, r0		// r8 = EINVAL
	adds r10=-1, r0			// r10 = -1
	MCKINLEY_E9_WORKAROUND
	br.ret.spnt.many b6		// return with r8 set to EINVAL
END(fsys_gettimeofday)

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
	data8 fsys_getppid		// getppid
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
	data8 fsys_gettimeofday		// gettimeofday
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
	data8 fsys_set_tid_address	// set_tid_address
	data8 fsys_fallback_syscall	// unused
	data8 fsys_fallback_syscall	// unused		// 1235
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
	data8 fsys_fallback_syscall	// timer_create
	data8 fsys_fallback_syscall	// timer_settime
	data8 fsys_fallback_syscall	// timer_gettime 	// 1250
	data8 fsys_fallback_syscall	// timer_getoverrun
	data8 fsys_fallback_syscall	// timer_delete
	data8 fsys_fallback_syscall	// clock_settime
	data8 fsys_fallback_syscall	// clock_gettime
	data8 fsys_fallback_syscall	// clock_getres		// 1255
	data8 fsys_fallback_syscall	// clock_nanosleep
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
