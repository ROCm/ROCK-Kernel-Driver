/*
 * ia64/kernel/entry.S
 *
 * Kernel entry points.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <Don.Dugger@intel.com>
 */
/*
 * ia64_switch_to now places correct virtual mapping in in TR2 for
 * kernel stack. This allows us to handle interrupts without changing
 * to physical mode.
 *
 * ar.k4 is now used to hold last virtual map address
 * 
 * Jonathan Nickin	<nicklin@missioncriticallinux.com>
 * Patrick O'Rourke	<orourke@missioncriticallinux.com>
 * 11/07/2000
 /
/*
 * Global (preserved) predicate usage on syscall entry/exit path:
 *
 *	pKern:		See entry.h.
 *	pSys:		See entry.h.
 *	pNonSys:	!pSys
 *	p2:		(Alias of pKern!) True if any signals are pending.
 */

#include <linux/config.h>

#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/offsets.h>
#include <asm/processor.h>
#include <asm/unistd.h>
#include <asm/asmmacro.h>
#include <asm/pgtable.h>
	
#include "entry.h"

	.text
	.psr abi64
	.psr lsb
	.lsb

	/*
	 * execve() is special because in case of success, we need to
	 * setup a null register window frame.
	 */
ENTRY(ia64_execve)
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(3))
	alloc loc1=ar.pfs,3,2,4,0
	mov loc0=rp
	UNW(.body)
	mov out0=in0			// filename
	;;				// stop bit between alloc and call
	mov out1=in1			// argv
	mov out2=in2			// envp
	add out3=16,sp			// regs
	br.call.sptk.few rp=sys_execve
.ret0:	cmp4.ge p6,p0=r8,r0
	mov ar.pfs=loc1			// restore ar.pfs
	;;
(p6)	mov ar.pfs=r0			// clear ar.pfs in case of success
	sxt4 r8=r8			// return 64-bit result
	mov rp=loc0

	br.ret.sptk.few rp
END(ia64_execve)

GLOBAL_ENTRY(sys_clone2)
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2))
	alloc r16=ar.pfs,3,2,4,0
	DO_SAVE_SWITCH_STACK
	mov loc0=rp
	mov loc1=r16				// save ar.pfs across do_fork
	UNW(.body)
	mov out1=in1
	mov out3=in2
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp	// out2 = &regs
	mov out0=in0				// out0 = clone_flags
	br.call.sptk.few rp=do_fork
.ret1:	UNW(.restore sp)
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop the switch stack
	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(sys_clone2)

GLOBAL_ENTRY(sys_clone)
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2))
	alloc r16=ar.pfs,2,2,4,0
	DO_SAVE_SWITCH_STACK
	mov loc0=rp
	mov loc1=r16				// save ar.pfs across do_fork
	UNW(.body)
	mov out1=in1
	mov out3=0
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp	// out2 = &regs
	mov out0=in0				// out0 = clone_flags
	br.call.sptk.few rp=do_fork
.ret2:	UNW(.restore sp)
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop the switch stack
	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(sys_clone)

#define KSTACK_TR	2

/*
 * prev_task <- ia64_switch_to(struct task_struct *next)
 */
GLOBAL_ENTRY(ia64_switch_to)
	UNW(.prologue)
	alloc r16=ar.pfs,1,0,0,0
	DO_SAVE_SWITCH_STACK
	UNW(.body)

	adds r22=IA64_TASK_THREAD_KSP_OFFSET,r13
	mov r27=ar.k4
	dep r20=0,in0,61,3		// physical address of "current"
	;;
	st8 [r22]=sp			// save kernel stack pointer of old task
	shr.u r26=r20,_PAGE_SIZE_256M
	;;
	cmp.eq p7,p6=r26,r0		// check < 256M
	adds r21=IA64_TASK_THREAD_KSP_OFFSET,in0
	;;
	/*
	 * If we've already mapped this task's page, we can skip doing it
	 * again.
	 */
(p6)	cmp.eq p7,p6=r26,r27
(p6)	br.cond.dpnt.few .map
	;;
.done:	ld8 sp=[r21]			// load kernel stack pointer of new task
(p6)	ssm psr.ic			// if we we had to map, renable the psr.ic bit FIRST!!!
	;;
(p6)	srlz.d
	mov ar.k6=r20			// copy "current" into ar.k6
	mov r8=r13			// return pointer to previously running task
	mov r13=in0			// set "current" pointer
	;;
(p6)	ssm psr.i			// renable psr.i AFTER the ic bit is serialized
	DO_LOAD_SWITCH_STACK( )

#ifdef CONFIG_SMP
	sync.i				// ensure "fc"s done by this CPU are visible on other CPUs
#endif 
	br.ret.sptk.few rp		// boogie on out in new context

.map:
	rsm psr.i | psr.ic
	movl r25=__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RWX
	;;
	srlz.d
	or r23=r25,r20			// construct PA | page properties
	mov r25=_PAGE_SIZE_256M<<2
	;;
	mov cr.itir=r25
	mov cr.ifa=in0			// VA of next task...
	;;
	mov r25=KSTACK_TR		// use tr entry #2...
	mov ar.k4=r26			// remember last page we mapped...
	;;
	itr.d dtr[r25]=r23		// wire in new mapping...
	br.cond.sptk.many .done
	;;
END(ia64_switch_to)

#ifndef CONFIG_IA64_NEW_UNWIND
	/*
	 * Like save_switch_stack, but also save the stack frame that is active
	 * at the time this function is called.
	 */
ENTRY(save_switch_stack_with_current_frame)
	UNW(.prologue)
	alloc r16=ar.pfs,0,0,0,0		// pass ar.pfs to save_switch_stack
	DO_SAVE_SWITCH_STACK
	br.ret.sptk.few rp
