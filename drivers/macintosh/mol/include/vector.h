/* 
 *   Creation Date: <2003/05/26 00:00:28 samuel>
 *   Time-stamp: <2003/09/03 12:34:47 samuel>
 *   
 *	<vector.h>
 *	
 *	Vector hooks
 *   
 *   Copyright (C) 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *   
 */

#ifndef _H_VECTOR
#define _H_VECTOR

#define MOL_SPRG2_MAGIC		0x1779

#ifdef __MPC107__
#include "mpcvector.h"
#else

#define	PERFMON_VECTOR		0xf00


/************************************************************************/
/*	physical/virtual conversion					*/
/************************************************************************/

mDEFINE(_RELOC_ACTION, [action, label], [
	.text	95
	.long	EXTERN(_action) + (_label - r__reloctable_start)
])

mDEFINE(RELOC_ACTION_1, [action, p1], [
89:	nop
	_RELOC_ACTION( _action, 89b )
	.long	_p1
	.text
])

mDEFINE(RELOC_ACTION_2, [action, p1, p2], [
89:	nop
	nop
	_RELOC_ACTION( _action, 89b )
	.long	_p1, _p2
	.text
])

	/* replaced with lis dreg,addr@ha ; addi dreg,dreg,addr@l */
#define LI_PHYS( dreg, addr ) \
	RELOC_ACTION_2( Action_LI_PHYS, dreg, (addr - r__reloctable_start) )

	/* replaced with addis dreg,reg,addr@ha ; lwz dreg,addr@lo(dreg). */
#define LWZ_PHYSADDR_R( dreg, addr, reg ) \
	RELOC_ACTION_2( Action_LWZ_PHYSADDR_R, (dreg*32 + reg), (addr - r__reloctable_start) )

#define LWZ_PHYS( dreg, addr ) \
	LWZ_PHYSADDR_R( dreg, addr, 0 );

mDEFINE(RELOC_LOW, [destvar], [
	_RELOC_ACTION( Action_RELOCATE_LOW, _destvar[]_dummy )
	.long _destvar[]_end - _destvar[]_start
	.long EXTERN( _destvar )
_destvar[]_start:
])

mDEFINE(RELOC_LOW_END, [destvar], [
_destvar[]_end:
	.text
_destvar[]_dummy:
])

	/* syntax: tophys rD,rS */
MACRO(tophys, [dreg, sreg], [
	RELOC_ACTION_1( Action_TOPHYS, (_dreg * 32 + _sreg) )
])
	/* syntax: tovirt rD,rS */
MACRO(tovirt, [dreg, sreg], [
	RELOC_ACTION_1( Action_TOVIRT, (_dreg * 32 + _sreg) )
])


	
/************************************************************************/
/*	Vector entry point definitions					*/
/************************************************************************/

/*
 * This code uses the dynamic linkage/action symbol functionality of 
 * the MOL kernel loader to automatically install the hooks. Refer to 
 * hook.c for the actual implementation.
 */

/* Description of Action_RELOC_HOOK:
 *
 *	.long	Action_RELOC_HOOK
 *	.long	vector
 *	.long	#bytes to copy to lowmem
 *	.long	offset to vret function
 *	.long	offset to vector entry
 *	-- offsets are calculated from here --
 */

mDEFINE(VECTOR_HOOK, [v], [
	_RELOC_ACTION( Action_RELOC_HOOK, vhook_entry_[]_v )
	.long	_v
	.long	vhook_end_[]_v - vhook_[]_v
	.long	vret_[]_v - vhook_[]_v
vhook_[]_v:
	mtsprg_a0 r3
	addis	r3,0,0				/* [1] hook address inserted */
	mtsprg_a1 r1
	ori	r3,r3,0				/* [3] at module initialization */
	mfctr	r1
	mtctr	r3
	bctr

vret_[]_v:
	nop					/* overwritten instruction is inserted here */
	ba	_v + 0x4
vhook_end_[]_v:

	.text
	balign_32
vhook_entry_[]_v:
])

/* SPRG0,1 = saved r3,r1, r1 = saved lr */
mDEFINE(VECTOR_, [v, dummy_str, secondary, not_mol_label], [

not_mol_[]_v:
	mtcr	r3
	mfsprg_a1 r1
	mfsprg_a0 r3
	.long	EXTERN(Action_VRET) + _v	/* ba vret_xxx */

secondary_int_[]_v:
	li	r3,_v
	b	_secondary

	VECTOR_HOOK( _v )

	/* entrypoint */
	mtctr	r1
	mfcr	r3
	mfsprg_a2 r1
	cmpwi	r1,MOL_SPRG2_MAGIC
	bne-	_not_mol_label
soft_603_entry_[]_v:
	mfsrr1	r1
	andi.	r1,r1,MSR_PR			/* MSR_PR set? */
	mfsprg_a3 r1
	beq-	secondary_int_[]_v		/* if not, take a secondary trap? */
])

#define VECTOR(v, dummy_str, secondary) \
	VECTOR_(v, dummy_str, secondary, not_mol_##v )

#define TAKE_EXCEPTION( v ) 					\
	bl	take_exception					; \
	.long	EXTERN(Action_VRET) + v

#define CONTINUE_TRAP( v )					\
	.long	EXTERN(Action_VRET) + v

/* no need to relocate the 0xf00 trap */
#define PERFMON_VECTOR_RELOCATION( newvec )


/************************************************************************/
/*	603 vector HOOKs (r0-r3, cr0 saved by hardware)			*/
/************************************************************************/

mDEFINE(VECTOR_603, [v, dummy_str], [
	_RELOC_ACTION( Action_RELOC_HOOK, vhook_entry_[]_v )
	.long	_v
	.long	vhook_end_[]_v - vhook_[]_v
	.long	vret_[]_v - vhook_[]_v
vhook_[]_v:
	mfsprg_a2 r1
	addis	r3,0,0	   			/* [1] hook address inserted */
	cmpwi	r1,MOL_SPRG2_MAGIC
	ori	r3,r3,0				/* [3] at module initialization */
	bne	vret_[]_v
	mfctr	r0
	mtctr	r3
	bctr

vret_[]_v:
	nop					/* overwritten instruction is inserted here */
	ba	_v + 0x4
vhook_end_[]_v:

	.text
	balign_32
vhook_entry_[]_v:
])


/* all register are assumed to be unmodified */
mDEFINE(SOFT_VECTOR_ENTRY_603, [v], [
	mtsprg_a0 r3
	mtsprg_a1 r1
	mfcr	r3
	b	soft_603_entry_[]_v
])
	

/************************************************************************/
/*	FUNCTION_HOOK							*/
/************************************************************************/

mDEFINE(FHOOK, [symind], [
	_RELOC_ACTION( Action_HOOK_FUNCTION, 89f )
	.long	_symind
	.long	fhook_end_[]_symind - fhook_[]_symind
	.long	fret_[]_symind - fhook_[]_symind
fhook_[]_symind:
	mflr	r10
	addis	r9,0,0		/* [1] address inserted */
	ori	r9,r9,0		/* [2] at runtime */
	mtctr	r9
	bctrl
	mtlr	r10
fret_[]_symind:
	nop			/* overwritten instruction is inserted here */
	nop			/* return (through a relative branch) */
fhook_end_[]_symind:

	.text
89:	/* hook goes here */
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

#endif	 /* MOLMPC */
#endif   /* _H_VECTOR */
