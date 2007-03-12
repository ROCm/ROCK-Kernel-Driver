/* 
 *   Creation Date: <1998-12-02 03:23:31 samuel>
 *   Time-stamp: <2004/03/13 16:57:31 samuel>
 *   
 *	<mmu_io.c>
 *	
 *	Translate mac_phys to whatever has been mapped in at
 *	a particular address (linux ram, framebuffer, ROM, etc.)
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
#include "kernel_vars.h"
#include "mmu.h"
#include "misc.h"
#include "mtable.h"
#include "performance.h"
#include "processor.h"

#define MAX_BLOCK_TRANS		6

/* Block translations are used for ROM, RAM, VRAM and similar things.
 *
 * IO-translations are a different type of mappings. Whenever an IO-area 
 * is accessed, a page fault occurs. If there is a page present (although 
 * r/w prohibited), then the low-level exception handler examines if the 
 * page has a magic signature in the first 8 bytes. If there is a match, 
 * then the page is of the type io_page_t and contains the information 
 * necessary to emulate the IO. If no page is present, then the corresponding
 * IO-page is looked up and hashed.
 */

typedef struct {
	ulong		mbase;
	char		*lvbase;
	pte_lvrange_t	*lvrange;

	size_t		size;
	int		flags;

	int		id;
} block_trans_t;

typedef struct io_data {
	block_trans_t	btable[MAX_BLOCK_TRANS];
	int		num_btrans;
	int		next_free_id;
	io_page_t 	*io_page_head;
} io_data_t;

static char		*scratch_page;

#define MMU 		(kv->mmu)
#define DECLARE_IOD	io_data_t *iod = kv->mmu.io_data



int
init_mmu_io( kernel_vars_t *kv )
{
	if( !(MMU.io_data=kmalloc_mol(sizeof(io_data_t))) )
		return 1;
	memset( MMU.io_data, 0, sizeof(io_data_t) );
	return 0;
}

void 
cleanup_mmu_io( kernel_vars_t *kv )
{
	DECLARE_IOD;
	io_page_t *next2, *p2;
	int i;
	
	if( !iod )
		return;

	for( p2=iod->io_page_head; p2; p2=next2 ) {
		next2 = p2->next;
		free_page_mol( (ulong)p2 );
	}
	iod->io_page_head = 0;

	/* release the scratch page (not always allocated) */
	if( !g_num_sessions && scratch_page ) {
		free_page_mol( (int)scratch_page );
		scratch_page = 0;
	}

	/* release any lvranges */
	for( i=0; i<iod->num_btrans; i++ )
		if( iod->btable[i].lvrange )
			free_lvrange( kv, iod->btable[i].lvrange );

	kfree_mol( iod );
	MMU.io_data = NULL;
}


/* This is primarily intended for framebuffers */
static int
bat_align( int flags, ulong ea, ulong lphys, ulong size, ulong bat[2] )
{
	ulong s;
	ulong offs1, offs2;

	s=0x20000;	/* 128K */
	if( s> size )
		return 1;
	/* Limit to 128MB in order not to cross segments (256MB is bat-max) */
	if( size > 0x10000000 )
		size = 0x10000000;
	for( ; s<size ; s = (s<<1) )
		;
	offs1 = ea & (s-1);
	offs2 = lphys & (s-1);
	if( offs1 != offs2 ) {
		printk("Can't use DBAT since offsets differ (%ld != %ld)\n", offs1, offs2 );
		return 1;
	}
	/* BEPI | BL | VS | VP */
	bat[0] = (ea & ~(s-1)) | (((s-1)>>17) << 2) | 3;
	bat[1] = (lphys & ~(s-1)) | 2;	/* pp=10, R/W */

#ifndef CONFIG_AMIGAONE
	bat[1] |= BIT(27);		/* [M] (memory coherence) */
#endif

	if( !(flags & MAPPING_FORCE_CACHE) ) {
		bat[1] |= BIT(26);	/* [I] (inhibit cache) */
	} else {
		bat[1] |= BIT(25);	/* [W] (write through) */
	}
	return 0;
}


