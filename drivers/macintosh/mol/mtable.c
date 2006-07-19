/* 
 *   Creation Date: <2002/05/26 14:46:42 samuel>
 *   Time-stamp: <2004/02/28 19:33:21 samuel>
 *   
 *	<mtable.c>
 *	
 *	Keeps track of all PTEs MOL uses.
 *   
 *   Copyright (C) 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifdef UL_DEBUG
#include "mtable_dbg.c"
#else
#include "archinclude.h"
#include "alloc.h"
#include "kernel_vars.h"
#include "asmfuncs.h"
#include "mmu.h"
#include "performance.h"
#endif
#include "mtable.h"
#include "hash.h"

/* #define DEBUG */

/* 
 * Implementation notes:
 *
 * - It is assumed bit the ITLB/DTLB is addressed by ea bits 14-19.
 * This holds true for all CPUs at the moment (603, 604, 750, 7400, 
 * 7410, 7450) except the 601 (which uses bits 13-19).
 */

typedef struct pterec	pterec_t;

struct pterec {
	pterec_t	*ea_next;		/* ea ring (MUST GO FIRST) */
	pterec_t	*lv_next;		/* lv ring */
	uint		pent;			/* defined below */
};

#define PENT_LV_HEAD	BIT(0)			/* Resident - do not put on free list */
#define PENT_UNUSED	BIT(1)			/* (lvhead) PTE index is not valid */
#define PENT_EA_BIT14	BIT(2)			/* for the partial ea used by tlbie */
#define PENT_EA_LAST	BIT(3)			/* next entry is the pelist pointer */
#define PENT_TOPEA_MASK	0x0f800000		/* bit 4-8 of ea */
#define PENT_SV_BIT	0x00400000		/* PTE uses vsid_sv */
#define PENT_INDEX_MASK	0x003fffff		/* PTE index (there can be at most 2^22 PTEs) */
#define PENT_CMP_MASK	(PENT_TOPEA_MASK | PENT_SV_BIT)

/* The index below corresponds to bit 15-19 of the ea. Bit 14 of the ea 
 * is stored in the pent field. Thus bits 14-19 of the ea can hence always
 * be reconstructed (this struct is always properly aligned). Note that the 
 * pelist forms a ring (this is the reason why ea_next must be 
 * the first element in the pterec struct). 
 */

typedef struct {
	pterec_t	*pelist[32];		/* always NULL if ring is empty */
} pent_table_t;

struct vsid_ent {				/* record which describes a mac vsid */
	vsid_ent_t	*myself_virt;		/* virtual address of this struct */
	int		linux_vsid;		/* munged mac context | VSID(Kp) */
	int		linux_vsid_sv;		/* munged privileged mac context | VSID(Kp) */
	pent_table_t	*lev2[64];		/* bit 9-14 of ea */
};

#define LEV2_MASK	0x0001ffff		/* bit 15-31 */

#define LEV2_IND(ea)	(((ea) >> (12+5)) & 0x3f)	/* lev2 index is bit 9-14 */
#define PELIST_IND(ea)	(((ea) >> 12) & 0x1f)		/* pelist index is 15-19 */

#define PTE_TO_IND(pte)	((((int)pte - (int)ptehash.base) & ptehash.pte_mask) >> 3)

#define ZERO_PTE(pent)	*((ulong*)ptehash.base + ((pent & PENT_INDEX_MASK) << 1)) = 0


struct pte_lvrange {
	pterec_t	*pents;
	ulong		base;			/* we want to do unsigned compares */
	ulong		size;
	pte_lvrange_t	*next;			/* linked list */
};

typedef struct alloc_ent {
	struct alloc_ent *next;
	char		*ptr;
	int		what;			/* ALLOC_CONTENTS_XXX */
} alloc_ent_t;

struct vsid_info {
	mol_spinlock_t	lock;			/* lvrange and pent ring lock */

	pte_lvrange_t	*lvrange_head;
	pterec_t	*free_pents;		/* free list (lv_next is used) */
	pent_table_t	*free_pent_tables;	/* pelist[0] is used for the linked list */

