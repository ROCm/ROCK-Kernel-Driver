/*
 * 
 *
 * PowerPC64 port by Mike Corrigan and Dave Engebretsen
 *   {mikejc|engebret}@us.ibm.com
 *
 *    Copyright (c) 2000 Mike Corrigan <mikejc@us.ibm.com>
 * 
 *    Module name: htab.c
 *
 *    Description:
 *      PowerPC Hashed Page Table functions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/init.h>
#include <asm/system.h>
#include <asm/iSeries/LparData.h>
#include <linux/spinlock.h>
#include <asm/ppcdebug.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>
#include <linux/cache.h>
#include <asm/uaccess.h>
#include <asm/Naca.h>
#include <asm/system.h>
#include <asm/pmc.h>
#include <asm/machdep.h>
#include <asm/lmb.h>
#ifdef CONFIG_PPC_EEH
#include <asm/eeh.h>
#endif

/* For iSeries */
#include <asm/iSeries/HvCallHpt.h>

/* Note:  pte   --> Linux PTE
 *        HPTE  --> PowerPC Hashed Page Table Entry
 */

HTAB htab_data = {NULL, 0, 0, 0, 0};

int proc_dol2crvec(ctl_table *table, int write, struct file *filp,
		   void *buffer, size_t *lenp);

void htab_initialize(void);
void make_pte_LPAR(HPTE *htab,
	      unsigned long va, unsigned long pa, int mode,
	      unsigned long hash_mask, int large);

extern unsigned long reloc_offset(void);
extern unsigned long get_kernel_vsid( unsigned long ea );
extern void cacheable_memzero( void *, unsigned int );

extern unsigned long _SDR1;
extern unsigned long klimit;
extern struct Naca *naca;

extern char _stext[], _etext[], __start_naca[], __end_stab[];

static spinlock_t hash_table_lock __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;

#define PTRRELOC(x)	((typeof(x))((unsigned long)(x) - offset))
#define PTRUNRELOC(x)	((typeof(x))((unsigned long)(x) + offset))
#define RELOC(x)	(*PTRRELOC(&(x)))

extern unsigned long htab_size( unsigned long );
unsigned long hpte_getword0_iSeries( unsigned long slot );

#define KB (1024)
#define MB (1024*KB)
static inline void
create_pte_mapping(unsigned long start, unsigned long end,
		   unsigned long mode, unsigned long mask, int large)
{
	unsigned long addr, offset = reloc_offset();
	HTAB *_htab_data = PTRRELOC(&htab_data);
	HPTE  *htab  = (HPTE *)__v2a(_htab_data->htab);
	unsigned int step;

	if (large)
		step = 16*MB;
	else
		step = 4*KB;

	for (addr = start; addr < end; addr += step) {
		unsigned long vsid = get_kernel_vsid(addr);
		unsigned long va = (vsid << 28) | (addr & 0xfffffff);
		make_pte(htab, va, (unsigned long)__v2a(addr), mode, mask,
				large);
	}
}

void
htab_initialize(void)
{
	unsigned long table, htab_size_bytes;
	unsigned long pteg_count;
	unsigned long mode_ro, mode_rw, mask;
	unsigned long offset = reloc_offset();
	struct Naca *_naca = RELOC(naca);
	HTAB *_htab_data = PTRRELOC(&htab_data);

	/*
	 * Calculate the required size of the htab.  We want the number of
	 * PTEGs to equal one half the number of real pages.
	 */ 
	htab_size_bytes = 1UL << _naca->pftSize;
	pteg_count = htab_size_bytes >> 7;

	/* For debug, make the HTAB 1/8 as big as it normally would be. */
	ifppcdebug(PPCDBG_HTABSIZE) {
		pteg_count >>= 3;
		htab_size_bytes = pteg_count << 7;
	}

	_htab_data->htab_num_ptegs = pteg_count;
	_htab_data->htab_hash_mask = pteg_count - 1;

	if(_machine == _MACH_pSeries) {
		/* Find storage for the HPT.  Must be contiguous in
		 * the absolute address space.
		 */
		table = lmb_alloc(htab_size_bytes, htab_size_bytes);
		if ( !table )
			panic("ERROR, cannot find space for HPTE\n");
		_htab_data->htab = (HPTE *)__a2v(table);

		/* htab absolute addr + encoded htabsize */
		RELOC(_SDR1) = table + __ilog2(pteg_count) - 11;

		/* Initialize the HPT with no entries */
		cacheable_memzero((void *)table, htab_size_bytes);
	} else {
		_htab_data->htab = NULL;
		RELOC(_SDR1) = 0; 
	}

	mode_ro = _PAGE_ACCESSED | _PAGE_COHERENT | PP_RXRX;
	mode_rw = _PAGE_ACCESSED | _PAGE_COHERENT | PP_RWXX;
	mask = pteg_count-1;

	/* Create PTE's for the kernel text and data sections plus
	 * the HPT and HPTX arrays.  Make the assumption that
	 * (addr & KERNELBASE) == 0 (ie they are disjoint).
	 * We also assume that the va is <= 64 bits.
	 */
#if 0
	create_pte_mapping((unsigned long)_stext,       (unsigned long)__start_naca,                 mode_ro, mask);
	create_pte_mapping((unsigned long)__start_naca, (unsigned long)__end_stab,                   mode_rw, mask);
	create_pte_mapping((unsigned long)__end_stab,   (unsigned long)_etext,                       mode_ro, mask);
	create_pte_mapping((unsigned long)_etext,       RELOC(klimit),                               mode_rw, mask);
	create_pte_mapping((unsigned long)__a2v(table), (unsigned long)__a2v(table+htab_size_bytes), mode_rw, mask);
#else
#ifndef CONFIG_PPC_ISERIES
	if (__is_processor(PV_POWER4) && _naca->physicalMemorySize > 256*MB) {
		create_pte_mapping((unsigned long)KERNELBASE, 
				   KERNELBASE + 256*MB, mode_rw, mask, 0);
		create_pte_mapping((unsigned long)KERNELBASE + 256*MB, 
				   KERNELBASE + (_naca->physicalMemorySize), 
				   mode_rw, mask, 1);
		return;
	}
#endif
	create_pte_mapping((unsigned long)KERNELBASE, 
			   KERNELBASE+(_naca->physicalMemorySize), 
			   mode_rw, mask, 0);
#endif
}
#undef KB
#undef MB

