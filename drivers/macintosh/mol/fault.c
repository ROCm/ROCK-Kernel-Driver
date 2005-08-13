/* 
 *   Creation Date: <2002/06/08 20:53:20 samuel>
 *   Time-stamp: <2004/02/22 12:42:21 samuel>
 *   
 *	<fault.c>
 *	
 *	Page fault handler
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
#include "mmu.h"
#include "mmu_contexts.h"
#include "asmfuncs.h"
#include "emu.h"
#include "misc.h"
#include "constants.h"
#include "rvec.h"
#include "mtable.h"
#include "performance.h"
#include "processor.h"

/* exception bits (srr1/dsisr and a couple of mol defined bits) */
#define		EBIT_PAGE_FAULT		BIT(1)		/* I/D, PTE missing */
#define		EBIT_NO_EXEC		BIT(3)		/* I,   no-execute or guarded */
#define		EBIT_PROT_VIOL		BIT(4)		/* I/D, protection violation */
#define		EBIT_IS_WRITE		BIT(6)		/* D    */
#define		EBIT_IS_DSI		1		/* D,   virtual bit */
#define		EBIT_USE_MMU		2		/* I/D, virtual bit */

#define		use_mmu(ebits)		((ebits) & EBIT_USE_MMU)
#define		is_write(ebits)		((ebits) & EBIT_IS_WRITE)
#define		is_dsi(ebits)		((ebits) & EBIT_IS_DSI)
#define		is_prot_viol(ebits)	((ebits) & EBIT_PROT_VIOL)
#define		is_page_fault(ebits)	((ebits) & EBIT_PAGE_FAULT)

typedef struct {
	/* filled in by exception handler */
	ulong		ea;
	ulong		*sr_base;
	struct vsid_ent	**vsid_eptr;	/* pointer to MMU.vsid or MMU.unmapped_vsid */
	
	/* filled in by lookup_mphys */
	mPTE_t		*mpte;		/* lvptr to mac-pte (if != NULL) */
	ulong		mphys_page;	/* ea of mphys page */
	int		pte1;		/* RPN | 000 | R | C | WIMG | 00 | PP */
	int		key;		/* pp key bit */
} fault_param_t;

static const char priv_viol_table[16] = {	/* [is_write | key | PP] */
	0,0,0,0,1,0,0,0,   			/* read (1 == violation) */
	0,0,0,1,1,1,0,1    			/* write */
};

#define NO_MMU_PTE1	(PTE1_R | PTE1_C /*| PTE1_M*/ | 0x2 /*pp*/ )

#define MREGS		(kv->mregs)
#define MMU		(kv->mmu)

#ifdef CONFIG_SMP
#define SMP_PTE1_M	PTE1_M
#else
#define SMP_PTE1_M	0
#endif


/************************************************************************/
/*	Debugging							*/
/************************************************************************/

static inline void
DEBUG_print_inserted_pte( ulong *slot, ulong pte0, ulong pte1, ulong ea )
{
#if 0
	mPTE_t pte;
	ulong *p = (ulong)&pte;
	p[0] = pte0;
	p[1] = pte1;

	printk("[%p] ", slot );
	printk("RPN %08X  API %08X  EA %08lX  ", pte.rpn << 12, pte.api<<12, ea );
	printk("%c%c %c%c; PP %d\n", 
	       pte.h ? 'H' : 'h', 
	       pte.v ? 'V' : 'v', 
	       pte.r ? 'R' : 'r', 
	       pte.c ? 'C' : 'c', pte.pp );
#endif
}


/************************************************************************/
/*	MMU virtualization and page fault handling			*/
/************************************************************************/

#ifdef EMULATE_603
static inline int
lookup_603_pte( kernel_vars_t *kv, ulong vsid, ulong ea, int is_dsi, mPTE_t **ret_pte )
{
	int ind = (ea >> 12) & 0x1f;		/* 32x2 PTEs */
	ulong mask, phash, cmp, pteg, cmp_ea, *eap;
	mPTE_t *p;

	// printk("lookup_603_pte %08lX\n", ea);

	if( is_dsi ) {
		p = &MMU.ptes_d_603[ind];
		eap = &MMU.ptes_d_ea_603[ind];
	} else {
		p = &MMU.ptes_i_603[ind];
		eap = &MMU.ptes_i_ea_603[ind];
	}
	cmp_ea = ea & 0x0ffff000;
	for( ; ind < 64 ; ind += 32, p += 32, eap += 32 ) {
		if( *eap == cmp_ea && p->vsid == vsid ) {
			*ret_pte = p;
			return 0;
		}
	}
	mask = MMU.hash_mask >> 6;

	/* calculate primary and secondary PTEG */
	phash = (cmp_ea >> 12) ^ (vsid & 0x7ffff);
	pteg = ((phash & mask) << 6);
	MREGS.spr[S_HASH1] = MMU.hash_mbase + pteg;
	MREGS.spr[S_HASH2] = MMU.hash_mbase + (pteg ^ (mask << 6));

	/* construct compare word */
	cmp = BIT(0) | (vsid <<7) | (cmp_ea >> 22);
	if( is_dsi ) {
		MREGS.spr[S_DCMP] = cmp;
		MREGS.spr[S_DMISS] = ea;
	} else {
		MREGS.spr[S_ICMP] = cmp;
		MREGS.spr[S_IMISS] = ea;
	}
	return 1;
}
#endif

