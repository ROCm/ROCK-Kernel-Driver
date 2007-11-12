/* 
 *   Creation Date: <97/07/26 18:23:02 samuel>
 *   Time-stamp: <2004/02/22 13:12:14 samuel>
 *   
 *	<emulation.S>
 *	
 *	Low-level emulation of some privileged instructions
 *   
 *   Copyright (C) 1997-2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */
	
MACRO_0(INC_NIP, [
	addi	r6,r6,4
])

	
/************************************************************************/
/*	program exception vector					*/
/************************************************************************/
	
	//////////////////////////////////////////////////////////////
	// exception preamble
	//	r1		stack (mregs)
	//	r6,r7		nip / srr1
	//	cr4-7		flag_bits
	//
	// Saved r0-r5 (NOT SAVED: ctr, xer)

VECTOR_( 0x700, "Program", secint_bad, mac_entry )
	EXCEPTION_PREAMBLE			// 46 cycles on a G3
	TRACE(0x700, "Program")

	mtcrf	0x10,r7				// put srr1 bits (12-15) into cr3
	bt	14,mac_program_trap
	GET_INSTR_OPCODE			// r6=nip, r2-r3, r4=opcode (cr not touched)
	bt+	13,emulate_priv_instr
	bt+	12,emulate_illegal_instr
	mtcrf	0x20,r7				// put srr1 bits (8-11) into cr2
	bt+	11,mac_program_trap		// bit 11 = fpu_exception

	// we should not come here
	MAC_EXIT_SAVE( RVEC_UNUSUAL_PROGRAM_EXCEP )

unhandled_priv_inst:
	MAC_EXIT_SAVE( RVEC_PRIV_INST )		// r4 = opcode

emulate_illegal_instr:
	rlwinm	r2,r4,32-1,22,31
	lwz	r3,xMSR(r1)
	rlwinm	r5,r4,14,24,28			// r5 = reg_num << 3 (instr. bits 6-10)
	rlwimi	r2,r4,16,16,21			// r2 = opcode

	rlwinm	r0,r2,0,~0x80			// clear mtspr/mfspr bit
	cmpwi	r0,OPCODE(31,339)		// mfspr/mtspr
	bne	1f

	rlwinm	r7,r7,0,14,10			// clear srr1 bit 11-13
	cmpwi	cr1,r2,OPCODE(31,339)		// mfspr
	oris	r7,r7,0x4			// set bit 13 (privileged instr)

	beq-	cr1,emulate_mfspr		// r3=xMSR, r4=opcode, r5=reg_num
	b	emulate_mtspr			// must be mtspr	
1:
#ifdef EMULATE_603
	bl	emulate_603_instr
#endif
	MAC_EXIT_SAVE( RVEC_ILLEGAL_INST )	// r4 = opcode


/************************************************************************/
/*	mac exceptions							*/
/************************************************************************/
	
	/////////////////////////////////////////////////////////////
	// mac_trap
	//
	//	r2	exception vector
	//	r6	nip
	//	r7	reason bits (0-15)
	//
	// r0,r2-r5 may be modified

mac_irq_trap:
	li	r2,0x500
	li	r7,0			// no reason bits
	b	mac_trap

mac_dec_trap:
	li	r2,0x900
	crclr	FBIT_DecINT
	li	r7,0			// no reason bits
	b	mac_trap				

	balign_32
mac_program_trap:			// the reason bits already in r7 are used
	__BUMP("mac_trap")
	li	r2,0x700
