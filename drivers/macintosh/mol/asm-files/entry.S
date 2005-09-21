/* 
 *   Creation Date: <2001/01/30 00:22:35 samuel>
 *   Time-stamp: <2003/09/04 11:24:31 samuel>
 *   
 *	<entry.S>
 *	
 *	Emulator/mac switching
 *   
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

/* Short note about DEC. Due to the kernel implementation of the DEC 
 * exception handler (and get_time_of_day) in the 2.3/2.4 kernel, we 
 * must try to maintain coherency between DEC and TB. In any case, 
 * we must NEVER let DEC tick faster than TB, or get_time_of_day will 
 * occasionally return bogus values (the result is usually 
 * misbehaving X).
 */
	
/************************************************************************/
/*	Mac Entry							*/
/************************************************************************/

	//////////////////////////////////////////////////////////
	// mac_entry_initialize
	//
	// Run once in order to store certain things (segment
	// registers and NIP)
	//
	//	sprg1	userspace stack
	//	r6	emulator return point after 'call_kernel'
	//
	// Safe to modify: r0-r12, lr, ctr

mac_entry_initialize:
	__ZERO_TICK_CNT(entry)

	// Save emulator return point (skip over illegal inst)
	mfsrr0	r5
	addi	r5,r5,4
	stw	r5,K_EMULATOR_NIP(r1)		// This sets the return point once and for all
	stw	r2,K_EMULATOR_TOC(r1)		// r2==TOC (not on linux though)
	stw	r6,K_EMULATOR_KCALL_NIP(r1)	// Return point after 'call_kernel'
	mfsrr1	r6
	mfsprg_a1 r3
	stw	r6,K_EMULATOR_MSR(r1)		// we want to return through mac_exit
	stw	r3,K_EMULATOR_STACK(r1)

	lwz	r3,xFLAG_BITS(r1)		// Best to initialize the flag bits
	mtcr	r3

	// msr to be used in mac-mode
	LOADI	r3,(MSR_ME | MSR_IR | MSR_DR | MSR_PR | MSR_EE )
	stw	r3,K_MSR(r1)

	PERF_MONITOR_SETUP /**/ r4

	addi	r3,r1,K_EMULATOR_SR		// Save segment registers
	SAVE_SEGMENT_REGS r3, /**/ r4,r5
	SAVE_IBATS K_IBAT0U_SAVE, /**/ r3	// And save linux BAT registers
	SAVE_DBATS K_DBAT0U_SAVE, /**/ r3
	
	bl	initialize_msr_sr_table

	addi	r3,r1,K_EMULATOR_SR
	LOAD_SEGMENT_REGS r3, /**/ r4,r5

	li	r3,RVEC_EXIT
	b	exit_

		
	////////////////////////////////////////////////////////////////////
	// mac_entry [ENTRYPOINT]
	// 
	//	sprg1		userspace stack
	//	r4		MOL_ENTRY_R4_MAGIC
	//	r5		magic
	//	r6		call_kernel return point (if initializing)
	//	r7		session number | initialize flag
	//
	//	srr1		emulator msr
	//	r13-r31		mac gprs
	//	fr14-fr31	mac fp registers
	//	fr0-fr12	mac fp registers (see xFPU_STATE)

1:	li	r4,1
	stw	r4,ST_MAGIC(r1)
2:	mtcr	r3				// restore registers
	mfsprg_a1 r1
	mfsprg_a0 r3
	CONTINUE_TRAP( 0x700 )			// not MOL, take the trap

	balign_32
