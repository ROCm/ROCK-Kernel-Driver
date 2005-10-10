/* 
 *   Creation Date: <97/05/26 02:10:43 samuel>
 *   Time-stamp: <2003/08/30 16:36:16 samuel>
 *   
 *	<misc.c>
 *	
 *	Kernel interface
 *   
 *   Copyright (C) 1997-2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include "archinclude.h"

#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <asm/prom.h>

#include "kernel_vars.h"
#include "misc.h"
#include "performance.h"

kernel_vars_t *
alloc_kvar_pages( void )
{
	kernel_vars_t *kv;
	int i, order;
	char *ptr;

	for( i=1, order=0; i<NUM_KVARS_PAGES; i=i<<1, order++ )
		;
	if( !(kv=(kernel_vars_t*)__get_free_pages(GFP_KERNEL, order)) )
		return NULL;

	/* To be able to export the kernel variables to user space, we
	 * must set the PG_reserved bit. This is due to a check
	 * in remap_pte_range() in kernel/memory.c (is this bug or a feature?).
	 */
	for( ptr=(char*)kv, i=0; i<NUM_KVARS_PAGES; i++, ptr+=0x1000 )
		SetPageReserved( virt_to_page(ptr) );

	return kv;
}

void
free_kvar_pages( kernel_vars_t *kv )
{
	char *ptr = (char*)kv;
	int i, order;

	for( i=0; i<NUM_KVARS_PAGES; i++, ptr+=0x1000 )
		ClearPageReserved( virt_to_page(ptr) );

	for( i=1, order=0; i<NUM_KVARS_PAGES; i=i<<1, order++ )
		;
	free_pages( (ulong)kv, order );
}

/************************************************************************/
/*	kernel lowmem asm <-> kernel C-code switching			*/
/************************************************************************/

typedef int	(*kernelfunc_t)( kernel_vars_t *, ulong, ulong, ulong );
typedef void	(*trampoline_t)( struct pt_regs *regs );
static trampoline_t old_trampoline;

static void
mol_trampoline_vector( struct pt_regs *r )
{
	kernel_vars_t *kv = (kernel_vars_t*)r->gpr[8];

#ifndef LINUX_26
	/* the 0x2f00 trap does not enable MSR_EE */
	local_irq_enable();
#endif
	TICK_CNTR_PUSH( kv );
	r->gpr[3] = (*(kernelfunc_t)r->gpr[3])( kv, r->gpr[4], r->gpr[5], r->gpr[6] );
	TICK_CNTR_POP( kv, in_kernel );
}

static trampoline_t
set_trampoline( trampoline_t tramp ) 
{
	trampoline_t old;
#ifdef LINUX_26
	extern trampoline_t mol_trampoline;
	old = mol_trampoline;
	mol_trampoline = tramp;
#else
	/* we steal the unused 0x2f00 exception vector... */
	u32 *p = (u32*)(KERNELBASE + 0x2f00);
	static trampoline_t *tptr = NULL;
	int i;

	/* look for bl xxxx ; .long vector; .long exception_return */
	for( i=0; !tptr && i<0x100/4; i++ ) {
		if( (p[i] & ~0xffff) != 0x48000000 )
			continue;
		if( (p[i+1] & ~0x7fffff) != KERNELBASE || (p[i+2] & ~0x0fffff) != KERNELBASE )
			continue;
		tptr = (trampoline_t*)&p[i+1];
	}
	if( !tptr ) {
		printk("MOL trampoline not found!\n");
		return NULL;
	}
	old = *tptr;
	*tptr = tramp;
#endif
	return old;
}

int
arch_common_init( void ) 
{
	old_trampoline = set_trampoline( mol_trampoline_vector );
	return !old_trampoline;
}

void
arch_common_cleanup( void ) 
{
	set_trampoline( old_trampoline );
}