mac_trap:
	lwz	r3,xMSR(r1)
	stw	r6,xSRR0(r1)		// mac-srr0 = nip
	mr	r4,r3			// copy xMSR to SRR1
	mr	r6,r2			// new nip
	rlwinm	r5,r3,25+1,31,31	// put MSR_IP (bit 25) in r5[31]
	rlwimi	r4,r7,0,0,15		// copy reason bits from r7 to SRR1
	rlwimi	r4,r3,0,6,6		// copy MSR_VEC bit of xMSR to SRR1
	stw	r4,xSRR1(r1)		// srr1 = (msr & (0xffff|MSR_VEC)) | (srr1 & ~(0xffff|MSR_VEC))
	rlwinm	r4,r3,0,25,25		// copy MSR_IP to xMSR
	neg	r5,r5			// r5 = 0xffffffff * MSR_IP
	rlwimi	r4,r3,0,19,19		// copy MSR_ME to xMSR
	rlwimi	r6,r5,0,0,11		// insert exception prefix
	stw	r4,xMSR(r1)
	GET_TICK_CNT(entry,"mac-expception")
	b	msr_exception_return

rvec_trace_trap:	
	MAC_EXIT_SAVE( RVEC_TRACE_TRAP )


/************************************************************************/
/*	decode privileged instruction					*/
/************************************************************************/

	/////////////////////////////////////////////////////////////
	// emulate_priv_instr
	//	r4	opcode
	//	r6,r7	nip/srr1
	//
	// r0,r2-r5,lr free

	balign_32
emulate_priv_instr:
	rlwinm	r2,r4,32-1,22,31
	lwz	r3,xMSR(r1)
	rlwinm	r5,r4,14,24,28			// r5 = reg_num << 3 (instr. bits 6-10)
	rlwimi	r2,r4,16,16,21			// r2 = opcode
	
	GET_TICK_CNT(entry,"get_inst")
	BUMP("decode_priv_inst")

	cmpwi	cr1,r2,OPCODE(31,339)		// mfspr (both user and supervisor mode)
	beq-	cr1,emulate_mfspr		// r3=xMSR, r4=opcode, r5=reg_num

	cmpwi	cr2,r2,OPCODE(31,467)		// mtspr (both user and supervisor mode)
	beq-	cr2,emulate_mtspr

	andi.	r3,r3,MSR_PR			// only emulate in supervisor mode
	bne-	mac_program_trap
	
	cmpwi	cr3,r2,OPCODE(31,83)		// mfmsr
	beq-	cr3,emulate_mfmsr

	cmpwi	cr0,r2,OPCODE(31,146)		// mtmsr
	beq-	cr0,emulate_mtmsr

	cmpwi	cr1,r2,OPCODE(19,50)		// rfi
	beq-	cr1,emulate_rfi

	cmpwi	cr2,r2,OPCODE(31,595)		// mfsr
	beq-	cr2,emulate_mfsr

	cmpwi	cr3,r2,OPCODE(31,659)		// mfsrin
	beq-	cr3,emulate_mfsrin

	cmpwi	cr0,r2,OPCODE(31,210)		// mtsr
	beq-	cr0,emulate_mtsr

	cmpwi	cr1,r2,OPCODE(31,242)		// mtsrin
	beq-	cr1,emulate_mtsrin

	cmpwi	cr2,r2,OPCODE(31,306)		// tlbie
	beq-	cr2,emulate_tlbie

	cmpwi	cr3,r2,OPCODE(31,566)		// tlbsync
	beq-	cr3,emulate_tlbsync

	cmpwi	cr0,r2,OPCODE(31,467)		// dcbi
	beq-	cr0,emulate_dcbi
#ifdef EMULATE_603
	cmpwi	cr1,r2,OPCODE(31,978)		// tlbld
	beq-	cr1,emulate_tlbld

	cmpwi	cr2,r2,OPCODE(31,1010)		// tlbli
	beq-	cr2,emulate_tlbli
#endif
	// Program-trap, illegal instruction
	b	unhandled_priv_inst		// r4 = opcode


