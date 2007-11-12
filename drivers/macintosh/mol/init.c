/* 
 *   Creation Date: <2002/01/13 20:45:37 samuel>
 *   Time-stamp: <2004/02/14 14:01:09 samuel>
 *   
 *	<init.c>
 *	
 *	Kernel module initialization
 *   
 *   Copyright (C) 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include "archinclude.h"
#include "alloc.h"
#include "kernel_vars.h"
#include "misc.h"
#include "mmu.h"
#include "asmfuncs.h"
#include "performance.h"
#include "mol-ioctl.h"
#include "version.h"
#include "hash.h"

/* globals */
session_table_t		*g_sesstab;
int 			g_num_sessions;


/************************************************************************/
/*	init/cleanup kernel module					*/
/************************************************************************/

int
common_init( void )
{
	if( init_hash() )
		return 1;

	if( !(g_sesstab=kmalloc_cont_mol(sizeof(*g_sesstab))) ) {
		cleanup_hash();
		return 1;
	}
	
	memset( g_sesstab, 0, sizeof(*g_sesstab) );
	init_MUTEX_mol( &g_sesstab->lock );
	
	if( arch_common_init() ) {
		free_MUTEX_mol( &g_sesstab->lock );
		kfree_cont_mol( g_sesstab );
		cleanup_hash();
		return 1;
	}
	return 0;
}

void
common_cleanup( void )
{
	arch_common_cleanup();

	free_MUTEX_mol( &g_sesstab->lock );
	kfree_cont_mol( g_sesstab );
	g_sesstab = NULL;

	cleanup_hash();
}


/************************************************************************/
/*	initialize / destroy session					*/
/************************************************************************/

static int 
initialize_session_( uint index )
{
	kernel_vars_t *kv;
	ulong kv_phys;
	
	if( g_sesstab->magic == 1 )
		return -EMOLSECURITY;

	/* printk("initialize_session\n" ); */
	if( g_sesstab->kvars[index] )
		return -EMOLINUSE;

	if( !g_num_sessions && perform_actions() )
		return -EMOLGENERAL;
	
	if( !(kv=alloc_kvar_pages()) )
		goto error;

	memset( kv, 0, NUM_KVARS_PAGES * 0x1000 );
	kv->session_index = index;
	kv->kvars_virt = kv;
	kv_phys = tophys_mol(kv);
	kv->kvars_tophys_offs = kv_phys - (ulong)kv;

	if( init_mmu(kv) )
		goto error;

	init_host_irqs(kv);
	initialize_spr_table( kv );

	msr_altered( kv );

	g_num_sessions++;

	g_sesstab->kvars_ph[index] = kv_phys;
	g_sesstab->kvars[index] = kv;

	return 0;
 error:
	if( !g_num_sessions )
		cleanup_actions();
	if( kv )
		free_kvar_pages( kv );
	return -EMOLGENERAL;
}

int
initialize_session( uint index ) 
{
	int ret;
	if( index >= MAX_NUM_SESSIONS )
		return -EMOLINVAL;

	SESSION_LOCK;
	ret = initialize_session_( index );
	SESSION_UNLOCK;

	return ret;
}

void
destroy_session( uint index )
{
	kernel_vars_t *kv;

	if( index >= MAX_NUM_SESSIONS )
		return;

	if( g_sesstab->magic == 1 ) {
		printk("Security alert! Somebody other than MOL has tried to invoke\n"
		       "the MOL switch magic. The MOL infrastructure has been disabled.\n"
		       "Reboot in order to get MOL running again\n");
		/* make it impossible to unload the module */
		prevent_mod_unload();
	}

	SESSION_LOCK;
	if( (kv=g_sesstab->kvars[index]) ) {

		g_sesstab->kvars[index] = NULL;
		g_sesstab->kvars_ph[index] = 0;

		/* decrease before freeing anything (simplifies deallocation of shared resources) */
		g_num_sessions--;
		cleanup_host_irqs(kv);
		cleanup_mmu( kv );

		if( kv->emuaccel_page )
			free_page_mol( kv->emuaccel_page );
		
		memset( kv, 0, NUM_KVARS_PAGES * 0x1000 );
		free_kvar_pages( kv );

		if( !g_num_sessions )
			cleanup_actions();
	}
	SESSION_UNLOCK;
}

uint
get_session_magic( uint random_magic )
{
	if( random_magic < 2 )
		random_magic = 2;
	/* negative return values are interpreted as errors */
	random_magic &= 0x7fffffff;

	SESSION_LOCK;
	if( !g_sesstab->magic )
		g_sesstab->magic = random_magic;
	SESSION_UNLOCK;

	return g_sesstab->magic;
}
