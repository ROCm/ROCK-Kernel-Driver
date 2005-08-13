/* 
 *   Creation Date: <1998-11-11 11:56:45 samuel>
 *   Time-stamp: <2003/08/26 21:05:25 samuel>
 *   
 *	<mmu.c>
 *	
 *	Handles page mappings and the mac MMU
 *   
 *   Copyright (C) 1998-2003 Samuel Rydh (samuel@ibrium.se)
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

static char	*hash_allocation;
static int	sdr1_owned;		/* clear sdr1 at exit */

static ulong	hw_sdr1;
mPTE_t		*hw_hash_base;
ulong		hw_hash_mask;		/* _SDR1 = Hash_mask >> 10 */
ulong		hw_pte_offs_mask;	/* gives offset into hash table */

#define MREGS	(kv->mregs)
#define MMU	(kv->mmu)


/************************************************************************/
/*	arch_mmu_init() calls one of these functions			*/
/************************************************************************/

static void
init_hash_globals( kernel_vars_t *kv, ulong sdr1, mPTE_t *base )
{
	/* initialize only once... */ 
	if( !hw_sdr1 ) {
		hw_sdr1 = sdr1;
		hw_hash_base = base;
		hw_hash_mask = ((sdr1 & 0x1ff) << 10) | 0x3ff;
		hw_pte_offs_mask = (hw_hash_mask << 6) | 0x38;
	}
	MMU.hw_sdr1 = hw_sdr1;
}

void
share_pte_hash( kernel_vars_t *kv, ulong sdr1, mPTE_t *base )
{
	init_hash_globals( kv, sdr1, base );
}

int
create_pte_hash( kernel_vars_t *kv, int set_sdr1 )
{
	ulong size = 1024*128;		/* 128K is the kmalloc limit */ 
	ulong sdr1, mask, base;

	if( hw_sdr1 ) {
		init_hash_globals( kv, hw_sdr1, hw_hash_base );
		return 0;
	}

	if( !(hash_allocation=kmalloc_mol(size)) )
		return 1;
	memset( hash_allocation, 0, size );
	
	base = (ulong)hash_allocation;
	if( (base & (size-1)) ) {
		printk("Badly aligned SDR1 allocation - 64K wasted\n");
		size /= 2;
		base = (((ulong)hash_allocation + size) & ~(size-1));
	}
	mask = (size-1) >> 6;
	sdr1 = mask >> 10;
	sdr1 |= tophys_mol( (char*)base );

	init_hash_globals( kv, sdr1, (mPTE_t*)base );
	if( set_sdr1 ) {
		_set_sdr1( sdr1 );
		sdr1_owned = 1;
	}
	printk("SDR1 = %08lX\n", sdr1 );
	return 0;
}


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
	while( atomic_read(&g_sesstab->external_thread_cnt) )
		;

	cleanup_mmu_tracker( kv );
	cleanup_mmu_fb( kv );
	cleanup_mmu_io( kv );
	cleanup_mtable( kv );
	cleanup_contexts( kv );

	if( MMU.pthash_inuse_bits )
		kfree_mol( MMU.pthash_inuse_bits );

	memset( &MMU, 0, sizeof(mmu_vars_t) );

	if( g_num_sessions )
		return;

	if( hash_allocation ) {
		if( sdr1_owned )
			_set_sdr1(0);
		kfree_mol( hash_allocation );
		hash_allocation = NULL;
	}
	hw_hash_base = NULL;
	sdr1_owned = hw_sdr1 = hw_hash_mask = 0;
	
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

	for(i=0; i<16; i++ )
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

	atomic_inc( &g_sesstab->external_thread_cnt );

	for( i=0; i<MAX_NUM_SESSIONS; i++ ) {
		if( !(kv=g_sesstab->kvars[i]) || context != kv->mmu.emulator_context )
			continue;

		BUMP_N( block_destroyed_ctr, n );
		for( ; n-- ; va += 0x1000 )
			flush_lvptr( kv, va );
		break;
	}

	atomic_dec( &g_sesstab->external_thread_cnt );
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
	base = (ulong)hw_hash_base;
	mask = hw_hash_mask | 0x3ff;

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
