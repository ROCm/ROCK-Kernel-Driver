/* 
 *   Creation Date: <2000/07/11 03:38:32 samuel>
 *   Time-stamp: <2003/08/20 16:37:04 samuel>
 *   
 *	<splitmode.S>
 *	
 *	Handles splitmode (MSR_IR != MSR_DR)
 *   
 *   Copyright (C) 2000, 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

MACRO(SM_SET_MSR_DR, [scr], [
	li	_scr,MSR_DR
	mtmsr	_scr
	isync
])
MACRO(SM_CLEAR_MSR_DR, [scr], [
	li	_scr,0
	mtmsr	_scr
	isync
])
MACRO(SPLITMODE_SMP_LOCK, [scr1,scr2], [
#ifdef CONFIG_SMP
	LI_PHYS( _scr1,splitmode_lock )
7:	lwarx	_scr2,0,_scr1
	cmpwi	_scr2,0
	li	_scr2,1
	bne-	7b
	stwcx.	_scr2,0,_scr1
	bne-	7b
	isync
#endif
])
MACRO(SPLITMODE_SMP_UNLOCK, [scr1,scr2], [
#ifdef CONFIG_SMP
	LI_PHYS( _scr1,splitmode_lock )
	li	_scr2,0
	stw	_scr2,0(_scr1)
#endif
])

	/////////////////////////////////////////////////////////////
	// prepare_splitmode
	//
	//	r6,r7:	nip/srr1
	//
	// M: r0,r3-r5
	//
	// Fill in splitmode segment register table. The segment register
	// containing xNIP is set up for instruction access (if xNIP does
	// not hold nip, an extra ISI exception will occur). The instruction
	// segment is protected from data access through the use of a DBAT
	// register. 
	//
	// It MUST be safe to call this function even if we are *not* in
	// splitmode.

prepare_splitmode:
	bf	FBIT_PrepareSplitmode, ret_from_prep_splitmode

	ZERO_TICK_CNT(splitmode_prep)

	// fill split mode table with data segment registers
	lwz	r3,K_SR_DATA(r1)		// physical addr
	addi	r4,r1,K_SPLIT_SR_BASE-4
	li	r5,16
	mfctr	r0				// save ctr in r0
	mtctr	r5
	addi	r3,r3,-4
1:	lwzu	r5,4(r3)
	oris	r5,r5,0x1000			// no-execute segment bit
	stwu	r5,4(r4)
	bdnz	1b
	mtctr	r0				// restore ctr
	
	// insert instruction mode segment
	rlwinm	r3,r6,0,0,3
	stw	r3,K_SPLIT_NIP_SEGMENT(r1)
	rlwinm	r3,r6,4+2,26,29			// r3 = offset, ((sr & 0xf000000) >> 28 ) * 4
	lwz	r5,K_SR_INST(r1)
	lwzx	r5,r3,r5			// segment register for instructions
	addi	r4,r1,K_SPLIT_SR_BASE
	stwx	r5,r3,r4

	// and protect it with DBAT0. 
	//
	// The supervisor valid bit must be cleared 
	// - we don't want to block get_opcode.

	rlwinm	r3,r6,0,0,3			// segment base
	ori	r3,r3,0x1ffd			// user valid bit | 256MB mask
	stw	r3,K_SPLIT_DBAT0U(r1)
	li	r4,0
	stw	r4,K_SPLIT_DBAT0L(r1)		// pp=0, wimg=0
	GET_TICK_CNT(splitmode_prep, "splitmode_prep")
	b	ret_from_prep_splitmode
	

	///////////////////////////////////////////////////////////////////
	// split_sr_no_execute
	//
	//	r6,r7:		nip/srr1
	//
	// An instruction is to be fetched from one of the no-execute
	// segments. This function reinitializes the segment registers.
	//
	// M: r0, r3-r5

split_sr_no_execute:
	rlwinm.	r0,r7,0,3,3			// Guarded access or no-execute?
	beqlr

	rlwinm	r3,r6,0,0,3			// segment
	lwz	r4,K_SPLIT_NIP_SEGMENT(r1)
	cmpw	r3,r4
	beqlr					// guarded PTE/mac-guarded segment

	crset	FBIT_PrepareSplitmode
	crset	FBIT_LoadSegreg
	b	exception_return



	//////////////////////////////////////////////////////////////////
	// splitmode_dsi
	//
	//	r6/r7:		nip/srr1
	//
	// An DSI exception occured (DBAT protection violation).
	// That is, a load/store instruction targeted the segment
	// instructions was fetched from.
	//
	// Safe to modify: r0,r2-r5, (lr)

splitmode_dsi:
	mfdsisr	r3				// DBAT/page protection violation?
	rlwinm.	r0,r3,0,4,4
	beqlr-					// If not, it does not concern us

	mfdar	r2				// Normal page protected exception?
	lwz	r4,K_SPLIT_NIP_SEGMENT(r1)	// the instruction segment?
	rlwinm	r5,r2,0,0,3			// data segment
	cmpw	r4,r5
	bnelr					// exit - not in the instruction segment

	// splitmode write, r2=dar
	mfsrin	r0,r2				// r0 = old segment register
	stw	r6,xNIP(r1)			// need one additional reg
	rlwinm	r4,r2,4+2,26,29			// sr_offset = sr_num * 4
	lwz	r5,K_SR_DATA(r1)
	lwzx	r4,r4,r5
	stw	r0,K_TMP_SCRATCH0(r1)		// save old segment register
	oris	r4,r4,0x4000			// set supervisor key bit (Ks)
	mtsrin	r4,r2

	rlwinm.	r3,r3,0,6,6			// cr[eq] set if this a read
	lwz	r3,xINST_OPCODE(r1)
	LI_PHYS( R4, secint_splitm )		// r4 = secint handler

	// handle simple stores r2=seg#, r3=opcode, r4=secint
	beq	splitm_load

	rlwinm	r5,r3,6+5+3,(0x1f<<3)		// r5 = rS << 3
	EMU_LOAD_GPR r5, /**/ R6		// r0 = value
	mtlr	r4				// secint handler	
	SM_SET_MSR_DR /**/ r5

	rlwinm	r6,r3,6,0x3e			// primary opcode & ~1
	mtcrf	0x40,r3				// cr[5] = update bit (if opcode != 31)
	cmpwi	r6,30				// 31 & ~1
	beq-	splitm_store_op31
	cmpwi	r6,36				// 36, stw/stwu
	beq	splitm_stw
	cmpwi	r6,38				// 38, stb/stbu
	beq	splitm_stb
	cmpwi	r6,44				// 44, stb/stbu
	beq	splitm_sth
	b	splitm_fallback