mac_entry:
	lis	r1,HA(EXTERN(Symbol_SESSION_TABLE))
	cmpwi	r4,MOL_ENTRY_R4_MAGIC		// MOL switch magic.
	addi	r1,r1,LO(EXTERN(Symbol_SESSION_TABLE))
	bne-	2b
	lwz	r4,ST_MAGIC(r1)			// check that the magic match
	cmplwi	r5,1				// and is greater than 1
	cmpw	cr1,r5,r4
	ble-	2b
	addi	r5,r1,ST_KVARS_PH		// get kvars for this session
	rlwinm	r4,r7,2,((MAX_NUM_SESSIONS-1)*4)
	bne-	cr1,1b				// r1 must point to session table
	lwzx	r1,r4,r5			// set r1 to kvars
	cmpwi	cr1,r1,0
	cmplwi	r7,MAX_NUM_SESSIONS
	beq-	cr1,2b				// kvars == NULL
	bge-	mac_entry_initialize		// initialize flag set

	// =====================================================================
	//  entrypoint
	// =====================================================================	

	__ZERO_TICK_CNT(entry)
	TRACE(0x1111, "mac_entry")
		
	// Save emulator registers (r1,msr) and restore flag bits
	mfsprg_a1 r3
	lwz	r4,xFLAG_BITS(r1)
	stw	r3,K_EMULATOR_STACK(r1)
	mfsrr1	r6
	mtcr	r4
	stw	r6,K_EMULATOR_MSR(r1)		// The MSR_FP/MSR_VEC bits are of interest...

	// Setup mac-environment

	btl	FBIT_RecalcDecInt, recalc_int_stamp	// M: r0,r2-r5	
	bl	set_mac_context			// M: r0,r2-r12,XER
	bl	set_mol_dec			// M: r0,r2-r5

	crset	FBIT_LoadSegreg			// Load segment registers below
	
	RESTORE_MIDDLE_REGS			// Loads r6,r7 (nip/msr) r8-r12, ctr, xer, 

	rlwinm	r7,r7,0,~MSR_VEC		// We always clear MSR_VEC (MSR_FP should be off already)
	stw	r7,K_MSR(r1)			// (enabling MSR_VEC is relatively cheap)
	btl-	FBIT_MsrModified,msr_altered_entry
	
	GET_TICK_CNT(entry, "mac_entry")
	
	bt-	FBIT_DecINT,test_for_dec_int	// check for DEC interrupts
	b	exception_return

test_for_dec_int:
	lwz	r3,xMSR(r1)
	rlwinm.	r3,r3,0,MSR_EE			// MSR_EE is set?
	bne	mac_dec_trap			// if so take a DEC interrupt
	b	exception_return
	
msr_altered_entry:
	crclr	FBIT_MsrModified
	b	force_msr_altered
	
	
	/////////////////////////////////////////////////////////////
	// All paths back to mac-mode goes through one of these
	// functions.
	//	
	// 	emulation_done
	//	emulation_done_noinc
	//	exception_return
	
	balign_32
emulation_done:
	addi	r6,r6,4				// increase NIP
emulation_done_noinc:
	bt-	FBIT_Trace, rvec_trace_trap
	GET_TICK_CNT(entry,"emulation_done")
exception_return:
	btl-	FBIT_LoadSegreg,reload_sr	// M: r3-r5, r6=nip
	mtsrr1	r7				// setup SRR1
	lwz	r0,xCR(r1)
	lwz	r3,xLINK(r1)
	mtsrr0	r6
	lwz	r4,xGPR4(r1)
	lwz	r5,xGPR5(r1)
	mfcr	r2				// Save flag bits
	lwz	r6,xGPR6(r1)
	lwz	r7,xGPR7(r1)
	mtlr	r3
	stw	r2,xFLAG_BITS(r1)
	lwz	r2,xGPR2(r1)
	lwz	r3,xGPR3(r1)
	mtcr	r0
	lwz	r0,xGPR0(r1)
	__GET_TICK_CNT(entry,"asm-all")		// performance measurements
	__BUMP("asm-all")
	lwz	r1,xGPR1(r1)
	rfi

	
/************************************************************************/
/*	Exit Mac-Mode Paths						*/
/************************************************************************/

	// THESE ARE _ALL_ THE POSSIBLE EXIT PATHS. KEEP IT THAT WAY
	// OR HAVE A *VERY GOOD* REASON TO INTRODUCE A NEW ONE.	

	/////////////////////////////////////////////////////////
	// giveup_fpu
	//
	//	Save fpscr and fpr13 and clear the MSR_FP bit.
	//	Restore the emulator fpscr value.
	//
	//	IMPORTANT: Call this function only if FBIT_FPUInUse is set
	//
	//	modifies: r7,r8 (turns on MSR_FP if FP is set in K_MSR)
	