	alloc_ent_t	*allocations;		/* the allocations we have performed */
	int		alloc_size;		/* total size of allocations */
	int		alloc_limit;		/* imposed limit */
};

/* don't change the CHUNK_SIZE unless you know what you are doing... */
#define CHUNK_SIZE	(0x1000 - sizeof(alloc_ent_t))

#define ALLOC_CONT_ANY	0
#define ALLOC_CONT_VSID	1
#define ALLOC_CONT_PENT	2
#define ALLOC_CONT_LEV2	3

#define	MMU		(kv->mmu)

#define LOCK		spin_lock_mol( &vi->lock )
#define UNLOCK		spin_unlock_mol( &vi->lock )

/*
 * Remarks about locking: There is one asynchronous entrypoint
 * (flush_lvptr). This function touches the lvranges as well
 * as all pent rings. It will not free vsids or unlink
 * level2 tables (but pents are put on the free list).
 */


/************************************************************************/
/*	Table Flushing							*/
/************************************************************************/

#define vsid_ent_lookup( kv, mvsid )	((vsid_ent_t*)skiplist_lookup( &kv->mmu.vsid_sl, mvsid ))

static void
flush_vsid_ea_( vsid_info_t *vi, vsid_ent_t *r, ulong ea )
{
	pent_table_t *t = r->lev2[LEV2_IND(ea)];
	pterec_t **pp, **headp, *pr, *next, *lvp;
	uint topea, pent;
	int worked;

	if( !t || !(*(pp=&t->pelist[PELIST_IND(ea)])) )
		return;

	topea = (ea & PENT_TOPEA_MASK);
	worked = 0;
	headp = pp;
	pr = *pp;
	do {
		pent = pr->pent;
		next = pr->ea_next;

		if( (pent & PENT_TOPEA_MASK) == topea ) {
			worked = 1;
			/* unlink ea */
			*pp = pr->ea_next;
			
			/* unlink it from lv ring (unless it is the lv-head) */
			if( pent & PENT_LV_HEAD ) {
				pr->pent = PENT_UNUSED | PENT_LV_HEAD;
			} else {
				/* it is not certain it belong to a lv ring at all... */
				if( pr->lv_next ) {
					for( lvp=pr->lv_next ; lvp->lv_next != pr ; lvp=lvp->lv_next )
						;
					lvp->lv_next = pr->lv_next;
				}
				/* ...and put it on the free list */
				//printk("pent released\n");
				pr->lv_next = vi->free_pents;
				vi->free_pents = pr;
			}
			ZERO_PTE( pent );

			if( pent & PENT_EA_LAST ) {
				if( pp == headp ) {
					/* ring empty, set pelist pointer to NULL */
					*headp = NULL;
				} else {
					/* put marker on previous entry */
					((pterec_t*)pp)->pent |= PENT_EA_LAST;
				}
			}
		} else {
			pp = &pr->ea_next;
		}
		pr = next;
	} while( !(pent & PENT_EA_LAST) );
	
	if( worked )
		__tlbie( ea );
}

void
flush_vsid_ea( kernel_vars_t *kv, int mac_vsid, ulong ea )
{
	vsid_info_t *vi = MMU.vsid_info;
	vsid_ent_t *r;

	LOCK;
	if( (r=vsid_ent_lookup(kv, mac_vsid)) )
		flush_vsid_ea_( vi, r, ea );
	UNLOCK;
}


