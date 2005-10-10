/* 
 *   Creation Date: <2001/01/27 16:25:14 samuel>
 *   Time-stamp: <2003/08/26 21:37:56 samuel>
 *   
 *	<traps.S>
 *	
 *	Exception Vectors
 *   
 *   Copyright (C) 2000, 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#define DBG_TRACE	0	// enable TRACE macro?

#include "archinclude.h"
#include "processor.h"		/* avoid <asm/processor.h> (bogus SPRN_TBWU/L) */
#include "asm_offsets.h"
#include "asmdefs.h"
#include "molasm.h"
#include "vector.h"
#include "reloc.h"
#include "rvec.h"
#include "constants.h"
#include "mac_registers.h"
#include "mmu.h"
#include "osi.h"
#include "kernel_vars.h"


/************************************************************************/
/*	Exception Vector Definitions					*/
/************************************************************************/

	.text
GLOBAL_SYMBOL(r__reloctable_start):
	
MACRO_0(EXCEPTION_PREAMBLE, [
	// SPRG1 = r1, SPRG0 = r3, r3=CR, r1=MOL_STACK
	__ZERO_TICK_CNT(entry)
	stw	r4,xGPR4(r1)		// store instruction are store serializing
	lwz	r4,xFLAG_BITS(r1)
	stw	r5,xGPR5(r1)		// and mix very badly with execution serializing
	stw	r6,xGPR6(r1)		// instructions (like mfspr)
	mfsrr0	r6
	stw	r7,xGPR7(r1)
	mfsrr1	r7
	stw	r0,xGPR0(r1)
	mtcr	r4
	stw	r2,xGPR2(r1)
	mflr	r2
	stw	r3,xCR(r1)
	mfsprg_a1 r5			// SPRG1 = r1
	stw	r2,xLINK(r1)
	mfsprg_a0 r3			// SPRG0 = r3
	stw	r5,xGPR1(r1)
	stw	r3,xGPR3(r1)
	
	// saved: r0-r7, cr, lr
	// r1 = stack, cr5-7=flag_bits, r6/r7 = nip/msr
])

	balign_32
save_middle_regs:			// save r8-r12 and nip (r13-r31 should always be in regs)
	stw	r6,xNIP(r1)
	mfxer	r7
	mfctr	r6
	stw	r8,xGPR8(r1)
	stw	r9,xGPR9(r1)
	stw	r10,xGPR10(r1)
	stw	r11,xGPR11(r1)
	stw	r12,xGPR12(r1)
	stw	r6,xCTR(r1)
	stw	r7,xXER(r1)
	bt-	FBIT_FPUInUse,giveup_fpu	// modifies r7/r8
	blr

MACRO_0(RESTORE_MIDDLE_REGS, [		// reverse save_middle_regs, restores registers
	lwz	r11,xCTR(r1)		// r8-r12, ctr, xer, r6/r7 = nip/msr
	lwz	r12,xXER(r1)
	lwz	r8,xGPR8(r1)
	lwz	r9,xGPR9(r1)
	mtctr	r11
	lwz	r10,xGPR10(r1)
	lwz	r11,xGPR11(r1)
	mtxer	r12
	lwz	r12,xGPR12(r1)
	lwz	r6,xNIP(r1)
	lwz	r7,K_MSR(r1)
])

#define EXCEPTION_SAVE_ALL				\
	EXCEPTION_PREAMBLE				;\
	bl save_middle_regs				;
	
#define VECTOR_KERNEL( v, dummy_name, secint )		\
	VECTOR( v, dummy_name, secint )			;\
	EXCEPTION_SAVE_ALL				;\
	TAKE_EXCEPTION( v )				;

#define VECTOR_RESERVED( v, dummy_name, secint )	\
	VECTOR( v, dummy_name, secint )			;\
	DEBUGGER_SAVE( v )				;

#define MAC_EXIT( rvec_code ) 				\
	li	r3,rvec_code				;\
	b	mac_exit				;

#define MAC_EXIT_SAVE( rvec_code )			\
	bl	save_middle_regs			;\
	li	r3,rvec_code				;\
	b	mac_exit				;

#define MAC_TRAP( trap_num )				\
	li	r2,trap_num				;\
	b	mac_trap				;

