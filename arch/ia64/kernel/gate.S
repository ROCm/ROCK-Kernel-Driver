/*
 * This file contains the code that gets mapped at the upper end of
 * each task's text region.  For now, it contains the signal
 * trampoline code only.
 *
 * Copyright (C) 1999-2000 Hewlett-Packard Co
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/asmmacro.h>
#include <asm/offsets.h>
#include <asm/sigcontext.h>
#include <asm/system.h>
#include <asm/unistd.h>
#include <asm/page.h>

	.psr abi64
	.psr lsb
	.lsb

	.section __gate_section,"ax"

	.align PAGE_SIZE

#	define SIGINFO_OFF	16
#	define SIGCONTEXT_OFF	(SIGINFO_OFF + ((IA64_SIGINFO_SIZE + 15) & ~15))
#	define FLAGS_OFF	IA64_SIGCONTEXT_FLAGS_OFFSET
#	define CFM_OFF		IA64_SIGCONTEXT_CFM_OFFSET
#	define FR6_OFF		IA64_SIGCONTEXT_FR6_OFFSET
#	define BSP_OFF		IA64_SIGCONTEXT_AR_BSP_OFFSET
#	define RNAT_OFF		IA64_SIGCONTEXT_AR_RNAT_OFFSET
#	define base0		r2
#	define base1		r3
	/*
	 * When we get here, the memory stack looks like this:
	 *
	 *   +===============================+
       	 *   |				     |
       	 *   //	    struct sigcontext        //
       	 *   |				     |
	 *   +===============================+ <-- sp+SIGCONTEXT_OFF
       	 *   |				     |
	 *   //     rest of siginfo    	     //
       	 *   | 			   	     |
       	 *   +               +---------------+
       	 *   | 		     | siginfo.code  |
	 *   +---------------+---------------+
	 *   | siginfo.errno | siginfo.signo |
	 *   +-------------------------------+ <-- sp+SIGINFO_OFF
	 *   |      16 byte of scratch       |
	 *   |            space              |
	 *   +-------------------------------+ <-- sp
	 *
	 * The register stack looks _exactly_ the way it looked at the
	 * time the signal occurred.  In other words, we're treading
	 * on a potential mine-field: each incoming general register
	 * may be a NaT value (includeing sp, in which case the process
	 * ends up dying with a SIGSEGV).
	 *
	 * The first need to do is a cover to get the registers onto
	 * the backing store.  Once that is done, we invoke the signal
	 * handler which may modify some of the machine state.  After
	 * returning from the signal handler, we return control to the
	 * previous context by executing a sigreturn system call.  A
	 * signal handler may call the rt_sigreturn() function to
	 * directly return to a given sigcontext.  However, the
	 * user-level sigreturn() needs to do much more than calling
	 * the rt_sigreturn() system call as it needs to unwind the
	 * stack to restore preserved registers that may have been
	 * saved on the signal handler's call stack.
	 *
	 * On entry:
	 *	r2	= signal number
	 *	r3	= plabel of signal handler
	 *	r15	= new register backing store (ignored)
	 *	[sp+16] = sigframe
	 */

GLOBAL_ENTRY(ia64_sigtramp)
	ld8 r10=[r3],8				// get signal handler entry point
	br.call.sptk.many rp=invoke_sighandler
END(ia64_sigtramp)

ENTRY(invoke_sighandler)
	ld8 gp=[r3]			// get signal handler's global pointer
	mov b6=r10
	cover				// push args in interrupted frame onto backing store
	;;
	alloc r8=ar.pfs,0,0,3,0		// get CFM0, EC0, and CPL0 into r8
	;;
	mov r17=ar.bsp			// fetch ar.bsp
	cmp.ne p8,p0=r15,r0		// do we need to switch the rbs?
	mov out0=r2			// signal number
(p8)	br.cond.spnt.few setup_rbs	// yup -> (clobbers r14 and r16)
back_from_setup_rbs:
	adds base0=(BSP_OFF+SIGCONTEXT_OFF),sp
	;;
	st8 [base0]=r17,(CFM_OFF-BSP_OFF)	// save sc_ar_bsp
	dep r8=0,r8,38,26		// clear EC0, CPL0 and reserved bits
	adds base1=(FR6_OFF+16+SIGCONTEXT_OFF),sp
	;;

	st8 [base0]=r8				// save CFM0
	adds base0=(FR6_OFF+SIGCONTEXT_OFF),sp
	;;
	stf.spill [base0]=f6,32
	stf.spill [base1]=f7,32
	;;
	stf.spill [base0]=f8,32
	stf.spill [base1]=f9,32
	;;
	stf.spill [base0]=f10,32
	stf.spill [base1]=f11,32
	adds out1=SIGINFO_OFF,sp	// siginfo pointer
	;;
	stf.spill [base0]=f12,32
	stf.spill [base1]=f13,32
	adds out2=SIGCONTEXT_OFF,sp	// sigcontext pointer
	;;
	stf.spill [base0]=f14,32
	stf.spill [base1]=f15,32
	br.call.sptk.few rp=b6			// call the signal handler
.ret0:	adds base0=(BSP_OFF+SIGCONTEXT_OFF),sp
	;;
	ld8 r15=[base0],(CFM_OFF-BSP_OFF)	// fetch sc_ar_bsp and advance to CFM_OFF
	mov r14=ar.bsp
	;;
	ld8 r8=[base0]				// restore (perhaps modified) CFM0, EC0, and CPL0
	cmp.ne p8,p0=r14,r15			// do we need to restore the rbs?
(p8)	br.cond.spnt.few restore_rbs		// yup -> (clobbers r14 and r16)
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
END(invoke_sighandler)

ENTRY(setup_rbs)
	flushrs					// must be first in insn
	mov ar.rsc=r0				// put RSE into enforced lazy mode
	adds r16=(RNAT_OFF+SIGCONTEXT_OFF),sp
	;;
	mov r14=ar.rnat				// get rnat as updated by flushrs
	mov ar.bspstore=r15			// set new register backing store area
	;;
	st8 [r16]=r14				// save sc_ar_rnat
	mov ar.rsc=0xf				// set RSE into eager mode, pl 3
	invala					// invalidate ALAT
	br.cond.sptk.many back_from_setup_rbs
END(setup_rbs)

ENTRY(restore_rbs)
	flushrs
	mov ar.rsc=r0				// put RSE into enforced lazy mode
	adds r16=(RNAT_OFF+SIGCONTEXT_OFF),sp
	;;
	ld8 r14=[r16]				// get new rnat
	mov ar.bspstore=r15			// set old register backing store area
	;;
	mov ar.rnat=r14				// establish new rnat
	mov ar.rsc=0xf				// (will be restored later on from sc_ar_rsc)
	// invala not necessary as that will happen when returning to user-mode
	br.cond.sptk.many back_from_restore_rbs
END(restore_rbs)