static void
pent_flush_unlink_ea( pterec_t *pr )
{
	pterec_t **head, *prev;
	uint ea;

	//BUMP( pent_flush_unlink_ea );
#ifdef DEBUG
	if( pr->pent & PENT_UNUSED )
		printk("pent_flush_unlink_ea: Internal error\n");
#endif
	/* find head and previous pent in ea ring */
	for( prev=pr; !(prev->pent & PENT_EA_LAST); prev=prev->ea_next )
		;
	head = (pterec_t**)prev->ea_next;
	for( ; prev->ea_next != pr ; prev=prev->ea_next )
		;

	if( (pr->pent & PENT_EA_LAST) ) {
		/* just a single entry in the ea ring? */
		if( prev == (pterec_t*)head ) {
			pr->ea_next = NULL;	/* prev->ea_next is set to this below */
		} else {
			prev->pent |= PENT_EA_LAST;
		}
	}
	prev->ea_next = pr->ea_next;

	/* OK... it is unlinked. Reconstruct EA and flush it */
	ZERO_PTE( pr->pent );
	ea = ((uint)head >> 2) & 0x1f;		/* Bits 15-19 of ea */
	if( pr->pent & PENT_EA_BIT14 )
		ea |= 0x20;
	ea = ea << 12;
	__tlbie(ea);				/* Bits 14-19 used */

	// printk("lvflush: ea (bit 14-19) %08X (pent %08X)\n", ea, pr->pent  );

	/* caller's responsibility to free the pent */
}

static void
flush_lvptr_( vsid_info_t *vi, ulong lvptr )
{
	pterec_t *head, *last, *first;
	pte_lvrange_t *lvr;

	//BUMP( pent_flush_lvptr );

	for( lvr=vi->lvrange_head; lvr && lvptr - lvr->base >= lvr->size ; lvr=lvr->next )
		;
	if( !lvr )
		return;
	// printk("flush_lvptr: %08lX\n", lvptr );

	head = lvr->pents + ((lvptr - lvr->base) >> 12);
#ifdef DEBUG
	if( !(head->pent & PENT_LV_HEAD) ) {
		printk("flush: Internal error\n");
		return;
	}
#endif
	/* first pent to be put on the free list */
	first = head->lv_next;

	/* not just a single entry? */
	if( first != head ) {
		last = head;
		do {
			last = last->lv_next;
			pent_flush_unlink_ea( last );
		} while( last->lv_next != head );
		
		last->lv_next = vi->free_pents;
		vi->free_pents = first;
	}
	if( !(head->pent & PENT_UNUSED) )
		pent_flush_unlink_ea( head );

	//head->ea_next = NULL;
	head->lv_next = head;
	head->pent = PENT_UNUSED | PENT_LV_HEAD;
}

/* asynchronous entrypoint (caused e.g. a swapout) */
void
flush_lvptr( kernel_vars_t *kv, ulong lvptr )
{
	vsid_info_t *vi = MMU.vsid_info;	
	LOCK;
	if( (char*)lvptr == MMU.lvptr_reservation )
		MMU.lvptr_reservation_lost = 1;
	flush_lvptr_( vi, lvptr );
	UNLOCK;
}


void
flush_lv_range( kernel_vars_t *kv, ulong lvbase, int size )
{
	vsid_info_t *vi = MMU.vsid_info;
	LOCK;
	/* this is quite inefficient but the function is seldom used */
	for( ; size > 0 ; lvbase += 0x1000, size -= 0x1000 )
		flush_lvptr_( vi, lvbase );
	UNLOCK;
}

void
flush_ea_range( kernel_vars_t *kv, ulong org_ea, int size )
{
	vsid_info_t *vi = MMU.vsid_info;
	skiplist_iter_t iter;
	pent_table_t *t;
	char *userdata;
	ulong ea, end;
	int i;

	//BUMP( flush_ea_range );
	//printk("flush_ea_range\n");

	LOCK;
#ifdef DEBUG
	if( size > 0x10000000 || org_ea & 0xf0000000 ) {
		printk("flush_ea_range: Bad parameters %08lX %08X\n", org_ea, size);
		size=0x10000000;
		org_ea=0;
	}
#endif
	end = org_ea + size;

	/* XXX: This is horribly inefficient */
	iter = skiplist_iterate( &MMU.vsid_sl );
	while( skiplist_getnext(&MMU.vsid_sl, &iter, &userdata) ) {
		vsid_ent_t *r = (vsid_ent_t*)userdata;
		ea = org_ea;
		while( ea < end ) {
			if( !(t=r->lev2[LEV2_IND(ea)]) ) {
				ea = (ea & ~LEV2_MASK) + LEV2_MASK + 1;
				continue;
			}
			for( i=PELIST_IND(ea); i<32 && ea < end; i++, ea += 0x1000 ) {
				if( t->pelist[i] )
					flush_vsid_ea_( vi, r, ea );
			}
		}
	}
	UNLOCK;
}