giveup_fpu:
	li	r8,MSR_FP | MSR_FE0 | MSR_FE1
	lwz	r7,K_MSR(r1)
	andc	r8,r7,r8			// Clear MSR_FEx bits
	stw	r8,K_MSR(r1)
	
	mfmsr	r7				// Temporary enable FPU in order to 
	ori	r8,r7,MSR_FP			// save fpscr and fpr13
	mtmsr	r8
	isync
	stfd	fr13,xFPR13(r1)
	mffs	fr13
	stfd	fr13,xFPSCR-4(r1)
	li	r7,FPU_STATE_DIRTY
	lfd	fr13,xEMULATOR_FPSCR-4(r1)	// We must restore FPSCR before since the emulator might
	mtfsf	0xff,fr13			// use the FPU at any time, for instance in a signal handler.
	stw	r7,xFPU_STATE(r1)		// Go to FPU_STATE_DIRTY

	crclr	FBIT_FPUInUse			// FPU no longer in use
	blr


	////////////////////////////////////////////////////////
	// PREPARE_ERET
	//
	// M: r0,r2, r9-r11
	
MACRO(PREP_ERET,[nip_variable], [
	btl	FBIT_MolDecLoaded, set_kernel_dec	// M: r0,r2,r9-r11

	lwz	r10,_nip_variable[](r1)
	mfcr	r9
	lwz	r11,K_EMULATOR_MSR(r1)
	mtsrr0	r10
	lwz	r2,K_EMULATOR_TOC(r1)
	stw	r9,xFLAG_BITS(r1)	
	mtsrr1	r11
])

	////////////////////////////////////////////////////////
	// mac_exit (return to emulator)
	//	r3	RVEC return code
	//
	// On stack:		nip, ctr, lr, xer, r0-r12
	// In registers:	r13-r31
	
mac_exit:
	TRACE(0x2220, "mac_exit")
	bl	set_emulator_context		// M: r0,r2,r7-r11,XER
exit_:
	PREP_ERET K_EMULATOR_NIP		// M: r0-r2,r9-r11
	GET_TICK_CNT(entry, "mac_exit")
	lwz	r1,K_EMULATOR_STACK(r1)
	rfi

		
	////////////////////////////////////////////////////////
	// take_exception (take a linux exception)
	//
	// On stack:		nip, ctr, lr, xer, r0-r12
	// In registers:	r13-r31

take_exception:
	TRACE(0x2221, "take_exception")
	
	mflr	r12
	bl	set_emulator_context		// M: r0,r2,r7-r11,XER

	PREP_ERET K_EMULATOR_NIP		// M: r0,r2,r9-r11
	GET_TICK_CNT(entry, "take_exception")
	lwz	r1,K_EMULATOR_STACK(r1)
	mtlr	r12
	li	r3,RVEC_NOP	
	blr


	//////////////////////////////////////////////////////////////
	// call_kernel (call mol kernel routine)
	//	r3	kernel routine
	//	r4..r6	args
	//
	// On stack:		nip, ctr, lr, xer, r0-r12
	// In registers:	r13-r31

#ifdef __linux__
call_kernel_save:
	bl	save_middle_regs		// saves r8-r11, nip, ctr, xer
call_kernel:
	bl	set_emulator_context		// M: r0,r2,r7-r11,XER

	TRACE(0x2222, "call_kernel")
		
	lwz	r8,K_KERNEL_VARS(r1)		// r8 = kvars (lvptr)
	PREP_ERET K_EMULATOR_KCALL_NIP		// M: r0,r2,r9-r11
	GET_TICK_CNT(entry, "call_kernel_save")
	lwz	r1,K_EMULATOR_STACK(r1)
	ba	0x2f00				// MOL trampoline
#endif	

