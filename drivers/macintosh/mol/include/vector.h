/*
 *   Creation Date: <2003/05/26 00:00:28 samuel>
 *   Time-stamp: <2004/03/07 14:44:50 samuel>
 *
 *	<vector.h>
 *
 *	Vector hooks
 *
 *   Copyright (C) 2003, 2004 Samuel Rydh (samuel@ibrium.se)
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
/*	Vector entry point definitions					*/
/************************************************************************/

/*
 * This code uses the dynamic linkage/action symbol functionality of
 * the MOL kernel loader to automatically install the hooks. Refer to
 * hook.c for the actual implementation.
 */

/* Description of ACTION_RELOC_HOOK:
 *
 *	.long	ACTION_RELOC_HOOK
 *	.long	vector
 *	.long	#bytes to copy to lowmem
 *	.long	offset to vret function
 *	-- offsets are calculated from here --
 */

mDEFINE(VECTOR_HOOK, [v], [
	balign_32
	ACTION_PB( ACTION_RELOC_HOOK )
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
	/* entrypoint */
])

/* these macros are to be used from the not_mol vector hook */
#define CONTINUE_TRAP( v )					\
	mfsprg_a0 r3						; \
	fix_sprg2 /**/ R1	/* sprg2 == sprg_a0 */		; \
	mfsprg_a1 r1						; \
	ACTION_1( ACTION_VRET, v )	/* ba vret_xxx */

#define ABORT_TRAP( dummy_v )					\
	mfsprg_a0 r3						; \
	fix_sprg2 /**/ R1	/* sprg2 == sprg_a0 */		; \
	mfsprg_a1 r1						; \
	rfi

/* SPRG0,1 = saved r3,r1, r1 = saved lr */
mDEFINE(VECTOR_, [v, dummy_str, secondary, not_mol_label], [

not_mol_[]_v:
	mtcr	r3
	CONTINUE_TRAP( _v )

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

/* this macro takes an exception from mac mode (call save_middle_regs first) */
#define TAKE_EXCEPTION( v ) 					\
	bl	take_exception					; \
	ACTION_1( ACTION_VRET, v )

/* no need to relocate the 0xf00 trap */
#define PERFMON_VECTOR_RELOCATION( newvec )


/************************************************************************/
/*	603 vector HOOKs (r0-r3, cr0 saved by hardware)			*/
/************************************************************************/

mDEFINE(VECTOR_603, [v, dummy_str], [
	balign_32
	ACTION_PB( ACTION_RELOC_HOOK )
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
	/* entrypoint */
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
	ACTION_PB( ACTION_HOOK_FUNCTION )
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
	/* hook goes here */
])


#endif	 /* MOLMPC */
#endif   /* _H_VECTOR */