/* clear all pte entries belonging to this vsid */
static void
flush_vsid( vsid_info_t *vi, vsid_ent_t *r )
{
	pent_table_t *t;
	ulong ea=0;
	int i;

	//BUMP( flush_vsid );

	/* not very efficient */
	while( ea < 0x10000000 ) {
		if( !(t=r->lev2[LEV2_IND(ea)]) ) {
			ea = (ea & ~LEV2_MASK) + LEV2_MASK + 1;
			continue;
		}
		for( i=PELIST_IND(ea); i<32; i++, ea += 0x1000 ) {
			if( t->pelist[i] )
				flush_vsid_ea_( vi, r, ea );
		}
	}
	/* free level2 tables */
	for( i=0; i<64; i++ ) {
		pent_table_t *t = r->lev2[i];
		r->lev2[i] = NULL;

		/* XXX: The lev2 table _should_ be empty but we 
		 * might want to verify this...
		 */
		if( t ) {
			t->pelist[0] = (void*)vi->free_pent_tables;
			vi->free_pent_tables = t;
		}
	}
}


/************************************************************************/
/*	Allocations							*/
/************************************************************************/

/* this function allocates 0x1000 - sizeof(alloc_ent_t) zeroed bytes */
static void *
do_chunk_kmalloc( vsid_info_t *vi, int what )
{
	alloc_ent_t *mp;
	char *ptr;

	if( vi->alloc_size > vi->alloc_limit )
		return NULL;
	if( !(ptr=(char*)alloc_page_mol()) )
		return NULL;
	mp = (alloc_ent_t*)((char*)ptr + 0x1000 - sizeof(alloc_ent_t));

	mp->next = vi->allocations;
	mp->ptr = ptr;
	mp->what = what;
	vi->allocations = mp;

	vi->alloc_size += 0x1000;
	BUMP_N( alloced, 0x1000 );
	return ptr;
}

static void
do_kfree( vsid_info_t *vi, int what )
{
	alloc_ent_t *p, **mp = &vi->allocations;
	
	while( *mp ) {
		p = *mp;
		if( p->what == what || what == ALLOC_CONT_ANY ) {
			*mp = p->next;
			free_page_mol( (ulong)p->ptr );

			vi->alloc_size -= 0x1000;
			BUMP_N( released, 0x1000 );
		} else {
			mp = &p->next;
		}
	}
}

/* Note: mtable_memory_check() must have been called previously */
static inline pent_table_t *
get_free_lev2( vsid_info_t *vi )
{
	pent_table_t *t = vi->free_pent_tables;

	vi->free_pent_tables = (pent_table_t*)vi->free_pent_tables->pelist[0];
	t->pelist[0] = NULL;
	return t;
}

/* this function is responsible for setting PENT_LV_HEAD and lv_next */
static pterec_t *
get_free_pent( vsid_info_t *vi, pte_lvrange_t *lvrange, char *lvptr )
{
	pterec_t *pr, *pr2;
	int pent = 0;
	int ind;

	if( lvrange ) {
		ind = (((int)lvptr - lvrange->base) >> 12);
		pr2 = &lvrange->pents[ind];

		if( (pr2->pent & PENT_UNUSED) ) {
			pr = pr2;
			pent = PENT_LV_HEAD;
		} else {
			/* alloc new entry */
			pr = vi->free_pents;		
			vi->free_pents = pr->lv_next;

			/* add to lv ring (after the head element) */
			pr->lv_next = pr2->lv_next;
			pr2->lv_next = pr;
		}
	} else {
		/* alloc new entry */
		pr = vi->free_pents;
		vi->free_pents = pr->lv_next;

		pr->lv_next = NULL;
	}
	
	/* allocate pterec_t and insert into the lv ring */
	pr->pent = pent;
	return pr;
}