#define DEBUGGER(n)		li r4,n ; MAC_EXIT( RVEC_DEBUGGER );
#define DEBUGGER_SAVE(n)	li r4,n ; MAC_EXIT_SAVE( RVEC_DEBUGGER );


/************************************************************************/
/*	Misc macros							*/
/************************************************************************/

	//////////////////////////////////////////////////////////////
	// _get_instr_opcode
	//	r6	nip
	// ret:	r4	opcode
	//
	// Modifies: r2,r3. 
	// Side-effects: Might return from the exception

MACRO(GET_INSTR_OPCODE, [dummy], [
	mfmsr	r3			// r3 = exception MSR
	ori	r2,r3,MSR_DR
	bl	9f

	// secondary interrupt entry (we _know_ this must be a DSI)
#ifdef CONFIG_SMP
	LOADI	r5,EXTERN(compat_hash_table_lock)
	li	r4,1
	tophys	r5,r5
8:	lwarx	r0,0,r5			// lock
	cmpwi	r0,0
	bne-	8b
	stwcx.	r4,0,r5
	bne-	8b
	isync
	tlbie	r6
	eieio
	tlbsync
	sync
	stw	r0,0(r5)		// release lock
#else
	tlbie	r6			// flush PTE from ITLB
#endif
	b	exception_return
9:	
	mtmsr	r2
	isync

	lwz     r4,0(r6)		// get opcode
	
	mtmsr	r3			// restore exception MSR
	isync
])
	
/************************************************************************/
/*	Reserved / Kernel Vectors					*/
/************************************************************************/

VECTOR_KERNEL( 0x100, "System Reset", secint_bad )
VECTOR_KERNEL( 0x500, "External Interrupt", secint_bad )
VECTOR_KERNEL( 0x1400, "System Management Interrupt", secint_bad )
VECTOR_KERNEL( 0x1700, "Thermal Management Interrupt", secint_bad )

VECTOR_RESERVED( 0x200, "Machine Check", secint_bad )
VECTOR_RESERVED( 0xa00, "Reserved", secint_bad )
VECTOR_RESERVED( 0xb00, "Reserved", secint_bad )
VECTOR_RESERVED( 0xe00, "FPU Assist", secint_bad )

PERFMON_VECTOR_RELOCATION( PERFMON_VECTOR )
VECTOR_RESERVED( PERFMON_VECTOR, "Performance Monitor Interrupt", secint_bad )

//VECTOR_RESERVED( 0x1000, "InstructionTLBMiss-603", secint_bad )
//VECTOR_RESERVED( 0x1100, "DataLoadTLBMiss-603", secint_bad )
//VECTOR_RESERVED( 0x1200, "DataLoadTLBMiss-603", secint_bad )

/************************************************************************/
/*	DSI Exceptions							*/
/************************************************************************/
	
VECTOR( 0x300, "DSI", secint_lr_call )
	EXCEPTION_PREAMBLE
	TRACE(0x300, "DSI")

	GET_INSTR_OPCODE		// m: r2-r3, ret: r4=opcode, r6=nip
	stw	r4,xINST_OPCODE(r1)
	bl	check_pthash_hit	// m: r0,r2-r5
	btl-	FBIT_InSplitmode,splitmode_dsi
	bl	save_middle_regs
	bl	check_io_page

dsi_cont:
	LOADI	r3,EXTERN(dsi_exception)
	mfdar	r4			// We might need to do this earlier
	mfdsisr	r5			// when the splitmode code is activated...
	b	call_kernel


/************************************************************************/
/*	ISI Exceptions							*/
/************************************************************************/

VECTOR( 0x400, "ISI", secint_bad )
	EXCEPTION_PREAMBLE
	TRACE(0x400, "ISI")

	// emuaccel engine
	rlwinm.	r0,r7,0,4,4			// protection violation
	beq-	1f
	lwz	r3,K_EMUACCEL_MPHYS(r1)
	rlwinm	r4,r6,0,~0x0fff			// nip page
	rlwinm	r2,r6,0,0x0ff8			// nip offset (dword align)
	lwz	r5,K_EMUACCEL_PAGE_PHYS(r1)
	cmpw	r3,r4
	bne-	1f
	lwzux	r0,r2,r5			// r0 = handler
	mtlr	r0
	lwz	r6,4(r2)			// address of next instruction
	blr					// parameters: r2 == emuaccel_slot