#ifdef EMULATE_603
emulate_603_instr:
	rlwinm	r2,r4,32-1,22,31
	rlwimi	r2,r4,16,16,21			// r2 = opcode

	cmpwi	cr0,r2,OPCODE(31,978)		// tlbld
	beq	cr0,2f
	cmpwi	cr1,r2,OPCODE(31,1010)		// tlbli
	beq	cr1,2f
	cmpwi	cr2,r2,OPCODE(31,339)		// mfspr
	beq	cr2,1f
	cmpwi	cr3,r2,OPCODE(31,467)		// mtspr
	bnelr	cr3
1:	rlwinm	r3,r4,32-16,27,31
	rlwimi	r3,r4,32-6,22,26		// r3 = spr#
	cmpwi	r3,976				// first 603 SPR
	bltlr
	cmpwi	r3,982				// last 603 SPR
	bgtlr
2:
	rlwinm	r7,r7,0,14,10			// clear bit 11-13
	oris	r7,r7,0x4			// set bit 13
	b	emulate_priv_instr
	
#endif /* EMULATE_603 */

			
/************************************************************************/
/*	mac register access						*/
/************************************************************************/

MACRO(LOAD_EMUGPR_IM, [dreg,ereg], [
.if _ereg <= 7
	lwz	_dreg,xGPR0+_ereg[]*4(r1)
.else
	mr	_dreg,rPREFIX[]_ereg
.endif
])

MACRO(STORE_EMUGPR_IM, [sreg,ereg], [
.if _ereg <= 7
	stw	_sreg,xGPR0+_ereg[]*4(r1)
.else
	mr	rPREFIX[]_ereg, _sreg
.endif
])
	
	balign_32
store_gpr_table:
mFORLOOP([i],0,31,[
	STORE_EMUGPR_IM r0,i
	blr
])

load_gpr_table:
mFORLOOP([i],0,31,[
	LOAD_EMUGPR_IM r0,i
	blr
])

MACRO(EMU_LOAD_GPR, [reg, scr], [
	LI_PHYS( _scr, load_gpr_table )
	add	rPREFIX[]_scr,_reg,rPREFIX[]_scr
	mtlr	rPREFIX[]_scr
	blrl
])

MACRO(EMU_STORE_GPR, [reg, scr], [
	LI_PHYS( _scr, store_gpr_table )
	add	rPREFIX[]_scr,_reg,rPREFIX[]_scr
	mtlr	rPREFIX[]_scr
	blrl
])

	

/************************************************************************/
/*	instruction emulation						*/
/************************************************************************/

	//////////////////////////////////////////////////////////
	// emulate_xxxxx
	//	r3	xMSR
	//	r4	opcode
	//	r5	regnum<<3 (from opcode bits 6-10)
	//
	// May modify: r0,r2-r5 (lr)

/********************************************************************/
emulate_mfmsr:
	lwz	r0,xMSR(r1)
	EMU_STORE_GPR r5, /**/ R2
	GET_TICK_CNT( entry, "mfmsr" )
	b	emulation_done

/********************************************************************/
emulate_mfspr:					// r3 = xMSR
	BUMP("emulate_mfspr")
	rlwimi	r4,r4,32-10,21,25		// flip spr
	rlwinm	r0,r3,32-14,31,31		// r0(bit31) = MSR_PR	
	addi	r3,r1,K_SPR_HOOKS
	rlwinm	r2,r4,32-4,20,29		// r2 = spr# << 2
	addi	r4,r1,xSPR_BASE
	lwzx	r3,r2,r3			// hook in r3
	and.	r0,r0,r3			// privileged SPR?
	bne-	mac_program_trap
	lwzx	r0,r2,r4			// value in r0
	mtlr	r3
	blr					// call hook

/********************************************************************/
emulate_mtspr:					// r3 = xMSR
	BUMP("emulate_mtspr")
	rlwimi	r4,r4,32-10,21,25		// flip spr
	EMU_LOAD_GPR r5, /**/ R2		// value in r0
	rlwinm	r2,r4,32-4,20,29		// r2 = spr# << 2
	addi	r4,r1,K_SPR_HOOKS
	rlwinm	r3,r3,32-14,31,31		// r3(bit31) = MSR_PR	
	lwzx	r4,r2,r4			// hook in r4
	addi	r5,r2,xSPR_BASE			// r5+r1 points to SPR reg
	addi	r4,r4,4				// branch to hook +4
	and.	r3,r3,r4			// privileged SPR?
	bne-	mac_program_trap		// privileged exception
	mtlr	r4				// lsb is discarded...
	blr					// call hook
	

