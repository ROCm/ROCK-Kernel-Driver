/* 
 *   Creation Date: <1998-11-20 16:18:20 samuel>
 *   Time-stamp: <2004/02/28 19:27:16 samuel>
 *   
 *	<context.c>
 *	
 *	MMU context allocation
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
#include "mmu.h"
#include "mmu_contexts.h"
#include "misc.h"
#include "asmfuncs.h"
#include "emu.h"
#include "mtable.h"
#include "performance.h"


#define MMU	(kv->mmu)

static int
flush_all_PTEs( kernel_vars_t *kv )
{
	ulong ea, v, sdr1 = _get_sdr1();
	ulong *pte = phys_to_virt( sdr1 & ~0xffff );
	int npte = ((((sdr1 & 0x1ff) << 16) | 0xffff) + 1) / 8;
	int i, count=0;

	/* SDR1 might not be initialized (yet) on the 603 */
	if( !sdr1 )
		return 0;

	for( i=0; i<npte; i++, pte+=2 ) {
		v = *pte;
		if( !(v & BIT(0)) )	/* test V-bit */
			continue;
		v = (v & ~BIT(0)) >> 7;
		v = (v - ((v & 0xf) * MUNGE_ESID_ADD)) * MUNGE_MUL_INVERSE;
		v = (v>>4) & CTX_MASK;

		if( v >= MMU.first_mol_context && v <= MMU.last_mol_context ) {
 			*pte = 0;
			count++;
		}
	}

	/* perform a tlbia */
	for( ea=0; ea <= (0x3f << 12); ea += 0x1000 )
		__tlbie( ea );
	
	if( count )
		printk("%d stale PTEs flushed (something is wrong)\n", count );
	return count;
}

int
init_contexts( kernel_vars_t *kv )
{
	MMU.first_mol_context = FIRST_MOL_CONTEXT( kv->session_index );
	MMU.last_mol_context = LAST_MOL_CONTEXT( kv->session_index );
	MMU.next_mol_context = MMU.first_mol_context;

	MMU.illegal_sr = alloc_context(kv) | VSID_Kp | VSID_N;

	flush_all_PTEs( kv );
	return 0;
}

void
cleanup_contexts( kernel_vars_t *kv )
{
	flush_all_PTEs( kv );
}

void
handle_context_wrap( kernel_vars_t *kv, int n )
{
	if( MMU.next_mol_context + n > MMU.last_mol_context ) {
		printk("MOL context wrap\n");

		clear_all_vsids( kv );
		init_contexts( kv );
	}
}

int
alloc_context( kernel_vars_t *kv )
{
	int mol_context = MMU.next_mol_context++;
	int vsid = MUNGE_CONTEXT((mol_context >> 4)) << 4;

	vsid += MUNGE_ESID_ADD * (mol_context & 0xf);
	return (vsid & VSID_MASK);
}