END(save_switch_stack_with_current_frame)
#endif /* !CONFIG_IA64_NEW_UNWIND */

/*
 * Note that interrupts are enabled during save_switch_stack and
 * load_switch_stack.  This means that we may get an interrupt with
 * "sp" pointing to the new kernel stack while ar.bspstore is still
 * pointing to the old kernel backing store area.  Since ar.rsc,
 * ar.rnat, ar.bsp, and ar.bspstore are all preserved by interrupts,
 * this is not a problem.  Also, we don't need to specify unwind
 * information for preserved registers that are not modified in
 * save_switch_stack as the right unwind information is already
 * specified at the call-site of save_switch_stack.
 */

/*
 * save_switch_stack:
 *	- r16 holds ar.pfs
 *	- b7 holds address to return to
 *	- rp (b0) holds return address to save
 */
GLOBAL_ENTRY(save_switch_stack)
	UNW(.prologue)
	UNW(.altrp b7)
	flushrs			// flush dirty regs to backing store (must be first in insn group)
	mov r17=ar.unat		// preserve caller's
	adds r2=16,sp		// r2 = &sw->caller_unat
	;;
	mov r18=ar.fpsr		// preserve fpsr
	mov ar.rsc=r0		// put RSE in mode: enforced lazy, little endian, pl 0
	;;
	mov r19=ar.rnat
	adds r3=24,sp		// r3 = &sw->ar_fpsr
	;;
	.savesp ar.unat,SW(CALLER_UNAT)
	st8 [r2]=r17,16
	.savesp ar.fpsr,SW(AR_FPSR)
	st8 [r3]=r18,24
	;;
	UNW(.body)
	stf.spill [r2]=f2,32
	stf.spill [r3]=f3,32
	mov r21=b0
	;;
	stf.spill [r2]=f4,32
	stf.spill [r3]=f5,32
	;;
	stf.spill [r2]=f10,32
	stf.spill [r3]=f11,32
	mov r22=b1
	;;
	stf.spill [r2]=f12,32
	stf.spill [r3]=f13,32
	mov r23=b2
	;;
	stf.spill [r2]=f14,32
	stf.spill [r3]=f15,32
	mov r24=b3
	;;
	stf.spill [r2]=f16,32
	stf.spill [r3]=f17,32
	mov r25=b4
	;;
	stf.spill [r2]=f18,32
	stf.spill [r3]=f19,32
	mov r26=b5
	;;
	stf.spill [r2]=f20,32
	stf.spill [r3]=f21,32
	mov r17=ar.lc				// I-unit
	;;
	stf.spill [r2]=f22,32
	stf.spill [r3]=f23,32
	;;
	stf.spill [r2]=f24,32
	stf.spill [r3]=f25,32
	;;
	stf.spill [r2]=f26,32
	stf.spill [r3]=f27,32
	;;
	stf.spill [r2]=f28,32
	stf.spill [r3]=f29,32
	;;
	stf.spill [r2]=f30,32
	stf.spill [r3]=f31,24
	;;
.mem.offset 0,0;	st8.spill [r2]=r4,16
.mem.offset 8,0;	st8.spill [r3]=r5,16
	;;
.mem.offset 0,0;	st8.spill [r2]=r6,16
.mem.offset 8,0;	st8.spill [r3]=r7,16
	;;
	st8 [r2]=r21,16		// save b0
	st8 [r3]=r22,16		// save b1
	/* since we're done with the spills, read and save ar.unat: */
	mov r18=ar.unat		// M-unit
	mov r20=ar.bspstore	// M-unit
	;;
	st8 [r2]=r23,16		// save b2
	st8 [r3]=r24,16		// save b3
	;;
	st8 [r2]=r25,16		// save b4
	st8 [r3]=r26,16		// save b5
	;;
	st8 [r2]=r16,16		// save ar.pfs
	st8 [r3]=r17,16		// save ar.lc
	mov r21=pr
	;;
	st8 [r2]=r18,16		// save ar.unat
	st8 [r3]=r19,16		// save ar.rnat
	mov b7=r28
	;;
	st8 [r2]=r20		// save ar.bspstore
	st8 [r3]=r21		// save predicate registers
	mov ar.rsc=3		// put RSE back into eager mode, pl 0
	br.cond.sptk.few b7
END(save_switch_stack)

/*
 * load_switch_stack:
 *	- b7 holds address to return to
 */