/*
 * Create a pte.  Used during initialization only.
 * We assume the PTE will fit in the primary PTEG.
 */
void make_pte(HPTE *htab,
	      unsigned long va, unsigned long pa, int mode,
	      unsigned long hash_mask, int large)
{
	HPTE  *hptep;
	unsigned long hash, i;
	volatile unsigned long x = 1;
	unsigned long vpn;

#ifdef CONFIG_PPC_PSERIES
	if(_machine == _MACH_pSeriesLP) {
		make_pte_LPAR(htab, va, pa, mode, hash_mask, large); 
		return;
	}
#endif

	if (large)
		vpn = va >> 24;
	else
		vpn = va >> 12;

	hash = hpt_hash(vpn, large);

	hptep  = htab +  ((hash & hash_mask)*HPTES_PER_GROUP);

	for (i = 0; i < 8; ++i, ++hptep) {
		if ( hptep->dw0.dw0.v == 0 ) {		/* !valid */
			hptep->dw1.dword1 = pa | mode;
			hptep->dw0.dword0 = 0;
			hptep->dw0.dw0.avpn = va >> 23;
			hptep->dw0.dw0.bolted = 1;	/* bolted */
			hptep->dw0.dw0.v = 1;		/* make valid */
			return;
		}
	}

	/* We should _never_ get here and too early to call xmon. */
	for(;x;x|=1);
}

/* Functions to invalidate a HPTE */
static void hpte_invalidate_iSeries( unsigned long slot )
{
	HvCallHpt_invalidateSetSwBitsGet( slot, 0, 0 );
}

static void hpte_invalidate_pSeries( unsigned long slot )
{
	/* Local copy of the first doubleword of the HPTE */
	union {
		unsigned long d;
		Hpte_dword0   h;
	} hpte_dw0;

	/* Locate the HPTE */
	HPTE  * hptep  = htab_data.htab  + slot;

	/* Get the first doubleword of the HPTE */
	hpte_dw0.d = hptep->dw0.dword0;

	/* Invalidate the hpte */
	hptep->dw0.dword0 = 0;

	/* Invalidate the tlb   */
	{
		unsigned long vsid, group, pi, pi_high;

		vsid = hpte_dw0.h.avpn >> 5;
		group = slot >> 3;
		if(hpte_dw0.h.h) {
			group = ~group;
		} 
		pi = (vsid ^ group) & 0x7ff;
		pi_high = (hpte_dw0.h.avpn & 0x1f) << 11;
		pi |= pi_high;
		_tlbie(pi << 12);
	}
}


/* Select an available HPT slot for a new HPTE
 *   return slot index (if in primary group)
 *   return -slot index (if in secondary group) 
 */
