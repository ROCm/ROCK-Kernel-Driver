/*
 * This file contains the code that gets mapped at the upper end of each task's text
 * region.  For now, it contains the signal trampoline code only.
 *
 * Copyright (C) 1999-2001 Hewlett-Packard Co
 * 	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/asmmacro.h>
#include <asm/offsets.h>
#include <asm/sigcontext.h>
#include <asm/system.h>
#include <asm/unistd.h>
#include <asm/page.h>

	.section .text.gate,"ax"

#	define ARG0_OFF		(16 + IA64_SIGFRAME_ARG0_OFFSET)
#	define ARG1_OFF		(16 + IA64_SIGFRAME_ARG1_OFFSET)
#	define ARG2_OFF		(16 + IA64_SIGFRAME_ARG2_OFFSET)
#	define SIGHANDLER_OFF	(16 + IA64_SIGFRAME_HANDLER_OFFSET)
#	define SIGCONTEXT_OFF	(16 + IA64_SIGFRAME_SIGCONTEXT_OFFSET)

#	define FLAGS_OFF	IA64_SIGCONTEXT_FLAGS_OFFSET
#	define CFM_OFF		IA64_SIGCONTEXT_CFM_OFFSET
#	define FR6_OFF		IA64_SIGCONTEXT_FR6_OFFSET
#	define BSP_OFF		IA64_SIGCONTEXT_AR_BSP_OFFSET
#	define RNAT_OFF		IA64_SIGCONTEXT_AR_RNAT_OFFSET
#	define UNAT_OFF		IA64_SIGCONTEXT_AR_UNAT_OFFSET
#	define FPSR_OFF		IA64_SIGCONTEXT_AR_FPSR_OFFSET
#	define PR_OFF		IA64_SIGCONTEXT_PR_OFFSET
#	define RP_OFF		IA64_SIGCONTEXT_B0_OFFSET
#	define SP_OFF		IA64_SIGCONTEXT_R12_OFFSET
#	define RBS_BASE_OFF	IA64_SIGCONTEXT_RBS_BASE_OFFSET
#	define LOADRS_OFF	IA64_SIGCONTEXT_LOADRS_OFFSET
#	define base0		r2
#	define base1		r3
	/*
	 * When we get here, the memory stack looks like this:
	 *
	 *   +===============================+
       	 *   |				     |
       	 *   //	    struct sigframe          //
       	 *   |				     |
	 *   +-------------------------------+ <-- sp+16
	 *   |      16 byte of scratch       |
	 *   |            space              |
	 *   +-------------------------------+ <-- sp
	 *
	 * The register stack looks _exactly_ the way it looked at the time the signal
	 * occurred.  In other words, we're treading on a potential mine-field: each
	 * incoming general register may be a NaT value (including sp, in which case the
	 * process ends up dying with a SIGSEGV).
	 *
	 * The first thing need to do is a cover to get the registers onto the backing
	 * store.  Once that is done, we invoke the signal handler which may modify some
	 * of the machine state.  After returning from the signal handler, we return
	 * control to the previous context by executing a sigreturn system call.  A signal
	 * handler may call the rt_sigreturn() function to directly return to a given
	 * sigcontext.  However, the user-level sigreturn() needs to do much more than
	 * calling the rt_sigreturn() system call as it needs to unwind the stack to
	 * restore preserved registers that may have been saved on the signal handler's
	 * call stack.
	 */

GLOBAL_ENTRY(ia64_sigtramp)
	// describe the state that is active when we get here:
	.prologue
	.unwabi @svr4, 's'		// mark this as a sigtramp handler (saves scratch regs)
	.savesp ar.unat, UNAT_OFF+SIGCONTEXT_OFF
	.savesp ar.fpsr, FPSR_OFF+SIGCONTEXT_OFF
	.savesp pr, PR_OFF+SIGCONTEXT_OFF
	.savesp rp, RP_OFF+SIGCONTEXT_OFF
	.vframesp SP_OFF+SIGCONTEXT_OFF
	.body

	.label_state 1

	adds base0=SIGHANDLER_OFF,sp
	adds base1=RBS_BASE_OFF+SIGCONTEXT_OFF,sp
	br.call.sptk.many rp=1f
1:
	ld8 r17=[base0],(ARG0_OFF-SIGHANDLER_OFF)	// get pointer to signal handler's plabel
	ld8 r15=[base1]					// get address of new RBS base (or NULL)
	cover				// push args in interrupted frame onto backing store
	;;
	cmp.ne p8,p0=r15,r0		// do we need to switch the rbs?
	mov.m r9=ar.bsp			// fetch ar.bsp
	.spillsp.p p8, ar.rnat, RNAT_OFF+SIGCONTEXT_OFF