ENTRY(load_switch_stack)
	UNW(.prologue)
	UNW(.altrp b7)
	invala			// invalidate ALAT
	UNW(.body)
	adds r2=IA64_SWITCH_STACK_B0_OFFSET+16,sp	// get pointer to switch_stack.b0
	mov ar.rsc=r0		// put RSE into enforced lazy mode
	adds r3=IA64_SWITCH_STACK_B0_OFFSET+24,sp	// get pointer to switch_stack.b1
	;;
	ld8 r21=[r2],16		// restore b0
	ld8 r22=[r3],16		// restore b1
	;;
	ld8 r23=[r2],16		// restore b2
	ld8 r24=[r3],16		// restore b3
	;;
	ld8 r25=[r2],16		// restore b4
	ld8 r26=[r3],16		// restore b5
	;;
	ld8 r16=[r2],16		// restore ar.pfs
	ld8 r17=[r3],16		// restore ar.lc
	;;
	ld8 r18=[r2],16		// restore ar.unat
	ld8 r19=[r3],16		// restore ar.rnat
	mov b0=r21
	;;
	ld8 r20=[r2]		// restore ar.bspstore
	ld8 r21=[r3]		// restore predicate registers
	mov ar.pfs=r16
	;;
	mov ar.bspstore=r20
	;;
	loadrs			// invalidate stacked regs outside current frame
	adds r2=16-IA64_SWITCH_STACK_SIZE,r2	// get pointer to switch_stack.caller_unat
	;;			// stop bit for rnat dependency
	mov ar.rnat=r19
	mov ar.unat=r18		// establish unat holding the NaT bits for r4-r7
	adds r3=16-IA64_SWITCH_STACK_SIZE,r3	// get pointer to switch_stack.ar_fpsr
	;;
	ld8 r18=[r2],16		// restore caller's unat
	ld8 r19=[r3],24		// restore fpsr
	mov ar.lc=r17
	;;
	ldf.fill f2=[r2],32
	ldf.fill f3=[r3],32
	mov pr=r21,-1
	;;
	ldf.fill f4=[r2],32
	ldf.fill f5=[r3],32
	;;
	ldf.fill f10=[r2],32
	ldf.fill f11=[r3],32
	mov b1=r22
	;;
	ldf.fill f12=[r2],32
	ldf.fill f13=[r3],32
	mov b2=r23
	;;
	ldf.fill f14=[r2],32
	ldf.fill f15=[r3],32
	mov b3=r24
	;;
	ldf.fill f16=[r2],32
	ldf.fill f17=[r3],32
	mov b4=r25
	;;
	ldf.fill f18=[r2],32
	ldf.fill f19=[r3],32
	mov b5=r26
	;;
	ldf.fill f20=[r2],32
	ldf.fill f21=[r3],32
	;;
	ldf.fill f22=[r2],32
	ldf.fill f23=[r3],32
	;;
	ldf.fill f24=[r2],32
	ldf.fill f25=[r3],32
	;;
	ldf.fill f26=[r2],32
	ldf.fill f27=[r3],32
	;;
	ldf.fill f28=[r2],32
	ldf.fill f29=[r3],32
	;;
	ldf.fill f30=[r2],32
	ldf.fill f31=[r3],24
	;;
	ld8.fill r4=[r2],16
	ld8.fill r5=[r3],16
	;;
	ld8.fill r6=[r2],16
	ld8.fill r7=[r3],16
	mov ar.unat=r18				// restore caller's unat
	mov ar.fpsr=r19				// restore fpsr
	mov ar.rsc=3				// put RSE back into eager mode, pl 0
	br.cond.sptk.few b7
END(load_switch_stack)

GLOBAL_ENTRY(__ia64_syscall)
	.regstk 6,0,0,0
	mov r15=in5				// put syscall number in place
	break __BREAK_SYSCALL
	movl r2=errno
	cmp.eq p6,p7=-1,r10
	;;
(p6)	st4 [r2]=r8
(p6)	mov r8=-1
	br.ret.sptk.few rp
END(__ia64_syscall)

	//
	// We invoke syscall_trace through this intermediate function to
	// ensure that the syscall input arguments are not clobbered.  We
	// also use it to preserve b6, which contains the syscall entry point.
	//
GLOBAL_ENTRY(invoke_syscall_trace)
#ifdef CONFIG_IA64_NEW_UNWIND
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8))
	alloc loc1=ar.pfs,8,3,0,0
	mov loc0=rp
	UNW(.body)
	mov loc2=b6
	;;
	br.call.sptk.few rp=syscall_trace
.ret3:	mov rp=loc0
	mov ar.pfs=loc1
	mov b6=loc2
	br.ret.sptk.few rp
#else /* !CONFIG_IA64_NEW_SYSCALL */
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8))
	alloc loc1=ar.pfs,8,3,0,0
	;;			// WAW on CFM at the br.call
	mov loc0=rp
	br.call.sptk.many rp=save_switch_stack_with_current_frame	// must preserve b6!!
.ret4:	mov loc2=b6
	br.call.sptk.few rp=syscall_trace
.ret5:	adds sp=IA64_SWITCH_STACK_SIZE,sp	// drop switch_stack frame
	mov rp=loc0
	mov ar.pfs=loc1
	mov b6=loc2
	;;
	br.ret.sptk.few rp
#endif /* !CONFIG_IA64_NEW_SYSCALL */
END(invoke_syscall_trace)

	//
	// Invoke a system call, but do some tracing before and after the call.
	// We MUST preserve the current register frame throughout this routine
	// because some system calls (such as ia64_execve) directly
	// manipulate ar.pfs.
	//
	// Input:
	//	r15 = syscall number
	//	b6  = syscall entry point
	//
	.global ia64_strace_leave_kernel

GLOBAL_ENTRY(ia64_trace_syscall)
	PT_REGS_UNWIND_INFO(0)
	br.call.sptk.few rp=invoke_syscall_trace // give parent a chance to catch syscall args
.ret6:	br.call.sptk.few rp=b6			// do the syscall
strace_check_retval:
	cmp.lt p6,p0=r8,r0			// syscall failed?
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp	// r2 = &pt_regs.r8
	adds r3=IA64_PT_REGS_R8_OFFSET+32,sp	// r3 = &pt_regs.r10
	mov r10=0
(p6)	br.cond.sptk.few strace_error		// syscall failed ->
	;;					// avoid RAW on r10
strace_save_retval:
.mem.offset 0,0;	st8.spill [r2]=r8	// store return value in slot for r8
.mem.offset 8,0;	st8.spill [r3]=r10	// clear error indication in slot for r10
ia64_strace_leave_kernel:
	br.call.sptk.few rp=invoke_syscall_trace // give parent a chance to catch return value
.rety:	br.cond.sptk.many ia64_leave_kernel