static long hpte_selectslot_iSeries( unsigned long vpn )
{
	HPTE hpte;
	long ret_slot, orig_slot;
	unsigned long primary_hash;
	unsigned long hpteg_slot;
	unsigned long slot;
	unsigned i, k;
	union {
		unsigned long	d;
		Hpte_dword0	h;
	} hpte_dw0;

	ret_slot = orig_slot = HvCallHpt_findValid( &hpte, vpn );
	if ( hpte.dw0.dw0.v ) {		/* If valid ...what do we do now? */
		udbg_printf( "hpte_selectslot_iSeries: vpn 0x%016lx already valid at slot 0x%016lx\n", vpn, ret_slot );
		udbg_printf( "hpte_selectslot_iSeries: returned hpte 0x%016lx 0x%016lx\n", hpte.dw0.dword0, hpte.dw1.dword1 );
		panic("select_hpte_slot found entry already valid\n");
	}
	if ( ret_slot == -1 ) {		/* -1 indicates no available slots */

		/* No available entry found in secondary group */

		PMC_SW_SYSTEM(htab_capacity_castouts);

		primary_hash = hpt_hash(vpn, 0);
		hpteg_slot = ( primary_hash & htab_data.htab_hash_mask ) * HPTES_PER_GROUP;
		k = htab_data.next_round_robin++ & 0x7;

		for ( i=0; i<HPTES_PER_GROUP; ++i ) {
			if ( k == HPTES_PER_GROUP )
				k = 0;
			slot = hpteg_slot + k;
			hpte_dw0.d = hpte_getword0_iSeries( slot );
			if ( !hpte_dw0.h.bolted ) {
				hpte_invalidate_iSeries( slot );
				ret_slot = slot;
			}
			++k;
		}
	} else {
		if ( ret_slot < 0 ) {
			PMC_SW_SYSTEM(htab_primary_overflows);
			ret_slot &= 0x7fffffffffffffff;
			ret_slot = -ret_slot;
		}
	}
	if ( ret_slot == -1 ) {
		/* No non-bolted entry found in primary group - time to panic */
        	udbg_printf("hpte_selectslot_pSeries - No non-bolted HPTE in group 0x%lx! \n", hpteg_slot/HPTES_PER_GROUP);
        	panic("No non-bolted HPTE in group %lx", (unsigned long)hpteg_slot/HPTES_PER_GROUP);
	}
	PPCDBG(PPCDBG_MM, "hpte_selectslot_iSeries: vpn=0x%016lx, orig_slot=0x%016lx, ret_slot=0x%016lx \n",
	       vpn, orig_slot, ret_slot );	
	return ret_slot;
}

static long hpte_selectslot_pSeries(unsigned long vpn)
{
	HPTE * hptep;
	unsigned long primary_hash;
	unsigned long hpteg_slot;
	unsigned i, k;

	/* Search the primary group for an available slot */

	primary_hash = hpt_hash(vpn, 0);
	hpteg_slot = ( primary_hash & htab_data.htab_hash_mask ) * HPTES_PER_GROUP;
	hptep = htab_data.htab + hpteg_slot;
	
	for (i=0; i<HPTES_PER_GROUP; ++i) {
		if ( hptep->dw0.dw0.v == 0 ) {
			/* If an available slot found, return it */
			return hpteg_slot + i;
		}
		hptep++;
	}

	/* No available entry found in primary group */

	PMC_SW_SYSTEM(htab_primary_overflows);

	/* Search the secondary group */

	hpteg_slot = ( ~primary_hash & htab_data.htab_hash_mask ) * HPTES_PER_GROUP;
	hptep = htab_data.htab + hpteg_slot;

	for (i=0; i<HPTES_PER_GROUP; ++i) {
		if ( hptep->dw0.dw0.v == 0 ) {
			/* If an available slot found, return it */
			return -(hpteg_slot + i);
		}
		hptep++;
	}

	/* No available entry found in secondary group */

	PMC_SW_SYSTEM(htab_capacity_castouts);

	/* Select an entry in the primary group to replace */

	hpteg_slot = ( primary_hash & htab_data.htab_hash_mask ) * HPTES_PER_GROUP;
	hptep = htab_data.htab + hpteg_slot;
	k = htab_data.next_round_robin++ & 0x7;

	for (i=0; i<HPTES_PER_GROUP; ++i) {
		if (k == HPTES_PER_GROUP)
			k = 0;

		if (!hptep[k].dw0.dw0.bolted) {
			hpteg_slot += k;
			/* Invalidate the current entry */
			ppc_md.hpte_invalidate(hpteg_slot); 
			return hpteg_slot;
		}
		++k;
	}

	/* No non-bolted entry found in primary group - time to panic */
        udbg_printf("hpte_selectslot_pSeries - No non-bolted HPTE in group 0x%lx! \n", hpteg_slot/HPTES_PER_GROUP);
	/*      xmon(0); */
        panic("No non-bolted HPTE in group %lx", (unsigned long)hpteg_slot/HPTES_PER_GROUP);

	/* keep the compiler happy */
	return 0;
}

unsigned long hpte_getword0_iSeries( unsigned long slot )
{
	unsigned long dword0;

	HPTE hpte;
	HvCallHpt_get( &hpte, slot );
	dword0 = hpte.dw0.dword0;

	return dword0;
}

unsigned long hpte_getword0_pSeries( unsigned long slot )
{
	unsigned long dword0;
	HPTE * hptep = htab_data.htab + slot;

	dword0 = hptep->dw0.dword0;
	return dword0;
}

