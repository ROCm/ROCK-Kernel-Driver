/* 
 *   Creation Date: <2000/09/07 20:36:54 samuel>
 *   Time-stamp: <2004/02/14 14:45:33 samuel>
 *   
 *	<mmu_tracker.c>
 *	
 *	Keeps track of dirty RAM pages
 *   
 *   Copyright (C) 2000, 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include "archinclude.h"
#include "alloc.h"
#include "uaccess.h"
#include "mmu.h"


typedef struct tracker_data {
	char 	*table;
	size_t	table_size;
	
	int	npages;
	ulong	lvbase;
} tracker_data_t;

#define MMU		(kv->mmu)
#define DECLARE_TS	tracker_data_t *ts = MMU.tracker_data



int
init_mmu_tracker( kernel_vars_t *kv )
{
	/* track_lvrange does the initialization */
	return 0;
}

void
cleanup_mmu_tracker( kernel_vars_t *kv )
{
	DECLARE_TS;
	if( !ts )
		return;

	if( ts->table )
		vfree_mol( ts->table );

	kfree_mol( ts );
	MMU.tracker_data = NULL;
}

int
track_lvrange( kernel_vars_t *kv )
{
	ulong lvbase = MMU.userspace_ram_base;
	int size = MMU.ram_size;
	
	DECLARE_TS;
	if( ts )
		cleanup_mmu_tracker( kv );
	if( !size )
		return 0;

	if( !(ts=kmalloc_mol(sizeof(tracker_data_t))) )
		return 1;
	memset( ts, 0, sizeof(tracker_data_t) );
	MMU.tracker_data = ts;

	ts->npages = size >> 12;
	ts->table_size = (ts->npages+7)/8;
	ts->lvbase = lvbase;
	if( !(ts->table=vmalloc_mol(ts->table_size)) ) {
		cleanup_mmu_tracker( kv );
		return 1;
	}
	memset( ts->table, 0, ts->table_size );
	return 0;
}

void
lvpage_dirty( kernel_vars_t *kv, ulong lvbase )
{
	DECLARE_TS;
	int pgindex;

	if( !ts )
		return;

	pgindex = (lvbase - ts->lvbase) >> 12;

	if( pgindex >=0 && pgindex < ts->npages )
		ts->table[pgindex >> 3] |= (1 << (pgindex & 7));
}


size_t
get_track_buffer( kernel_vars_t *kv, char *retbuf )
{
	DECLARE_TS;
	
	if( !ts )
		return 0;
	if( !retbuf )
		return ts->table_size;

	if( copy_to_user_mol(retbuf, ts->table, ts->table_size) )
		return 0;
	return ts->table_size;
}

void
set_track_buffer( kernel_vars_t *kv, char *buf )
{
	DECLARE_TS;

	if( !ts || !buf ) {
		printk("set_track_buffer: error\n");
		return;
	}
	if( copy_from_user_mol(ts->table, buf, ts->table_size) ) {
		printk("set_track_buffer: Bad access\n");
	}
}
