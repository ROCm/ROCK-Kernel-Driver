/*
 * This file contains the light-weight system call handlers (fsyscall-handlers).
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 * 	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 25-Sep-03 davidm	Implement fsys_rt_sigprocmask().
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
#include <asm/signal.h>
#include <asm/system.h>
#include <asm/unistd.h>

#include "entry.h"

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
	.prologue
	.altrp b6
	.body
	mov r8=ENOSYS
	mov r10=-1
	FSYS_RETURN
END(fsys_ni_syscall)

ENTRY(fsys_getpid)
	.prologue
	.altrp b6
	.body
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
	FSYS_RETURN
END(fsys_getpid)

ENTRY(fsys_getppid)
	.prologue
	.altrp b6
	.body
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
	FSYS_RETURN
END(fsys_getppid)

ENTRY(fsys_set_tid_address)
	.prologue
	.altrp b6
	.body
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
	FSYS_RETURN
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
	.prologue
	.altrp b6
	.body
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	addl r3=THIS_CPU(cpu_info),r0

	mov.m r31=ar.itc		// put time stamp into r31 (ITC) == now		(35 cyc)
#ifdef CONFIG_SMP
	movl r10=__per_cpu_offset
	movl r2=sal_platform_features
	;;

	ld8 r2=[r2]
	movl r19=xtime			// xtime is a timespec struct

	ld8 r10=[r10]			// r10 <- __per_cpu_offset[0]
	addl r21=THIS_CPU(cpu_info),r0
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
(p6)	br.cond.spnt.few .fail_einval

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
EX(.fail_efault, probe.w.fault r32, 3)		// this must come _after_ NaT-check
EX(.fail_efault, probe.w.fault r10, 3)		// this must come _after_ NaT-check
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
EX(.fail_efault, st8 [r32]=r2)
	adds r9=8, r32
	mov r8=r0			// success
	;;
EX(.fail_efault, st8 [r9]=r3)		// store them in the timeval struct
	mov r10=0
	FSYS_RETURN
	/*
	 * Note: We are NOT clearing the scratch registers here.  Since the only things
	 *	 in those registers are time-related variables and some addresses (which
	 *	 can be obtained from System.map), none of this should be security-sensitive
	 *	 and we should be fine.
	 */

.fail_einval:
	mov r8=EINVAL			// r8 = EINVAL
	mov r10=-1			// r10 = -1
	FSYS_RETURN

.fail_efault:
	mov r8=EFAULT			// r8 = EFAULT
	mov r10=-1			// r10 = -1
	FSYS_RETURN
END(fsys_gettimeofday)

/*
 * long fsys_rt_sigprocmask (int how, sigset_t *set, sigset_t *oset, size_t sigsetsize).
 */
#if _NSIG_WORDS != 1
# error Sorry, fsys_rt_sigprocmask() needs to be updated for _NSIG_WORDS != 1.
#endif
ENTRY(fsys_rt_sigprocmask)
	.prologue
	.altrp b6
	.body

	mf					// ensure reading of current->blocked is ordered
	add r2=IA64_TASK_BLOCKED_OFFSET,r16
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;
	/*
	 * Since we're only reading a single word, we can do it
	 * atomically without acquiring current->sighand->siglock.  To
	 * be on the safe side, we need a fully-ordered load, though:
	 */
	ld8.acq r3=[r2]				// read/prefetch current->blocked
	ld4 r9=[r9]
	add r31=IA64_TASK_SIGHAND_OFFSET,r16
	;;
#ifdef CONFIG_SMP
	ld8 r31=[r31]				// r31 <- current->sighand
#endif
	and r9=TIF_ALLWORK_MASK,r9
	tnat.nz p6,p0=r32
	;;
	cmp.ne p7,p0=0,r9
	tnat.nz.or p6,p0=r35
	tnat.nz p8,p0=r34
	;;
	cmp.ne p15,p0=r0,r34			// oset != NULL?
	cmp.ne.or p6,p0=_NSIG_WORDS*8,r35
	tnat.nz.or p8,p0=r33