static inline mPTE_t *
lookup_mac_pte( kernel_vars_t *kv, ulong vsid, ulong ea )
{
	ulong phash, cmp, pteg, *p;
	ulong mask;
	int i;

	/* we are only interested in the page index */
	ea &= 0x0ffff000;
	mask = MMU.hash_mask>>6;

	/* calculate primary hash function */
	phash = (ea >> 12) ^ (vsid & 0x7ffff);
	pteg = ((phash & mask) << 6);

	/* construct compare word */
	cmp = BIT(0) | (vsid <<7) | ((ea&0x0fffffff)>>22);

	/* look in primary PTEG */
	p = (ulong*)((ulong)MMU.hash_base + pteg);
	for( i=0; i<8; i++, p+=2 )
		if( cmp == *p )
			return (mPTE_t*)p;
				
	/* look in secondary PTEG */
	p = (ulong*)( (ulong)MMU.hash_base + (pteg ^ (mask << 6)) );
	cmp |= BIT(25);

	for( i=0; i<8; i++,p+=2 )
		if( cmp == *p )
			return (mPTE_t*)p;
	return NULL;
}

static int 
lookup_mphys( kernel_vars_t *kv, fault_param_t *pb, const int ebits )
{
	ulong ea = (pb->ea & ~0xfff);
	mSEGREG segr;
	mac_bat_t *bp;
       	int sv_mode, i, sbits;
	mPTE_t *mpte;

	pb->mpte = NULL;

	if( !use_mmu(ebits) ) {
		pb->mphys_page = ea;
		pb->pte1 = NO_MMU_PTE1;
		pb->key = 0;
		return 0;
	}

	segr = *(mSEGREG*)&MREGS.segr[ea>>28];
	sv_mode = !(MREGS.msr & MSR_PR);

	/* I/O segment? */
	if( segr.t ) {
		/* Memory forced (601/604)? Note that the 601 uses I/O segments
		 * even if translation is off(!). We don't implement this though.
		 */
		ulong sr = MREGS.segr[ea>>28];
		BUMP( memory_forced_segment );

		if( ((sr >> 20) & 0x1ff) != 0x7f )
			return RVEC_MMU_IO_SEG_ACCESS;
		pb->mphys_page = (ea & 0x0ffff000) | ((sr & 0xf)<<28);
		pb->pte1 = NO_MMU_PTE1;
		pb->key = 0;
		return 0;
	}
	
	/* BAT translation? 0-3 = IBATs, 4-7 = DBATs. Separated I/D BATS, hace 3/8/99 */
	bp = is_dsi(ebits) ? &MMU.bats[4] : &MMU.bats[0];
	for( i=0; i<4; i++, bp++ ) {
		if( !bp->valid )
			continue;
		if( (sv_mode && !bp->vs) || (!sv_mode && !bp->vp) )
			continue;
		if( ea < bp->base || ea > bp->base+bp->size-1 )
			continue;

		pb->mphys_page = ea - bp->base + bp->mbase;
		pb->pte1 = bp->pp | (bp->wimg << 3) | PTE1_R | PTE1_C;
		pb->key = sv_mode ? bp->ks : bp->ku;
		return 0;
	}

#ifdef EMULATE_603
	if( (MREGS.spr[S_PVR] >> 16) == 3 ) {
		if( lookup_603_pte(kv, segr.vsid, ea, is_dsi(ebits), &mpte) )
			return is_dsi(ebits) ? (is_write(ebits) ? RVEC_DMISS_STORE_TRAP :
					     RVEC_DMISS_LOAD_TRAP) : RVEC_IMISS_TRAP;

		pb->mpte = NULL;	/* imporant */
		pb->mphys_page = (mpte->rpn << 12);
		pb->pte1 = ((ulong*)mpte)[1] & (PTE1_PP | PTE1_WIMG | PTE1_R | PTE1_C);
		pb->key = sv_mode ? segr.ks : segr.kp;
		return 0;
	}
#endif
	/* mac page table lookup */
	if( (mpte=lookup_mac_pte(kv, segr.vsid, ea)) ) {
		pb->mpte = mpte;
		pb->mphys_page = (mpte->rpn << 12);
		pb->pte1 = ((ulong*)mpte)[1] & (PTE1_PP | PTE1_WIMG | PTE1_R | PTE1_C);
		pb->key = sv_mode ? segr.ks : segr.kp;
		return 0;
	}
	/* mac page fault */
	sbits = EBIT_PAGE_FAULT | (ebits & EBIT_IS_WRITE);	/* r/w bit + page_fault */
	RVEC_RETURN_2( &MREGS, is_dsi(ebits) ? RVEC_DSI_TRAP : RVEC_ISI_TRAP, pb->ea, sbits );
}