splitm_store_op31:
	rlwinm. r6,r3,0,(32<<1)			// update form?
	rlwinm	r6,r3,32-1,22,31		// secondary opcode
	rlwinm	r6,r6,0,~32			// clear update bit
	crnot	5,eq
	cmpwi	r6,151				// stwx/stwxu
	beq+	splitm_stw
	cmpwi	r6,215				// stbx / stbxu
	beq-	splitm_stb
	cmpwi	r6,407				// sthx / sthxu
	beq-	splitm_sth
	rlwinm	r6,r3,32-1,22,31		// secondary opcode
	cmpwi	r6,150				// stwcx.
	beq-	splitm_stwcx
	cmpwi	r6,1014				// 1014, dcbz	
	beq-	splitm_dcbz
	cmpwi	r6,662				// 662, stwbrx
	beq-	splitm_stwbrx
	b	splitm_fallback

splitm_load:
	mtlr	r4				// secint handler
	SM_SET_MSR_DR /**/ r5
	rlwinm	r6,r3,6,0x3e			// primary opcode & ~1
	mtcrf	0x40,r3				// cr[5] = update bit (if opcode != 31)
	cmpwi	r6,30				// 31 & ~1
	beq-	splitm_load_op31
	cmpwi	r6,32				// 32, lwz/lwzu
	beq+	splitm_lwz
	cmpwi	r6,34				// 34, lbz/lbzu
	beq-	splitm_lbz
	cmpwi	r6,40				// 40, lhz/lhzu
	beq-	splitm_lhz
	b	splitm_fallback
