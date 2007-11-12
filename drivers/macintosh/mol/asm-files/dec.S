/* 
 *   Creation Date: <2001/06/21 17:10:35 samuel>
 *   Time-stamp: <2004/03/07 13:16:58 samuel>
 *   
 *	<dec.S>
 *	
 *	DEC / TimeBase stuff
 *   
 *   Copyright (C) 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */
	
/************************************************************************/
/*	TBWU / TBWL							*/
/************************************************************************/
	
#if 0
spr_tbwu:
	blr				// read (won't come here)
	mr	r3,r0
	mftb	r4
	b	update_tb
		
spr_tbwl:
	blr				// read (won't come here)
	mftbu	r3
	mr	r4,r0
	// ...fall through...

// r3 = tbu, r4 = tbl, r0,r2,r5 free.
update_tb:
	lwz	r5,K_LOADED_DEC(r1)
	mfdec	r2
	sub	r5,r5,r2		// r5 = elapsed ticks
		
	li	r2,0			// Set timebase
	mttbl	r4
	mttbu	r3
	mttbl	r4
	
	mfxer	r2
	subfc	r4,r5,r4
	addme	r3,r3
	stw	r4,xTBL(r1)
	stw	r3,xTBU(r1)
	mtxer	r2
	b	emulation_done
#endif


/************************************************************************/
/*	Load/restore DEC and TB						*/
/************************************************************************/

	//////////////////////////////////////////////////////////////
	// recalc_int_stamp
	//
	// May modify: r0,r2-r5
	//
	
recalc_int_stamp:
	crclr	FBIT_RecalcDecInt

	lwz	r5,xTIMER_STAMP(r1)
	bf	FBIT_DecSeenZero, 1f		// FBIT_DecSeenZero must be set when DEC is loaded
	bt	FBIT_DecINT, 1f			// interrupt might already be flagged...
	mftb	r3
	lwz	r4,xDEC_STAMP(r1)		// Has the xDEC overflown already?
	sub.	r0,r4,r3
	blt	2f				// branch if xDEC < 0
	sub.	r0,r4,r5
	blt	2f				// branch if xDEC < xTIMER
1:	mr	r4,r5
2:	stw	r4,K_INT_STAMP(r1)		// dec = xTIMER
	blr
	
	
	//////////////////////////////////////////////////////////////
	// set_mol_dec 
	//
	// May modify: r0,r2-r5
	//
	// Old 2.4 kernels assume that linux-DEC never ticks faster
	// than the DEC interval measured from TB. Unfortunately,
	// it is virtually impossible to keep DEC/TB in sync.
	//
	// Experimentally, the "worst case" senario is a linux DEC tick
	// beeing delayed 0.04 ms (to be compare with the 20 ms period).
	//
	// Sequences similar to
	//
	//	1: mftb r2; mfdec r3; mftb r4; cmp r2,r4; bne 1b
	//
	// do *not* work - DEC and TB probably ticks on different edges.
	
set_mol_dec:
	lwz	r5,K_INT_STAMP(r1)		// DEC = K_INT_STAMP - tbl
	mfdec	r3				// Make sure linux interrupts *never*
	mftb	r2				// occur too fast
	
	sub	r4,r5,r2
	cmpw	r4,r3
	bgtlr
	add	r5,r3,r2			// K_DEC_STAMP = DEC + tbl
	mtdec	r4

	stw	r5,K_DEC_STAMP(r1)
	crset	FBIT_MolDecLoaded
	blr

	///////////////////////////////////////////////////////////////	
	// set_kernel_dec 
	//
	// May modify: r0,r2, cr
	//

set_kernel_dec:
	lwz	r0,K_DEC_STAMP(r1)
	crclr	FBIT_MolDecLoaded
	mftb	r2				// Keep linux-DEC coherent
	sub	r2,r0,r2			// DEC = stamp - tbl
	mtdec	r2
	blr

	

/************************************************************************/
/*	DEC read/write							*/
/************************************************************************/

_dec_read:
	lwz	r4,xDEC_STAMP(r1)
	mftb	r3
	sub	r0,r4,r3

	BUMP("dec_read")
	GET_TICK_CNT(entry,"dec_read")
	b	simple_mfspr
	
spr_dec:
	b	_dec_read

	// dec write. r0 = spr_value
	BUMP("mtdec")
	rlwinm.	r5,r0,0,0,0			// seen zero?
	mftb	r4
	cror	FBIT_DecSeenZero,FBIT_DecSeenZero,eq
	add	r5,r4,r0
	stw	r5,xDEC_STAMP(r1)		// set new dec value
	bf	FBIT_DecSeenZero, emulation_done
	bl	recalc_int_stamp		// M: r0,r2-r5
	btl	FBIT_MolDecLoaded, set_kernel_dec	// M: r0,r2
	bl	set_mol_dec			// M: r0,r2-r5
	b	emulation_done


/************************************************************************/
/*	Decrementer Exception						*/
/************************************************************************/
	
	// __dec_VECTOR (non-MOL dec exception)
	//
	//	r3=cr, sprg1=saved_r1, sprg0=saved_r3
	//
	// An exception with DEC>=0 can occur if a mac-DEC overflows occurs 
	// just prior to a context switch. These exceptions should be 
	// dropped silently. 
	
__dec_VECTOR:
	mfdec	r1
	cmpwi	r1,0
	blt+	1f
	mtcr	r3					// Restore and exit
	ABORT_TRAP( 0x900 )
1:
	mtcr	r3					// Restore and continue trap
	CONTINUE_TRAP( 0x900 )
	
VECTOR_( 0x900, "Decrementer", secint_bad, __dec_VECTOR )
	EXCEPTION_PREAMBLE				// r0-r5, CR, LR, r6/r7 = msr/nip
	TRACE(0x900, "Decrementer")
	mfdec	r4
	cmpwi	r4,0
	bge	exception_return
	bf	FBIT_MolDecLoaded, take_linux_dec_exception

	mftb	r3
	lis	r2,0x1000				// r2 = DEC rearm constant
	
	bf	FBIT_DecSeenZero, 1f			// check for xDEC overflow
	lwz	r4,xDEC_STAMP(r1)
	sub.	r0,r4,r3				// lt set if xDEC has overflown
	cror	FBIT_DecINT, FBIT_DecINT, lt		// dec_int |= lt
	crandc	FBIT_DecSeenZero, FBIT_DecSeenZero, lt	// szero &= ~lt
1:
	lwz	r5,xTIMER_STAMP(r1)			// r5 = xTIMER_STAMP
	sub.	r0,r5,r3				// lt set if xTIMER has overflown
	mtdec	r2					// rearm DEC
	blt-	2f					// xTIMER has higher priority...

	// mac-dec interrupt
	BUMP("DEC-overflow")
	bl	set_kernel_dec
	bl	recalc_int_stamp
	bl	set_mol_dec
	GET_TICK_CNT(entry,"dec-overflow")
	bf-	FBIT_DecINT,exception_return		// could occur if xTIMER has changed on us
	lwz	r4,xMSR(r1)
	rlwinm.	r0,r4,0,MSR_EE
	beq-	exception_return			// no... simply return
	BUMP("DEC-exception")
	b	mac_dec_trap

	// timer interrupt
2:	BUMP("Timer-interrupt")
	crset	FBIT_RecalcDecInt			// dec must be recalced
	GET_TICK_CNT(entry,"timer-overflow")
	MAC_EXIT_SAVE( RVEC_TIMER )


take_linux_dec_exception:
	BUMP("Linux-DEC")
	bl	save_middle_regs
	TAKE_EXCEPTION( 0x900 )