/*
 * Handle block translations (translations of mac-physical
 * blocks to linux virtual physical addresses)
 */
static int
add_block_trans( kernel_vars_t *kv, ulong mbase, char *lvbase, ulong size, int flags )
{
	DECLARE_IOD;
	block_trans_t *bt;
	pte_lvrange_t *lvrange = NULL;
	int ind, i;

	/* warn if things are not aligned properly */
	if( (size & 0xfff) || ((int)lvbase & 0xfff) || (mbase & 0xfff) )
		printk("Bad block translation alignement\n");

	/* we keep an unsorted list - RAM should be added first, then ROM, then VRAM etc */
	if( iod->num_btrans >= MAX_BLOCK_TRANS ) {
		printk("Maximal number of block translations exceeded!\n");
		return -1;
	}

	/* remove illegal combinations */
	flags &= ~MAPPING_IO;
	if( (flags & MAPPING_DBAT) && !(flags & MAPPING_PHYSICAL) )
		flags &= ~MAPPING_DBAT;

	/* scratch pages are always physical - lvbase isn't used */
	if( (flags & MAPPING_SCRATCH) ) {
		lvbase = NULL;
		flags |= MAPPING_PHYSICAL;
		flags &= ~MAPPING_DBAT;
	}

	/* IMPORTANT: DBATs can _only_ be used when we KNOW that ea == mphys. */
	if( (flags & MAPPING_DBAT) ) {
		ulong bat[2];
		if( !bat_align(flags, mbase, (ulong)lvbase, size, bat) ) {
			/* printk("BATS: %08lX %08lX\n", bat[0], bat[1] ); */
			MMU.transl_dbat0.word[0] = bat[0];
			MMU.transl_dbat0.word[1] = bat[1];
		}
	}

	if( !(flags & MAPPING_PHYSICAL) )
		if( !(lvrange=register_lvrange(kv, lvbase, size)) )
			return -1;

	/* Determine where to insert the translation in the table.
	 * RAM should go right efter entries marked with MAPPING_PUT_FIRST.
	 * The MAPPING_PUT_FIRST flag is used to do magic things like
	 * embedding a copy of mregs in RAM.
	 */
	ind = (!mbase || (flags & MAPPING_PUT_FIRST)) ? 0 : iod->num_btrans;
	for( i=0; i<iod->num_btrans && ind <= i; i++ )
		if( iod->btable[i].flags & MAPPING_PUT_FIRST )
			ind++;
	bt = &iod->btable[ind];
	if( ind < iod->num_btrans )
		memmove( &iod->btable[ind+1], bt, sizeof(iod->btable[0]) * (iod->num_btrans - ind) );
	iod->num_btrans++;
	memset( bt, 0, sizeof(block_trans_t) );

	bt->mbase = mbase;
	bt->lvbase = lvbase;
	bt->lvrange = lvrange;
	bt->size = size;
	bt->flags = flags | MAPPING_VALID;
	bt->id = ++iod->next_free_id;

	/* flush everything if we a translation was overridden */
	if( flags & MAPPING_PUT_FIRST )
		clear_pte_hash_table( kv );

	return bt->id;
}

static void 
remove_block_trans( kernel_vars_t *kv, int id )
{
	DECLARE_IOD;
	block_trans_t *p;
	int i;

	/* Remove all mappings in the TLB table...
	 * (too difficult to find the entries we need to flush)
	 */
	BUMP(remove_block_trans);
	clear_pte_hash_table( kv );

	for( p=iod->btable, i=0; i<iod->num_btrans; i++, p++ ) {
		if( id == p->id ) {
			if( p->flags & MAPPING_DBAT ) {
				MMU.transl_dbat0.word[0] = 0;
				MMU.transl_dbat0.word[1] = 0;
			}
			if( p->lvrange )
				free_lvrange( kv, p->lvrange );

			memmove( p,p+1, (iod->num_btrans-1-i)*sizeof(block_trans_t)  );
			iod->num_btrans--;
			return;
		}
	}
	printk("Trying to remove nonexistent block mapping!\n");
}