(p6)	br.spnt.few .fail_einval		// fail with EINVAL
(p7)	br.spnt.many fsys_fallback_syscall	// got pending kernel work...
(p8)	br.spnt.few .fail_efault		// fail with EFAULT
	;;

	cmp.eq p6,p7=r0,r33			// set == NULL?
	add r31=IA64_SIGHAND_SIGLOCK_OFFSET,r31	// r31 <- current->sighand->siglock
(p6)	br.dpnt.many .store_mask		// -> short-circuit to just reading the signal mask

	/* Argh, we actually have to do some work and _update_ the signal mask: */

EX(.fail_efault, probe.r.fault r33, 3)		// verify user has read-access to *set
EX(.fail_efault, ld8 r14=[r33])			// r14 <- *set
	mov r17=(1 << (SIGKILL - 1)) | (1 << (SIGSTOP - 1))
	;;

	rsm psr.i				// mask interrupt delivery
	mov ar.ccv=0
	andcm r14=r14,r17			// filter out SIGKILL & SIGSTOP

#ifdef CONFIG_SMP
	mov r17=1
	;;
	cmpxchg4.acq r18=[r31],r17,ar.ccv	// try to acquire the lock
	mov r8=EINVAL			// default to EINVAL
	;;
	ld8 r3=[r2]			// re-read current->blocked now that we hold the lock
	cmp4.ne p6,p0=r18,r0
(p6)	br.cond.spnt.many .lock_contention
	;;
#else
	ld8 r3=[r2]			// re-read current->blocked now that we hold the lock
	mov r8=EINVAL			// default to EINVAL
#endif
	add r18=IA64_TASK_PENDING_OFFSET+IA64_SIGPENDING_SIGNAL_OFFSET,r16
	add r19=IA64_TASK_SIGNAL_OFFSET,r16
	cmp4.eq p6,p0=SIG_BLOCK,r32
	;;
	ld8 r19=[r19]			// r19 <- current->signal
	cmp4.eq p7,p0=SIG_UNBLOCK,r32
	cmp4.eq p8,p0=SIG_SETMASK,r32
	;;
	ld8 r18=[r18]			// r18 <- current->pending.signal
	.pred.rel.mutex p6,p7,p8
(p6)	or r14=r3,r14			// SIG_BLOCK
(p7)	andcm r14=r3,r14		// SIG_UNBLOCK

(p8)	mov r14=r14			// SIG_SETMASK
(p6)	mov r8=0			// clear error code
	// recalc_sigpending()
	add r17=IA64_SIGNAL_GROUP_STOP_COUNT_OFFSET,r19

	add r19=IA64_SIGNAL_SHARED_PENDING_OFFSET+IA64_SIGPENDING_SIGNAL_OFFSET,r19
	;;
	ld4 r17=[r17]		// r17 <- current->signal->group_stop_count
(p7)	mov r8=0		// clear error code

	ld8 r19=[r19]		// r19 <- current->signal->shared_pending
	;;
	cmp4.gt p6,p7=r17,r0	// p6/p7 <- (current->signal->group_stop_count > 0)?
(p8)	mov r8=0		// clear error code

	or r18=r18,r19		// r18 <- current->pending | current->signal->shared_pending
	;;
	// r18 <- (current->pending | current->signal->shared_pending) & ~current->blocked:
	andcm r18=r18,r14
	add r9=TI_FLAGS+IA64_TASK_SIZE,r16
	;;

(p7)	cmp.ne.or.andcm p6,p7=r18,r0		// p6/p7 <- signal pending
	mov r19=0					// i must not leak kernel bits...
(p6)	br.cond.dpnt.many .sig_pending
	;;