static long hpte_find_iSeries(unsigned long vpn)
{
	HPTE hpte;
	long slot;

	slot = HvCallHpt_findValid( &hpte, vpn );
	if ( hpte.dw0.dw0.v ) {
		if ( slot < 0 ) {
			slot &= 0x7fffffffffffffff;
			slot = -slot;
		}
	} else
		slot = -1;
	return slot;
}

static long hpte_find_pSeries(unsigned long vpn)
{
	union {
		unsigned long d;
		Hpte_dword0   h;
	} hpte_dw0;
	long slot;
	unsigned long hash;
	unsigned long i,j;

	hash = hpt_hash(vpn, 0);
	for ( j=0; j<2; ++j ) {
		slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
		for ( i=0; i<HPTES_PER_GROUP; ++i ) {
			hpte_dw0.d = hpte_getword0_pSeries( slot );
			if ( ( hpte_dw0.h.avpn == ( vpn >> 11 ) ) &&
			     ( hpte_dw0.h.v ) &&
			     ( hpte_dw0.h.h == j ) ) {
				/* HPTE matches */
				if ( j )
					slot = -slot;
				return slot;
			}
			++slot;
		}
		hash = ~hash;
	}
	return -1;
} 

/* This function is called by iSeries setup when initializing the hpt */
void build_valid_hpte( unsigned long vsid, unsigned long ea, unsigned long pa,
		       pte_t * ptep, unsigned hpteflags, unsigned bolted )
{
	unsigned long vpn, flags;
	long hpte_slot;
	unsigned hash;
	pte_t pte;

	vpn = ((vsid << 28) | ( ea & 0xffff000 )) >> 12;

	spin_lock_irqsave( &hash_table_lock, flags );

	hpte_slot = ppc_md.hpte_selectslot( vpn );
	hash = 0;
	if ( hpte_slot < 0 ) {
		hash = 1;
		hpte_slot = -hpte_slot;
	}
	ppc_md.hpte_create_valid( hpte_slot, vpn, pa >> 12, hash, ptep,
				  hpteflags, bolted );

	if ( ptep ) {
		/* Get existing pte flags */
		pte = *ptep;
		pte_val(pte) &= ~_PAGE_HPTEFLAGS;

		/* Add in the has hpte flag */
		pte_val(pte) |= _PAGE_HASHPTE;

		/* Add in the _PAGE_SECONDARY flag */
		pte_val(pte) |= hash << 15;

		/* Add in the hpte slot */
		pte_val(pte) |= (hpte_slot << 12) & _PAGE_GROUP_IX;
               
		/* Save the new pte.  */
		*ptep = pte;
               
	}
	spin_unlock_irqrestore( &hash_table_lock, flags );
}


/* Create an HPTE and validate it
 *   It is assumed that the HPT slot currently is invalid.
 *   The HPTE is set with the vpn, rpn (converted to absolute)
 *   and flags
 */
static void hpte_create_valid_iSeries(unsigned long slot, unsigned long vpn,
				      unsigned long prpn, unsigned hash, 
				      void * ptep, unsigned hpteflags, 
				      unsigned bolted )
{
	/* Local copy of HPTE */
	struct {
		/* Local copy of first doubleword of HPTE */
		union {
			unsigned long d;
			Hpte_dword0   h;
		} dw0;
		/* Local copy of second doubleword of HPTE */
		union {
			unsigned long     d;
			Hpte_dword1       h;
			Hpte_dword1_flags f;
		} dw1;
	} lhpte;
	
	unsigned long avpn = vpn >> 11;
	unsigned long arpn = physRpn_to_absRpn( prpn );

	/* Fill in the local HPTE with absolute rpn, avpn and flags */
	lhpte.dw1.d        = 0;
	lhpte.dw1.h.rpn    = arpn;
	lhpte.dw1.f.flags  = hpteflags;

	lhpte.dw0.d        = 0;
	lhpte.dw0.h.avpn   = avpn;
	lhpte.dw0.h.h      = hash;
	lhpte.dw0.h.bolted = bolted;
	lhpte.dw0.h.v      = 1;

	/* Now fill in the actual HPTE */
	HvCallHpt_addValidate( slot, hash, (HPTE *)&lhpte );
}

