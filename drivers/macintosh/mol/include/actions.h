/*
 *   Creation Date: <2004/01/31 13:08:42 samuel>
 *   Time-stamp: <2004/03/07 14:25:23 samuel>
 *
 *	<actions.h>
 *
 *
 *
 *   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#ifndef _H_ACTIONS
#define _H_ACTIONS

/* Certain assembly macros (like LI_PHYS) adds meta information to a special
 * ELF segment. This information is parsed when the module is loaded/used and
 * the appropriate action is performed (a few assembly instruction are typically
 * modified).
 *
 * Actions with lower opcodes are performed before actions with higher opcodes.
 */

#define ACTION_LIS_SPECVAR_H		1	/* dreg, special_var_index */
#define ACTION_ORI_SPECVAR_L		2	/* dreg*32 + sreg, special_var_index */
#define ACTION_LI_PHYS			3	/* dreg, addr_offs */
#define ACTION_LWZ_PHYSADDR_R		4	/* dreg*32 + reg, addr_offs */
#define ACTION_TOPHYS			5	/* dreg*32 + sreg */
#define ACTION_TOVIRT			6	/* dreg*32 + sreg */
#define ACTION_RELOCATE_LOW		7	/* code_size, destvar, code... */
#define ACTION_VRET			8	/* vector [special, used by RELOC_HOOK] */
#define ACTION_FIX_SPRG2		9	/* scratch_reg */

#define FLUSH_CACHE_ACTION		9	/* flush the icache at this point */

#define ACTION_HOOK_FUNCTION		10
#define ACTION_RELOC_HOOK		11	/* trigger, size, vret_action#, vret_offs */
#define MAX_NUM_ACTIONS			12

/* Special vars (ACTION_*_SPECVAR) */
#define SPECVAR_SESSION_TABLE		1

/* Function hooks (ACTION_HOOK_FUNCTION) */
#define FHOOK_FLUSH_HASH_PAGE		1

#ifndef __ASSEMBLY__
typedef struct {
	int	action;				/* ACTION_xxx */
	int	offs;				/* target instruction offset (from r__reloctable_start) */
	int	params[1];			/* parameters */
} action_pb_t;
#endif /* __ASSEMBLY__ */


/************************************************************************/
/*	assembly macros							*/
/************************************************************************/

/*
 * WARNING: These macros uses the 9 label (the OSX assembler
 * can only use labels (0-9).
 */

#ifdef __ASSEMBLY__

#ifdef __linux__
#define ACTIONS_SECTION			.text 95
#define ACTIONS_OFFS_SECTION		.text 96
#endif
#ifdef __darwin__
#define ACTIONS_SECTION			.section __TEXT,__areloc
#define ACTIONS_OFFS_SECTION		.section __DATA,__areloc_offs
#endif

mDEFINE(ACTION_PB, [action], [
	.text
9:
	ACTIONS_SECTION
	.long	_action				/* action */
	.long	(9b - r__reloctable_start)	/* target PC */
9:
	ACTIONS_OFFS_SECTION
	.long	(9b - r__actions_section - 8)	/* store pointer to PB */
	ACTIONS_SECTION
])

mDEFINE(ACTION_1, [action, p1], [
	ACTION_PB( _action )
	.long	_p1
	.text
	nop				/* replaced */
])

mDEFINE(ACTION_21, [action, p1, p2], [
	ACTION_PB( _action )
	.long	_p1, _p2
	.text
	nop				/* replaced */
])

mDEFINE(ACTION_2, [action, p1, p2], [
	ACTION_PB( _action )
	.long	_p1, _p2
	.text
	nop				/* replaced */
	nop				/* replaced */
])

mDEFINE(ACTION_13, [action, p1], [
	ACTION_PB( _action )
	.long	_p1
	.text
	nop				/* replaced */
	nop				/* replaced */
	nop				/* replaced */
])


	/* replaced with lis dreg,addr@ha ; addi dreg,dreg,addr@l */
#define LI_PHYS( dreg, addr ) \
	ACTION_2( ACTION_LI_PHYS, dreg, (addr - r__reloctable_start) )

	/* replaced with addis dreg,reg,addr@ha ; lwz dreg,addr@lo(dreg). */
#define LWZ_PHYSADDR_R( dreg, addr, reg ) \
	ACTION_2( ACTION_LWZ_PHYSADDR_R, (dreg*32 + reg), (addr - r__reloctable_start) )

#define LWZ_PHYS( dreg, addr ) \
	LWZ_PHYSADDR_R( dreg, addr, 0 );

	/* syntax: tophys rD,rS */
MACRO(tophys, [dreg, sreg], [
	ACTION_1( ACTION_TOPHYS, (_dreg * 32 + _sreg) )
])
	/* syntax: tovirt rD,rS */
MACRO(tovirt, [dreg, sreg], [
	ACTION_1( ACTION_TOVIRT, (_dreg * 32 + _sreg) )
])

	/* syntax: lis_specvar_ha rD,SPECIAL_VAR */
MACRO(lis_svh, [dreg, specvariable], [
	ACTION_21( ACTION_LIS_SPECVAR_H, _dreg, _specvariable )
])

	/* syntax: addi_specvar_ha rD,rS,SPECIAL_VAR */
MACRO(ori_svl, [dreg, sreg, specvariable], [
	ACTION_21( ACTION_ORI_SPECVAR_L, (_dreg * 32)+_sreg, _specvariable )
])

	/* syntax: FIX_SPRG2 rN */
MACRO(fix_sprg2, [reg], [
	/* only darwin needs this (sprg_a0 holds bits describing the CPU) */
#ifdef __darwin__
	ACTION_13( ACTION_FIX_SPRG2, _reg )
#endif
])

mDEFINE(RELOC_LOW, [destvar], [
	ACTION_PB( ACTION_RELOCATE_LOW )
	.long	_destvar[]_end - _destvar[]_start
	.long	EXTERN([]_destvar)
_destvar[]_start:
])

mDEFINE(RELOC_LOW_END, [destvar], [
_destvar[]_end:
	.text
])


#endif /* __ASSEMBLY__ */


#endif   /* _H_ACTIONS */
