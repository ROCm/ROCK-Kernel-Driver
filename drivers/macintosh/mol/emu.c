/* 
 *   Creation Date: <1998-11-21 16:07:47 samuel>
 *   Time-stamp: <2004/03/13 14:08:18 samuel>
 *   
 *	<emu.c>
 *	
 *	Emulation of some assembly instructions
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
#include "kernel_vars.h"
#include "emu.h"
#include "asmfuncs.h"
#include "rvec.h"
#include "processor.h"
#include "mtable.h"
#include "performance.h"
#include "emuaccel_sh.h"
#include "misc.h"
#include "map.h"

#define BAT_PERFORMANCE_HACK
// #define DEBUG

/* If BAT_PERFORMANCE_HACK is defined, PTEs corresponding to a mac bat
 * mapping will not necessary be flushed when the bat registers are 
 * touched. This gives a huge performance gain in MacOS 9.1 (which 
 * clears the bat registers in the idle loop). Of course, this break
 * compatibility (although most operating systems initializes the
 * BATs once and for all).
 */

#ifdef BAT_PERFORMANCE_HACK
 #define BAT_HACK(kv) (!MREGS.use_bat_hack || kv->mmu.bat_hack_count++ < 0x100)
#else
 #define BAT_HACK(kv) 1
#endif

#define MREGS	(kv->mregs)
#define MMU	(kv->mmu)


int
do_mtsdr1( kernel_vars_t *kv, ulong value )
{
	ulong mbase, mask;
	int s;
	
	MREGS.spr[S_SDR1] = value;

	/* the mask must be a valid one; we hade better make sure we are
	 * not tricked by a bogus sdr1 value
	 */
	for( mask=BIT(23); mask && !(mask & value) ; mask=mask>>1 )
		;
	mask = mask? ((mask | (mask-1)) << 16) | 0xffff : 0xffff;
	mbase = value & ~mask;

	if( mbase + mask >= MMU.ram_size ) {
		/* S_SDR1 out of range, fallback to a safe setting */
		printk("WARNING, S_SDR1, %08lX is out of range\n", value);
		mbase = 0;
		mask = 0xffff;
	}

	MMU.hash_mbase = mbase;
	MMU.hash_mask = mask;
	MMU.pthash_sr = -1;		/* clear old tlbhash matching */

	if( MMU.hash_base )
		unmap_emulated_hash( kv );
	MMU.hash_base = map_emulated_hash( kv, MMU.hash_mbase, mask+1 );

	/* try to allocate the PTE bitfield table (16K/128 MB ram). The worst
	 * case is 512K which will fail since the kmalloc limit is 128K.
	 * If the allocation fails, we simply don't use the bitfield table. 
	 */
	s = (mask+1)/8/8;		
	if( MMU.pthash_inuse_bits )
		kfree_cont_mol( MMU.pthash_inuse_bits );
	if( !(MMU.pthash_inuse_bits=kmalloc_cont_mol(s)) )
		MMU.pthash_inuse_bits_ph = 0;
	else {
		memset( MMU.pthash_inuse_bits, 0, s );
		MMU.pthash_inuse_bits_ph = tophys_mol( MMU.pthash_inuse_bits );
	}

	/* make sure the unmapped ram range is flushed... */
	flush_lv_range( kv, MMU.userspace_ram_base + mbase, mask+1 );

	/* ...as well as any MMU mappings */
	clear_pte_hash_table( kv );

	BUMP(do_mtsdr1);
	return RVEC_NOP;
}

/* This function is _very_ slow, since it must destroy a lot of PTEs.
 * Fortunately, BAT-maps are normally static.
 */
int 
do_mtbat( kernel_vars_t *kv, int sprnum, ulong value, int force )
{
	mac_bat_t *d;
	int batnum;
	mBAT *p;
	
	BUMP(do_mtbat);

	if( !force && MREGS.spr[sprnum] == value )
		return RVEC_NOP;

	/* printk("do_mtbat %d %08lX\n", sprnum, value); */

	MREGS.spr[sprnum] = value;

	/* upper bat register have an even number */
	batnum = (sprnum - S_IBAT0U) >>1;
	d = &MMU.bats[batnum];

	/* First we must make sure that all PTEs corresponding to 
	 * the old BAT-mapping are purged from the hash table.
	 */
	if( BAT_HACK(kv) && d->valid )
		flush_ea_range(kv, d->base & ~0xf0000000, d->size );

	p = (mBAT*)&MREGS.spr[sprnum & ~1];
	d->valid = p->batu.vs | p->batu.vp;
	d->vs = p->batu.vs;
	d->vp = p->batu.vp;
	d->wimg = (p->batl.w<<3) | (p->batl.i<<2) | (p->batl.m<<1) | p->batl.g;
	d->ks = d->ku = 1;	/* IBAT/DBATs, behaves as if key==1 */
	d->pp = p->batl.pp;
	d->size = (p->batu.bl+1)<<17;
	d->base = (p->batu.bepi & ~p->batu.bl)<<17;
	d->mbase = (p->batl.brpn & ~p->batu.bl)<<17;

	/* Next, we must make sure that no PTEs refer to the new
	 * BAT-mapped area.
	 */

	if( BAT_HACK(kv) && d->valid )
		flush_ea_range( kv, d->base & ~0xf0000000, d->size );

	return RVEC_NOP;
}


/************************************************************************/
/*	Emulation acceleration						*/
/************************************************************************/

static ulong
lookup_emuaccel_handler( int emuaccel )
{
	extern ulong emuaccel_table[];
	ulong handler, *p = emuaccel_table;
	
	for( ; p[0]; p+=3 ) {
		if( (emuaccel & EMUACCEL_INST_MASK) != p[0] )
			continue;
		emuaccel &= p[2];	/* offset mask */
		handler = p[1] + (ulong)emuaccel_table + emuaccel * 8;
		return tophys_mol( (ulong*)reloc_ptr(handler) );
	}
	return 0;
}

int
alloc_emuaccel_slot( kernel_vars_t *kv, int emuaccel, int param, int inst_addr )
{
	ulong *p = (ulong*)((char*)kv->emuaccel_page + kv->emuaccel_size);
	ulong handler = lookup_emuaccel_handler( emuaccel );
	int size, ret;
	
	size = (emuaccel & EMUACCEL_HAS_PARAM)? 16 : 8;
	if( !handler || !p || kv->emuaccel_size + size > 0x1000 )
		return 0;
	
	ret = kv->emuaccel_mphys + kv->emuaccel_size;
	p[0] = handler;
	p[1] = inst_addr + 4;

	if( emuaccel & EMUACCEL_HAS_PARAM ) {
		/* p[2] is already EMUACCEL_NOP */
		p[3] = param;
	}

	kv->emuaccel_size += size;
	return ret;
}

int
mapin_emuaccel_page( kernel_vars_t *kv, int mphys )
{
	int i, handler;
	ulong *p;

	if( kv->emuaccel_page || (mphys & 0xfff) )
		return 0;

	if( !(kv->emuaccel_page=alloc_page_mol()) )
		return 0;

	kv->emuaccel_page_phys = tophys_mol( (char*)kv->emuaccel_page );
	kv->emuaccel_mphys = mphys;
	p = (ulong*)kv->emuaccel_page;

	handler = lookup_emuaccel_handler( EMUACCEL_NOP );
	for( i=0; i<0x1000/sizeof(int); i+=2 ) {
		p[i] = handler;
		p[i+1] = 0;
	}

	/* flush translations - an old translation is overridden */
	clear_pte_hash_table( kv );
	/* printk("emuaccel_mapin: %08x\n", mphys ); */
	return mphys;
}