static int
lev2_alloc( vsid_info_t *vi )
{
	const int m = sizeof(pent_table_t) - 1;
	pent_table_t *t;
	int i, n = CHUNK_SIZE/sizeof(pent_table_t);
	
	//BUMP( lev2_alloc );

	if( !(t=do_chunk_kmalloc(vi, ALLOC_CONT_LEV2)) )
		return 1;

	/* the alignment must be correct (the ea calculation will fail otherwise) */
	if( (int)t & m ) {
		t = (pent_table_t*)((int)t + m + 1 - ((int)t & m));
		n--;
	}

	memset( t, 0, n*sizeof(pent_table_t) );
	for( i=0; i<n-1; i++ )
		t[i].pelist[0] = (void*)&t[i+1];
	LOCK;
	t[i].pelist[0] = (void*)vi->free_pent_tables;
	vi->free_pent_tables = &t[0];
	UNLOCK;       
	return 0;
}

static int
pent_alloc( vsid_info_t *vi )
{
	const int n = CHUNK_SIZE/sizeof(pterec_t);
	pterec_t *pr;
	int i;

	//BUMP( pent_alloc );

	if( !(pr=do_chunk_kmalloc(vi, ALLOC_CONT_PENT)) )
		return 1;
	memset( pr, 0, CHUNK_SIZE );
	
	for( i=0; i<n-1; i++ )
		pr[i].lv_next = &pr[i+1];
	LOCK;
	pr[i].lv_next = vi->free_pents;
	vi->free_pents = &pr[0];
	UNLOCK;
	return 0;
}


/* This function is to be called at a safe time (it might allocate
 * memory). It ensures the next pte_inserted call will succeed.
 */
int
mtable_memory_check( kernel_vars_t *kv ) 
{
	vsid_info_t *vi = MMU.vsid_info;

	/* optimize the common case */
	if( vi->free_pents && vi->free_pent_tables )
		return 0;

	if( !vi->free_pent_tables )
		lev2_alloc(vi);
	if( !vi->free_pents )
		pent_alloc(vi);
	
	if( !vi->free_pents || !vi->free_pent_tables ) {
		clear_all_vsids( kv );
		return 1;
	}
	return 0;
}


/************************************************************************/
/*	pte_insert							*/
/************************************************************************/

static inline void
relink_lv( vsid_info_t *vi, pterec_t *pr, pte_lvrange_t *lvrange, char *lvptr ) 
{
	int ind = (((int)lvptr - lvrange->base) >> 12);
	pterec_t *pnew, *p, *lv_head = &lvrange->pents[ind];

	if( !pr->lv_next ) {
		//printk("Not previously on lvlist\n");
		pr->lv_next = lv_head->lv_next;
		lv_head->lv_next = pr;
		return;
	}
	
	if( pr->pent & PENT_LV_HEAD ) {
		if( pr == lv_head ) {
			//printk("lvptr is head (correct lv ring)\n");
			return;
		}
		
		/* unlink from ea ring and add new pent */
		for( p=pr->ea_next; p->ea_next != pr ; p=p->ea_next )
				;
		pnew = get_free_pent( vi, lvrange, lvptr );
		pnew->ea_next = pr->ea_next;
		p->ea_next = pnew;

		pnew->pent |= (pr->pent & ~(PENT_UNUSED | PENT_LV_HEAD));

		/* clear old lvhead */
		// pr->ea_next = NULL;
		pr->pent = PENT_LV_HEAD | PENT_UNUSED;

		//printk("lvptr is head\n");
		return;
	} else {
		for( p=pr->lv_next; !(p->pent & PENT_LV_HEAD) ; p=p->lv_next )
			;
		if( p == lv_head ) {
			//printk("lvptr is on the correct lv ring\n");
			return;
		}

		/* lvptr has chagned, unlink */
		for( ; p->lv_next != pr ; p=p->lv_next )
			;
		p->lv_next = pr->lv_next;

		/* add to lv ring */
		pr->lv_next = lv_head->lv_next;
		lv_head->lv_next = pr;
	}
}