static void hpte_create_valid_pSeries(unsigned long slot, unsigned long vpn,
				      unsigned long prpn, unsigned hash, 
				      void * ptep, unsigned hpteflags, 
				      unsigned bolted)
{
	/* Local copy of HPTE */
	struct {
		/* Local copy of first doubleword of HPTE */
		union {
			unsigned long d;
			Hpte_dword0   h;
		} dw0;
		/* Local copy of second doubleword of HPTE */
		union {
			unsigned long     d;
			Hpte_dword1       h;
			Hpte_dword1_flags f;
		} dw1;
	} lhpte;
	
	unsigned long avpn = vpn >> 11;
	unsigned long arpn = physRpn_to_absRpn( prpn );

	HPTE *hptep;

	/* Fill in the local HPTE with absolute rpn, avpn and flags */
	lhpte.dw1.d        = 0;
	lhpte.dw1.h.rpn    = arpn;
	lhpte.dw1.f.flags  = hpteflags;

	lhpte.dw0.d        = 0;
	lhpte.dw0.h.avpn   = avpn;
	lhpte.dw0.h.h      = hash;
	lhpte.dw0.h.bolted = bolted;
	lhpte.dw0.h.v      = 1;

	/* Now fill in the actual HPTE */
	hptep  = htab_data.htab  + slot;

	/* Set the second dword first so that the valid bit
	 * is the last thing set
	 */
	
	hptep->dw1.dword1 = lhpte.dw1.d;

	/* Guarantee the second dword is visible before
	 * the valid bit
	 */
	
	__asm__ __volatile__ ("eieio" : : : "memory");

	/* Now set the first dword including the valid bit */
	hptep->dw0.dword0 = lhpte.dw0.d;

	__asm__ __volatile__ ("ptesync" : : : "memory");
}

/* find_linux_pte returns the address of a linux pte for a given 
 * effective address and directory.  If not found, it returns zero.
 */

pte_t  * find_linux_pte( pgd_t * pgdir, unsigned long ea )
{
	pgd_t *pg;
	pmd_t *pm;
	pte_t *pt = NULL;
	pte_t pte;
	pg = pgdir + pgd_index( ea );
	if ( ! pgd_none( *pg ) ) {

		pm = pmd_offset( pg, ea );
		if ( ! pmd_none( *pm ) ) { 
			pt = pte_offset_kernel( pm, ea );
			pte = *pt;
			if ( ! pte_present( pte ) )
				pt = NULL;
		}
	}

	return pt;

}

static inline unsigned long computeHptePP( unsigned long pte )
{
	return (     pte & _PAGE_USER )           |
		( ( ( pte & _PAGE_USER )    >> 1 ) &
		  ( ( ~( ( pte >> 2 ) &		/* _PAGE_RW */
		         ( pte >> 7 ) ) ) &     /* _PAGE_DIRTY */
			 1 ) );
}

static void hpte_updatepp_iSeries(long slot, unsigned long newpp, unsigned long va)
{
	HvCallHpt_setPp( slot, newpp );
}

static void hpte_updatepp_pSeries(long slot, unsigned long newpp, unsigned long va)
{
	/* Local copy of first doubleword of HPTE */
	union {
		unsigned long d;
		Hpte_dword0   h;
	} hpte_dw0;
	
	/* Local copy of second doubleword of HPTE */
	union {
		unsigned long     d;
		Hpte_dword1       h;
		Hpte_dword1_flags f;
	} hpte_dw1;	

	HPTE *  hptep  = htab_data.htab  + slot;

	/* Turn off valid bit in HPTE */
	hpte_dw0.d = hptep->dw0.dword0;
	hpte_dw0.h.v = 0;
	hptep->dw0.dword0 = hpte_dw0.d;

	/* Ensure it is out of the tlb too */
	_tlbie( va );

	/* Insert the new pp bits into the HPTE */
	hpte_dw1.d = hptep->dw1.dword1;
	hpte_dw1.h.pp = newpp;
	hptep->dw1.dword1 = hpte_dw1.d;

	/* Ensure it is visible before validating */
	__asm__ __volatile__ ("eieio" : : : "memory");

	/* Turn the valid bit back on in HPTE */
	hpte_dw0.h.v = 1;
	hptep->dw0.dword0 = hpte_dw0.d;

	__asm__ __volatile__ ("ptesync" : : : "memory");
}

/*
 * Update the page protection bits.  Intended to be used to create
 * guard pages for kernel data structures on pages which are bolted
 * in the HPT.  Assumes pages being operated on will not be stolen. 
 */
void hpte_updateboltedpp_iSeries(unsigned long newpp, unsigned long ea )
{
	unsigned long vsid,va,vpn;
	long slot;

	vsid = get_kernel_vsid( ea );
	va = ( vsid << 28 ) | ( ea & 0x0fffffff );
	vpn = va >> PAGE_SHIFT;

	slot = ppc_md.hpte_find( vpn );
	HvCallHpt_setPp( slot, newpp );
}


static __inline__ void set_pp_bit(unsigned long  pp, HPTE *addr)
{
	unsigned long old;
	unsigned long *p = (unsigned long *)(&(addr->dw1));

	__asm__ __volatile__(
        "1:	ldarx	%0,0,%3\n\
                rldimi  %0,%2,0,62\n\
                stdcx.	%0,0,%3\n\
            	bne	1b"
        : "=&r" (old), "=m" (*p)
        : "r" (pp), "r" (p), "m" (*p)
        : "cc");
}

/*
 * Update the page protection bits.  Intended to be used to create
 * guard pages for kernel data structures on pages which are bolted
 * in the HPT.  Assumes pages being operated on will not be stolen. 
 */
