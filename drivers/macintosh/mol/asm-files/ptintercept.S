/* 
 *   Creation Date: <2001/03/17 18:00:05 samuel>
 *   Time-stamp: <2003/05/26 00:08:48 samuel>
 *   
 *	<ptintercept.S>
 *	
 *	Handles writes to the (mac) hash table
 *   
 *   Copyright (C) 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

//////////////////////////////////////////////////////////////////////
// Implementing the tlbie instruction properly is tricky.
// The tlbie is supposed to invalidate an equivalence 
// class of PTEs and this does not map well to the huge TLB
// MOL uses (the linux PTE hash, a lot bigger than the 2x64
// table found in most CPUs).
//
// The solution is intercepting PTE writes and maintain
// coherence without relying on the tlbie instruction (which
// can safely be replaced by a nop).
//
// Splitmode: The instruction sr might be active. DBAT0 does
// _not_ have the supervisor valid bit set so we have to load
// the data segment register.
//////////////////////////////////////////////////////////////////////

MACRO(SET_MSR_DR, [scr], [
	li	_scr,MSR_DR
	mtmsr	_scr
	isync
])
MACRO(CLEAR_MSR_DR, [scr], [
	li	_scr,0
	mtmsr	_scr
	isync
])

	// r3 = exception vector
secint_pt_store:
	bf+	FBIT_InSplitmode,1f
	mfdar	r2
	lwz	r4,K_TMP_SCRATCH0(r1)		// in splitmode (might be
	mtsrin	r4,r2				// the instrucion sr)
1:	
	lwz	r6,xNIP(r1)			// restore NIP & MSR
	lwz	r7,K_MSR(r1)	
	cmpwi	r3,0x300
	beq	1f
	cmpwi	r3,0x600			// Alignment
	beq	2f
	DEBUGGER_SAVE( 0x6909 )
1:	bl	save_middle_regs
	b	dsi_cont
2:	bl	save_middle_regs
	b	alignment_cont
		
	////////////////////////////////////////////////////////////////////////
	// check_pthash_hit
	// 
	// m: r0,r2-r5, cr
	
check_pthash_hit:
	mfdsisr	r3
	rlwinm.	r4,r3,0,4,4			// Is this a protection violation?
	beqlr
	rlwinm.	r4,r3,0,6,6			// and a write?
	beqlr-

	mfdar	r2				// Is this a pthash hit?
	lwz	r3,K_TLBHASH_BASE_EA(r1)	// First check EA...
	lwz	r4,K_HASH_MASK(r1)
	sub	r5,r2,r3
	cmplw	r5,r4
	bgtlr+

	lwz	r5,K_SR_DATA(r1)		// Correct context for EA?
	rlwinm	r3,r2,4+2,26,29			// #sr << 2
	lwz	r4,K_TLBHASH_SR(r1)
	lwzx	r3,r3,r5	
	cmpw	r3,r4
	bnelr-

	/////////////////////////////////////////////////
	// Handle page table write,  r2=dar, r4=segreg
	/////////////////////////////////////////////////

	bf+	FBIT_InSplitmode,1f	
	mfsrin	r3,r2				// save old segment register
	stw	r3,K_TMP_SCRATCH0(r1)		// in splitmode (might be
	mtsrin	r4,r2				// the instrucion sr)
1:	
	lwz	r3,xINST_OPCODE(r1)
	LI_PHYS( R4,secint_pt_store )		// r4 = secondary exception handler
	stw	r6,xNIP(r1)
	
	// r2 = dar, r3 = opcode
	rlwinm	r0,r3,6,0x3e			// primary opcode & ~1
	rlwinm	r5,r3,6+5+3,(0x1f<<3)		// r5 = rS << 3
	cmpwi	r0,30				// 31 & ~1
	beq-	do_opcode_31
	mtcrf	0x40,r3				// cr[5] = update bit
	cmpwi	r0,36				// 36, stw/stwu
	beq	do_stw
	cmpwi	r0,38				// 38, stb/stbu
	beq	do_stb
	cmpwi	r0,44				// 44, stb/stbu
	beq	do_sth
	cmpwi	r0,52				// 52, stfs/stsu
	beq	do_stfs
	cmpwi	r0,54				// 54, stfd/stfdu
	beq	do_stfd
	cmpwi	r0,46				// 47, stmw
	beq	do_stmw
	b	do_st_bad			// ???

do_opcode_31:
	rlwinm	r0,r3,32-1,22,31		// secondary opcode
	rlwinm. r6,r3,0,(32<<1)			// update form?
	rlwinm	r0,r0,0,~32			// clear update bit
	crnot	5,eq

	cmpwi	r0,151				// stwx/stwxu
	beq+	do_stw
	cmpwi	r0,215				// stbx / stbxu
	beq-	do_stb
	cmpwi	r0,150				// stwcx.
	beq-	do_stwcx
	cmpwi	r0,407				// sthx / sthxu
	beq-	do_sth
	cmpwi	r0,727				// stfdx / stfdxu
	beq-	do_stfd
	cmpwi	r0,663				// stfsx / stfsxu
	beq-	do_stfs			
	crclr	5
	rlwinm	r0,r3,32-1,22,31		// unmasked secondary opcode
	cmpwi	r0,470				// dcbi
	beq-	do_dcbi
	cmpwi	r0,1014				// dcbz
	beq-	do_dcbz
	cmpwi	r0,983				// stfiwx [optional]
	beq-	do_stfiw
	cmpwi	r0,725				// stswi
	beq-	do_stswi
	cmpwi	r0,661
	beq-	do_stswx
	b	do_st_bad			// float, cache or altivec

do_st_bad:
do_stfd:
do_stfs:
do_stfiw:
do_stswi:
do_stswx:
	lwz	r6,xNIP(r1)
	DEBUGGER_SAVE(0x1882)			// unimplemented store instruction

	// r2=dar, r3=opcode, r4=secint_handler, r5=rS_offs
do_dcbi:
do_dcbz:
	mtlr	r4
	SET_MSR_DR /**/ r6
	rlwinm	r6,r2,0,~0x7
	lwz	r4,0(r6)
	lwz	r5,4(r6)
	dcbz	0,r2
	CLEAR_MSR_DR /**/ r0
	b	st_continue_2
