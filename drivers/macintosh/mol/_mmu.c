/* 
 *   Creation Date: <2002/07/13 13:58:00 samuel>
 *   Time-stamp: <2004/02/14 12:47:09 samuel>
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
	MMU.emulator_context = current->mm->context.id;
	return 0;
}
