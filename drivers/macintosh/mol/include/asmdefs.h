/*   -*- asm -*- 
 *
 *   Creation Date: <2001/02/03 19:38:07 samuel>
 *   Time-stamp: <2003/07/08 18:55:50 samuel>
 *   
 *	<asmdefs.h>
 *	
 *	Common assembly definitions
 *   
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_ASMDEFS
#define _H_ASMDEFS

#include "asm.m4"
	
#ifndef __ASSEMBLY__
#error	This file is only to be included from assembler code!
#endif


/************************************************************************/
/*	High/low halfword compatibility macros				*/
/************************************************************************/

#ifdef __linux__
#define 	ha16( v )	(v)##@ha
#define 	hi16( v )	(v)##@h
#define 	lo16( v )	(v)##@l
#endif
#define		HA(v)		ha16(v)
#define		HI(v)		hi16(v)
#define		LO(v)		lo16(v)

	
/************************************************************************/
/*	Register name prefix						*/
/************************************************************************/

#ifdef __linux__
define([rPREFIX], [])
define([fPREFIX], [])
define([srPREFIX], [])
#else
define([rPREFIX], [r])
define([fPREFIX], [f])
define([srPREFIX], [sr])
/* frN -> fN */
mFORLOOP([i],0,31,[define(fr[]i,f[]i)])
#endif

/************************************************************************/
/*	Macros and definitions						*/
/************************************************************************/

#ifdef __darwin__
#define balign_4	.align 2,0
#define balign_8	.align 3,0
#define balign_16	.align 4,0
#define balign_32	.align 5,0
#endif

#ifdef __linux__
#define balign_4	.balign 4,0
#define balign_8	.balign 8,0
#define balign_16	.balign 16,0
#define balign_32	.balign 32,0
#endif	

MACRO(LOADVAR, [dreg, variable], [
	lis	_dreg,HA(_variable)
	lwz	_dreg,LO(_variable)(_dreg)
])

MACRO(LOADI, [dreg, addr], [
	lis	_dreg,HA(_addr)
	addi	_dreg,_dreg,LO(_addr)
])

MACRO(LOAD_GPR_RANGE, [start, endx, offs, base], [
	mFORLOOP([i],0,31,[ .if (i >= _start) & (i <= _endx)
		lwz rPREFIX[]i,_offs+i[]*4(_base)
	.endif 
])])

MACRO(STORE_GPR_RANGE, [start, endx, offs, base], [
	 mFORLOOP([i],0,31,[ .if (i >= _start) & (i <= _endx)
		stw rPREFIX[]i,_offs+i[]*4(_base)
	.endif 
])])

MACRO(LOAD_FPR_RANGE, [start, endx, offs, base], [
	mFORLOOP([i],0,31,[ .if (i >= _start) & (i <= _endx)
		lfd fPREFIX[]i,_offs+i[]*8(_base)
	.endif
])])

MACRO(STORE_FPR_RANGE, [start, endx, offs, base], [
	mFORLOOP([i],0,31,[ .if (i >= _start) & (i <= _endx)
		stfd fPREFIX[]i,_offs+i[]*8(_base)
	.endif 
])])

/************************************************************************/
/*	FPU load/save macros						*/
/************************************************************************/

	// The FPU macros are used both in the kernel and in
	// mainloop_asm.h.

MACRO(xFPR_LOAD_RANGE, [from, to, mbase], [
	LOAD_FPR_RANGE _from,_to,xFPR_BASE,_mbase
])
MACRO(xFPR_SAVE_RANGE, [from, to, mbase], [
	STORE_FPR_RANGE _from,_to,xFPR_BASE,_mbase
])
	// The low half of the fpu is fr0-fr12. I.e. the FPU registers 
	// that might be overwritten when a function call is taken
	// (fr13 and fpscr are treated specially).

MACRO(xLOAD_LOW_FPU, [mbase], [
	xFPR_LOAD_RANGE 0,12,_mbase
])