strace_error:
	ld8 r3=[r2]				// load pt_regs.r8
	sub r9=0,r8				// negate return value to get errno value
	;;
	cmp.ne p6,p0=r3,r0			// is pt_regs.r8!=0?
	adds r3=16,r2				// r3=&pt_regs.r10
	;;
(p6)	mov r10=-1
(p6)	mov r8=r9
	br.cond.sptk.few strace_save_retval
END(ia64_trace_syscall)

/*
 * A couple of convenience macros to help implement/understand the state
 * restoration that happens at the end of ia64_ret_from_syscall.
 */
#define rARPR		r31
#define rCRIFS		r30
#define rCRIPSR		r29
#define rCRIIP		r28
#define rARRSC		r27
#define rARPFS		r26
#define rARUNAT		r25
#define rARRNAT		r24
#define rARBSPSTORE	r23
#define rKRBS		r22
#define rB6		r21

GLOBAL_ENTRY(ia64_ret_from_clone)
	PT_REGS_UNWIND_INFO(0)
#ifdef CONFIG_SMP
	// In SMP mode, we need to call schedule_tail to complete the scheduling process.
	// Called by ia64_switch_to after do_fork()->copy_thread().  r8 contains the
	// address of the previously executing task.
	br.call.sptk.few rp=invoke_schedule_tail
.ret8:
#endif
	adds r2=IA64_TASK_PTRACE_OFFSET,r13
	;;
	ld8 r2=[r2]
	;;
	mov r8=0
	tbit.nz p6,p0=r2,PT_TRACESYS_BIT
(p6)	br strace_check_retval
	;;					// added stop bits to prevent r8 dependency
END(ia64_ret_from_clone)
	// fall through
GLOBAL_ENTRY(ia64_ret_from_syscall)
	PT_REGS_UNWIND_INFO(0)
	cmp.ge p6,p7=r8,r0			// syscall executed successfully?
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp	// r2 = &pt_regs.r8
	adds r3=IA64_PT_REGS_R8_OFFSET+32,sp	// r3 = &pt_regs.r10
	;;
	.mem.offset 0,0
(p6)	st8.spill [r2]=r8	// store return value in slot for r8 and set unat bit
	.mem.offset 8,0
(p6)	st8.spill [r3]=r0	// clear error indication in slot for r10 and set unat bit
(p7)	br.cond.spnt.few handle_syscall_error	// handle potential syscall failure
END(ia64_ret_from_syscall)
	// fall through
GLOBAL_ENTRY(ia64_leave_kernel)
	// check & deliver software interrupts:

	PT_REGS_UNWIND_INFO(0)
#ifdef CONFIG_SMP
	adds r2=IA64_TASK_PROCESSOR_OFFSET,r13
	movl r3=irq_stat		// softirq_active
	;;
	ld4 r2=[r2]
	;;
	shl r2=r2,SMP_CACHE_SHIFT	// can't use shladd here...
	;;
	add r3=r2,r3
#else
	movl r3=irq_stat		// softirq_active
#endif
	;;
	ld8 r2=[r3]		// r3 (softirq_active+softirq_mask) is guaranteed to be 8-byte aligned!
	;;
	shr r3=r2,32
	;;
	and r2=r2,r3
	;;
	cmp4.ne p6,p7=r2,r0
(p6)	br.call.spnt.many rp=invoke_do_softirq
1:
(pKern)	br.cond.dpnt.many restore_all	// yup -> skip check for rescheduling & signal delivery

	// call schedule() until we find a task that doesn't have need_resched set:

back_from_resched:
	{ .mii
	  adds r2=IA64_TASK_NEED_RESCHED_OFFSET,r13
	  mov r3=ip
	  adds r14=IA64_TASK_SIGPENDING_OFFSET,r13
	}
	;;
	ld8 r2=[r2]
	ld4 r14=[r14]
	mov rp=r3			// arrange for schedule() to return to back_from_resched
	;;
	cmp.ne p6,p0=r2,r0
	cmp.ne p2,p0=r14,r0		// NOTE: pKern is an alias for p2!!
	srlz.d
(p6)	br.call.spnt.many b6=invoke_schedule	// ignore return value
2:
	// check & deliver pending signals:
(p2)	br.call.spnt.few rp=handle_signal_delivery
.ret9:
#ifdef CONFIG_IA64_SOFTSDV_HACKS
	// Check for lost ticks
	rsm psr.i
	mov r2 = ar.itc
	movl r14 = 1000			// latency tolerance
	mov r3 = cr.itm
	;;
	sub r2 = r2, r3
	;;
	sub r2 = r2, r14
	;;
	cmp.ge p6,p7 = r2, r0
(p6)	br.call.spnt.few rp=invoke_ia64_reset_itm
.ret10:
	;;
	ssm psr.i
#endif 
restore_all:

	// start restoring the state saved on the kernel stack (struct pt_regs):

	adds r2=IA64_PT_REGS_R8_OFFSET+16,r12
	adds r3=IA64_PT_REGS_R8_OFFSET+24,r12
	;;
	ld8.fill r8=[r2],16
	ld8.fill r9=[r3],16
	;;
	ld8.fill r10=[r2],16
	ld8.fill r11=[r3],16
	;;
	ld8.fill r16=[r2],16
	ld8.fill r17=[r3],16
	;;
	ld8.fill r18=[r2],16
	ld8.fill r19=[r3],16
	;;
	ld8.fill r20=[r2],16
	ld8.fill r21=[r3],16
	;;
	ld8.fill r22=[r2],16
	ld8.fill r23=[r3],16
	;;
	ld8.fill r24=[r2],16
	ld8.fill r25=[r3],16
	;;
	ld8.fill r26=[r2],16
	ld8.fill r27=[r3],16
	;;
	ld8.fill r28=[r2],16
	ld8.fill r29=[r3],16
	;;
	ld8.fill r30=[r2],16
	ld8.fill r31=[r3],16
	;;
	ld8 r1=[r2],16		// ar.ccv
	ld8 r13=[r3],16		// ar.fpsr
	;;
	ld8 r14=[r2],16		// b0
	ld8 r15=[r3],16+8	// b7
	;;
	ldf.fill f6=[r2],32
	ldf.fill f7=[r3],32
	;;
	ldf.fill f8=[r2],32
	ldf.fill f9=[r3],32
	;;
	mov ar.ccv=r1
	mov ar.fpsr=r13
	mov b0=r14
	// turn off interrupts, interrupt collection
	rsm psr.i | psr.ic
	;;
	srlz.i			// EAS 2.5
	mov b7=r15
	;;
	invala			// invalidate ALAT
	bsw.0;;			// switch back to bank 0 (must be last in insn group)
	;;