void hpte_updateboltedpp_pSeries(unsigned long newpp, unsigned long ea)
{
	unsigned long vsid,va,vpn,flags;
	long slot;
	HPTE *hptep;

	vsid = get_kernel_vsid( ea );
	va = ( vsid << 28 ) | ( ea & 0x0fffffff );
	vpn = va >> PAGE_SHIFT;

	slot = ppc_md.hpte_find( vpn );
	hptep = htab_data.htab  + slot;

	set_pp_bit(newpp , hptep);

	/* Ensure it is out of the tlb too */
	spin_lock_irqsave( &hash_table_lock, flags );
	_tlbie( va );
	spin_unlock_irqrestore( &hash_table_lock, flags );
}



/* This is called very early. */
void hpte_init_iSeries(void)
{
	ppc_md.hpte_invalidate   = hpte_invalidate_iSeries;
	ppc_md.hpte_updatepp     = hpte_updatepp_iSeries;
	ppc_md.hpte_updateboltedpp = hpte_updateboltedpp_iSeries;
	ppc_md.hpte_getword0     = hpte_getword0_iSeries;
	ppc_md.hpte_selectslot   = hpte_selectslot_iSeries;
	ppc_md.hpte_create_valid = hpte_create_valid_iSeries;
	ppc_md.hpte_find	 = hpte_find_iSeries;
}
void hpte_init_pSeries(void)
{
	ppc_md.hpte_invalidate   = hpte_invalidate_pSeries;
	ppc_md.hpte_updatepp     = hpte_updatepp_pSeries;
	ppc_md.hpte_updateboltedpp = hpte_updateboltedpp_pSeries;
	ppc_md.hpte_getword0     = hpte_getword0_pSeries;
	ppc_md.hpte_selectslot   = hpte_selectslot_pSeries;
	ppc_md.hpte_create_valid = hpte_create_valid_pSeries;
	ppc_md.hpte_find	 = hpte_find_pSeries;
}

/*
 * Handle a fault by adding an HPTE. If the address can't be determined
 * to be valid via Linux page tables, return 1. If handled return 0
 */
