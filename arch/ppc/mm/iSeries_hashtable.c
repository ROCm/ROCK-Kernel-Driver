/*
 * 
 *
 *    Copyright (c) 2000 Mike Corrigan <mikejc@us.ibm.com>  IBM Corporation
 *    updated by Dave Boutcher (boutcher@us.ibm.com)
 *
 *    Module name: iSeries_hashtable.c
 *
 *    Description:
 *      Handles Hash Table faults for iSeries LPAR.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
 

#include <asm/processor.h>
#include <asm/pgtable.h>
#include <linux/mm.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/types.h>
#include <linux/spinlock.h>
#include <asm/iSeries/HvCallHpt.h>
#include <asm/iSeries/LparData.h>

#include <asm/mmu_context.h>

int iSeries_hpt_loaded;

static spinlock_t hash_table_lock = SPIN_LOCK_UNLOCKED;

extern unsigned long htab_reloads;	// Defined in ppc/kernel/ppc_htab.c
extern unsigned long htab_evicts;
unsigned long htab_pp_update = 0;

unsigned long flush_hash_page_count  = 0;
unsigned long Hash_mask;		

unsigned long flush_hash_range_count  = 0;
unsigned long flush_hash_range_hv_finds  = 0;
unsigned long flush_hash_range_hv_evicts  = 0;
extern int iSeries_max_kernel_hpt_slot;
static unsigned next_slot  = 4;		// good enough starting value

extern void flush_hash_range( u64, u64, unsigned );

static inline u32  computeHptePP( unsigned long pte )
{
	return ( ( pte & _PAGE_USER )      >>  1 )  |
		( ( ( pte & _PAGE_USER )    >>  2 )  &
		  ( (~pte & _PAGE_RW   )    >> 10 )  );
}

static inline u32  computeHpteHash( unsigned long vsid,
				    unsigned long pid )
{
	return ( vsid ^ pid ) & Hash_mask;
}

/*
 * Should be called with hash page lock
 */
static void __create_hpte(unsigned long vsid, unsigned long va, unsigned long newpte)
{
	PTE hpte;
	u64 vpn;
	u64 rtnIndex;
	u64 *hpte0Ptr, *hpte1Ptr;
	u32 newpp, gIndex;
 	vsid = vsid & 0x7ffffff;
	vpn = ((u64)vsid << 16) | ((va >> 12) & 0xffff);

	hpte0Ptr = (u64 *)&hpte;
	hpte1Ptr = hpte0Ptr + 1;
	*hpte0Ptr = *hpte1Ptr = 0;

	rtnIndex = HvCallHpt_findValid( &hpte, vpn );
	
	newpp = computeHptePP( newpte );

	if ( hpte.v ) {
		/* A matching valid entry was found
		 * Just update the pp bits
		 */
		++htab_pp_update;
		HvCallHpt_setPp( rtnIndex, newpp );
	} else { /* No matching entry was found  Build new hpte */
		hpte.vsid = vsid;
		hpte.api = (va >> 23) & 0x1f;
		hpte.v = 1;
		hpte.rpn = physRpn_to_absRpn(newpte>>12);
		hpte.r = 1;
		hpte.c = 1;
		hpte.m = 1;
		hpte.pp = newpp;
		
		if ( ( rtnIndex != ~0 ) && 
		     ( rtnIndex != 0x00000000ffffffff ) ) {
				/* Free entry was found */
		  if ( ( rtnIndex >> 63 ) ||
		       ( rtnIndex & 0x80000000 ) )
		    hpte.h = 1;
		  HvCallHpt_addValidate(
					rtnIndex,
					hpte.h,
					&hpte );
		} else {
			/* No free entry was found */
			gIndex = computeHpteHash( vsid, vpn & 0xffff );
			rtnIndex = gIndex*8 + next_slot;
			if ( ++next_slot > 7 )
				next_slot = iSeries_max_kernel_hpt_slot+1;
			HvCallHpt_invalidateSetSwBitsGet(
				rtnIndex, 0, 1 );
			HvCallHpt_addValidate(
				rtnIndex, 0, &hpte );
			++htab_evicts;
		}
	}
}		

int iSeries_create_hpte( unsigned long access, unsigned long va )
{
	struct thread_struct *ts;
	pgd_t * pg;
	pmd_t * pm;
	pte_t * pt;
	u32 vsid;
	unsigned flags;

	vsid = mfsrin( va ) & 0x07ffffff;

	if ( va >= KERNELBASE )
		pg = swapper_pg_dir;
	else {
		// Get the thread structure
		ts = (struct thread_struct *)mfspr(SPRG3);
		// Get the page directory
		pg = ts->pgdir;
	}
	
	pg = pg + pgd_index( va );	// offset into first level
	pm = pmd_offset( pg, va );	// offset into second level
	if ( pmd_none( *pm ) ) 		// if no third level
		return 1;		// indicate failure		
	pt = pte_offset( pm, va );	// offset into third level
	
	access |= _PAGE_PRESENT;		// _PAGE_PRESENT also needed

	spin_lock( &hash_table_lock );
	// check if pte is in the required state
	if ( ( access & ~(pte_val(*pt)) ) ) {
		spin_unlock( &hash_table_lock );
		return 1;
	}

	/* pte allows the access we are making */
	flags = _PAGE_ACCESSED | _PAGE_HASHPTE | _PAGE_COHERENT;
	if ( access & _PAGE_RW )	/* If write access */
		flags |= _PAGE_RW;

	/* atomically update pte */
	pte_update( pt, 0, flags );
	__create_hpte(vsid,
		      va, 
		      pte_val(*pt));

	spin_unlock( &hash_table_lock );
	return 0;
}

void add_hash_page(unsigned context, unsigned long va, pte_t *ptep)
{
	spin_lock( &hash_table_lock );
	pte_update(ptep,0,_PAGE_HASHPTE);
	__create_hpte(CTX_TO_VSID(context, va), 
		      va, 
		      pte_val(*ptep));
	spin_unlock( &hash_table_lock );
}

int flush_hash_page(unsigned context, unsigned long va, pte_t *ptep)
{
	int rc;
	PTE hpte;
	u64 vpn;
	unsigned long vsid;
	u64 rtnIndex;
	u64 *hpte0Ptr, *hpte1Ptr;

	vsid = CTX_TO_VSID(context, va);

	vpn = ((u64)vsid << 16) | ((va >> 12) & 0xffff);

	hpte0Ptr = (u64 *)&hpte;
	hpte1Ptr = hpte0Ptr + 1;
	*hpte0Ptr = *hpte1Ptr = 0;
  
	spin_lock( &hash_table_lock );
	rtnIndex = HvCallHpt_findValid( &hpte, vpn );
  
	if ( hpte.v ) {
		pte_update(ptep, _PAGE_HASHPTE, 0);
		HvCallHpt_invalidateSetSwBitsGet(rtnIndex, 0, 1 );
		rc = 0;
	} else
		rc = 1;
	spin_unlock( &hash_table_lock );
	return rc;
}