#ifdef CONFIG_ITANIUM_ASTEP_SPECIFIC
	nop.i 0x0
	;;
	nop.i 0x0
	;;
	nop.i 0x0
	;;
#endif
	adds r16=16,r12
	adds r17=24,r12
	;;
	ld8 rCRIPSR=[r16],16	// load cr.ipsr
	ld8 rCRIIP=[r17],16	// load cr.iip
	;;
	ld8 rCRIFS=[r16],16	// load cr.ifs
	ld8 rARUNAT=[r17],16	// load ar.unat
	;;
	ld8 rARPFS=[r16],16	// load ar.pfs
	ld8 rARRSC=[r17],16	// load ar.rsc
	;;
	ld8 rARRNAT=[r16],16	// load ar.rnat (may be garbage)
	ld8 rARBSPSTORE=[r17],16	// load ar.bspstore (may be garbage)
	;;
	ld8 rARPR=[r16],16	// load predicates
	ld8 rB6=[r17],16	// load b6
	;;
	ld8 r18=[r16],16	// load ar.rsc value for "loadrs"
	ld8.fill r1=[r17],16	// load r1
	;;
	ld8.fill r2=[r16],16
	ld8.fill r3=[r17],16
	;;
	ld8.fill r12=[r16],16
	ld8.fill r13=[r17],16
	extr.u r19=rCRIPSR,32,2	// extract ps.cpl
	;;
	ld8.fill r14=[r16],16
	ld8.fill r15=[r17],16
	cmp.eq p6,p7=r0,r19	// are we returning to kernel mode? (psr.cpl==0)
	;;
	mov b6=rB6
	mov ar.pfs=rARPFS
(p6)	br.cond.dpnt.few skip_rbs_switch

	/*
	 * Restore user backing store.
	 *
	 * NOTE: alloc, loadrs, and cover can't be predicated.
	 *
	 * XXX This needs some scheduling/tuning once we believe it
	 *     really does work as intended.
	 */
	mov r16=ar.bsp			// get existing backing store pointer
(pNonSys) br.cond.dpnt.few dont_preserve_current_frame
	cover				// add current frame into dirty partition
	;;
	mov rCRIFS=cr.ifs		// fetch the cr.ifs value that "cover" produced
	mov r17=ar.bsp			// get new backing store pointer
	;;
	sub r16=r17,r16			// calculate number of bytes that were added to rbs
	;;
	shl r16=r16,16			// shift additional frame size into position for loadrs
	;;
	add r18=r16,r18			// adjust the loadrs value
	;;
dont_preserve_current_frame:
	alloc r16=ar.pfs,0,0,0,0	// drop the current call frame (noop for syscalls)
	;;
	mov ar.rsc=r18			// load ar.rsc to be used for "loadrs"
#ifdef CONFIG_IA32_SUPPORT
	tbit.nz p6,p0=rCRIPSR,IA64_PSR_IS_BIT
	;;
(p6)	mov ar.rsc=r0                   // returning to IA32 mode
#endif
 	;;
	loadrs
	;;
	mov ar.bspstore=rARBSPSTORE
	;;
	mov ar.rnat=rARRNAT	// must happen with RSE in lazy mode

skip_rbs_switch:
	mov ar.rsc=rARRSC
	mov ar.unat=rARUNAT
	mov cr.ifs=rCRIFS	// restore cr.ifs only if not a (synchronous) syscall
	mov pr=rARPR,-1
	mov cr.iip=rCRIIP
	mov cr.ipsr=rCRIPSR
	;;
	rfi;;			// must be last instruction in an insn group
END(ia64_leave_kernel)

ENTRY(handle_syscall_error)
	/*
	 * Some system calls (e.g., ptrace, mmap) can return arbitrary
	 * values which could lead us to mistake a negative return
	 * value as a failed syscall.  Those syscall must deposit
	 * a non-zero value in pt_regs.r8 to indicate an error.
	 * If pt_regs.r8 is zero, we assume that the call completed
	 * successfully.
	 */
	PT_REGS_UNWIND_INFO(0)
	ld8 r3=[r2]		// load pt_regs.r8
	sub r9=0,r8		// negate return value to get errno
	;;
	mov r10=-1		// return -1 in pt_regs.r10 to indicate error
	cmp.eq p6,p7=r3,r0	// is pt_regs.r8==0?
	adds r3=16,r2		// r3=&pt_regs.r10
	;;
(p6)	mov r9=r8
(p6)	mov r10=0
	;;
.mem.offset 0,0; st8.spill [r2]=r9	// store errno in pt_regs.r8 and set unat bit
.mem.offset 8,0; st8.spill [r3]=r10	// store error indication in pt_regs.r10 and set unat bit
	br.cond.sptk.many ia64_leave_kernel