/********************************************************************/
	balign_32
emulate_mtmsr:
	lwz	r3,xMSR(r1)			// r3 = old MSR
	LI_PHYS( R2, load_gpr_table )
	add	r2,r5,r2
	mtlr	r2
	blrl					// r0 = new MSR

	INC_NIP
	bl	msr_altered

	GET_TICK_CNT(entry, "mtmsr")
	BUMP("emulate_mtmsr")
		
	beq+	cr1,emulation_done_noinc
	MAC_EXIT_SAVE( RVEC_MSR_POW )		// POW 1 => doze


/********************************************************************/
emulate_rfi:
	BUMP("rfi")
	lis	r4,(MSR_VEC)>>16
	lwz	r0,xSRR1(r1)
	ori	r4,r4,0xffff
#ifdef EMULATE_603
	oris	r4,r4,(MSR_TGPR)>>16
#endif
	lwz	r3,xMSR(r1)			// r3 = old MSR
	and	r0,r0,r4
	andc	r5,r3,r4
	lwz	r6,xSRR0(r1)			// new nip = SRR0
	or	r0,r0,r5
		
	bl	msr_altered			// r0,r2-r5, r7 [srr1] updated

	GET_TICK_CNT(entry,"rfi")

	lwz	r3,K_BREAK_FLAGS(r1)		// break at rfi support
	andi.	r3,r3,BREAK_RFI
	beq+	exception_return
	li	r4,BREAK_RFI			// r4 = flag causing the break
	MAC_EXIT_SAVE( RVEC_BREAK )


/********************************************************************/
#ifdef EMULATE_603
emulate_tlbli:
	LOADI	r3,EXTERN(do_tlbli)
	b	1f
emulate_tlbld:	
	LOADI	r3,EXTERN(do_tlbld)
	b	1f
emulate_tlbie:
	LOADI	r3,EXTERN(do_tlbie)
1:	INC_NIP
	rlwinm	r5,r4,32-8,24,28		// r5 = #B << 3
	EMU_LOAD_GPR r5, /**/ R2		// value ret. in r0
	mr	r4,r0				// r4 = ea
	b	call_kernel_save
#else
emulate_tlbie:
	b	emulation_done
#endif /* EMULATE_603 */
	
/********************************************************************/
emulate_tlbsync:
	b	emulation_done

/********************************************************************/
emulate_dcbi:
	b	unhandled_priv_inst		// r4 = opcode

/********************************************************************/
emulate_mfsrin:
	rlwinm	r2,r4,32-8,24,28		// r2 = #B << 3
	EMU_LOAD_GPR r2, /**/ R3		// r0 = reg B
	rlwinm	r3,r0,6,26,29			// r3 = #sr << 2
	b	1f
emulate_mfsr:
	rlwinm	r3,r4,32-14,26,29		// r3 = #sr << 2
1:	addi	r2,r1,xSEGR_BASE
	lwzx	r0,r3,r2
	EMU_STORE_GPR r5, /**/ R2
	GET_TICK_CNT(entry, "mfsr")
	BUMP("mfsr_")
	b	emulation_done


			
/************************************************************************/
/*	SPR - emulation							*/
/************************************************************************/

	////////////////////////////////////////////////////////////
	// read (offset 0)
	//	r0	spr_value
	//	r2	spr << 2
	//	r5	dreg << 3
	//
	// write (offset 4)
	//	r0	gpr_value
	//	r2	spr << 2
	//	r5	spr offset (relative r1)
	//
	// Safe to modify:	r2-r5, lr
	// NOT SAVED:		ctr, xer