(p8)	br.cond.spnt setup_rbs		// yup -> (clobbers r14, r15, and r16)
back_from_setup_rbs:

	.spillreg ar.pfs, r8
	alloc r8=ar.pfs,0,0,3,0		// get CFM0, EC0, and CPL0 into r8
	ld8 out0=[base0],16		// load arg0 (signum)
	adds base1=(ARG1_OFF-(RBS_BASE_OFF+SIGCONTEXT_OFF)),base1
	;;
	ld8 out1=[base1]		// load arg1 (siginfop)
	ld8 r10=[r17],8			// get signal handler entry point
	;;
	ld8 out2=[base0]		// load arg2 (sigcontextp)
	ld8 gp=[r17]			// get signal handler's global pointer

	adds base0=(BSP_OFF+SIGCONTEXT_OFF),sp
	;;
	.spillsp ar.bsp, BSP_OFF+SIGCONTEXT_OFF
	st8 [base0]=r9,(CFM_OFF-BSP_OFF)	// save sc_ar_bsp
	dep r8=0,r8,38,26			// clear EC0, CPL0 and reserved bits
	adds base1=(FR6_OFF+16+SIGCONTEXT_OFF),sp
	;;
	.spillsp ar.pfs, CFM_OFF
	st8 [base0]=r8				// save CFM0
	adds base0=(FR6_OFF+SIGCONTEXT_OFF),sp
	;;
	stf.spill [base0]=f6,32
	stf.spill [base1]=f7,32
	;;
	stf.spill [base0]=f8,32
	stf.spill [base1]=f9,32
	mov b6=r10
	;;
	stf.spill [base0]=f10,32
	stf.spill [base1]=f11,32
	;;
	stf.spill [base0]=f12,32
	stf.spill [base1]=f13,32
	;;
	stf.spill [base0]=f14,32
	stf.spill [base1]=f15,32
	br.call.sptk.many rp=b6			// call the signal handler
.ret0:	adds base0=(BSP_OFF+SIGCONTEXT_OFF),sp
	;;
	ld8 r15=[base0],(CFM_OFF-BSP_OFF)	// fetch sc_ar_bsp and advance to CFM_OFF
	mov r14=ar.bsp
	;;
	ld8 r8=[base0]				// restore (perhaps modified) CFM0, EC0, and CPL0
	cmp.ne p8,p0=r14,r15			// do we need to restore the rbs?
(p8)	br.cond.spnt restore_rbs		// yup -> (clobbers r14 and r16)
	;;
back_from_restore_rbs:
	adds base0=(FR6_OFF+SIGCONTEXT_OFF),sp
	adds base1=(FR6_OFF+16+SIGCONTEXT_OFF),sp
	;;
	ldf.fill f6=[base0],32
	ldf.fill f7=[base1],32
	;;
	ldf.fill f8=[base0],32
	ldf.fill f9=[base1],32
	;;
	ldf.fill f10=[base0],32
	ldf.fill f11=[base1],32
	;;
	ldf.fill f12=[base0],32
	ldf.fill f13=[base1],32
	;;
	ldf.fill f14=[base0],32
	ldf.fill f15=[base1],32
	mov r15=__NR_rt_sigreturn
	break __BREAK_SYSCALL

	.body
	.copy_state 1
setup_rbs:
	mov ar.rsc=0				// put RSE into enforced lazy mode
	;;
	.save ar.rnat, r16
	mov r16=ar.rnat				// save RNaT before switching backing store area
	adds r14=(RNAT_OFF+SIGCONTEXT_OFF),sp

	mov ar.bspstore=r15			// switch over to new register backing store area
	;;
	.spillsp ar.rnat, RNAT_OFF+SIGCONTEXT_OFF
	st8 [r14]=r16				// save sc_ar_rnat
	adds r14=(LOADRS_OFF+SIGCONTEXT_OFF),sp

	mov.m r16=ar.bsp			// sc_loadrs <- (new bsp - new bspstore) << 16
	;;
	invala
	sub r15=r16,r15
	;;
	shl r15=r15,16
	;;
	st8 [r14]=r15				// save sc_loadrs
	mov ar.rsc=0xf				// set RSE into eager mode, pl 3
	br.cond.sptk back_from_setup_rbs

	.prologue
	.copy_state 1
	.spillsp ar.rnat, RNAT_OFF+SIGCONTEXT_OFF
	.body
restore_rbs:
	alloc r2=ar.pfs,0,0,0,0			// alloc null frame
	adds r16=(LOADRS_OFF+SIGCONTEXT_OFF),sp
	;;
	ld8 r14=[r16]
	adds r16=(RNAT_OFF+SIGCONTEXT_OFF),sp
	;;
	mov ar.rsc=r14				// put RSE into enforced lazy mode
	ld8 r14=[r16]				// get new rnat
	;;
	loadrs					// restore dirty partition
	;;
	mov ar.bspstore=r15			// switch back to old register backing store area
	;;
	mov ar.rnat=r14				// restore RNaT
	mov ar.rsc=0xf				// (will be restored later on from sc_ar_rsc)
	// invala not necessary as that will happen when returning to user-mode
	br.cond.sptk back_from_restore_rbs
END(ia64_sigtramp)