END(handle_syscall_error)

#ifdef CONFIG_SMP
	/*
	 * Invoke schedule_tail(task) while preserving in0-in7, which may be needed
	 * in case a system call gets restarted.
	 */
ENTRY(invoke_schedule_tail)
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8))
	alloc loc1=ar.pfs,8,2,1,0
	mov loc0=rp
	mov out0=r8				// Address of previous task
	;;
	br.call.sptk.few rp=schedule_tail
.ret11:	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(invoke_schedule_tail)

#endif /* CONFIG_SMP */

#ifdef CONFIG_IA64_SOFTSDV_HACKS

ENTRY(invoke_ia64_reset_itm)
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8))
	alloc loc1=ar.pfs,8,2,0,0
	mov loc0=rp
	;;
	UNW(.body)
	br.call.sptk.many rp=ia64_reset_itm
.ret12:	;;
	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(invoke_ia64_reset_itm)

#endif /* CONFIG_IA64_SOFTSDV_HACKS */

	/*
	 * Invoke do_softirq() while preserving in0-in7, which may be needed
	 * in case a system call gets restarted.
	 */
ENTRY(invoke_do_softirq)
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8))
	alloc loc1=ar.pfs,8,2,0,0
	mov loc0=rp
	;;
	UNW(.body)
	br.call.sptk.few rp=do_softirq
.ret13:	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(invoke_do_softirq)

	/*
	 * Invoke schedule() while preserving in0-in7, which may be needed
	 * in case a system call gets restarted.
	 */
ENTRY(invoke_schedule)
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8))
	alloc loc1=ar.pfs,8,2,0,0
	mov loc0=rp
	;;
	UNW(.body)
	br.call.sptk.few rp=schedule
.ret14:	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(invoke_schedule)

	//
	// Setup stack and call ia64_do_signal.  Note that pSys and pNonSys need to
	// be set up by the caller.  We declare 8 input registers so the system call
	// args get preserved, in case we need to restart a system call.
	//
ENTRY(handle_signal_delivery)
#ifdef CONFIG_IA64_NEW_UNWIND
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8)
	alloc loc1=ar.pfs,8,2,3,0 // preserve all eight input regs in case of syscall restart!
	mov r9=ar.unat
	mov loc0=rp				// save return address
	mov out0=0				// there is no "oldset"
	adds out1=0,sp				// out1=&sigscratch
(pSys)	mov out2=1				// out2==1 => we're in a syscall
	;;
(pNonSys) mov out2=0				// out2==0 => not a syscall
	.fframe 16
	.spillpsp ar.unat, 16			// (note that offset is relative to psp+0x10!)
	st8 [sp]=r9,-16				// allocate space for ar.unat and save it
	.body
	br.call.sptk.few rp=ia64_do_signal
.ret15:	.restore sp
	adds sp=16,sp				// pop scratch stack space
	;;
	ld8 r9=[sp]				// load new unat from sw->caller_unat
	mov rp=loc0
	;;
	mov ar.unat=r9
	mov ar.pfs=loc1
	br.ret.sptk.many rp
#else /* !CONFIG_IA64_NEW_UNWIND */
	.prologue
	alloc r16=ar.pfs,8,0,3,0 // preserve all eight input regs in case of syscall restart!
	DO_SAVE_SWITCH_STACK
	UNW(.body)

	mov out0=0				// there is no "oldset"
	adds out1=16,sp				// out1=&sigscratch
	.pred.rel.mutex pSys, pNonSys
(pSys)	mov out2=1				// out2==1 => we're in a syscall
(pNonSys) mov out2=0				// out2==0 => not a syscall
	br.call.sptk.few rp=ia64_do_signal
.ret16:	// restore the switch stack (ptrace may have modified it)
	DO_LOAD_SWITCH_STACK( )
	br.ret.sptk.many rp
#endif /* !CONFIG_IA64_NEW_UNWIND */
END(handle_signal_delivery)

GLOBAL_ENTRY(sys_rt_sigsuspend)
#ifdef CONFIG_IA64_NEW_UNWIND
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8)
	alloc loc1=ar.pfs,8,2,3,0 // preserve all eight input regs in case of syscall restart!
	mov r9=ar.unat
	mov loc0=rp				// save return address
	mov out0=in0				// mask
	mov out1=in1				// sigsetsize
	adds out2=0,sp				// out2=&sigscratch
	;;
	.fframe 16
	.spillpsp ar.unat, 16			// (note that offset is relative to psp+0x10!)
	st8 [sp]=r9,-16				// allocate space for ar.unat and save it
	.body
	br.call.sptk.few rp=ia64_rt_sigsuspend
.ret17:	.restore sp
	adds sp=16,sp				// pop scratch stack space
	;;
	ld8 r9=[sp]				// load new unat from sw->caller_unat
	mov rp=loc0
	;;
	mov ar.unat=r9
	mov ar.pfs=loc1
	br.ret.sptk.many rp
#else /* !CONFIG_IA64_NEW_UNWIND */
	UNW(.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2))
	alloc r16=ar.pfs,2,0,3,0
	DO_SAVE_SWITCH_STACK
	UNW(.body)

	mov out0=in0				// mask
	mov out1=in1				// sigsetsize
	adds out2=16,sp				// out1=&sigscratch
	br.call.sptk.many rp=ia64_rt_sigsuspend
.ret18:	// restore the switch stack (ptrace may have modified it)
	DO_LOAD_SWITCH_STACK( )
	br.ret.sptk.many rp
#endif /* !CONFIG_IA64_NEW_UNWIND */
END(sys_rt_sigsuspend)