splitm_load_op31:
	rlwinm. r6,r3,0,(32<<1)			// update form?
	rlwinm	r6,r3,32-1,22,31		// secondary opcode
	rlwinm	r6,r6,0,~32			// clear update bit
	crnot	5,eq
	cmpwi	r6,23				// 23, lwzx/lwzux
	beq+	splitm_lwz
	cmpwi	r6,87				// 87, lbzx/lbzux
	beq-	splitm_lbz
	cmpwi	r6,279				// 279, lhzx/lhzux
	beq-	splitm_lhz
	rlwinm	r6,r3,32-1,22,31		// secondary opcode
	crclr	5
	cmpwi	r6,20				// 20, lwarx
	beq-	splitm_lwarx
	cmpwi	r6,86				// 86, dcbf
	beq-	splitm_dcbf
	cmpwi	r6,982				// 982, icbi
	beq-	splitm_icbi
	cmpwi	r6,534				// 534, lwbrx
	beq-	splitm_lwbrx
	b	splitm_fallback


	// r0=value, r2=ea, r3=opcode
splitm_stwcx:
	stwcx.	r0,0,r2
	SM_CLEAR_MSR_DR /**/ r0
	lwz	r6,xCR(r1)
	mfcr	r0
	rlwimi	r6,r0,0,0,3
	stw	r6,xCR(r1)
	b	splitm_done2
splitm_sth:	
	sth	r0,0(r2)
	b	splitm_store_continue
splitm_stb:
	stb	r0,0(r2)
	b	splitm_store_continue
splitm_stw:
	stw	r0,0(r2)
	b	splitm_store_continue
splitm_lwz:
	lwz	r0,0(r2)
	b	splitm_load_continue
splitm_lhz:
	lhz	r0,0(r2)
	b	splitm_load_continue
splitm_lbz:
	lbz	r0,0(r2)
	b	splitm_load_continue
splitm_lwarx:
	lwarx	r0,0,r2
	b	splitm_load_continue
splitm_lwbrx:
	lwbrx	r0,0,r2
	b	splitm_load_continue
splitm_dcbz:
	dcbz	0,r2
	b	splitm_done
splitm_icbi:
	icbi	0,r2
	b	splitm_done
splitm_dcbf:
	dcbf	0,r2
	b	splitm_done
splitm_stwbrx:	
	stwbrx	r0,0,r2
	b	splitm_done

splitm_load_continue:
	SM_CLEAR_MSR_DR /**/ r4
	BUMP("splitm_load")
	rlwinm	r4,r3,6+5+3,(0x1f<<3)		// r5 = rS << 3
	EMU_STORE_GPR r4, /**/ R6		// r0 = value
	bf+	5,splitm_done2			// update form?
	b	1f

splitm_store_continue:	
	SM_CLEAR_MSR_DR /**/ r0
	BUMP("splitm_store")
	bf+	5,splitm_done2			// update form?
1:	rlwinm	r3,r3,6+5+5+3,(31<<3)
	mr	r0,r2
	EMU_STORE_GPR r3, /**/ R6		// r0 = value
	b	splitm_done2
splitm_done:
	SM_CLEAR_MSR_DR /**/ r0
splitm_done2:	
	lwz	r3,K_TMP_SCRATCH0(r1)
	lwz	r6,xNIP(r1)			// restore NIP
	mtsrin	r3,r2
	b	emulation_done


	// fallback, store and execute the instruction, r3=opcode
splitm_fallback:
	SM_CLEAR_MSR_DR /**/ r0
#if 0
	stw	r3,xDEBUG1(r1)
	stw	r6,xDEBUG0(r1)
	lwz	r6,xNIP(r1)
	DEBUGGER_SAVE(0x1111)	