1:	ld4 r17=[r9]				// r17 <- current->thread_info->flags
	;;
	mov ar.ccv=r17
	and r18=~_TIF_SIGPENDING,r17		// r18 <- r17 & ~(1 << TIF_SIGPENDING)
	;;

	st8 [r2]=r14				// update current->blocked with new mask
	cmpxchg4.acq r14=[r9],r18,ar.ccv	// current->thread_info->flags <- r18
	;;
	cmp.ne p6,p0=r17,r14			// update failed?
(p6)	br.cond.spnt.few 1b			// yes -> retry

#ifdef CONFIG_SMP
	st4.rel [r31]=r0			// release the lock
#endif
	ssm psr.i
	cmp.ne p9,p0=r8,r0			// check for bad HOW value
	;;

	srlz.d					// ensure psr.i is set again
	mov r18=0					// i must not leak kernel bits...
(p9)	br.spnt.few .fail_einval		// bail out for bad HOW value

.store_mask:
EX(.fail_efault, (p15) probe.w.fault r34, 3)	// verify user has write-access to *oset
EX(.fail_efault, (p15) st8 [r34]=r3)
	mov r2=0					// i must not leak kernel bits...
	mov r3=0					// i must not leak kernel bits...
	mov r8=0				// return 0
	mov r9=0					// i must not leak kernel bits...
	mov r14=0					// i must not leak kernel bits...
	mov r17=0					// i must not leak kernel bits...
	mov r31=0					// i must not leak kernel bits...
	FSYS_RETURN

.sig_pending:
#ifdef CONFIG_SMP
	st4.rel [r31]=r0			// release the lock
#endif
	ssm psr.i
	;;
	srlz.d
	br.sptk.many fsys_fallback_syscall	// with signal pending, do the heavy-weight syscall

#ifdef CONFIG_SMP
.lock_contention:
	/* Rather than spinning here, fall back on doing a heavy-weight syscall.  */
	ssm psr.i
	;;
	srlz.d
	br.sptk.many fsys_fallback_syscall
#endif
END(fsys_rt_sigprocmask)

ENTRY(fsys_fallback_syscall)
	.prologue
	.altrp b6
	.body
	/*
	 * We only get here from light-weight syscall handlers.  Thus, we already
	 * know that r15 contains a valid syscall number.  No need to re-check.
	 */
	adds r17=-1024,r15
	movl r14=sys_call_table
	;;
	shladd r18=r17,3,r14
	;;
	ld8 r18=[r18]				// load normal (heavy-weight) syscall entry-point
	mov r29=psr				// read psr (12 cyc load latency)
	mov r27=ar.rsc
	mov r21=ar.fpsr
	mov r26=ar.pfs
END(fsys_fallback_syscall)
	/* FALL THROUGH */
GLOBAL_ENTRY(fsys_bubble_down)
	.prologue
	.altrp b6
	.body
	/*
	 * We get here for syscalls that don't have a lightweight handler.  For those, we
	 * need to bubble down into the kernel and that requires setting up a minimal
	 * pt_regs structure, and initializing the CPU state more or less as if an
	 * interruption had occurred.  To make syscall-restarts work, we setup pt_regs
	 * such that cr_iip points to the second instruction in syscall_via_break.
	 * Decrementing the IP hence will restart the syscall via break and not
	 * decrementing IP will return us to the caller, as usual.  Note that we preserve
	 * the value of psr.pp rather than initializing it from dcr.pp.  This makes it
	 * possible to distinguish fsyscall execution from other privileged execution.
	 *
	 * On entry:
	 *	- normal fsyscall handler register usage, except that we also have:
	 *	- r18: address of syscall entry point
	 *	- r21: ar.fpsr
	 *	- r26: ar.pfs
	 *	- r27: ar.rsc
	 *	- r29: psr
	 */
