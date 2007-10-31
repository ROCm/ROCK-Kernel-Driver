/*
 *   Creation Date: <1998-11-11 11:56:45 samuel>
 *   Time-stamp: <2004/03/13 14:25:26 samuel>
 *
 *	<mmu.c>
 *
 *	Handles page mappings and the mac MMU
 *
 *   Copyright (C) 1998-2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "archinclude.h"
#include "alloc.h"
#include "kernel_vars.h"
#include "mmu.h"
#include "mmu_contexts.h"
#include "asmfuncs.h"
#include "emu.h"
#include "misc.h"
#include "mtable.h"
#include "performance.h"
#include "context.h"
#include "hash.h"
#include "map.h"

#define MREGS	(kv->mregs)
#define MMU	(kv->mmu)


/************************************************************************/
/*	init / cleanup							*/
/************************************************************************/

int
init_mmu( kernel_vars_t *kv )
{
	int success;

	success =
		!arch_mmu_init( kv ) &&
		!init_contexts( kv ) &&
		!init_mtable( kv ) &&
		!init_mmu_io( kv ) &&
		!init_mmu_fb( kv ) &&
		!init_mmu_tracker( kv );

	if( !success ) {
		cleanup_mmu( kv );
		return 1;
	}

	clear_vsid_refs( kv );

	/* SDR1 is set from fStartEmulation */
	return 0;
}

void
cleanup_mmu( kernel_vars_t *kv )
{
	/* We have to make sure the flush thread are not using the mtable
	 * facilities. The kvars entry has been clear so we just have
	 * to wait around until no threads are using it.
	 */
	while( atomic_read_mol(&g_sesstab->external_thread_cnt) )
		;

	cleanup_mmu_tracker( kv );
	cleanup_mmu_fb( kv );
	cleanup_mmu_io( kv );
	cleanup_mtable( kv );
	cleanup_contexts( kv );

	if( MMU.pthash_inuse_bits )
		kfree_cont_mol( MMU.pthash_inuse_bits );
	if( MMU.hash_base )
		unmap_emulated_hash( kv );

	memset( &MMU, 0, sizeof(mmu_vars_t) );
}


/************************************************************************/
/*	misc								*/
/************************************************************************/

/* All vsid entries have been flushed; clear dangling pointers */
void
clear_vsid_refs( kernel_vars_t *kv )
{
	int i;
	for( i=0; i<16; i++ ) {
		MMU.vsid[i] = NULL;
		MMU.unmapped_vsid[i] = NULL;

		MMU.user_sr[i] = MMU.illegal_sr;
		MMU.sv_sr[i] = MMU.illegal_sr;
		MMU.unmapped_sr[i] = MMU.illegal_sr;
		MMU.split_sr[i] = MMU.illegal_sr;
	}
	invalidate_splitmode_sr( kv );
}

/*
 * This function is called whenever the mac MMU-registers have
 * been manipulated externally.
 */
void
mmu_altered( kernel_vars_t *kv )
{
	int i;

	for( i=0; i<16; i++ ) {
		MMU.vsid[i] = NULL;
		MMU.user_sr[i] = MMU.illegal_sr;
		MMU.sv_sr[i] = MMU.illegal_sr;
	}
	invalidate_splitmode_sr( kv );

	do_mtsdr1( kv, MREGS.spr[S_SDR1] );

	for( i=0; i<16; i++ )
		do_mtbat( kv, S_IBAT0U+i, MREGS.spr[ S_IBAT0U+i ], 1 );
}

/*
 * A page we might be using is about to be destroyed (e.g. swapped out).
 * Any PTEs referencing this page must be flushed. The context parameter
 * is vsid >> 4.
 *
 * ENTRYPOINT!
 */
void
do_flush( ulong context, ulong va, ulong *dummy, int n )
{
	int i;
	kernel_vars_t *kv;
	BUMP( do_flush );

	atomic_inc_mol( &g_sesstab->external_thread_cnt );

	for( i=0; i<MAX_NUM_SESSIONS; i++ ) {
		if( !(kv=g_sesstab->kvars[i]) || context != kv->mmu.emulator_context )
			continue;

		BUMP_N( block_destroyed_ctr, n );
		for( ; n-- ; va += 0x1000 )
			flush_lvptr( kv, va );
		break;
	}

	atomic_dec_mol( &g_sesstab->external_thread_cnt );
}


/************************************************************************/
/*	Debugger functions						*/
/************************************************************************/

int
dbg_get_PTE( kernel_vars_t *kv, int context, ulong va, mPTE_t *retptr )
{
	ulong base, mask;
	ulong vsid, ptmp, stmp, *pteg, *steg;
	ulong cmp;
        ulong *uret = (ulong*)retptr;
	int i, num_match=0;

	switch( context ) {
	case kContextUnmapped:
		vsid = MMU.unmapped_sr[va>>28];
		break;
	case kContextMapped_S:
		vsid = MMU.sv_sr[va>>28];
		break;
	case kContextMapped_U:
		vsid = MMU.user_sr[va>>28];
		break;
	case kContextEmulator:
		vsid = (MUNGE_CONTEXT(MMU.emulator_context) + ((va>>28) * MUNGE_ESID_ADD)) & 0xffffff;
		break;
        case kContextKernel:
                vsid = 0;
                break;
	default:
		printk("get_PTE: no such context: %d\n", context );
		return 0;
	}

	/* mask vsid and va */
	vsid &= 0xffffff;
	va &= 0x0ffff000;

	/* get hash base and hash mask */
	base = (ulong)ptehash.base;
	mask = ptehash.pteg_mask >> 6;

	/* hash function */
	ptmp = (vsid ^ (va>>12)) & mask;
	stmp = mask & ~ptmp;
	pteg = (ulong*)((ptmp << 6) + base);
	steg = (ulong*)((stmp << 6) + base);

	/* construct compare word */
	cmp = 0x80000000 | (vsid <<7) | (va>>22);

	/* look in primary PTEG */
	for( i=0; i<8; i++ ) {
		if( cmp == pteg[i*2] ) {
			if( !num_match++ && uret ) {
				uret[0] = pteg[i*2];
				uret[1] = pteg[i*2+1];
			}
			if( num_match == 2 ) {
				printk("Internal ERROR: duplicate PTEs!\n");
				printk("p-hash: low_pte: %08lX  high_pte: %08lX\n",
				      uret ? uret[0]:0, retptr? uret[1]:0 );
			}
			if( num_match >= 2 ) {
				printk("p-hash: low_pte: %08lX  high_pte: %08lX\n",
				       pteg[i*2], pteg[i*2+1] );
			}
		}
	}

	/* look in secondary PTEG */
	cmp |= 0x40;
	for( i=0; i<8; i++ ) {
		if( cmp == steg[i*2] ) {
			if( !num_match++ && uret ) {
				uret[0] = steg[i*2];
				uret[1] = steg[i*2+1];
			}
			if( num_match == 2 ) {
				printk("Internal ERROR: duplicate PTEs!\n");
				printk("?-hash: low_pte: %08lX  high_pte: %08lX\n",
				      uret? uret[0]:0, uret? uret[1]:0 );
			}
			if( num_match >= 2 ) {
				printk("s-hash: low_pte: %08lX  high_pte: %08lX\n",
				       steg[i*2], steg[i*2+1] );
			}
		}
	}
	return num_match;
}