int hash_page(unsigned long ea, unsigned long access)
{
	void * pgdir = NULL;
	unsigned long va, vsid, vpn;
	unsigned long newpp, hash_ind, prpn;
	unsigned long hpteflags;
	long slot;
	struct mm_struct *mm;
	pte_t old_pte, new_pte, *ptep;

	/* Check for invalid addresses. */
	if (!IS_VALID_EA(ea))
		return 1;

	switch (REGION_ID(ea)) {
	case USER_REGION_ID:
		mm = current->mm;
		if (mm == NULL)
			return 1;

		vsid = get_vsid(mm->context, ea);
		break;
	case IO_REGION_ID:
		mm = &ioremap_mm;
		vsid = get_kernel_vsid(ea);
		break;
	case VMALLOC_REGION_ID:
		mm = &init_mm;
		vsid = get_kernel_vsid(ea);
		break;
#ifdef CONFIG_PPC_EEH
	case IO_UNMAPPED_REGION_ID:
		udbg_printf("EEH Error ea = 0x%lx\n", ea);
		PPCDBG_ENTER_DEBUGGER();
		panic("EEH Error ea = 0x%lx\n", ea);
		break;
#endif
	case KERNEL_REGION_ID:
		/*
		 * As htab_initialize is now, we shouldn't ever get here since
		 * we're bolting the entire 0xC0... region.
		 */
		udbg_printf("Little faulted on kernel address 0x%lx\n", ea);
		PPCDBG_ENTER_DEBUGGER();
		panic("Little faulted on kernel address 0x%lx\n", ea);
		break;
	default:
		/* Not a valid range, send the problem up to do_page_fault */
		return 1;
		break;
	}

	/* Search the Linux page table for a match with va */
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;
	pgdir = mm->pgd;

	if (pgdir == NULL)
		return 1;

	/*
	 * Lock the Linux page table to prevent mmap and kswapd
	 * from modifying entries while we search and update
	 */
	spin_lock(&mm->page_table_lock);

	ptep = find_linux_pte(pgdir, ea);

	/*
	 * If no pte found or not present, send the problem up to
	 * do_page_fault
	 */
	if (!ptep || !pte_present(*ptep)) {
		spin_unlock(&mm->page_table_lock);
		return 1;
	}

	/*
	 * Check the user's access rights to the page.  If access should be
	 * prevented then send the problem up to do_page_fault.
	 */
	access |= _PAGE_PRESENT;
	if (access & ~(pte_val(*ptep))) {
		spin_unlock(&mm->page_table_lock);
		return 1;
	}

	/*
	 * Acquire the hash table lock to guarantee that the linux
	 * pte we fetch will not change
	 */
	spin_lock(&hash_table_lock);
	
	old_pte = *ptep;
	
	/* At this point we have found a pte (which was present).
	 * The spinlocks prevent this status from changing
	 * The hash_table_lock prevents the _PAGE_HASHPTE status
	 * from changing (RPN, DIRTY and ACCESSED too)
	 * The page_table_lock prevents the pte from being 
	 * invalidated or modified
	 */

/* At this point, we have a pte (old_pte) which can be used to build or update
 * an HPTE.   There are 5 cases:
 *
 * 1. There is a valid (present) pte with no associated HPTE (this is 
 *	the most common case)
 * 2. There is a valid (present) pte with an associated HPTE.  The
 *	current values of the pp bits in the HPTE prevent access because the
 *	user doesn't have appropriate access rights.
 * 3. There is a valid (present) pte with an associated HPTE.  The
 *	current values of the pp bits in the HPTE prevent access because we are
 *	doing software DIRTY bit management and the page is currently not DIRTY. 
 * 4. This is a Kernel address (0xC---) for which there is no page directory.
 *	There is an HPTE for this page, but the pp bits prevent access.
 *      Since we always set up kernel pages with R/W access for the kernel
 *	this case only comes about for users trying to access the kernel.
 *	This case is always an error and is not dealt with further here.
 * 5. This is a Kernel address (0xC---) for which there is no page directory.
 *	There is no HPTE for this page.
 */

	/*
	 * Check if pte might have an hpte, but we have
	 * no slot information
	 */
	if ( pte_val(old_pte) & _PAGE_HPTENOIX ) {
		unsigned long slot;	
		pte_val(old_pte) &= ~_PAGE_HPTEFLAGS;
		slot = ppc_md.hpte_find( vpn );
		if ( slot != -1 ) {
			if ( slot < 0 ) {
				pte_val(old_pte) |= _PAGE_SECONDARY;
				slot = -slot;
			}
			pte_val(old_pte) |= ((slot << 12) & _PAGE_GROUP_IX) | _PAGE_HASHPTE;
			
		}
	}

	/* User has appropriate access rights. */
	new_pte = old_pte;
	/* If the attempted access was a store */
	if ( access & _PAGE_RW )
		pte_val(new_pte) |= _PAGE_ACCESSED |
			_PAGE_DIRTY;
	else
		pte_val(new_pte) |= _PAGE_ACCESSED;

	/* Only cases 1, 3 and 5 still in play */

	newpp = computeHptePP( pte_val(new_pte) );
	
	/* Check if pte already has an hpte (case 3) */
	if ( pte_val(old_pte) & _PAGE_HASHPTE ) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long hash, slot, secondary;
		/* Local copy of first doubleword of HPTE */
		union {
			unsigned long d;
			Hpte_dword0   h;
		} hpte_dw0;
		hash = hpt_hash(vpn, 0);
		secondary = (pte_val(old_pte) & _PAGE_SECONDARY) >> 15;
		if ( secondary )
			hash = ~hash;
		slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
		slot += (pte_val(old_pte) & _PAGE_GROUP_IX) >> 12;
		/* If there is an HPTE for this page it is indexed by slot */
		hpte_dw0.d = ppc_md.hpte_getword0( slot );
		if ( (hpte_dw0.h.avpn == (vpn >> 11) ) &&
		     (hpte_dw0.h.v) && 
		     (hpte_dw0.h.h == secondary ) ){
			/* HPTE matches */
			ppc_md.hpte_updatepp( slot, newpp, va );
			if ( !pte_same( old_pte, new_pte ) )
				*ptep = new_pte;
		} else {
			/* HPTE is not for this pte */
			pte_val(old_pte) &= ~_PAGE_HPTEFLAGS;
		}
	}
	if ( !( pte_val(old_pte) & _PAGE_HASHPTE ) ) {
		/* Cases 1 and 5 */
		/* For these cases we need to create a new
		 * HPTE and update the linux pte (for
		 * case 1).  For case 5 there is no linux pte.
		 *
		 * Find an available HPTE slot
		 */
		slot = ppc_md.hpte_selectslot(vpn);

		hash_ind = 0;
		if (slot < 0) {
			slot = -slot;
			hash_ind = 1;
		}

		/* Set the physical address */
		prpn = pte_val(old_pte) >> PTE_SHIFT;
		
		/* Update the linux pte with the HPTE slot */
		pte_val(new_pte) &= ~_PAGE_HPTEFLAGS;
		pte_val(new_pte) |= hash_ind << 15;
		pte_val(new_pte) |= (slot<<12) & _PAGE_GROUP_IX;
		pte_val(new_pte) |= _PAGE_HASHPTE;
		/* No need to use ldarx/stdcx here because all
		 * who might be updating the pte will hold the page_table_lock
		 * or the hash_table_lock (we hold both)
		 */
		*ptep = new_pte;
			
		/* copy appropriate flags from linux pte */
		hpteflags = (pte_val(new_pte) & 0x1f8) | newpp;

		/* Create the HPTE */
		ppc_md.hpte_create_valid(slot, vpn, prpn, hash_ind, ptep, hpteflags, 0);

	}

	spin_unlock(&hash_table_lock);
	spin_unlock(&mm->page_table_lock);
	return 0;
}

