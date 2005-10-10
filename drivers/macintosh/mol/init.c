/* 
 *   Creation Date: <2002/01/13 20:45:37 samuel>
 *   Time-stamp: <2004/01/14 21:43:52 samuel>
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

/* globals */
session_table_t		*g_sesstab;
int 			g_num_sessions;


/************************************************************************/
/*	init/cleanup kernel module					*/
/************************************************************************/

int
common_init( void )
{
	if( !(g_sesstab=kmalloc_cont_mol(sizeof(*g_sesstab))) )
		return 1;
	memset( g_sesstab, 0, sizeof(*g_sesstab) );
	init_MUTEX( &g_sesstab->mutex );
	
	if( arch_common_init() ) {
		kfree_cont_mol( g_sesstab );
		return 1;
	}
	if( relocate_code() ) {
		arch_common_cleanup();
		kfree_cont_mol( g_sesstab );
		return 1;
	}
	return 0;
}

void
common_cleanup( void )
{
	relocation_cleanup();
	arch_common_cleanup();
	kfree_cont_mol( g_sesstab );
}


/************************************************************************/
/*	initialize / destroy session					*/
/************************************************************************/

static int 
initialize_session_( uint index )
{
	kernel_vars_t *kv;

	if( g_sesstab->magic == 1 )
		return -EMOLSECURITY;

	/* printk("initialize_session\n" ); */
	if( g_sesstab->kvars[index] )
		return -EMOLINUSE;

	if( !g_num_sessions && write_hooks() )
		return -EMOLGENERAL;
	
	if( !(kv=alloc_kvar_pages()) ) {
		remove_hooks();
		return -EMOLGENERAL;
	}

	memset( kv, 0, NUM_KVARS_PAGES * 0x1000 );
	kv->session_index = index;
	kv->kvars_virt = kv;

	if( init_mmu(kv) ) {
		remove_hooks();
		free_kvar_pages( kv );
		return -EMOLGENERAL;
	}
	initialize_spr_table( kv );

	msr_altered( kv );

	g_num_sessions++;

	g_sesstab->kvars_ph[index] = tophys_mol(kv);
	g_sesstab->kvars[index] = kv;

	return 0;
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
		cleanup_mmu( kv );

		if( kv->emuaccel_page )
			free_page_mol( kv->emuaccel_page );
		
		memset( kv, 0, NUM_KVARS_PAGES * 0x1000 );
		free_kvar_pages( kv );

		if( !g_num_sessions )
			remove_hooks();
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
