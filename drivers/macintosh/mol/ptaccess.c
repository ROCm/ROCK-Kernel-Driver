/* 
 *   Creation Date: <2001/03/25 18:04:45 samuel>
 *   Time-stamp: <2002/08/03 17:43:10 samuel>
 *   
 *	<ptaccess.c>
 *	
 *	Handle stores to the (emulated) page table
 *   
 *   Copyright (C) 2001, 2002 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include "archinclude.h"
#include "mmu.h"
#include "rvec.h"
#include "mtable.h"
#include "misc.h"
#include "performance.h"

extern int do_intercept_tlbie( kernel_vars_t *kv, ulong pte0, ulong pte1, ulong pteoffs );
extern int do_intercept_tlbie_block( kernel_vars_t *kv, ulong pteoffs, ulong length );

#define	MMU	(kv->mmu)
#define	MREGS	(kv->mregs)

int 
do_intercept_tlbie( kernel_vars_t *kv, ulong pte0, ulong pte1, ulong pteoffs )
{
	int vsid = (pte0 >> 7) & VSID_MASK;
	ulong v;

	BUMP( do_intercept_tlbie );

	if( MMU.pthash_inuse_bits )
		clear_bit_mol( pteoffs >> 3, MMU.pthash_inuse_bits );
	
	v = (pteoffs >> 6);
	if( pte0 & BIT(25) )	/* secondary hash? */
		v = ~v;
	v ^= (pte0 >> 7);
	v = ((pte0 << 10) & 0xfc00) | (v & 0x3ff);

	//printk("do_intercept_tlbie: vsid %08lX, ea %08lX\n", vsid, (v<<12) );
	flush_vsid_ea( kv, vsid, (v<<12) );

	return RVEC_NOP;
}

int
do_intercept_tlbie_block( kernel_vars_t *kv, ulong pteoffs, ulong length )
{
	unsigned int finish;

	//printk("do_intercept_tlbie_block: pteoffs %08lX length %08lX\n", pteoffs, length);

	if (pteoffs + length > MMU.hash_mask) {
		printk("do_intercept_tlbie_block: length exceeding hash!\n");
		finish = MMU.hash_mask + 1;
	} else
		finish = pteoffs + length;

	if (MMU.pthash_inuse_bits == NULL)
		return RVEC_NOP;

	while (pteoffs < finish) {
		if (check_bit_mol(pteoffs >> 3, MMU.pthash_inuse_bits)) {
			ulong pte0, pte1;

			pte0 = *((unsigned int *) (MMU.hash_base + pteoffs));
			pte1 = *((unsigned int *) (MMU.hash_base + pteoffs + 4));
			do_intercept_tlbie(kv, pte0, pte1, pteoffs);
		} 

		pteoffs += 8;
	}

	return RVEC_NOP;
}

#ifdef EMULATE_603

extern int do_tlbli( kernel_vars_t *kv, ulong ea );
extern int do_tlbld( kernel_vars_t *kv, ulong ea );

int
do_tlbli( kernel_vars_t *kv, ulong ea )
{
	int ind = (ea >> 12) & 0x1f;
	mPTE_t *p;
	
	//printk("do_tlbli %08lX : %08lX %08lX\n", ea, MREGS.spr[S_ICMP], MREGS.spr[S_RPA] );
	if( MREGS.spr[S_SRR1] & BIT(14) )
		ind += 32;

	p = &MMU.ptes_i_603[ind];
	if( p->v )
		flush_vsid_ea( kv, p->vsid, MMU.ptes_i_ea_603[ind] );
	MMU.ptes_i_ea_603[ind] = ea & 0x0ffff000;
	*(ulong*)p = MREGS.spr[ S_ICMP ];
	*((ulong*)p+1) = MREGS.spr[ S_RPA ];

	return RVEC_NOP;
}

int
do_tlbld( kernel_vars_t *kv, ulong ea )
{
	int ind = (ea >> 12) & 0x1f;
	mPTE_t *p;

	//printk("do_tlbld %08lX\n", ea );

	if( MREGS.spr[S_SRR1] & BIT(14) )
		ind += 32;

	p = &MMU.ptes_d_603[ind];
	if( p->v )
		flush_vsid_ea( kv, p->vsid, MMU.ptes_d_ea_603[ind] );
	MMU.ptes_d_ea_603[ind] = ea & 0x0ffff000;
	*(ulong*)p = MREGS.spr[ S_DCMP ];
	*((ulong*)p+1) = MREGS.spr[ S_RPA ];

	return RVEC_NOP;
}

int
do_tlbie( kernel_vars_t *kv, ulong ea )
{
	int ind = (ea >> 12) & 0x1f;
	mPTE_t *pi, *pd;
	ulong *iea, *dea;

	pi = &MMU.ptes_i_603[ind];
	pd = &MMU.ptes_d_603[ind];
	iea = &MMU.ptes_i_ea_603[ind];
	dea = &MMU.ptes_d_ea_603[ind];
	for( ; ind < 64; ind +=32, pd += 32, pi += 32, iea += 32, dea +=32 ) {
		if( pi->v )
			flush_vsid_ea( kv, pi->vsid, *iea );
		if( pd->v )
			flush_vsid_ea( kv, pd->vsid, *dea );
		*(ulong*)pi = 0;
		*(ulong*)pd = 0;
	}
	return RVEC_NOP;
}

#endif	/* EMULATE_603 */