/************************************************************************/
/*	Set Mac/Emulator Context					*/
/************************************************************************/

	//////////////////////////////////////////////////////////////
	// set_mac_context [r0,r2-r12, ctr, --->XER<---]
	//
	//	- clear BATs (except DBAT0)
	//	- setup sprgs
	//	- reload_sr loads segment registers later on
	//
	// Currently unmodified r8-r12, ctr

set_mac_context:
	// Save and setup SPRG2 (magic) and SPRG3 (mol stack)
	mfsprg_a2 r6
	mfsprg_a3 r7	
	stw	r6,K_EMULATOR_SPRG2(r1)
	stw	r7,K_EMULATOR_SPRG3(r1)
	li	r2,MOL_SPRG2_MAGIC
	mtsprg_a3 r1
	mtsprg_a2 r2

	li	r4,0
	mtspr	IBAT0U,r4
	mtspr	IBAT1U,r4
	mtspr	IBAT2U,r4
	mtspr	IBAT3U,r4
	// DBAT0 set from reload_sr
	mtspr	DBAT1U,r4
	mtspr	DBAT2U,r4
	mtspr	DBAT3U,r4
	blr

		
	///////////////////////////////////////////////////////////////	
	// set_emulator_context [r0,r2,r7-r11,cr, --->XER<---]
	//
	// - load segr 0-15 with emulator context
	// - restore BATs
	// - restore DEC register

set_emulator_context:
	lwz	r0,K_EMULATOR_SPRG2(r1)
	lwz	r2,K_EMULATOR_SPRG3(r1)	
	mtsprg_a2 r0
	mtsprg_a3 r2
		
	// Restore segment registers
	addi	r8,r1,K_EMULATOR_SR
	LOAD_SEGMENT_REGS r8, /**/ r2,r10

	// BATS, r11 = linux DEC

	lwz	r7,K_IBAT0U_SAVE(r1)
	mtspr	IBAT0U,r7
	lwz	r2,K_IBAT1U_SAVE(r1)
	mtspr	IBAT1U,r2
	lwz	r7,K_IBAT2U_SAVE(r1)
	mtspr	IBAT2U,r7
	lwz	r2,K_IBAT3U_SAVE(r1)
	mtspr	IBAT3U,r2

	lwz	r7,K_DBAT0U_SAVE(r1)
	mtspr	DBAT0U,r7
	lwz	r7,K_DBAT0L_SAVE(r1)		// must also restore lower bat...
	mtspr	DBAT0L,r7
	lwz	r2,K_DBAT1U_SAVE(r1)
	mtspr	DBAT1U,r2
	lwz	r7,K_DBAT2U_SAVE(r1)
	mtspr	DBAT2U,r7
	lwz	r2,K_DBAT3U_SAVE(r1)
	mtspr	DBAT3U,r2
	blr


/************************************************************************/
/*	Reload Segment Registers					*/
/************************************************************************/

	////////////////////////////////////////////////////////////// 
	// reload_sr
	//
	//	r6 = mac-nip
	//
	// - loads segr 0-15 with mac context [modifies r3-r5]
	// - reloads DBAT0 (used for splitmode)
	//
	// Modifies: r3-r5
	
reload_sr:
	bt	FBIT_InSplitmode,prepare_splitmode		// M: r0,r3-r5, r6=mac-nip

	lwz	r3,K_TRANSL_DBAT0L(r1)
	lwz	r4,K_TRANSL_DBAT0U(r1)
	li	r5,0
	mtspr	DBAT0L,r3
	mtcrf	MMU_CR_FIELD,r5		// clear FBIT_LoadSegreg (and splitmode stuff)
	mtspr	DBAT0U,r4
1:	
	lwz	r4,K_CUR_SR_BASE(r1)
	LOAD_SEGMENT_REGS r4, /**/ r3,r5
	blr

ret_from_prep_splitmode:
	lwz	r3,K_SPLIT_DBAT0L(r1)
	lwz	r4,K_SPLIT_DBAT0U(r1)
	li	r5,fb_InSplitmode
	mtspr	DBAT0L,r3
	mtcrf	MMU_CR_FIELD,r5		// clear FBIT_LoadSegreg and FBIT_PrepareSplitmode
	mtspr	DBAT0U,r4
	b	1b