void flush_hash_page(unsigned long context, unsigned long ea, pte_t pte)
{
	unsigned long vsid, vpn, va, hash, secondary, slot, flags;
	union {
		unsigned long d;
		Hpte_dword0   h;
	} hpte_dw0;

	if ((ea >= USER_START) && (ea <= USER_END))
		vsid = get_vsid(context, ea);
	else
		vsid = get_kernel_vsid(ea);

	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;
	hash = hpt_hash(vpn, 0);
	secondary = (pte_val(pte) & _PAGE_SECONDARY) >> 15;
	if (secondary)
		hash = ~hash;
	slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
	slot += (pte_val(pte) & _PAGE_GROUP_IX) >> 12;

	spin_lock_irqsave(&hash_table_lock, flags);
	/*
	 * Id prefer to flush even if our hpte was stolen, but the new
	 * entry could be bolted - Anton
	 */
	hpte_dw0.d = ppc_md.hpte_getword0(slot);
	if ((hpte_dw0.h.avpn == (vpn >> 11)) &&
	    (hpte_dw0.h.v) && 
	    (hpte_dw0.h.h == secondary)){
		/* HPTE matches */
		ppc_md.hpte_invalidate(slot);	
	}

	spin_unlock_irqrestore(&hash_table_lock, flags);
}

int proc_dol2crvec(ctl_table *table, int write, struct file *filp,
		   void *buffer, size_t *lenp)
{
	int vleft, first=1, len, left, val;
#define TMPBUFLEN 256
	char buf[TMPBUFLEN], *p;
	static const char *sizestrings[4] = {
		"2MB", "256KB", "512KB", "1MB"
	};
	static const char *clockstrings[8] = {
		"clock disabled", "+1 clock", "+1.5 clock", "reserved(3)",
		"+2 clock", "+2.5 clock", "+3 clock", "reserved(7)"
	};
	static const char *typestrings[4] = {
		"flow-through burst SRAM", "reserved SRAM",
		"pipelined burst SRAM", "pipelined late-write SRAM"
	};
	static const char *holdstrings[4] = {
		"0.5", "1.0", "(reserved2)", "(reserved3)"
	};

	if ( ((_get_PVR() >> 16) != 8) && ((_get_PVR() >> 16) != 12))
		return -EFAULT;
	
	if ( /*!table->maxlen ||*/ (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	vleft = table->maxlen / sizeof(int);
	left = *lenp;
	
	for (; left /*&& vleft--*/; first=0) {
		if (write) {
			while (left) {
				char c;
				if(get_user(c,(char *) buffer))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				((char *) buffer)++;
			}
			if (!left)
				break;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if(copy_from_user(buf, buffer, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0);
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			buffer += len;
			left -= len;
#if 0
			/* DRENG need a def */
			_set_L2CR(0);
			_set_L2CR(val);
			while ( _get_L2CR() & 0x1 )
				/* wait for invalidate to finish */;
#endif
			  
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
#if 0
			/* DRENG need a def */
			val = _get_L2CR();
#endif
			p += sprintf(p, "0x%08x: ", val);
			p += sprintf(p, " %s", (val >> 31) & 1 ? "enabled" :
				     "disabled");
			p += sprintf(p, ", %sparity", (val>>30)&1 ? "" : "no ");
			p += sprintf(p, ", %s", sizestrings[(val >> 28) & 3]);
			p += sprintf(p, ", %s", clockstrings[(val >> 25) & 7]);
			p += sprintf(p, ", %s", typestrings[(val >> 23) & 2]);
			p += sprintf(p, "%s", (val>>22)&1 ? ", data only" : "");
			p += sprintf(p, "%s", (val>>20)&1 ? ", ZZ enabled": "");
			p += sprintf(p, ", %s", (val>>19)&1 ? "write-through" :
				     "copy-back");
			p += sprintf(p, "%s", (val>>18)&1 ? ", testing" : "");
			p += sprintf(p, ", %sns hold",holdstrings[(val>>16)&3]);
			p += sprintf(p, "%s", (val>>15)&1 ? ", DLL slow" : "");
			p += sprintf(p, "%s", (val>>14)&1 ? ", diff clock" :"");
			p += sprintf(p, "%s", (val>>13)&1 ? ", DLL bypass" :"");
			
			p += sprintf(p,"\n");
			
			len = strlen(buf);
			if (len > left)
				len = left;
			if(copy_to_user(buffer, buf, len))
				return -EFAULT;
			left -= len;
			buffer += len;
			break;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', (char *) buffer))
			return -EFAULT;
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left) {
			char c;
			if(get_user(c, p++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
}

