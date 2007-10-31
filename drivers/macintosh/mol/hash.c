/*
 *   Creation Date: <2004/02/14 11:42:19 samuel>
 *   Time-stamp: <2004/03/13 14:25:00 samuel>
 *
 *	<hash.c>
 *
 *	CPU PTE hash handling
 *
 *   Copyright (C) 2003, 2004 Samuel Rydh (samuel@ibrium.se)
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

/* GLOBALS */
hash_info_t	ptehash;

static struct {
	int	hash_mapped;
	int	sdr1_loaded;
	char	*allocation;
} hs;


static int
create_pte_hash( void )
{
	ulong size = 1024*128;		/* 128K is the kmalloc limit */
	ulong sdr1, mask, base, physbase;
	char *p;

	if( !(p=kmalloc_cont_mol(size)) )
		return 1;
	memset( p, 0, size );
	base = (ulong)p;
	physbase = tophys_mol( (char*)base );

	if( (physbase & (size-1)) ) {
		int offs;
		printk("Badly aligned SDR1 allocation - 64K wasted\n");
		size /= 2;
		offs = ((physbase + size) & ~(size-1)) - physbase;
		physbase += offs;
		base += offs;
	}
	mask = (size-1) >> 6;
	sdr1 = mask >> 10;
	sdr1 |= physbase;

	hs.allocation = p;
	ptehash.sdr1 = sdr1;
	ptehash.base = (ulong*)base;

	printk("SDR1 = %08lX\n", sdr1 );
	return 0;
}

int
init_hash( void )
{
	ulong sdr1;

	memset( &ptehash, 0, sizeof(ptehash) );

	if( IS_LINUX ) {
		sdr1  = _get_sdr1();

		/* linux does not use SDR1 on the 603[e] */
		if( !sdr1 ) {
			create_pte_hash();
			sdr1 = ptehash.sdr1;
			_set_sdr1( sdr1 );
			hs.sdr1_loaded = 1;
		}
	} else {
		/* sharing the hash under darwin is too complicated */
		create_pte_hash();
		sdr1 = ptehash.sdr1;
	}

	if( !sdr1 )
		return 1;

	ptehash.sdr1 = sdr1;
	ptehash.pteg_mask = (((sdr1 & 0x1ff) << 10) | 0x3ff) << 6;
	ptehash.pte_mask = ptehash.pteg_mask | 0x38;
	ptehash.physbase = sdr1 & ~0xffff;

	if( !ptehash.base ) {
		hs.hash_mapped = 1;
		ptehash.base = map_hw_hash( ptehash.physbase, ptehash.pte_mask + 8 );
	}

	return !ptehash.base;
}

void
cleanup_hash( void )
{
	if( hs.hash_mapped )
		unmap_hw_hash( ptehash.base );

	if( hs.sdr1_loaded )
		_set_sdr1( 0 );
	if( hs.allocation )
		kfree_cont_mol( hs.allocation );

	memset( &ptehash, 0, sizeof(ptehash) );
	memset( &hs, 0, sizeof(hs) );
}
