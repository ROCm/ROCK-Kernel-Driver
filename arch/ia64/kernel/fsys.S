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

ENTRY(fsys_gettimeofday)

	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;
	ld4 r9=[r9]
	;;	
	and r9=TIF_ALLWORK_MASK,r9
	;;
	// r32, r33 should contain the 2 args of gettimeofday
	
	tnat.nz p6,p7=r32		// in case the args are NaT
	cmp.ne p8, p0=0, r9
	;;
	
(p7)	tnat.nz p6,p0=r33		
(p8)	br.spnt.many fsys_fallback_syscall
	;;
(p6)	adds r8=EINVAL, r0		// r8 = EINVAL
(p6)	adds r10=-1, r0			// r10 = -1	
(p6)	br.ret.spnt.many b6		// return with r8 set to EINVAL

	movl r17=xtime_lock
	movl r19=xtime			// xtime is a timespec struct
	movl r20=cpu_info__per_cpu
	movl r26=jiffies
	movl r27=wall_jiffies
	movl r31=last_nsec_offset
	movl r24=2361183241434822607	// for division hack (only for / 1000)
	;;	
	setf.sig f9=r24			// f9 is used for division hack
	adds r21=IA64_CPUINFO_ITM_NEXT_OFFSET, r20
	adds r22=IA64_CPUINFO_ITM_DELTA_OFFSET, r20
	adds r30=IA64_CPUINFO_NSEC_PER_CYC_OFFSET, r20
	adds r3=IA64_TIMESPEC_TV_NSEC_OFFSET, r19	
					// r3 = &xtime->tv_nsec
	
	
while_loop_1:

	// *** seq = read_seqbegin(&xtime_lock); ***
	
	ld4 r23=[r17]			// since &xtime_lock == &xtime_lock->sequence
#ifdef CONFIG_SMP
	mf
#endif
	;;				// barrier()
	// now r23 = seq
	
	ld8 r14=[r31]			// r14 = old = last_nsec_offset		
	
	ld8 r28=[r26]			// r28 = jiffies
	ld8 r29=[r27]			// r29 = wall_jiffies
	;;
	
	ld8 r24=[r21]			// r24 now contains itm_next
	ld8 r25=[r22]			// r25 now contains itm_delta

	sub r28=r28, r29		// r28 now contains "lost"
	;;
	adds r28=1, r28			// r28 now contains "lost + 1"
	;;
	setf.sig f6=r28
	setf.sig f7=r25
	
	ld8 r2=[r19]			// r2 = sec = xtime.tv_sec
	;;
	
	ld8 r28=[r3]			// r28 = nsec = xtime.tv_nsec
	xma.l f8=f6, f7, f0		// put lower 64-bits result of f6 * f7 in f8
	;;
	getf.sig r18=f8			// r18 now contains the (lost + 1) * itm_delta
	;;
	sub r18=r24, r18		// r18 is last_tick 
	mov r25=ar.itc			// put time stamp into r25 (ITC) == now
	;;
	cmp.leu p7, p8 = r18, r25	// if last_tick <= now, p7 = 1
	;;
(p7)	ld8 r24=[r30]			// r24 contains local_cpu_data->nsec_per_cyc value
(p7)	sub r25=r25, r18		// elasped_cycles in r25
	;;
(p7)	setf.sig f6=r24
(p7)	setf.sig f7=r25
	;;
(p7)	xma.l f8=f6, f7, f0

	;;
(p7)	getf.sig r18=f8			// r18 = clasped_cycles * local_cpu_data->nsec_per_cyc
	;;	
(p7)	shr.u r18=r18, IA64_NSEC_PER_CYC_SHIFT
	
(p8) 	ld8 r18=[r31]			// r18 = last_time_offset (is unsigned long)

	// now end of gettimeoffset, r18 should contain the desire result (offset)

	// *** if (unlikely(read_seqretry(&xtime_lock, seq))) continue; ***

	;; 				// barrier()
	
#ifdef CONFIG_SMP
	mf
#endif
	adds r24=1, r0			// r24 = 1
	ld4 r25=[r17]			// r25 = xtime_lock->sequence (load again)
	;;
	and r24=r24, r23		// r24 = seq & 1
	xor r25=r25, r23		// r25 = xtime_lock->sequence ^ seq
	;;
	or r24=r24, r25			// now r24 = read_seqretry(&xtime_lock, seq)
	;;
	cmp.ne p7, p0=r24, r0
	;;
(p7)	br.spnt.many while_loop_1	// continue
	
	cmp.leu p7, p8 = r18, r14	// if (offset <= old)
	;;
(p7)	mov r18=r14			// offset = old
(p7)	br.spnt.few loop_exit_1		// break
	
	mov ar.ccv=r18			// ar.ccv = offset
	;;
	cmpxchg8.acq r25=[r31], r14, ar.ccv	
					// compare-and-exchange (atomic!)
	;;
	cmp.eq p8,p0 = r25, r14	
	;;
(p8)	br.sptk.many loop_exit_1
	br.sptk.many while_loop_1

loop_exit_1:

	// at this point, r28 is nsec and r18 is offset

	add r3=r28, r18			// r3 = (nsec + offset)
	;;
	// now we try to divide r3 by 1000 to get the value in usec instead of nsec
	
	shr.u r24 = r3, 3
	;;
	setf.sig f7 = r24
	;;
	xmpy.hu f6 = f7, f9
	;;
	getf.sig r3 = f6
	;;
	shr.u r3 = r3, 4
	// end of division, r3 is divided by 1000 (=usec)
	
	addl r24=1000000, r0		// r24 = 1000000
	;;
	
while_loop_2:
	
	cmp.geu p7, p8=r3, r24		// while (usec >= 1000000)
	;;
(p8)	br.sptk.many loop_exit_2

	sub r3=r3, r24			// usec -= 1000000
	adds r2=1, r2			// ++sec
	
	br.many while_loop_2
	
loop_exit_2:	 
	
	// finally, 	r2 = sec
	// 		r3 = usec
	
	mov r24=r32			// we need to preserve this...
	;;
	st8 [r32]=r2, 8
	;;
	st8 [r32]=r3			// store them in the timeval struct
	;;
	mov r32=r24
	
	mov r8=r0			// success
	
	MCKINLEY_E9_WORKAROUND
	
	br.ret.sptk.many b6
	// return to caller

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