/* Note: If lvrange is NULL then lvptr should be ignored */
void
pte_inserted( kernel_vars_t *kv, ulong ea, char *lvptr, pte_lvrange_t *lvrange, 
	      ulong *pte, vsid_ent_t *r, int segreg )
{
	vsid_info_t *vi = MMU.vsid_info;
	int pl_ind = PELIST_IND(ea);
	uint pent, pent_cmp;
	pterec_t *pr, **pp;
	pent_table_t **tt;
	
	LOCK;
	if( lvrange && MMU.lvptr_reservation_lost ) {
		printk("mtable: lvptr reservation lost %08x\n", (int)lvptr );
		pte[0] = 0;
		__tlbie(ea);
		goto out;
	}

	tt = &r->lev2[ LEV2_IND(ea) ];

	pent_cmp = (ea & PENT_TOPEA_MASK);
	if( (r->linux_vsid_sv & VSID_MASK) == (segreg & VSID_MASK) )
		pent_cmp |= PENT_SV_BIT;

	if( !*tt )
		*tt = get_free_lev2(vi);
	
	pp = &(**tt).pelist[ pl_ind ];
	if( (pr=*pp) ) {
		do {
			pent = pr->pent;
			if( (pent & PENT_CMP_MASK) == pent_cmp ) {
				pent &= ~PENT_INDEX_MASK;
				pent |= PTE_TO_IND(pte);
				pr->pent = pent;

				/* the lvptr might have changed */
				if( lvrange )
					relink_lv( vi, pr, lvrange, lvptr );
				else {
					/* The pent might belong to a lvring unnecessarily.
					 * It is not worth the extra overhead addressing this
					 * (uncommon) case
					 */
				}
				//printk("PTE entry reused\n");
				goto out;
			}
			pr=pr->ea_next;
		} while( !(pent & PENT_EA_LAST) );

		/* get_free_pent inserts the entry into the lvring and sets a few pent bits */
		pr = get_free_pent(vi, lvrange, lvptr);
		pr->pent |= PTE_TO_IND(pte) | pent_cmp | ((ea & BIT(14)) ? PENT_EA_BIT14 : 0);

		/* insert in (non-empty) ea ring */
		pr->ea_next = *pp;
		*pp = pr;
	} else {
		/* ea ring was empty */
		pr = *pp = get_free_pent(vi, lvrange, lvptr);
		pr->pent |= PENT_EA_LAST | PTE_TO_IND(pte) | pent_cmp
				| ((ea & BIT(14)) ? PENT_EA_BIT14 : 0);
		pr->ea_next = (pterec_t*)pp;
	}
 out:
	UNLOCK;
}


/************************************************************************/
/*	VSID allocation							*/
/************************************************************************/

/* initialize vsid element callback (ind loops from 0 to n-1) */
static void
_vsid_el_callback( char *data, int ind, int n, void *usr1_kv, void *dummy )
{
	kernel_vars_t *kv = (kernel_vars_t*)usr1_kv;
	vsid_ent_t *r = (vsid_ent_t*)data;

	r->linux_vsid = alloc_context(kv) | VSID_Kp;
	r->linux_vsid_sv = alloc_context(kv) | VSID_Kp;
	r->myself_virt = r;
}

/* mac_vsid might be negative (used as vsid for unmapped access).
 * Thus, do not apply this VSID mask anywhere...
 */