/* adds an I/O-translation. It is legal to add the same
 * range multiple times (for instance, to alter usr_data)
 */
int 
add_io_trans( kernel_vars_t *kv, ulong mbase, int size, void *usr_data )
{
	DECLARE_IOD;
	io_page_t *ip, **pre_next;
	ulong mb;
	int i, num;
	
	/* align mbase and size to double word boundarys */
	size += mbase & 7;
	mbase -= mbase & 7;
	size = (size+7) & ~7;

	while( size > 0 ) {
		mb = mbase & 0xfffff000;

		pre_next = &iod->io_page_head;
		for( ip=iod->io_page_head; ip && ip->mphys < mb; ip=ip->next )
			pre_next = &ip->next;

		if( !ip || ip->mphys != mb ) {
			/* create new page */
			if( !(ip=(io_page_t*)alloc_page_mol()) ) {
				printk("Failed allocating IO-page\n");
				return 1;
			}
			ip->next = *pre_next;
			*pre_next = ip;

			/* setup block */
			ip->magic = IO_PAGE_MAGIC_1;
			ip->magic2 = IO_PAGE_MAGIC_2;
			ip->me_phys = tophys_mol(ip);
			ip->mphys = mb;
		}
		/* fill in IO */
		num = size>>3;
		i = (mbase & 0xfff) >> 3;
		if( i+num > 512 )
			num = 512-i;
		mbase += num<<3;
		size -= num<<3;
		while( num-- )
			ip->usr_data[i++] = usr_data;
	}
	return 0;
}

int 
remove_io_trans( kernel_vars_t *kv, ulong mbase, int size )
{
	DECLARE_IOD;
	io_page_t *ip, **pre_next;
	ulong mb;
	int i, num;

	/* To remove an unused IO-page, we must make sure there are no
	 * dangling references to it. Hence we must search the PTE hash 
	 * table and remove all references. We must also issue a 
	 * tlbia to make sure it is not in the on-chip DTLB/ITLB cashe.
	 *
	 * XXX: Instead of seraching the hash, we simply make sure the magic 
	 * constants are invalid. This is perfectly safe since the exception 
	 * handler doesn't write to the page in question - and the physical 
	 * page always exists even if it is allocated by somebody else. 
	 * It is better to make sure there are no references of it though.
	 *
	 * XXX: This needs to be fixed... commonly, we reallocate
	 * the page ourselves for I/O so the magic constants might
	 * be valid...
	 */

	/* align mbase and size to double word boundarys */
	size += mbase & 7;
	mbase -= mbase & 7;
	size = (size+7) & ~7;

	while( size > 0 ) {
		mb = mbase & 0xfffff000;

		pre_next = &iod->io_page_head;
		for( ip=iod->io_page_head; ip && ip->mphys < mb; ip=ip->next )
			pre_next = &ip->next;

		if( !ip || ip->mphys != mb ) {
			/* no page... */
			size -= 0x1000 - (mbase & 0xfff);
			mbase += 0x1000 - (mbase & 0xfff);
			continue;
		}
		/* clear IO */
		num = size>>3;
		i = (mbase & 0xfff) >> 3;
		if( i+num > 512 )
			num = 512-i;
		mbase += num<<3;
		size -= num<<3;
		while( num-- )
			ip->usr_data[i++] = 0;

		/* May we free the page? */
		for( i=0; i<512 && !ip->usr_data[i]; i++ )
			;
		if( i==512 ) {
			/* Free page (XXX: Remove page fram hash, see above ) */
			*pre_next = ip->next;
			ip->magic2 = ip->magic = 0;	/* IMPORTANT */
			free_page_mol( (ulong)ip );
		}
	}
	return 0;
	
}