/* PTE0 must be fully initialized on entry (with V=1 and H=0).
 * The pte_present flag should be set from srr1/dsisr bit and indicates
 * that a valid PTE might already be present in the hash table.
 */
static inline ulong *
find_pte_slot( ulong ea, ulong *pte0, int pte_present, int *pte_replaced )
{
	static int grab_add=0;
	ulong phash, pteg, *p, cmp = *pte0;
	int i;

	/* we are only interested in the page index */
	ea &= 0x0ffff000;

	/* primary hash function */
	phash = (ea >> 12) ^ (PTE0_VSID(cmp) & 0x7ffff);
	pteg = (ulong)hw_hash_base | ((phash & hw_hash_mask) << 6);

	if( pte_present ) {
		*pte_replaced = 1;

		/* look in primary PTEG */
		p=(ulong*)pteg;
		for( i=0; i<8; i++, p+=2 )
			if( cmp == *p )
				return p;
     
		/* look in secondary PTEG */
		p = (ulong*)(pteg ^ (hw_hash_mask << 6));
		cmp |= BIT(25);
		for( i=0; i<8; i++, p+=2 )
			if( cmp == *p ) {
				*pte0 |= PTE0_H;
				return p;
			}
		/* we will actually come here if the previous PTE
		 * was only available in the on-chip cache.
		 */
	}
	*pte_replaced = 0;
	
	/* free slot in primary PTEG? */
	p=(ulong*)pteg;
	for( i=0; i<8; i++, p+=2 )
		if( !(*p & BIT(0)) )
			return p;

	/* free slot in secondary PTEG? */
	p = (ulong*)(pteg ^ (hw_hash_mask << 6));

	for( i=0; i<8; i++,p+=2 )
		if( !(*p & BIT(0)) ) {
			*pte0 |= PTE0_H;
			return p;
		}
	
	/* steal a primary PTEG slot */
	grab_add = (grab_add+1) & 0x7;

	/* printk("Grabbing slot %d, EA %08X\n",grab_add, ea ); */
	return (ulong*)(pteg + grab_add * sizeof(ulong[2]));
}