/********************************************************************/
simple_mfspr:
	EMU_STORE_GPR r5, /**/ R3
	GET_TICK_CNT(entry,"simple_mfspr")
	b	emulation_done

GLOBAL_SYMBOL(r__spr_read_write):
spr_read_write:
	b	simple_mfspr
	stwx	r0,r5,r1		// value in r0
	GET_TICK_CNT(entry,"simple_mtspr")
	b	emulation_done


/********************************************************************/
GLOBAL_SYMBOL(r__spr_read_only):
spr_read_only:	
	b	simple_mfspr			// allow read
	b	emulation_done			// ignore write	

/********************************************************************/	
GLOBAL_SYMBOL(r__spr_illegal):	
spr_illegal:
	nop					// spr read entry
	rlwinm	r7,r7,0,15,10			// clear srr1 bit 11-14
	oris	r7,r7,0x8			// set bit 12 (privileged instr)
	b	mac_program_trap
	
		
/********************************************************************/
unhandled_spr_read:
	srwi	r4,r2,2
	srwi	r5,r5,3
	// r4 = spr#
	// r5 = dest gpr
	MAC_EXIT_SAVE( RVEC_SPR_READ )

unhandled_spr:
	b	unhandled_spr_read		// read hook	(offs 0)
unhandled_spr_write:				// write hook	(offs 4)
	srwi	r4,r2,2
	mr	r5,r0
	// r4 = spr#
	// r5 = register-value
	MAC_EXIT_SAVE( RVEC_SPR_WRITE )

/********************************************************************/
spr_bat:
	b	simple_mfspr			// read has no side-effects
	INC_NIP
	LOADI	r3,EXTERN(do_mtbat)
	bl	save_middle_regs		// Must do this before touching r6-r12
	srwi	r4,r2,2				// r4 = spr#
	mr	r5,r0				// r5 = value
	li	r6,0				// not forced
	b	call_kernel

/********************************************************************/
spr_sdr1:
	b	simple_mfspr			// read has no side-effects
	INC_NIP
	LOADI	r3,EXTERN(do_mtsdr1)
	mr	r4,r0				// r4 = value
	b	call_kernel_save


	

/************************************************************************/
/*	handle MSR changes						*/
/************************************************************************/

	////////////////////////////////////////////////////////////
	// msr_exception_return (exception taken)
	//
	//	r6, r7:		nip / srr1
	//
	// modifies: r0,r2-r5 (r7 updated)
	
	balign_16
msr_exception_return:
	addi	r3,r1,K_UNMAPPED_SR_BASE		// set unmapped context
	li	r7,(MSR_ME | MSR_SE | MSR_IR | MSR_DR | MSR_PR)
	stw	r3,K_CUR_SR_BASE(r1)
	li	r5,(fb_DbgTrace | fb_Trace)
	ori	r7,r7,MSR_EE

	bt+	FBIT_DbgTrace, 1f
	li	r5,0
	rlwinm	r7,r7,0,~MSR_SE
1:
	stw	r3,K_SR_DATA(r1)
	li	r4,fb_LoadSegreg

	stw	r3,K_SR_INST(r1)
	mtcrf	TRACE_CR_FIELD,r5			// set singlestep bits [cr6]
	mtcrf	MMU_CR_FIELD,r4

	stw	r7,K_MSR(r1)
	b	exception_return

	
	////////////////////////////////////////////////////////////
	// msr_altered
	//
	//	r6, r7:		nip / srr1
	//
	//	r0 = new msr
	//	r3 = old msr
	//
	//	Sets cr1.ne if we MSR_POW is set
	//
	// M: r2-r5 (r7 updated).
	// r0 may _NOT_ be modified

#define MSR_CLEARBITS	(MSR_FP | MSR_FE0 | MSR_FE1 | MSR_BE | MSR_SE)	
#define MSR_COPYBITS	(MSR_BE | MSR_SE)	

	balign_32