MACRO(xLOAD_TOPHALF_FPU, [mbase], [
	xFPR_LOAD_RANGE 14,31,_mbase
])
MACRO(xLOAD_FULL_FPU, [mbase], [
	xLOAD_LOW_FPU _mbase
	xLOAD_TOPHALF_FPU _mbase
])

MACRO(xSAVE_LOW_FPU, [mbase], [
	xFPR_SAVE_RANGE 0,12,_mbase
])
MACRO(xSAVE_TOPHALF_FPU, [mbase], [
	xFPR_SAVE_RANGE 14,31,_mbase
])
MACRO(xSAVE_FULL_FPU, [mbase], [
	xSAVE_LOW_FPU _mbase
	xSAVE_TOPHALF_FPU _mbase	
])


/************************************************************************/
/*	GPR load/save macros						*/
/************************************************************************/

MACRO(xGPR_SAVE_RANGE, [from, to, mbase], [
	STORE_GPR_RANGE _from, _to, xGPR0, _mbase
])

MACRO(xGPR_LOAD_RANGE, [from, to, mbase], [
	LOAD_GPR_RANGE _from, _to, xGPR0, _mbase
])


/************************************************************************/
/*	AltiVec								*/
/************************************************************************/

#ifdef __linux__

define(vPREFIX,[])

#ifndef HAVE_ALTIVEC
#define VEC_OPCODE( op1,op2,A,B,C ) \
	.long	(((op1) << (32-6)) | (op2) | ((A) << (32-11)) | ((B) << (32-16)) | ((C) << (32-21))) ;	

#define __stvx( vS,rA,rB )	VEC_OPCODE( 31,0x1ce,vS,rA,rB )
#define __lvx( vD,rA,rB )	VEC_OPCODE( 31,0xce, vD,rA,rB )
#define __mfvscr( vD )		VEC_OPCODE( 4,1540,vD,0,0 )
#define __mtvscr( vB )		VEC_OPCODE( 4,1604,0,0,vB )
#define __stvewx( vS,rA,rB )	VEC_OPCODE( 31,(199<<1), vS,rA,rB )

mFORLOOP([i],0,31,[define(v[]i,[]i)])
MACRO(stvx, [vS,rA,rB],		[ __stvx( _vS,_rA,_rB ) ; ])
MACRO(lvx,  [vD,rA,rB],		[ __lvx( _vD,_rA,_rB ) ; ])
MACRO(mfvscr, [vD],		[ __mfvscr( _vD ) ; ])
MACRO(mtvscr, [vB],		[ __mtvscr( _vB ) ; ])
MACRO(stvewx, [vS,rA,rB],	[ __stvewx( _vS,_rA,_rB ) ; ])
#endif
#else	/* __linux__ */

define(vPREFIX,[v])

#endif	/* __linux__ */


// NOTE: Writing to VSCR won't cause exceptions (this
// is different compared to FPSCR).

MACRO(xVEC_SAVE, [mbase, scr], [
	addi	_scr,_mbase,xVEC_BASE
	mFORLOOP([i],0,31,[
		stvx	vPREFIX[]i,0,_scr
		addi	_scr,_scr,16
	])
	addi	_scr,_mbase,xVSCR-12
	mfvscr	v0
	stvx	v0,0,_scr
	addi	_scr,_mbase,xVEC0
	lvx	v0,0,_scr
	mfspr	_scr,S_VRSAVE
	stw	_scr,xVRSAVE(_mbase)
])

MACRO(xVEC_LOAD, [mbase, scr], [
	addi	_scr,_mbase,xVSCR-12
	lvx	v0,0,_scr
	mtvscr	v0
	addi	_scr,_mbase,xVEC_BASE
	mFORLOOP([i],0,31,[
		lvx	vPREFIX[]i,0,_scr
		addi	_scr,_scr,16
	])
	lwz	_scr,xVRSAVE(_mbase)
	mtspr	S_VRSAVE,_scr
])
	