1:
	btl-	FBIT_InSplitmode, split_sr_no_execute
	bl	save_middle_regs

	LOADI	r3,EXTERN(isi_exception)
	mfsrr0	r4
	mfsrr1	r5
	b	call_kernel


/************************************************************************/
/*	Alignement Exception						*/
/************************************************************************/

VECTOR( 0x600, "Alignment", secint_lr_call )
	EXCEPTION_SAVE_ALL
	TRACE(0x400, "Alignment")

alignment_cont:
	mfdar	r4
	mfdsisr	r5
	MAC_EXIT( RVEC_ALIGNMENT_TRAP )


/************************************************************************/
/*	FPU Unavailable	Exception					*/
/************************************************************************/

	// xFPU_STATE has only meaning when FBIT_FPUInUse is not set
	// 
	// FPU_STATE_DIRTY	- fr13 & fpscr are not loaded (everything else is).
	// FPU_STATE_HALF_SAVED	- fr14-fr31 are loaded.
	// FPU_STATE_SAVED	- fr14-fr31 are loaded (but also saved in mregs).
	//
	// FPU_STATE_DIRTY in the *emulator* means that all floating point 
	// registers *EXCEPT* fr13 and fpscr are valid.
	//
	// Implementation note: When we do not own the fpu, the MSR_FEx bits
	// must be cleared. Otherwise we might experience bogus FPU exceptions.
	//
	// MOL will never throw FPU exceptions went the FP bit is off. This
	// is a small violation of the standard but the alternative would be
	// always loading FPSCR (which requires FPU ownership...)
	//
	
VECTOR( 0x800, "FPU Unavailable", secint_lr_call )
	EXCEPTION_PREAMBLE
	TRACE(0x800, "FPU Unavailable")
fpu_cont:

	lwz	r2,xMSR(r1)			// r2 = xMSR (used below too)
	andi.	r4,r2,MSR_FP
	beq-	mac_fpu_unavailable		// mac trap?
	bt	FBIT_FPUInUse,2f		// FPU ready for use?
	
	lwz	r3,K_EMULATOR_MSR(r1)		// FPU owned by our userland process?
	andi.	r4,r3,MSR_FP
	bne+	1f
	MAC_EXIT_SAVE( RVEC_ENABLE_FPU )	// No... grab FPU in userspace

mac_fpu_unavailable:
	BUMP( "mac-fpu-trap" )
	MAC_TRAP( 0x800 )

	// userland process owns FPU
1:	BUMP( "enable-fpu" )
	ENABLE_MSR_FP /**/ r4			// enable kernel FPU
	// flag the fpu dirty
	lwz	r3,xFPU_STATE(r1)
	lfd	fr13,xFPSCR-4(r1)		// fp13 and fpscr are *ALWAYS* saved
	crset	FBIT_FPUInUse			// we own the FPU now
	cmpwi	r3,FPU_STATE_HALF_SAVED
	mtfsf	0xff,fr13
	lfd	fr13,xFPR13(r1)	
	bne	2f
	xLOAD_LOW_FPU	r1			// load fr0-fr12
2:
	li	r3,MSR_FP| MSR_FE0 | MSR_FE1	// FPU bits
	lwz	r7,K_MSR(r1)			// enable MSR_FP
	and	r2,r2,r3			// r2 = (xMSR & MSR_FEx)
	andc	r7,r7,r3			// K_MSR &= ~MSR_FEx
	or	r7,r7,r2			// K_MSR |= (xMSR & MSR_FEx)
	stw	r7,K_MSR(r1)
	GET_TICK_CNT(entry,"enable_fpu")
	b	exception_return

	
/************************************************************************/
/*	Decrementer Exception						*/
/************************************************************************/

// The 0x900 decrementer vector is in dec.S

/************************************************************************/
/*	System Call Exception						*/
/************************************************************************/

VECTOR( 0xc00, "System Call", secint_bad )
	EXCEPTION_PREAMBLE
	TRACE( 0xc00, "System Call")
	
	lwz	r3,xGPR3(r1)
	LOADI	r5,OSI_SC_MAGIC_R3
	lwz	r4,xGPR4(r1)
	LOADI	r2,OSI_SC_MAGIC_R4
	cmpw	cr1,r3,r5
	cmpw	cr0,r4,r2
	crand	eq,eq,cr1*4+eq
	beq+	2f

	MAC_TRAP(0xc00)			// r7 reason bits used (zero)