msr_altered:
#ifdef EMULATE_603
	bf+	FBIT_603_AltGPR,7f			// 603 alternate GPR support
	rlwinm.	r5,r0,0,MSR_TGPR
	bne+	7f
	lwz	r2,xGPRSAVE0_603(r1)			// MSR_TGPR cleared...
	lwz	r4,xGPRSAVE1_603(r1)
	lwz	r5,xGPRSAVE2_603(r1)
	stw	r2,xGPR0(r1)
	lwz	r2,xGPRSAVE3_603(r1)
	stw	r4,xGPR1(r1)
	stw	r5,xGPR2(r1)
	stw	r2,xGPR3(r1)
	crclr	FBIT_603_AltGPR
7:
#endif
	li	r7,(MSR_ME | MSR_SE | MSR_IR | MSR_DR | MSR_PR)
	stw	r0,xMSR(r1)
	xor	r3,r3,r0				// r3 == MSR bit toggle
	bt-	FBIT_IRQPending,test_for_irq		// M: r2
irq_test_ret:
	rlwinm	r4,r0,0,MSR_POW				// MSR_POW
	ori	r7,r7,MSR_EE
	andi.	r3,r3,(MSR_DR|MSR_IR|MSR_PR)		// MMU change (cr unused)?
	bt-	FBIT_DecINT,test_for_dec		// M: r2
dec_test_ret:
	cmpwi	cr1,r4,0				// MSR_POW set?
	li	r2,(fb_DbgTrace | fb_Trace)
	cmpwi	cr2,r3,0
	bt-	FBIT_DbgTrace,1f
	rlwinm	r2,r0,(21+32-FBIT_Trace),fb_Trace	// MSR_SE[21] -> FBIT_Trace
	rlwimi	r7,r0,0,(MSR_SE|MSR_BE)			// no debugger; copy MSR_SE and MSR_BE
1:	
	stw	r7,K_MSR(r1)
	mtcrf	TRACE_CR_FIELD,r2			// set singlestep bits [cr6]

	bne	cr2,1f					// bnelr is slower...	
	blr
1:
	/* MMU change */
	BUMP("MMU-change")

	andi.	r3,r0,(MSR_IR | MSR_DR)			// IR DR part of index
	addi	r5,r1,K_MSR_SR_TABLE
	addi	r4,r3,MSR_DR				// splitmode (MSR_DR != MSR_IR) testing
	rlwimi	r3,r0,32-8,25,25			// [PR IR DR] index to K_MSR_SR_TABLE
	lwzux	r3,r5,r3				// set sr bases from K_MSR_SR_TABLE
	andi.	r4,r4,MSR_IR				// non-zero if in splitmode
	lwz	r2,4(r5)
	li	r4,(fb_InSplitmode | fb_LoadSegreg | fb_PrepareSplitmode)
	lwz	r5,8(r5)
	stw	r3,K_CUR_SR_BASE(r1)			// new sr base in r3 (used below)
	stw	r2,K_SR_DATA(r1)
	bne-	1f
	li	r4,fb_LoadSegreg			// cur_sr_base changed
1:	stw	r5,K_SR_INST(r1)
	mtcrf	MMU_CR_FIELD,r4
	blr

test_for_irq:
	BUMP("test-for-irq")
	and	r2,r3,r0		// check whether we are turning external interrupts on
	andi.	r2,r2,MSR_EE		// we need to recheck IRQs in userspace then 
	beq+	1f
	lwz	r4,xHOSTIRQ_ACTIVE_CNT(r1)
	cmpwi	r4,0			// only return if some host irq is up
	beq+	1f
	MAC_EXIT_SAVE(RVEC_CHECK_IRQS)

1:	andi.	r2,r0,MSR_EE
	beq	irq_test_ret
	b	mac_irq_trap
	
test_for_dec:
	BUMP("test-for-dec")
	andi.	r2,r0,MSR_EE
	beq	dec_test_ret
	b	mac_dec_trap