static inline int 
insert_pte( kernel_vars_t *kv, fault_param_t *pb, const int ebits )
{
	ulong ea=pb->ea, mphys=pb->mphys_page;
	ulong sr=pb->sr_base[ea>>28];
	int status, pte_replaced;
	pte_lvrange_t *lvrange;
	ulong pte0, pte1, *slot;
	char *lvptr;

	pte1 = PTE1_M | PTE1_R | (pb->pte1 & (PTE1_R | PTE1_C | PTE1_WIMG))
		| (is_write(ebits) ? 2:3);

	/* PP and WIMG bits must set before the call to mphys_to_pte */
	status = mphys_to_pte( kv, mphys, &pte1, is_write(ebits), &lvrange );

	if( !status || (is_write(ebits) && (status & MAPPING_RO)) ) {
		ulong addr = (mphys | (ea & 0xfff));
		if( is_dsi(ebits) ) {
			int rvec = is_write(ebits) ? RVEC_IO_WRITE : RVEC_IO_READ;
			BUMP( io_read_write );
			RVEC_RETURN_2( &MREGS, rvec, addr, NULL );
		} else {
			RVEC_RETURN_1( &MREGS, RVEC_BAD_NIP, addr );
		}
	}

	/* tlbhash table hit? */
	if( (ulong)(pb->mphys_page - MMU.hash_mbase) < (ulong)MMU.hash_mask ) {
		/* printk("hash_table_hit at %08lX\n", pb->ea ); */
		MMU.pthash_sr = sr;
		MMU.pthash_ea_base = ea & ~MMU.hash_mask;

		/* user read (always), superuser r/w */
		pte1 &= ~PTE1_PP;
		pte1 |= is_write(ebits) ? 1:3;
		/* write accesses of the page table are handled in ptintercept.S */
	}

	pte0 = PTE0_V | (sr << 7) | ((ea>>22) & PTE0_API);
	slot = find_pte_slot( ea, &pte0, !is_page_fault(ebits), &pte_replaced );

	lvptr = (status & MAPPING_PHYSICAL) ? NULL : (char*)(pte1 & PTE1_RPN);

	/* the RC bits should correspond to the is_write flag; this prevents the
	 * CPU from stamping RC bits unnecessary (besides, the kernel seems to
	 * assume no RC-stamps will ever occur so RC-stamping is unsafe).
	 */
	if( is_write(ebits) )
		pte1 |= PTE1_C;
	pte1 |= SMP_PTE1_M;

	/* if a page-out occurs between prepare_pte_insert() and the pte_inserted()
	 * call, then the PTE slot is zeroed out.
	 */
	if( !(status & MAPPING_PHYSICAL) ) {
#if 0
		if( is_write(ebits) )
			lvpage_dirty( kv, lvptr );
#endif
		pte1 &= ~PTE1_RPN;

		/* we should not have problems with zero pages... */
		pte1 |= get_phys_page( kv, lvptr, is_write(ebits) );
		/* pte1 |= get_phys_page( kv, lvptr, !(status & MAPPING_RO) ); */
	}

	if( status & MAPPING_FB_ACCEL )
		video_pte_inserted( kv, lvptr, slot, pte0, pte1, ea );

	BUMP( page_fault_ctr );
	DEBUG_print_inserted_pte( slot, pte0, pte1, ea );

	__store_PTE( ea, slot, pte0, pte1 );

	pte_inserted( kv, ea, lvptr, lvrange, slot, pb->vsid_eptr[ea>>28], sr );

	/* debugger support */
	if( (kv->break_flags & BREAK_EA_PAGE) && (ea & ~0xfff) == MREGS.mdbg_ea_break )
		RVEC_RETURN_1( &MREGS, RVEC_BREAK, BREAK_EA_PAGE );

	return RVEC_NOP;
}

static int
page_fault( kernel_vars_t *kv, fault_param_t *pb, const int ebits )
{
	int topind = pb->ea >> 28;
	int ind, ret;

	BUMP( access_exception_ctr );

	if( (ret=lookup_mphys(kv, pb, ebits)) ) {
		BUMP(mac_page_fault);
		return ret;
	}

	/* printk("MPHYS_PAGE: %08lX, pp %d, key %d, wimg %d, mpte %p\n", 
		   pb->mphys_page, (pb->pte1 & 3), pb->key, ((pb->pte1 >> 3) & 0xf), pb->mpte ); */

	/* check privileges */
	ind = (is_write(ebits) ? 8:0) | (pb->pte1 & PTE1_PP) | (pb->key?4:0);
	if( priv_viol_table[ind] ) {
		/* r/w bit + priv. violation */
		int sbits = EBIT_PROT_VIOL | (ebits & EBIT_IS_WRITE);	
		BUMP(mac_priv_violation);
		RVEC_RETURN_2( &MREGS, is_dsi(ebits) ? RVEC_DSI_TRAP : RVEC_ISI_TRAP, pb->ea, sbits );
	}

	/* stamp R/C bits (mpte is NULL if this is not a page translation). */
	if( pb->mpte ) {
		pb->mpte->r = 1;
		if( is_write(ebits) )
			pb->mpte->c = 1;

		/* stamp pthash_inuse_bit */
		if( MMU.pthash_inuse_bits ) {
			int nr = ((int)pb->mpte - (int)MMU.hash_base) >> 3;
			set_bit_mol( nr, MMU.pthash_inuse_bits );
		}
	}

	/* perform memory allocations if necessary; we are not allowed to
	 * do this later (the mtable insertion must be atomic)
	 */
	if( mtable_memory_check(kv) )
		return RVEC_NOP;		/* out of memory */

	/* the vsid entry might have been released */
	if( !pb->vsid_eptr[topind] )
		return RVEC_NOP;

	return insert_pte( kv, pb, ebits );
}


/************************************************************************/
/*	VSID allocation (the normal VSID lookup occurs in vsid.S)	*/
/************************************************************************/