#endif
	SPLITMODE_SMP_LOCK /**/ r0,r2

	BUMP("splitm_fallback")
	bl	secint_splitm_fallback		// set secondary exception handler

	LI_PHYS( R2,split_store_patch )		// r2 = addr of split_store_patch
	stw     r3,0(r2)			// store instruction
	dcbst   0,r2
	sync
	icbi    0,r2
	sync					// 74xx needs this

	mtsrr0	r2				// The simplest thing is to do an RFI
	LOADI	r3,(MSR_EE | MSR_PR | MSR_IR | MSR_SE | MSR_BE)
	andc	r4,r7,r3			// Clear msr bits (r7=srr1)
	xGPR_LOAD	R6
	xGPR_LOAD	R7	
	mtsrr1	r4
	xGPR_LOAD_RANGE	R2,R5,r1		// Restore registers (except r1)
	xGPR_LOAD	R0
	xGPR_LOAD	R1
	rfi

split_store_patch:
	nop

	mtsprg_a0 r1				// restore MSR
	li	r1,MSR_ME
	mtmsr	r1
	isync
	mfsprg_a3 r1				// and stack pointer

	xGPR_SAVE_RANGE R2,R7,r1
	SPLITMODE_SMP_UNLOCK /**/ r3,r4

	mfsprg_a0 r2				// r1 - to be saved
	lwz	r6,xNIP(r1)			// restore r6,r7 and segment register
	lwz	r7,K_MSR(r1)
	stw	r0,xGPR0(r1)
	stw	r2,xGPR1(r1)
	lwz	r2,K_TMP_SCRATCH0(r1)
	mtsrin	r2,r6

	GET_TICK_CNT( entry, "splitmode_dsi" )
	b	emulation_done


	//////////////////////////////////////////////////////////////////////
	// secint_splitm / secint_splitm_fallback
	//	r1:		stack (sprg1 = old r1)
	//	r3:		vector index (sprg0 = old r3)
	//	srr0/srr1:	kernel nip/msr
	//
	// xGPR(0-5) are valid (unless this is a trace exception)

secint_splitm_fallback:
	blrl
	SPLITMODE_SMP_UNLOCK /**/ R2,R4

secint_splitm:
	lwz	r6,xNIP(r1)			// Restore nip/msr
	lwz	r7,K_MSR(r1)

	cmpwi	r3,0x300			// ** DSI **
	bne-	1f
	mfsrin	r2,r6				// r6 = NIP
	rlwinm	r2,r2,0,2,0			// Clear Ks [bit1] (supervisor key bit)
	mtsrin	r2,r6
	bl	save_middle_regs		// Note: If dsi_cont ever returns immediately,
	bl	check_io_page			// we will need to fix the segment registers before
	b	dsi_cont			// the last dsi_cont branch.

1:	lwz	r2,K_TMP_SCRATCH0(r1)		// We might return immediately...
	mtsrin	r2,r6
	
	cmpwi	r3,0x600			// ** Alignment **
	bne	2f
	bl	save_middle_regs
	b	alignment_cont
	
2:	cmpwi	r3,0x800			// ** FPU Unavailable **
	beq	fpu_cont
	cmpwi	r3,0xf20			// ** AltiVec Unavailable **
	beq	altivec_cont
	
	DEBUGGER_SAVE( 0x5918 )			// ERROR...

				
	////////////////////////////////////////////////////////////////////////
	// invalidate_splitmode( kernel_vars_t *kv )
	//
	// This function must be called whenever the segment registers are 
	// modified. A flag is set which will force a refresh of the slitmode 
	// segment registers (at mac context switch in). We could rewrite this 
	// in C but it might be better to keep things centralized.

GLOBAL_SYMBOL(r__invalidate_splitmode_sr):
	// this will have no effect if fb_InSplitmode is not set
	lwz	r4,xFLAG_BITS(r3)
	ori	r4,r4,fb_PrepareSplitmode | fb_LoadSegreg
	stw	r4,xFLAG_BITS(r3)
	blr

#ifdef CONFIG_SMP
splitmode_lock:
	.long	0
#endif