static vsid_ent_t *
alloc_vsid_ent( kernel_vars_t *kv, int mac_vsid )
{
	char *buf;

	if( skiplist_needalloc(&MMU.vsid_sl) ) {
		/* this check might invoke clear_all_vsids() */
		handle_context_wrap( kv, CHUNK_SIZE/sizeof(vsid_ent_t)*2 );

		if( !(buf=do_chunk_kmalloc(MMU.vsid_info, ALLOC_CONT_VSID)) )
			return NULL;
		memset( buf, 0, CHUNK_SIZE );

		(void) skiplist_prealloc( &MMU.vsid_sl, buf, CHUNK_SIZE, _vsid_el_callback, kv, NULL );
	}
	return (vsid_ent_t*)skiplist_insert( &MMU.vsid_sl, mac_vsid );
}

/* flushes all vsids (including the fake no-MMU vsids) */ 
void
clear_all_vsids( kernel_vars_t *kv )
{
	vsid_info_t *vi = MMU.vsid_info;
	skiplist_iter_t iter;
	char *userdata;

	LOCK;
	iter = skiplist_iterate( &MMU.vsid_sl );
	while( skiplist_getnext(&MMU.vsid_sl, &iter, &userdata) )
		flush_vsid( vi, (vsid_ent_t*)userdata );

	skiplist_init( &MMU.vsid_sl, sizeof(vsid_ent_t) );

	/* flush any dangling pointers */
	clear_vsid_refs( kv );

	/* all vsids cleared -> all lev2 cleared -> no pents in use */
	vi->free_pents = NULL;
	vi->free_pent_tables = NULL;
	UNLOCK;
	do_kfree( vi, ALLOC_CONT_ANY );

	BUMP(clear_all_vsids);
}

/* This function flushes *ALL* PTEs inserted by MOL. It is primarily
 * used when it is too difficult to make a more specific invalidation.
 */
void
clear_pte_hash_table( kernel_vars_t *kv )
{
	/* this will free the vsids too... */
	clear_all_vsids( kv );
}

vsid_ent_t *
vsid_get_user_sv( kernel_vars_t *kv, int mac_vsid, ulong *user_ret, ulong *sv_ret )
{
	vsid_ent_t *r = vsid_ent_lookup( kv, mac_vsid );

	if( !r && !(r=alloc_vsid_ent(kv, mac_vsid)) ) {
		clear_all_vsids( kv );
		if( !(r=alloc_vsid_ent(kv, mac_vsid)) ) {
			printk("VSID allocation failure\n");
			return NULL;
		}
	}
	*user_ret = r->linux_vsid;
	*sv_ret = r->linux_vsid_sv;
	return r;
}

/************************************************************************/
/*	resource reclaiming						*/
/************************************************************************/

void
mtable_reclaim( kernel_vars_t *kv )
{
	vsid_info_t *vi = MMU.vsid_info;
	skiplist_iter_t iter;
	pent_table_t *t;
	char *userdata;
	int i,j;

	/* This thread runs on the main thread, thus the skiplist stuff does
	 * not need locking. In fact, it is only the free_pent_tables
	 * list that needs spinlock protection.
	 */
	LOCK;
	iter = skiplist_iterate( &MMU.vsid_sl );
	while( skiplist_getnext(&MMU.vsid_sl, &iter, &userdata) ) {
		vsid_ent_t *r = (vsid_ent_t*)userdata;
		const int n1 = sizeof(r->lev2)/sizeof(r->lev2[0]);
		const int n2 = sizeof(t->pelist)/sizeof(t->pelist[0]);

		for( i=0; i<n1; i++ ) {
			if( !(t=r->lev2[i]) )
				continue;
			for( j=0; j<n2 && !(t->pelist[j]) ; j++ )
				;
			if( j != n2 )
				break;
			/* level2 empty... */
			r->lev2[i]->pelist[0] = (void*)vi->free_pent_tables;
			vi->free_pent_tables = r->lev2[i];
			r->lev2[i] = NULL;

			BUMP(lev2_reclaim);
		}
		if( i == n1 ) {
			int vsid = skiplist_iter_getkey( &MMU.vsid_sl, (char*)r );

			/* the segment might be in use... */
			for( i=0; i<16 && MMU.vsid[i] != r; i++ )
				;
			if( i != 16 || (uint)vsid > VSID_MASK )
				continue;
			skiplist_delete( &MMU.vsid_sl, vsid );
			BUMP(vsid_reclaim);
		}
	}
	UNLOCK;
}