force_msr_altered:
	BUMP("force-msr-altered")
	lwz	r0,xMSR(r1)			// r0 = new MSR
	xori	r3,r0,(MSR_DR|MSR_IR)		// r3 = faked old MSR
	b	msr_altered			// might throw an exception...
				
	// msr_altered( kv )
GLOBAL_SYMBOL(r__msr_altered):
	lwz	r5,xFLAG_BITS(r3)
	ori	r5,r5,fb_MsrModified
	stw	r5,xFLAG_BITS(r3)
	blr

	
/************************************************************************/
/*	initialize special purpose register table			*/
/************************************************************************/

MACRO(SPR_HOOK,	[spr, hook],	[ LI_PHYS( R8, _hook ) ; stw r8,(((_spr)*4)+K_SPR_HOOKS)(r3) ])
	
// The LSB of a SPR hook specifies that the SPR is privileged (these bits are
// set from C-code).

	// r3 = kvars
GLOBAL_SYMBOL(r__initialize_spr_table):
	LI_PHYS( R7, unhandled_spr )
	addi	r8,r3,K_SPR_HOOKS-4
	li	r9,1024
	mtctr	r9
1:	stwu	r7,4(r8)
	bdnz	1b

	// XXX for now...
	SPR_HOOK	TBWU,	spr_read_write
	SPR_HOOK	TBWL,	spr_read_write

	// SPRs that have side effects
	SPR_HOOK	SDR1,	spr_sdr1
	SPR_HOOK	DEC,	spr_dec

	// BATs
	mFORLOOP([nn],0,15,[
	 SPR_HOOK	eval(nn+IBAT0U), spr_bat
	])
	blr

		
/************************************************************************/
/*	initialize msr segment register table				*/
/************************************************************************/
	
	/////////////////////////////////////////////////////////////
	// initialize_sr_offs_table
	//
	// Copy sr_offs_table to K_MSR_SR_TABLE
	// r1 is added to each element
	
initialize_msr_sr_table:
	mflr	r8			// Get address of table
	bl	sr_offs_table
	mflr	r3
	mtlr	r8

	li	r5,4*8			// #words in table	
	mtctr	r5
	addi	r3,r3,-4
	addi	r4,r1,K_MSR_SR_TABLE-4
1:
	lwzu	r6,4(r3)
	add	r6,r6,r1		// And add r1
	stwu	r6,4(r4)
	bdnz	1b
	blr

	// Used to construct msr_sr_table (mbase is added)
sr_offs_table:
	blrl
	/*	K_CUR_SR_BASE,		K_SR_DATA_BASE,		K_SR_INST_BASE,		dummy */
	
	.long	K_UNMAPPED_SR_BASE,	K_UNMAPPED_SR_BASE,	K_UNMAPPED_SR_BASE,	0
	.long	K_SPLIT_SR_BASE,	K_SV_SR_BASE,		K_UNMAPPED_SR_BASE,	0	/* DR */
	.long	K_SPLIT_SR_BASE,	K_UNMAPPED_SR_BASE,	K_SV_SR_BASE,		0	/* IR */
	.long	K_SV_SR_BASE,		K_SV_SR_BASE,		K_SV_SR_BASE,		0	/* DR|IR */

	.long	K_UNMAPPED_SR_BASE,	K_UNMAPPED_SR_BASE,	K_UNMAPPED_SR_BASE,	0	/* PR */
	.long	K_SPLIT_SR_BASE,	K_USER_SR_BASE,		K_UNMAPPED_SR_BASE,	0	/* PR|DR */
	.long	K_SPLIT_SR_BASE,	K_UNMAPPED_SR_BASE,	K_USER_SR_BASE,		0	/* PR|IR */
	.long	K_USER_SR_BASE,		K_USER_SR_BASE,		K_USER_SR_BASE,		0	/* PR|DR|IR */