#	define PSR_PRESERVED_BITS	(IA64_PSR_UP | IA64_PSR_MFL | IA64_PSR_MFH | IA64_PSR_PK \
					 | IA64_PSR_DT | IA64_PSR_PP | IA64_PSR_SP | IA64_PSR_RT \
					 | IA64_PSR_IC)
	/*
	 * Reading psr.l gives us only bits 0-31, psr.it, and psr.mc.  The rest we have
	 * to synthesize.
	 */
#	define PSR_ONE_BITS		((3 << IA64_PSR_CPL0_BIT) | (0x1 << IA64_PSR_RI_BIT) \
					 | IA64_PSR_BN)

	invala
	movl r8=PSR_ONE_BITS

	mov r25=ar.unat			// save ar.unat (5 cyc)
	movl r9=PSR_PRESERVED_BITS

	mov ar.rsc=0			// set enforced lazy mode, pl 0, little-endian, loadrs=0
	movl r28=__kernel_syscall_via_break
	;;
	mov r23=ar.bspstore		// save ar.bspstore (12 cyc)
	mov r31=pr			// save pr (2 cyc)
	mov r20=r1			// save caller's gp in r20
	;;
	mov r2=r16			// copy current task addr to addl-addressable register
	and r9=r9,r29
	mov r19=b6			// save b6 (2 cyc)
	;;
	mov psr.l=r9			// slam the door (17 cyc to srlz.i)
	or r29=r8,r29			// construct cr.ipsr value to save
	addl r22=IA64_RBS_OFFSET,r2	// compute base of RBS
	;;
	mov.m r24=ar.rnat		// read ar.rnat (5 cyc lat)
	lfetch.fault.excl.nt1 [r22]
	adds r16=IA64_TASK_THREAD_ON_USTACK_OFFSET,r2

	// ensure previous insn group is issued before we stall for srlz.i:
	;;
	srlz.i				// ensure new psr.l has been established
	/////////////////////////////////////////////////////////////////////////////
	////////// from this point on, execution is not interruptible anymore
	/////////////////////////////////////////////////////////////////////////////
	addl r1=IA64_STK_OFFSET-IA64_PT_REGS_SIZE,r2	// compute base of memory stack
	cmp.ne pKStk,pUStk=r0,r0	// set pKStk <- 0, pUStk <- 1
	;;
	st1 [r16]=r0			// clear current->thread.on_ustack flag
	mov ar.bspstore=r22		// switch to kernel RBS
	mov b6=r18			// copy syscall entry-point to b6 (7 cyc)
	add r3=TI_FLAGS+IA64_TASK_SIZE,r2
	;;
	ld4 r3=[r3]				// r2 = current_thread_info()->flags
	mov r18=ar.bsp			// save (kernel) ar.bsp (12 cyc)
	mov ar.rsc=0x3			// set eager mode, pl 0, little-endian, loadrs=0
	br.call.sptk.many b7=ia64_syscall_setup
	;;
	ssm psr.i
	movl r2=ia64_ret_from_syscall
	;;
	mov rp=r2				// set the real return addr
	tbit.z p8,p0=r3,TIF_SYSCALL_TRACE

(p8)	br.call.sptk.many b6=b6			// ignore this return addr
	br.cond.sptk ia64_trace_syscall
END(fsys_bubble_down)

	.rodata
	.align 8
	.globl fsyscall_table

	data8 fsys_bubble_down
