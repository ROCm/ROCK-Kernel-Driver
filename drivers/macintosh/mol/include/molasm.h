/*   -*- asm -*-
 *
 *   Creation Date: <2001/01/28 20:33:22 samuel>
 *   Time-stamp: <2004/01/29 19:29:10 samuel>
 *
 *	<molasm.h>
 *
 *	Utility assembly macros
 *
 *   Copyright (C) 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#ifndef _H_MOLASM
#define _H_MOLASM

#define GLOBAL_SYMBOL( sym_name ) \
GLOBL(sym_name)


/************************************************************************/
/*	SPRG usage							*/
/************************************************************************/

/* Darwin and Linux uses the sprg's differently. Linux uses sprg0/1 in
 * the exception vectors while Darwin uses sprg2/3.
 */
#ifdef __linux__
define([mfsprg_a0], [mfsprg0])
define([mfsprg_a1], [mfsprg1])
define([mfsprg_a2], [mfsprg2])
define([mfsprg_a3], [mfsprg3])
define([mtsprg_a0], [mtsprg0])
define([mtsprg_a1], [mtsprg1])
define([mtsprg_a2], [mtsprg2])
define([mtsprg_a3], [mtsprg3])
#else
define([mfsprg_a0], [mfsprg2])
define([mfsprg_a1], [mfsprg3])
define([mfsprg_a2], [mfsprg0])
define([mfsprg_a3], [mfsprg1])
define([mtsprg_a0], [mtsprg2])
define([mtsprg_a1], [mtsprg3])
define([mtsprg_a2], [mtsprg0])
define([mtsprg_a3], [mtsprg1])
#endif


/************************************************************************/
/*	Utility								*/
/************************************************************************/

MACRO(LOAD_VARIABLE, [reg, offs], [
	lis	_reg,HA(k_mol_stack + _offs)
	lwz	_reg,LO(k_mol_stack + _offs)(_reg)
])

MACRO(SET_SESSION_TABLE, [reg], [
	lis	_reg,HA(EXTERN(Symbol_SESSION_TABLE))
	addi	_reg,_reg,LO(EXTERN(Symbol_SESSION_TABLE))
])


/************************************************************************/
/*	GPR save / restore						*/
/************************************************************************/

MACRO(xGPR_SAVE, [reg], [
	stw	rPREFIX[]_reg,(xGPR0 + _reg*4)(r1)
])

MACRO(xGPR_LOAD, [reg], [
	lwz	rPREFIX[]_reg,(xGPR0 + _reg*4)(r1)
])


/************************************************************************/
/*	FPU misc							*/
/************************************************************************/

MACRO(ENABLE_MSR_FP, [scr], [
	mfmsr	_scr
	ori	_scr,_scr,MSR_FP
	mtmsr	_scr
	isync
])

/************************************************************************/
/*	Segment registers						*/
/************************************************************************/

MACRO(LOAD_SEGMENT_REGS, [base, scr, scr2], [
	mFORLOOP([i],0,7,[
		lwz	_scr,eval(i * 8)(_base)
		lwz	_scr2,eval((i * 8)+4)(_base)
		mtsr	srPREFIX[]eval(i*2),_scr
		mtsr	srPREFIX[]eval(i*2+1),_scr2
	])
])

MACRO(SAVE_SEGMENT_REGS, [base, scr, scr2], [
	mFORLOOP([i],0,7,[
		mfsr	_scr,srPREFIX[]eval(i*2)
		mfsr	_scr2,srPREFIX[]eval(i*2+1)
		stw	_scr,eval(i * 8)(_base)
		stw	_scr2,eval((i * 8) + 4)(_base)
	])
])

/************************************************************************/
/*	BAT register							*/
/************************************************************************/

MACRO(SAVE_DBATS, [varoffs, scr1], [
	mfpvr	_scr1
	srwi	_scr1,_scr1,16
	cmpwi	r3,1
	beq	9f
	mFORLOOP([nn],0,7,[
		mfspr	_scr1, S_DBAT0U + nn
		stw	_scr1,(_varoffs + (4 * nn))(r1)
	])
9:
])

MACRO(SAVE_IBATS, [varoffs, scr1], [
	mFORLOOP([nn],0,7,[
		mfspr	_scr1, S_IBAT0U + nn
		stw	_scr1,(_varoffs + (4 * nn))(r1)
	])
])


#endif   /* _H_MOLASM */