/************************************************************************/
/*	lvrange allocation						*/
/************************************************************************/

pte_lvrange_t *
register_lvrange( kernel_vars_t *kv, char *lvbase, int size )
{
	vsid_info_t *vi = MMU.vsid_info;
	pte_lvrange_t *lvr;
	int i, nel = (size >> 12);
	int s = sizeof(pterec_t) * nel;

	/* printk("register_lvrange\n"); */

	if( !(lvr=kmalloc_mol(sizeof(pte_lvrange_t))) )
		return NULL;
	memset( lvr, 0, sizeof(pte_lvrange_t) );
	
	if( !(lvr->pents=vmalloc_mol(s)) ) {
		kfree_mol( lvr );
		return NULL;
	}
	/* setup empty lvrings */
	for( i=0; i<nel; i++ ) {
		lvr->pents[i].pent = PENT_LV_HEAD | PENT_UNUSED;
		lvr->pents[i].lv_next = &lvr->pents[i];
		lvr->pents[i].ea_next = NULL;
	}
	lvr->base = (ulong)lvbase;
	lvr->size = size;

	LOCK;
	/* add to linked list */
	lvr->next = vi->lvrange_head;
	vi->lvrange_head = lvr;
	UNLOCK;

	return lvr;
}

void
free_lvrange( kernel_vars_t *kv, pte_lvrange_t *lvrange )
{
	vsid_info_t *vi = MMU.vsid_info;
	pte_lvrange_t **lvr;

	lvr = &vi->lvrange_head;
	for( ; *lvr && *lvr != lvrange; lvr=&(**lvr).next )
		;
	if( !*lvr ) {
		printk("free_lvrange: Internal error\n");
		return;
	}
	flush_lv_range( kv, (**lvr).base, (**lvr).size );
	LOCK;
	*lvr = (**lvr).next;
	UNLOCK;

	vfree_mol( lvrange->pents );
	kfree_mol( lvrange );
}

/************************************************************************/
/*	init / cleanup							*/
/************************************************************************/

void
mtable_tune_alloc_limit( kernel_vars_t *kv, int ramsize_mb )
{
	vsid_info_t *vi = MMU.vsid_info;
	vi->alloc_limit = (ramsize_mb + 160) * 4096;
	/* printk("alloc_limit: %d K\n", vi->alloc_limit/1024 ); */
}

int
init_mtable( kernel_vars_t *kv )
{
	vsid_info_t *vi = kmalloc_mol( sizeof(vsid_info_t) );
	
	MMU.vsid_info = vi;
	if( !vi )
		return 1;
	memset( vi, 0, sizeof(vsid_info_t) );
	spin_lock_init_mol( &vi->lock );

	/* will be tuned when we know how much RAM we have */
	vi->alloc_limit = 2 * 1024 * 1024;

	skiplist_init( &MMU.vsid_sl, sizeof(vsid_ent_t) );

	if( !VSID_OFFSETS_OK ) {
		printk("VSID offsets are BAD (fix offset in source)!\n");
		return 1;
	}
	return 0;
}

void
cleanup_mtable( kernel_vars_t *kv )
{
	vsid_info_t *vi = MMU.vsid_info;
	
	if( vi ) {
		while( vi->lvrange_head ) {
			printk("Bug: lvrange unreleased!\n");
			free_lvrange( kv, vi->lvrange_head );
		}
		do_kfree( vi, ALLOC_CONT_ANY );
		kfree_mol( vi );
	}
	memset( &MMU.vsid_sl, 0, sizeof(MMU.vsid_sl) );
	MMU.vsid_info = NULL;
}


/************************************************************************/
/*	userland debug							*/
/************************************************************************/

#ifdef UL_DEBUG
#include "mtable_dbg.c"
#endif