do_stwcx:
	EMU_LOAD_GPR r5, /**/ R6		// r0 = value
	mtlr	r4
	SET_MSR_DR /**/ r6
	rlwinm	r6,r2,0,~0x7
	lwz	r4,0(r6)
	lwz	r5,4(r6)
	stwcx.	r0,0,r2
	CLEAR_MSR_DR /**/ r0
	lwz	r6,xCR(r1)
	mfcr	r0
	rlwimi	r6,r0,0,0,3
	stw	r6,xCR(r1)
	b	st_continue_2
do_sth:	
	EMU_LOAD_GPR r5, /**/ R6		// r0 = value
	mtlr	r4
	SET_MSR_DR /**/ r6
	rlwinm	r6,r2,0,~0x7
	lwz	r4,0(r6)
	lwz	r5,4(r6)
	sth	r0,0(r2)
	b	st_continue
do_stb:
	EMU_LOAD_GPR r5, /**/ R6		// r0 = value
	mtlr	r4
	SET_MSR_DR /**/ r6
	rlwinm	r6,r2,0,~0x7
	lwz	r4,0(r6)
	lwz	r5,4(r6)
	stb	r0,0(r2)
	b	st_continue
do_stw:
	EMU_LOAD_GPR r5, /**/ R6		// r0 = value
	mtlr	r4
	SET_MSR_DR /**/ r6
	rlwinm	r6,r2,0,~0x7
	lwz	r4,0(r6)
	lwz	r5,4(r6)
	stw	r0,0(r2)
	b	st_continue

	// r4=PTE0, r5=PTE1, r3=opcode, r2=dar, scratch: r0