ENTRY(sys_rt_sigreturn)
#ifdef CONFIG_IA64_NEW_UNWIND
	.regstk 0,0,3,0	// inherited from gate.s:invoke_sighandler()
	PT_REGS_UNWIND_INFO(0)
	.prologue
	PT_REGS_SAVES(16)
	adds sp=-16,sp
	.body
	cmp.eq pNonSys,p0=r0,r0			// sigreturn isn't a normal syscall...
	;;
	adds out0=16,sp				// out0 = &sigscratch
	br.call.sptk.few rp=ia64_rt_sigreturn
.ret19:	adds sp=16,sp		// doesn't drop pt_regs, so don't mark it as restoring sp!
	PT_REGS_UNWIND_INFO(0)	// instead, create a new body section with the smaller frame
	;;
	ld8 r9=[sp]				// load new ar.unat
	mov b7=r8
	;;
	mov ar.unat=r9
	br b7
#else /* !CONFIG_IA64_NEW_UNWIND */
	.regstk 0,0,3,0	// inherited from gate.s:invoke_sighandler()
	PT_REGS_UNWIND_INFO(0)
	UNW(.prologue)
	UNW(.fframe IA64_PT_REGS_SIZE+IA64_SWITCH_STACK_SIZE)
	UNW(.spillsp rp, PT(CR_IIP)+IA64_SWITCH_STACK_SIZE)
	UNW(.spillsp ar.pfs, PT(CR_IFS)+IA64_SWITCH_STACK_SIZE)
	UNW(.spillsp ar.unat, PT(AR_UNAT)+IA64_SWITCH_STACK_SIZE)
	UNW(.spillsp pr, PT(PR)+IA64_SWITCH_STACK_SIZE)
	adds sp=-IA64_SWITCH_STACK_SIZE,sp
	cmp.eq pNonSys,p0=r0,r0			// sigreturn isn't a normal syscall...
	;;
	UNW(.body)

	adds out0=16,sp				// out0 = &sigscratch
	br.call.sptk.few rp=ia64_rt_sigreturn
.ret20:	adds r3=IA64_SWITCH_STACK_CALLER_UNAT_OFFSET+16,sp
	;;
	ld8 r9=[r3]			// load new ar.unat
	mov b7=r8
	;;
	PT_REGS_UNWIND_INFO(0)
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// drop (dummy) switch-stack frame
	mov ar.unat=r9
	br b7
#endif /* !CONFIG_IA64_NEW_UNWIND */
END(sys_rt_sigreturn)

GLOBAL_ENTRY(ia64_prepare_handle_unaligned)
	//
	// r16 = fake ar.pfs, we simply need to make sure 
	// privilege is still 0
	//
	PT_REGS_UNWIND_INFO(0)
	mov r16=r0 				
	UNW(.prologue)
	DO_SAVE_SWITCH_STACK
	br.call.sptk.few rp=ia64_handle_unaligned // stack frame setup in ivt
.ret21:	.body
	DO_LOAD_SWITCH_STACK(PT_REGS_UNWIND_INFO(0))
	br.cond.sptk.many rp			  // goes to ia64_leave_kernel
END(ia64_prepare_handle_unaligned)

#ifdef CONFIG_IA64_NEW_UNWIND

	//
	// unw_init_running(void (*callback)(info, arg), void *arg)
	//
#	define EXTRA_FRAME_SIZE	((UNW_FRAME_INFO_SIZE+15)&~15)

GLOBAL_ENTRY(unw_init_running)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2)
	alloc loc1=ar.pfs,2,3,3,0
	;;
	ld8 loc2=[in0],8
	mov loc0=rp
	mov r16=loc1
	DO_SAVE_SWITCH_STACK
	.body

	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2)
	.fframe IA64_SWITCH_STACK_SIZE+EXTRA_FRAME_SIZE
	SWITCH_STACK_SAVES(EXTRA_FRAME_SIZE)
	adds sp=-EXTRA_FRAME_SIZE,sp
	.body
	;;
	adds out0=16,sp				// &info
	mov out1=r13				// current
	adds out2=16+EXTRA_FRAME_SIZE,sp	// &switch_stack
	br.call.sptk.few rp=unw_init_frame_info
1:	adds out0=16,sp				// &info
	mov b6=loc2
	mov loc2=gp				// save gp across indirect function call
	;;
	ld8 gp=[in0]
	mov out1=in1				// arg
	br.call.sptk.few rp=b6			// invoke the callback function
1:	mov gp=loc2				// restore gp

	// For now, we don't allow changing registers from within
	// unw_init_running; if we ever want to allow that, we'd
	// have to do a load_switch_stack here:
	.restore sp
	adds sp=IA64_SWITCH_STACK_SIZE+EXTRA_FRAME_SIZE,sp

	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(unw_init_running)

#endif

	.rodata
	.align 8
	.globl sys_call_table
