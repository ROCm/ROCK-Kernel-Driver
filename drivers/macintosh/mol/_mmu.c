/* 
 *   Creation Date: <2002/07/13 13:58:00 samuel>
 *   Time-stamp: <2003/08/27 15:21:06 samuel>
 *   
 *	<mmu.c>
 *	
 *	
 *   
 *   Copyright (C) 2002, 2003 Samuel Rydh (samuel@ibrium.se)
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
#include "asmfuncs.h"

#define MMU	(kv->mmu)

#ifdef CONFIG_SMP
void		(*xx_tlbie_lowmem)( void );
void		(*xx_store_pte_lowmem)( void );
#else
void		(*xx_store_pte_lowmem)( ulong *slot, int pte0, int pte1 );
#endif

int
arch_mmu_init( kernel_vars_t *kv )
{
	ulong sdr1 = _get_sdr1();
	
	if( !sdr1 ) { 
		/* Linux does not use SDR1 for the 603[e]. We just create the hash table. */
		if( create_pte_hash(kv, 1 /* set sdr1 */) )
			return 1;
	} else {
		mPTE_t *base = (mPTE_t*)phys_to_virt( sdr1 & ~0xffff );
		share_pte_hash( kv, sdr1, base );
	}

	MMU.emulator_context = current->mm->context;
	return 0;
}
