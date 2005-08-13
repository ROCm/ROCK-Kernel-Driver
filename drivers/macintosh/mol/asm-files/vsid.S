/* 
 *   Creation Date: <2003/03/06 22:03:59 samuel>
 *   Time-stamp: <2003/03/08 18:54:55 samuel>
 *   
 *	<vsid.S>
 *	
 *	VSID lookup (skiplist search)
 *   
 *   Copyright (C) 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include "mtable.h"


emulate_mtsrin:
	rlwinm	r2,r4,32-8,24,28		// r2 = #B << 8
	EMU_LOAD_GPR r2, /**/ R3		// r0 = reg B
	rlwinm	r4,r0,0,0,3			// #sr top 4 bits of r4
	b	1f
emulate_mtsr:
	rlwinm	r4,r4,12,0,3			// #sr in top 4 bits of r4
1:	EMU_LOAD_GPR r5, /**/ R2		// r0 = new segr value

	stw	r8,xGPR8(r1)			// save r8
	rlwinm	r8,r4,6,26,29			// r8 = #sr << 2
	stw	r9,xGPR9(r1)			// save r9
	add	r8,r8,r1			// r8 = r1 + sr_offset
	stw	r6,xNIP(r1)			// save r6 (need more registers)
	rlwinm	r9,r0,0,0,3			// r9 = [T Ks Kp N]
	stw	r0,xSEGR_BASE(r8)		// store new value
	rlwinm	r0,r0,0,8,31			// mask VSID

	/******* vsid lookup (skiplist search)  *******/
	
	lwz	r2,K_VSID_SL_SLEVEL(r1)		// n = search level (0..15)
	rlwinm	r2,r2,2,0,29			// n *= 4
	addi	r3,r1,K_VSID_SL_ROOT_ELEM	// p = root element
1:	add	r5,r2,r3			//
2:	lwz	r3,SKIPLIST_NEXT(r5)		// r3 = p->next[n]
	tophys	r3,r3
	lwz	r4,SKIPLIST_KEY(r3)		// r0 = p->next[n].key
	cmpw	cr1,r4,r0
	bge-	cr1,3f
	b	1b
3:	
	addi	r2,r2,-4			// r2 -= 4
	beq-	cr1,vsid_found			// jump if key matches
	cmpwi	r2,0
	addi	r5,r5,-4			// r3 = r3 - 4
	bge+	2b

	/******* vsid missing *******/
	BUMP("vsid_missing")
	// r8 used
11:	lwz	r2,K_ILLEGAL_SR(r1)		// vsid unallocated...
	li	r4,0				// no entry...
	stw	r2,K_SV_SR_BASE(r8)
	stw	r2,K_USER_SR_BASE(r8)
	stw	r2,K_SPLIT_SR_BASE(r8)		// this way we can forget the splitmode case
	stw	r4,K_VSID_ENT_BASE(r8)
	sub	r5,r8,r1			// r5 = VSID offset
	lwz	r0,K_CUR_SR_BASE(r1)		// r0 = cur_sr_table
	b	6f
	
	/******* vsid found *******/
vsid_found:
	BUMP("vsid_found")
	// match, r3 points to the skiplist element
	addi	r4,r3,-SIZEOF_VSID_ENT		// sizeof(vsid_ent_t)
	lwz	r2,VSID_USER_OFFS(r4)		// vsid_user
	lwz	r3,VSID_SV_OFFS(r4)		// vsid_sv
	tovirt	r4,r4				// r4 = vsid_element

	rlwinm	r6,r9,2,31,31			// r6 = Ks ? 1:0
	stw	r4,K_VSID_ENT_BASE(r8)		// store vsid entry (possibly NULL)
	rlwinm	r4,r9,3,31,31			// r4 = Kp ? 1:0
	rlwimi	r2,r9,0,3,3			// copy the N-bit (no-execute)
	rlwimi	r3,r9,0,3,3			// copy the N-bit (no-execute)
	cror	FBIT_LoadSegreg, FBIT_LoadSegreg, FBIT_InSplitmode
	addi	r6,r6,-1			// r6 = Ks-mask
	addi	r4,r4,-1			// r4 = Kp-mask
	andc	r0,r2,r6			// Kp == 1 case
	and	r6,r3,r6			// Kp == 0 case
	crmove	FBIT_PrepareSplitmode, FBIT_InSplitmode
	andc	r9,r2,r4			// Ks == 1 case
	or	r6,r6,r0			
	lwz	r0,K_CUR_SR_BASE(r1)		// r0 = cur_sr_table
	and	r4,r3,r4			// Ks == 0 case
	stw	r6,K_SV_SR_BASE(r8)		// sv_sr set
	or	r4,r4,r9
	sub	r5,r8,r1			// r5 = VSID offset
	stw	r4,K_USER_SR_BASE(r8)		// user_sr set
6:
	// r0,r5 used
	rlwinm	r2,r5,32-6,0,3			// r2 = sr# in bits 0-3
	lwzx	r3,r5,r0			// r3 = sr to be loaded into sr#
	mtsrin	r3,r2				// update segment register

	lwz	r6,xNIP(r1)			// restore r6
	lwz	r8,xGPR8(r1)			// restore r8
	lwz	r9,xGPR9(r1)			// restore r9
	GET_TICK_CNT(entry, "mtsr")
	BUMP("mtsr_")
	b	emulation_done
	