sys_call_table:
	data8 sys_ni_syscall		//  This must be sys_ni_syscall!  See ivt.S.
	data8 sys_exit				// 1025
	data8 sys_read
	data8 sys_write
	data8 sys_open
	data8 sys_close
	data8 sys_creat				// 1030
	data8 sys_link
	data8 sys_unlink
	data8 ia64_execve
	data8 sys_chdir
	data8 sys_fchdir			// 1035
	data8 sys_utimes
	data8 sys_mknod
	data8 sys_chmod
	data8 sys_chown
	data8 sys_lseek				// 1040
	data8 sys_getpid
	data8 sys_getppid
	data8 sys_mount
	data8 sys_umount
	data8 sys_setuid			// 1045
	data8 sys_getuid
	data8 sys_geteuid
	data8 sys_ptrace
	data8 sys_access
	data8 sys_sync				// 1050
	data8 sys_fsync
	data8 sys_fdatasync
	data8 sys_kill
	data8 sys_rename
	data8 sys_mkdir				// 1055
	data8 sys_rmdir
	data8 sys_dup
	data8 sys_pipe
	data8 sys_times
	data8 ia64_brk				// 1060
	data8 sys_setgid
	data8 sys_getgid
	data8 sys_getegid
	data8 sys_acct
	data8 sys_ioctl				// 1065
	data8 sys_fcntl
	data8 sys_umask
	data8 sys_chroot
	data8 sys_ustat
	data8 sys_dup2				// 1070
	data8 sys_setreuid
	data8 sys_setregid
	data8 sys_getresuid
	data8 sys_setresuid
	data8 sys_getresgid			// 1075
	data8 sys_setresgid
	data8 sys_getgroups
	data8 sys_setgroups
	data8 sys_getpgid
	data8 sys_setpgid			// 1080
	data8 sys_setsid
	data8 sys_getsid
	data8 sys_sethostname
	data8 sys_setrlimit
	data8 sys_getrlimit			// 1085
	data8 sys_getrusage
	data8 sys_gettimeofday
	data8 sys_settimeofday
	data8 sys_select
	data8 sys_poll				// 1090
	data8 sys_symlink
	data8 sys_readlink
	data8 sys_uselib
	data8 sys_swapon
	data8 sys_swapoff			// 1095
	data8 sys_reboot
	data8 sys_truncate
	data8 sys_ftruncate
	data8 sys_fchmod
	data8 sys_fchown			// 1100
	data8 ia64_getpriority
	data8 sys_setpriority
	data8 sys_statfs
	data8 sys_fstatfs
	data8 ia64_ni_syscall			// 1105
	data8 sys_semget
	data8 sys_semop
	data8 sys_semctl
	data8 sys_msgget
	data8 sys_msgsnd			// 1110
	data8 sys_msgrcv
	data8 sys_msgctl
	data8 sys_shmget
	data8 ia64_shmat
	data8 sys_shmdt				// 1115
	data8 sys_shmctl
	data8 sys_syslog
	data8 sys_setitimer
	data8 sys_getitimer
	data8 ia64_oldstat			// 1120
	data8 ia64_oldlstat
	data8 ia64_oldfstat
	data8 sys_vhangup
	data8 sys_lchown
	data8 sys_vm86				// 1125
	data8 sys_wait4
	data8 sys_sysinfo
	data8 sys_clone
	data8 sys_setdomainname
	data8 sys_newuname			// 1130
	data8 sys_adjtimex
	data8 ia64_create_module
	data8 sys_init_module
	data8 sys_delete_module
	data8 sys_get_kernel_syms		// 1135
	data8 sys_query_module
	data8 sys_quotactl
	data8 sys_bdflush
	data8 sys_sysfs
	data8 sys_personality			// 1140
	data8 ia64_ni_syscall		// sys_afs_syscall
	data8 sys_setfsuid
	data8 sys_setfsgid
	data8 sys_getdents
	data8 sys_flock				// 1145
	data8 sys_readv
	data8 sys_writev
	data8 sys_pread
	data8 sys_pwrite
	data8 sys_sysctl			// 1150
	data8 sys_mmap
	data8 sys_munmap
	data8 sys_mlock
	data8 sys_mlockall
	data8 sys_mprotect			// 1155
	data8 sys_mremap
	data8 sys_msync
	data8 sys_munlock
	data8 sys_munlockall
	data8 sys_sched_getparam		// 1160
	data8 sys_sched_setparam
	data8 sys_sched_getscheduler
	data8 sys_sched_setscheduler
	data8 sys_sched_yield
	data8 sys_sched_get_priority_max	// 1165
	data8 sys_sched_get_priority_min
	data8 sys_sched_rr_get_interval
	data8 sys_nanosleep
	data8 sys_nfsservctl
	data8 sys_prctl				// 1170
	data8 sys_getpagesize
	data8 sys_mmap2
	data8 sys_pciconfig_read
	data8 sys_pciconfig_write
	data8 sys_perfmonctl			// 1175
	data8 sys_sigaltstack
	data8 sys_rt_sigaction
	data8 sys_rt_sigpending
	data8 sys_rt_sigprocmask
	data8 sys_rt_sigqueueinfo		// 1180
	data8 sys_rt_sigreturn
	data8 sys_rt_sigsuspend
	data8 sys_rt_sigtimedwait
	data8 sys_getcwd
	data8 sys_capget			// 1185
	data8 sys_capset
	data8 sys_sendfile
	data8 sys_ni_syscall		// sys_getpmsg (STREAMS)
	data8 sys_ni_syscall		// sys_putpmsg (STREAMS)
	data8 sys_socket			// 1190
	data8 sys_bind
	data8 sys_connect
	data8 sys_listen
	data8 sys_accept
	data8 sys_getsockname			// 1195
	data8 sys_getpeername
	data8 sys_socketpair 
	data8 sys_send
	data8 sys_sendto
	data8 sys_recv				// 1200
	data8 sys_recvfrom
	data8 sys_shutdown
	data8 sys_setsockopt
	data8 sys_getsockopt
	data8 sys_sendmsg			// 1205
	data8 sys_recvmsg
	data8 sys_pivot_root
	data8 sys_mincore
	data8 sys_madvise
	data8 sys_newstat			// 1210
	data8 sys_newlstat
	data8 sys_newfstat
	data8 sys_clone2
	data8 sys_getdents64
	data8 ia64_ni_syscall			// 1215
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1220
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1225
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1230
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1235
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1240
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1245
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1250
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1255
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1260
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1265
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1270
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1275
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