static void 
fix_sr( kernel_vars_t *kv, int sr, int mapped )
{
	int macvsid = mapped ? (MREGS.segr[sr] & VSID_MASK) : VSID_MASK + 1 + sr;
	ulong user_sr, sv_sr;
	vsid_ent_t *r = vsid_get_user_sv( kv, macvsid, &user_sr, &sv_sr );

	BUMP(fix_sr);
	if( !r )
		return;

	if( mapped ) {
		int value = MREGS.segr[sr];
		int nbit = value & VSID_N;
		MMU.vsid[sr] = r;
		MMU.user_sr[sr] = ((value & VSID_Kp) ? user_sr : sv_sr) | nbit;
		MMU.sv_sr[sr] = ((value & VSID_Ks) ? user_sr : sv_sr) | nbit;
	} else {
		MMU.unmapped_vsid[sr] = r;
		MMU.unmapped_sr[sr] = user_sr;
	}
	invalidate_splitmode_sr( kv );
}


/************************************************************************/
/*	Exception entrypoints (called from assembly)			*/
/************************************************************************/

extern int dsi_exception( kernel_vars_t *kv, ulong dar, ulong dsisr );
extern int isi_exception( kernel_vars_t *kv, ulong nip, ulong srr1 );

int 
dsi_exception( kernel_vars_t *kv, ulong dar, ulong dsisr )
{
	int ebits, topind = dar >> 28;
	fault_param_t pb;
	
	/* printk("DSI: EA %08lX, DSISR %08lX\n", dar, dsisr ); */
	if( dsisr & 0x84500000 )	/* 0,5,9,11 */
		RVEC_RETURN_2( &MREGS, RVEC_UNUSUAL_DSISR_BITS, dar, dsisr );

	pb.ea = dar;
	ebits = EBIT_IS_DSI | (dsisr & (EBIT_PAGE_FAULT | EBIT_PROT_VIOL | EBIT_IS_WRITE))
		| ((MREGS.msr & MSR_DR) ? EBIT_USE_MMU : 0);

	pb.vsid_eptr = (MREGS.msr & MSR_DR) ? MMU.vsid : MMU.unmapped_vsid;
	pb.sr_base = phys_to_virt(MMU.sr_data);

	/* segment register switch-in required? */
	if( !pb.vsid_eptr[topind] ) {
		fix_sr( kv, topind, use_mmu(ebits) );
		return RVEC_NOP;
	}
	BUMP(dsi);
	return page_fault( kv, &pb, ebits );
}

int 
isi_exception( kernel_vars_t *kv, ulong nip, ulong srr1 )
{
	fault_param_t pb;
	/* printk("ISI: NIP %08lX, SRR1 %08lX\n", nip, srr1 ); */

	pb.vsid_eptr = (MREGS.msr & MSR_IR) ? MMU.vsid : MMU.unmapped_vsid;

	if( srr1 & EBIT_PAGE_FAULT ) {
		int ebits = EBIT_PAGE_FAULT | ((MREGS.msr & MSR_IR) ? EBIT_USE_MMU : 0);
		pb.ea = nip;
		pb.sr_base = phys_to_virt(MMU.sr_inst);
		BUMP(isi_page_fault);
		return page_fault( kv, &pb, ebits );
	}
	if( srr1 & EBIT_NO_EXEC ) {
		int sr = nip >> 28;
		if( !pb.vsid_eptr[sr] ) {
			fix_sr( kv, sr, (MREGS.msr & MSR_IR) );
			return RVEC_NOP;
		}
		/* printk("Guarded memory access at %08lX\n", nip ); */
		RVEC_RETURN_2( &MREGS, RVEC_ISI_TRAP, nip, EBIT_NO_EXEC );
	}

	BUMP(isi_prot_violation);
	/* must be privileges violation */
	RVEC_RETURN_2( &MREGS, RVEC_ISI_TRAP, nip, EBIT_PROT_VIOL );
}


/************************************************************************/
/*	debugger functions						*/
/************************************************************************/

int 
dbg_translate_ea( kernel_vars_t *kv, int context, ulong va, int *ret_mphys, int data_access )
{
	int ebits = data_access ? EBIT_IS_DSI : 0;
	fault_param_t pb;

	memset( &pb, 0, sizeof(pb) );
	pb.ea = va;

	switch( context ) {
	case kContextUnmapped:
		pb.sr_base = MMU.unmapped_sr;
		break;
	case kContextMapped_S:
		pb.sr_base = MMU.sv_sr;
		ebits |= EBIT_USE_MMU;
		break;
	case kContextMapped_U:
		pb.sr_base = MMU.user_sr;
		ebits |= EBIT_USE_MMU;
		break;
	default:
		return 1;
	}

	if( lookup_mphys(kv, &pb, ebits) )
		return 1;
	*ret_mphys = pb.mphys_page | (va & 0xfff);
	return 0;
}