2:	
	MAC_EXIT_SAVE( RVEC_OSI_SYSCALL )


/************************************************************************/
/*	Trace Exception							*/
/************************************************************************/

VECTOR( 0xd00, "Trace", secint_bad )
trace_vector:
	EXCEPTION_PREAMBLE
	TRACE(0xd00, "Trace")

	MAC_EXIT_SAVE( RVEC_TRACE_TRAP );


/************************************************************************/
/*	AltiVec Exception						*/
/************************************************************************/

VECTOR( 0xf20, "AltiVec", secint_lr_call )
	EXCEPTION_PREAMBLE
	TRACE(0xf20, "AltiVec")
altivec_cont:

	lwz	r4,xNO_ALTIVEC(r1)		// AltiVec support disabled?
	cmpwi	r4,0
	bne-	mac_altivec_unavailable
	
	lwz	r2,xMSR(r1)
	rlwinm.	r4,r2,0,6,6			// bit 6 = MSR_VEC
	beq-	mac_altivec_unavailable
	
	lwz	r3,K_EMULATOR_MSR(r1)
	rlwinm.	r4,r3,0,6,6			// bit 6 = MSR_VEC
	bne+	enable_altivec
	MAC_EXIT_SAVE( RVEC_ENABLE_ALTIVEC )

mac_altivec_unavailable:
	MAC_EXIT_SAVE( RVEC_ALTIVEC_UNAVAIL_TRAP )
	
enable_altivec:
	// We don't need to load any registers since the emulator
	// won't touch the altivec unit (at least for now).
	
	lwz	r7,K_MSR(r1)
	oris	r7,r7,HI(MSR_VEC)
	stw	r7,K_MSR(r1)
	b	exception_return

	
VECTOR( 0x1600, "AltiVec Assist", secint_bad )
	EXCEPTION_SAVE_ALL
	TRACE(0x1600, "AltiVec Assist")

	mr	r4,r7
	MAC_EXIT( RVEC_ALTIVEC_ASSIST )		// r4 = srr1


/************************************************************************/
/*	Instruction Breakpoint						*/
/************************************************************************/
	
VECTOR( 0x1300, "Instruction Breakpoint", secint_bad )
	EXCEPTION_SAVE_ALL
	TRACE(0x1300, "IABR")
		
	DEBUGGER(0x1300)


/************************************************************************/
/*	RunMode-601 (trace)						*/
/************************************************************************/
	
VECTOR( 0x2000, "RunMode-601", secint_bad )
	b	trace_vector


/************************************************************************/
/*	Secondary Interrupt Handlers					*/
/************************************************************************/

	//////////////////////////////////////////////////////////////////////
	// secint_xxx
	//
	//	r1:		stack (sprg1 = old r1)
	//	r3:		vector addr (sprg0 = old r3)
	//	srr0/srr1:	kernel nip/msr
	//
	// secint_lr_call:
	//	lr		secondary interrupt handler

secint_bad:
	TRACE(0xbad, "secint_bad")
	mr	r4,r3
	MAC_EXIT( RVEC_INTERNAL_ERROR )

secint_lr_call:
	blrl
	li	r4,0x6666
	MAC_EXIT( RVEC_INTERNAL_ERROR )

	
/**************************************************************
*  Includes
**************************************************************/

// We need to be sure this code is contiguous, the simplest/safest
// method is using only a single file. This will also effectively
// reduce the size of the relocation table.

#include "entry.S"
#include "dec.S"
#include "emulation.S"
#include "emuaccel.S"
#include "iopage.S"
#include "splitmode.S"
#include "ptintercept.S"
#include "vsid.S"
#ifdef __MPC107__
#include "./mpc107/mpc107.S"
#else
#ifdef __linux__
#include "linux.S"
#include "603.S"
#endif
#endif
	
#ifdef ACTION_SYM_GREPPING
#include "actions.S"
#endif

	.text	50
GLOBAL_SYMBOL(r__reloctable_end):

	
// The BUMP("counter") macro adds entries to text subsection 90.
// This adds labels before the counter entries.

	.text	89
GLOBAL_SYMBOL(__start_bumptable):
	.text	91
GLOBAL_SYMBOL(__end_bumptable):
	.text