fsyscall_table:
	data8 fsys_ni_syscall
	data8 0				// exit			// 1025
	data8 0				// read
	data8 0				// write
	data8 0				// open
	data8 0				// close
	data8 0				// creat		// 1030
	data8 0				// link
	data8 0				// unlink
	data8 0				// execve
	data8 0				// chdir
	data8 0				// fchdir		// 1035
	data8 0				// utimes
	data8 0				// mknod
	data8 0				// chmod
	data8 0				// chown
	data8 0				// lseek		// 1040
	data8 fsys_getpid		// getpid
	data8 fsys_getppid		// getppid
	data8 0				// mount
	data8 0				// umount
	data8 0				// setuid		// 1045
	data8 0				// getuid
	data8 0				// geteuid
	data8 0				// ptrace
	data8 0				// access
	data8 0				// sync			// 1050
	data8 0				// fsync
	data8 0				// fdatasync
	data8 0				// kill
	data8 0				// rename
	data8 0				// mkdir		// 1055
	data8 0				// rmdir
	data8 0				// dup
	data8 0				// pipe
	data8 0				// times
	data8 0				// brk			// 1060
	data8 0				// setgid
	data8 0				// getgid
	data8 0				// getegid
	data8 0				// acct
	data8 0				// ioctl		// 1065
	data8 0				// fcntl
	data8 0				// umask
	data8 0				// chroot
	data8 0				// ustat
	data8 0				// dup2			// 1070
	data8 0				// setreuid
	data8 0				// setregid
	data8 0				// getresuid
	data8 0				// setresuid
	data8 0				// getresgid		// 1075
	data8 0				// setresgid
	data8 0				// getgroups
	data8 0				// setgroups
	data8 0				// getpgid
	data8 0				// setpgid		// 1080
	data8 0				// setsid
	data8 0				// getsid
	data8 0				// sethostname
	data8 0				// setrlimit
	data8 0				// getrlimit		// 1085
	data8 0				// getrusage
	data8 fsys_gettimeofday		// gettimeofday
	data8 0				// settimeofday
	data8 0				// select
	data8 0				// poll			// 1090
	data8 0				// symlink
	data8 0				// readlink
	data8 0				// uselib
	data8 0				// swapon
	data8 0				// swapoff		// 1095
	data8 0				// reboot
	data8 0				// truncate
	data8 0				// ftruncate
	data8 0				// fchmod
	data8 0				// fchown		// 1100
	data8 0				// getpriority
	data8 0				// setpriority
	data8 0				// statfs
	data8 0				// fstatfs
	data8 0				// gettid		// 1105
	data8 0				// semget
	data8 0				// semop
	data8 0				// semctl
	data8 0				// msgget
	data8 0				// msgsnd		// 1110
	data8 0				// msgrcv
	data8 0				// msgctl
	data8 0				// shmget
	data8 0				// shmat
	data8 0				// shmdt		// 1115
	data8 0				// shmctl
	data8 0				// syslog
	data8 0				// setitimer
	data8 0				// getitimer
	data8 0					 		// 1120
	data8 0
	data8 0
	data8 0				// vhangup
	data8 0				// lchown
	data8 0				// remap_file_pages	// 1125
	data8 0				// wait4
	data8 0				// sysinfo
	data8 0				// clone
	data8 0				// setdomainname
	data8 0				// newuname		// 1130
	data8 0				// adjtimex
	data8 0
	data8 0				// init_module
	data8 0				// delete_module
	data8 0							// 1135
	data8 0
	data8 0				// quotactl
	data8 0				// bdflush
	data8 0				// sysfs
	data8 0				// personality		// 1140
	data8 0				// afs_syscall
	data8 0				// setfsuid
	data8 0				// setfsgid
	data8 0				// getdents
	data8 0				// flock		// 1145
	data8 0				// readv
	data8 0				// writev
	data8 0				// pread64
	data8 0				// pwrite64
	data8 0				// sysctl		// 1150
	data8 0				// mmap
	data8 0				// munmap
	data8 0				// mlock
	data8 0				// mlockall
	data8 0				// mprotect		// 1155
	data8 0				// mremap
	data8 0				// msync
	data8 0				// munlock
	data8 0				// munlockall
	data8 0				// sched_getparam	// 1160
	data8 0				// sched_setparam
	data8 0				// sched_getscheduler
	data8 0				// sched_setscheduler
	data8 0				// sched_yield
	data8 0				// sched_get_priority_max	// 1165
	data8 0				// sched_get_priority_min
	data8 0				// sched_rr_get_interval
	data8 0				// nanosleep
	data8 0				// nfsservctl
	data8 0				// prctl		// 1170
	data8 0				// getpagesize
	data8 0				// mmap2
	data8 0				// pciconfig_read
	data8 0				// pciconfig_write
	data8 0				// perfmonctl		// 1175
	data8 0				// sigaltstack
	data8 0				// rt_sigaction
	data8 0				// rt_sigpending
	data8 fsys_rt_sigprocmask	// rt_sigprocmask
	data8 0				// rt_sigqueueinfo	// 1180
	data8 0				// rt_sigreturn
	data8 0				// rt_sigsuspend
	data8 0				// rt_sigtimedwait
	data8 0				// getcwd
	data8 0				// capget		// 1185
	data8 0				// capset
	data8 0				// sendfile
	data8 0
	data8 0
	data8 0				// socket		// 1190
	data8 0				// bind
	data8 0				// connect
	data8 0				// listen
	data8 0				// accept
	data8 0				// getsockname		// 1195
	data8 0				// getpeername
	data8 0				// socketpair
	data8 0				// send
	data8 0				// sendto
	data8 0				// recv			// 1200
	data8 0				// recvfrom
	data8 0				// shutdown
	data8 0				// setsockopt
	data8 0				// getsockopt
	data8 0				// sendmsg		// 1205
	data8 0				// recvmsg
	data8 0				// pivot_root
	data8 0				// mincore
	data8 0				// madvise
	data8 0				// newstat		// 1210
	data8 0				// newlstat
	data8 0				// newfstat
	data8 0				// clone2
	data8 0				// getdents64
	data8 0				// getunwind		// 1215
	data8 0				// readahead
	data8 0				// setxattr
	data8 0				// lsetxattr
	data8 0				// fsetxattr
	data8 0				// getxattr		// 1220
	data8 0				// lgetxattr
	data8 0				// fgetxattr
	data8 0				// listxattr
	data8 0				// llistxattr
	data8 0				// flistxattr		// 1225
	data8 0				// removexattr
	data8 0				// lremovexattr
	data8 0				// fremovexattr
	data8 0				// tkill
	data8 0				// futex		// 1230
	data8 0				// sched_setaffinity
	data8 0				// sched_getaffinity
	data8 fsys_set_tid_address	// set_tid_address
	data8 0				// fadvise64_64
	data8 0				// tgkill		// 1235
	data8 0				// exit_group
	data8 0				// lookup_dcookie
	data8 0				// io_setup
	data8 0				// io_destroy
	data8 0				// io_getevents		// 1240
	data8 0				// io_submit
	data8 0				// io_cancel
	data8 0				// epoll_create
	data8 0				// epoll_ctl
	data8 0				// epoll_wait		// 1245
	data8 0				// restart_syscall
	data8 0				// semtimedop
	data8 0				// timer_create
	data8 0				// timer_settime
	data8 0				// timer_gettime 	// 1250
	data8 0				// timer_getoverrun
	data8 0				// timer_delete
	data8 0				// clock_settime
	data8 0				// clock_gettime
	data8 0				// clock_getres		// 1255
	data8 0				// clock_nanosleep
	data8 0				// fstatfs64
	data8 0				// statfs64
	data8 0
	data8 0							// 1260
	data8 0
	data8 0				// mq_open
	data8 0				// mq_unlink
	data8 0				// mq_timedsend
	data8 0				// mq_timedreceive	// 1265
	data8 0				// mq_notify
	data8 0				// mq_getsetattr
	data8 0
	data8 0
	data8 0							// 1270
	data8 0
	data8 0
	data8 0
	data8 0
	data8 0							// 1275
	data8 0
	data8 0
	data8 0
	data8 0

	.org fsyscall_table + 8*NR_syscalls	// guard against failures to increase NR_syscalls