/************************************************************************/
/*	Instructions							*/
/************************************************************************/

#ifdef __darwin__
MACRO(mtsprg0, [reg], [mtspr SPRG0,_reg] )
MACRO(mtsprg1, [reg], [mtspr SPRG1,_reg] )
MACRO(mtsprg2, [reg], [mtspr SPRG2,_reg] )
MACRO(mtsprg3, [reg], [mtspr SPRG3,_reg] )
MACRO(mfsprg0, [reg], [mfspr _reg,SPRG0] )
MACRO(mfsprg1, [reg], [mfspr _reg,SPRG1] )
MACRO(mfsprg2, [reg], [mfspr _reg,SPRG2] )
MACRO(mfsprg3, [reg], [mfspr _reg,SPRG3] )
#endif

/************************************************************************/
/*	Register names							*/
/************************************************************************/

#ifdef __darwin__
/* These symbols are not defined under Darwin */
#define		cr0	0
#define		cr1	1
#define		cr2	2
#define		cr3	3
#define		cr4	4
#define		cr5	5
#define		cr6	6
#define		cr7	7	
#define		cr8	8
#define		lt	0	/* Less than */
#define		gt	1	/* Greater than */
#define		eq	2	/* Equal */
#define		so	3	/* Summary Overflow */
#define		un	3	/* Unordered (after floating point) */
#endif
	
/* FPU register names (to be used as macro arguments) */
#define	FR0	0
#define	FR1	1
#define	FR2	2
#define	FR3	3
#define	FR4	4
#define	FR5	5
#define	FR6	6
#define	FR7	7
#define	FR8	8
#define	FR9	9
#define	FR10	10
#define	FR11	11
#define	FR12	12
#define	FR13	13
#define	FR14	14
#define	FR15	15
#define	FR16	16
#define	FR17	17
#define	FR18	18
#define	FR19	19
#define	FR20	20
#define	FR21	21
#define	FR22	22
#define	FR23	23
#define	FR24	24
#define	FR25	25
#define	FR26	26
#define	FR27	27
#define	FR28	28
#define	FR29	29
#define	FR30	30
#define	FR31	31

/* GPR register names (to be used as macro arguments) */
#define	R0	0
#define	R1	1
#define	R2	2
#define	R3	3
#define	R4	4
#define	R5	5
#define	R6	6
#define	R7	7
#define	R8	8
#define	R9	9
#define	R10	10
#define	R11	11
#define	R12	12
#define	R13	13
#define	R14	14
#define	R15	15
#define	R16	16
#define	R17	17
#define	R18	18
#define	R19	19
#define	R20	20
#define	R21	21
#define	R22	22
#define	R23	23
#define	R24	24
#define	R25	25
#define	R26	26
#define	R27	27
#define	R28	28
#define	R29	29
#define	R30	30
#define	R31	31
		
#ifndef __darwin__

/* GPR register names, rN -> N, frN -> N, vN -> N */
mFORLOOP([i],0,31,[define(r[]i,[]i)])	
mFORLOOP([i],0,31,[define(fr[]i,[]i)])	
mFORLOOP([i],0,31,[define(v[]i,[]i)])	

#endif /* __darwin__ */


/************************************************************************/
/*	useful macros							*/
/************************************************************************/

MACRO(ori_, [reg1, reg2, value], [
	.if (_value & 0xffff)
		ori _reg1, _reg2, (_value) & 0xffff
	.endif
	.if (_value & ~0xffff)
		oris _reg1, _reg2, (_value) >> 16
	.endif
])
	
/************************************************************************/
/*	MISC								*/
/************************************************************************/

#ifdef __linux__
#define GLOBL( name )		.globl name ; name
#define EXTERN( name )		name
#else
/* an underscore is needed on Darwin */
#define GLOBL( name )		.globl _##name ; name: ; _##name
#define EXTERN( name )		_##name
#endif	

#define	BIT(n)		(1<<(31-(n)))

#endif   /* _H_ASMDEFS */