st_continue:
	CLEAR_MSR_DR /**/ r0
	bf+	5,st_continue_2			// update form?
	rlwinm	r3,r3,6+5+5+3,(31<<3)
	mr	r0,r2
	EMU_STORE_GPR r3, /**/ R6		// r0 = value
st_continue_2:
	// check if the old PTE has been used (r2=dar,r4/r5=PTE)
	lwz	r3,K_TLBHASH_BASE_EA(r1)	// Calculate tlb offset
	sub	r0,r2,r3			// r0 = pte_nr * 8
	lwz	r6,K_PTHASH_INUSE_PH(r1)
	rlwinm	r3,r0,32-6,6,29			// r3 = word offset
	cmpwi	r6,0
	beq-	1f
	lwzx	r3,r3,r6
	rlwinm	r6,r2,32-3,27,31		// pte_nr & 0x1f
	li	r0,1
	slw	r6,r0,r6			// r6 = bit
	and.	r6,r6,r3
	GET_TICK_CNT(entry,"ptintercept-1")
	bne-	pt_intercept
1:	
	BUMP("pt_intercept_not_taken")
	// return from exception [r2=dar]
	lwz	r6,xNIP(r1)			// restore r6
	bf+	FBIT_InSplitmode,emulation_done
	lwz	r3,K_TMP_SCRATCH0(r1)		// in splitmode (might be
	mtsrin	r3,r2				// the instrucion sr)
	b	emulation_done

	// transfer to C-function [r2=dar, r3/r4=pte]
pt_intercept:
	BUMP("pt_intercept_taken")
	lwz	r6,xNIP(r1)			// restore r6
	lwz	r3,K_TLBHASH_BASE_EA(r1)	// calculate tlb offset
	addi	r6,r6,4				// inc NIP
	bl	save_middle_regs
	sub	r6,r2,r3
	rlwinm	r6,r6,0,0,28			// argument pteoffs (dword aligned)

	// r4=PTE0, r5=PTE1
	LOADI	r3,EXTERN(do_intercept_tlbie)
	b	call_kernel

	// stmw uses another emulator entry point because it might overwrite a bunch of PTEs
do_stmw:
	mtlr	r4

	// save some debugging info
//	stw	r2,xDBG_TRACE_SPACE(r1)

	SET_MSR_DR /**/ r6

	// now do the stmw. we do that manually since we have to access emulator regs.
	mr	r4,r2				// r4: memory pointer
	srwi	r6,r5,3				// r6: rS
1:	cmpwi	r5,32 << 3			// loop condition
	bge	2f
	EMU_LOAD_GPR r5, /**/ R3		// r0 = value
	stw	r0,0(r4)			// store the value
	addi	r5,r5,1 << 3			// update register counter
	addi	r4,r4,4				// and memory pointer
	b	1b

2:	CLEAR_MSR_DR /**/ r0

	// load up r4 and r5 for do_intercept_tlbie_block (see below)
	subfic	r5,r6,32			// number of registers (=words) stored
	slwi	r5,r5,2				// number of bytes stored
	add	r4,r2,r5			// last byte stored
	addi	r4,r4,7				// alignment to
	rlwinm  r5,r4,0,~0x7			// PTE size
	rlwinm	r4,r2,0,~0x7			// pte block pointer
	sub	r5,r5,r4			// substract block pointer -> length (in bytes)
	lwz	r3,K_TLBHASH_BASE_EA(r1)	// calculate tlb offset
	sub	r4,r4,r3			// tlb offset
	b	st_block_continue

	// transfer to C-function [r2=dar, r4=pte block offset, r5=pte block length]
st_block_continue:
	BUMP("pt_intercept_taken")
	lwz	r6,xNIP(r1)
	addi	r6,r6,4				// inc NIP
	bl	save_middle_regs

	LOADI 	r3,EXTERN(do_intercept_tlbie_block)
	b	call_kernel