/* Translate a mac-physical address (32 bit, not page-index) 
 * and fill in rpn (and _possibly_ other fields) of the pte.
 * The WIMG bits are not modified after this call. 
 * The calling function is not supposed to alter the pte after
 * this function call.
 *
 *	Retuns:
 *	  0 		no translation found
 *	  block_flags	translation found
 */

int
mphys_to_pte( kernel_vars_t *kv, ulong mphys, ulong *the_pte1, int is_write, pte_lvrange_t **lvrange )
{
	DECLARE_IOD;
	int i, num_btrans;
	block_trans_t *p;
	io_page_t *p2;
	int pte1 = *the_pte1;
	
	num_btrans = iod->num_btrans;
	mphys &= ~0xfff;

	/* check for emuaccel page */
	if( mphys == kv->emuaccel_mphys && kv->emuaccel_page_phys ) {
		/* printk("emuaccel - PTE-insert\n"); */
		pte1 |= kv->emuaccel_page_phys;
		/* supervisor r/w, no user access */
		pte1 &= ~(PTE1_W | PTE1_I | PTE1_PP);
		*lvrange = NULL;
		*the_pte1 = pte1;
		return MAPPING_VALID | MAPPING_PHYSICAL;
	}

	/* check for a block mapping. */
	for( p=iod->btable, i=0; i<num_btrans; i++,p++ ) {
		if( mphys - p->mbase < (ulong)p->size ) {
			if( (p->flags & MAPPING_SCRATCH) ) {
				/* it is OK to return silently if we run out of memory */
				if( !scratch_page && !(scratch_page=(char*)alloc_page_mol()) )
					return 0;
				pte1 |= tophys_mol(scratch_page);
			} else
				pte1 |= (mphys - p->mbase + (ulong)p->lvbase) & PTE1_RPN;
			
			if( p->flags & MAPPING_FORCE_CACHE ) {
				/* use write through for now */
				pte1 |= PTE1_W;
				pte1 &= ~PTE1_I;
			} else if( !(p->flags & MAPPING_MACOS_CONTROLS_CACHE) )
				pte1 &= ~(PTE1_W | PTE1_I);

			/* well, just a try...  */
			if ( p->flags & MAPPING_FORCE_WRITABLE ) {
				/* printk("forcing mphys page %lx writable\n", mphys); */
				pte1 = (pte1 & ~3) | 2;
			}

			*lvrange = p->lvrange;
			*the_pte1 = pte1;
			return p->flags;
		}
	}

	/* check for an I/O mapping. */
	for( p2=iod->io_page_head; p2 && p2->mphys<=mphys; p2=p2->next ) {
		if( p2->mphys != mphys )
			continue;
		pte1 |= p2->me_phys;
		/* supervisor R/W */
		pte1 &= ~(PTE1_PP | PTE1_W | PTE1_I);
		*lvrange = NULL;
		*the_pte1 = pte1;
		return MAPPING_VALID | MAPPING_IO | MAPPING_PHYSICAL;
	}
	return 0;
}

void 
mmu_add_map( kernel_vars_t *kv, struct mmu_mapping *m )
{
	if( m->flags & MAPPING_MREGS ) {
		char *start = (char*)tophys_mol(&kv->mregs);
		uint offs = (uint)m->lvbase;
		m->flags &= ~MAPPING_MREGS;
		m->flags |= MAPPING_PHYSICAL;
		m->lvbase = start + offs;
		m->id = -1;
		if( offs + (uint)m->size > NUM_MREGS_PAGES * 0x1000 ) {
			printk("Invalid mregs mapping\n");
			return;
		}
	}
	m->id = add_block_trans( kv, m->mbase, m->lvbase, m->size, m->flags );	
}

void 
mmu_remove_map( kernel_vars_t *kv, struct mmu_mapping *m )
{
	remove_block_trans( kv, m->id );
	m->id = 0;
}
