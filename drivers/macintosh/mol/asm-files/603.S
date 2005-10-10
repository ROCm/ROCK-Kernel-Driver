/* 
 *   Creation Date: <2001/06/15 20:10:49 samuel>
 *   Time-stamp: <2001/06/16 15:35:22 samuel>
 *   
 *	<603.S>
 *	
 *	603 MMU support
 *   
 *   Copyright (C) 2001 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#define P603_USE_G_BIT
#define P603_USE_R_BIT
#define P603_USE_C_BIT

/* NOTE: The 603 vectors are called with ctr saved in r0.
 * Secondary interrupts are not detected automatically.
 */
	
/************************************************************************/
/*	Instruction TLB Miss (603 specific vector)			*/
/************************************************************************/
	
VECTOR_603( 0x1000, "Instruction TLB Miss - 603" )
//	mfctr   r0				// Need to save this - CTR can't be touched!
	mfspr   r2,HASH1			// Get PTE pointer
	mfspr   r3,ICMP				// Partial item compare value
00:     li      r1,8				// 8 items / bucket
	mtctr   r1
	subi    r2,r2,8				// Preset pointer
10:     lwzu    r1,8(r2)			// Get next PTE
	cmp     0,r1,r3				// Found entry yet?
	bdnzf   2,10b				// Jump back if not, until CTR==0
	bne     30f				// Try secondary hash if CTR==0
	lwz     r1,4(r2)			// Get second word of entry
#ifdef P603_USE_G_BIT
	andi.	r3,r1,8				// check G-bit
	bne	DoISI_603_G			// if guarded, take an ISI
#endif
	mtctr   r0				// Restore CTR
	mfspr   r3,SRR1				// Need to restore CR0
	mtcrf   0x80,r3
	mfspr   r0,IMISS			// Set to update TLB
	mtspr   RPA,r1
#ifdef P603_USE_R_BIT
	ori	r1,r1,0x100			// Set reference bit
	srwi	r1,r1,8				// Get byte 7 of pte
	tlbli   r0				// Load the ITLB
	stb	r1,6(r2)			// update page table
#else
	tlbli   r0				// Load the ITLB
#endif
	rfi					// All done

	// Secondary hash
30:     andi.   r1,r3,0x40			// Already doing secondary hash?
	bne     DoISI_603			// Yes - item not in hash table
	mfspr   r2,HASH2			// Get hash table pointer
	ori     r3,r3,0x40			// Set secondary hash
	b       00b				// Try lookup again

	
/************************************************************************/
/*	Data Store TLB Miss (603 specific vector)			*/
/************************************************************************/
	
VECTOR_603( 0x1200, "Data Store TLB Miss - 603" )
//	mfctr	r0				// Need to save this - CTR can't be touched!
	mfspr	r2,HASH1			// Get PTE pointer
	mfspr	r3,DCMP				// Partial item compare value
00:	li	r1,8				// 8 items / bucket
	mtctr	r1
	subi	r2,r2,8				// Preset pointer
10:	lwzu	r1,8(r2)			// Get next PTE
	cmp	0,r1,r3				// Found entry yet?
	bdnzf	2,10b				// Jump back if not, until CTR==0
	bne	30f				// Try secondary hash if CTR==0
	lwz	r1,4(r2)			// Get second word of entry
#ifdef P603_USE_C_BIT
	andi.	r3,r1,0x80			// Check the C-bit
	beq	CheckProt_603
16:
#endif	
20:	mtctr	r0				// Restore CTR
	mfspr	r3,SRR1				// Need to restore CR0
	mtcrf	0x80,r3
	mfspr	r0,DMISS			// Set to update TLB
	mtspr	RPA,r1
	tlbld	r0
	rfi					// All done  

	// Secondary hash
30:	andi.	r1,r3,0x40			// Already doing secondary hash?
	bne	DoDSI_603			// Yes - item not in hash table
	mfspr	r2,HASH2			// Get hash table pointer
	ori	r3,r3,0x40      		// Set secondary hash
	b	00b				// Try lookup again

#ifdef P603_USE_C_BIT
	// Entry found and PTE[c] == 0. Check protection before setting PTE[c]
	// r0 = saved ctr, r1 = second word of PTE, r2 = pointer to pteg, r3 = trash
CheckProt_603:
	rlwinm.	r3,r1,30,0,1			// test PP
	bge-	50f				// if (PP == 00 or PP == 01) goto 50
	andi.	r3,r1,1				// test PP[0]
	beq+	60f				// return if PP[0] == 0
	b	DoDSI_603_P			// else DSI_P, (PP==11, read-only)

50:	mfspr	r3,SRR1				// get old msr
	andis.	r3,r3,0x0008			// test the KEY bit (SRR1-bit 12)
	beq	60f				// if KEY==0, goto 60
	b	DoDSI_603_P			// else DSI_P

60:	ori	r1,r1,0x180			// set reference and change bit
	sth	r1,6(r2)			// update page table
	b	16b				// and back we go
#endif

	
/************************************************************************/
/*	Data Load TLB Miss (603 specific vector)			*/
/************************************************************************/
		
VECTOR_603( 0x1100, "Data Load TLB Miss - 603" )
//	mfctr	r0				// Need to save this - CTR can't be touched!
	mfspr	r2,HASH1			// Get PTE pointer
	mfspr	r3,DCMP				// Partial item compare value
00:	li	r1,8				// 8 items / bucket
	mtctr	r1
	subi	r2,r2,8				// Preset pointer
10:	lwzu	r1,8(r2)			// Get next PTE
	cmp	0,r1,r3				// Found entry yet?
	bdnzf	2,10b				// Jump back if not, until CTR==0
	bne	30f				// Try secondary hash if CTR==0
	lwz	r1,4(r2)			// Get second word of entry
20:     mtctr	r0				// Restore CTR
	mfspr	r3,SRR1				// Need to restore CR0
	mtcrf	0x80,r3
	mfspr	r0,DMISS			// Set to update TLB
	mtspr	RPA,r1
#ifdef P603_USE_R_BIT
	ori	r1,r1,0x100			// set reference bit
	srwi	r1,r1,8
	tlbld	r0
	stb	r1,6(r2)
#else
	tlbld	r0
#endif
	rfi					// All done
	
// Secondary hash
30:	andi.	r1,r3,0x40			// Already doing secondary hash?
	bne	DoDSI_603			// Yes - item not in hash table
	mfspr	r2,HASH2			// Get hash table pointer
	ori	r3,r3,0x40			// Set secondary hash
	b	00b				// Try lookup again


/************************************************************************/
/*	Synthesize an ISI Exception					*/
/************************************************************************/

DoISI_603:
	mfspr   r3,SRR1
	andi.   r2,r3,0xFFFF			// Clear upper bits of SRR1
	addis   r2,r2,0x4000			// Set bit 1 -> PTE not found (in HTAB)
	mtctr   r0				// Restore CTR
40:	mtspr   SRR1,r2
	mfmsr   r0				// Restore "normal" registers
	xoris   r0,r0,MSR_TGPR>>16
	mtcrf   0x80,r3				// Restore CR0
	sync					// Some chip revs have problems here...
	mtmsr   r0
	SOFT_VECTOR_ENTRY_603( 0x400 )		// Jump to the ISI vector


#ifdef P603_USE_G_BIT
DoISI_603_G:
	mfspr	r3,SRR1
	andi.	r2,r3,0xFFFF			// Clear upper bits of SRR1
//	addis	r2,r2,0x0800			// Page protection violation
	addis	r2,r2,0x1000			// Guarded memory access
	b	40b
#endif

	
/************************************************************************/
/*	Synthesize a DSI exception					*/
/************************************************************************/
	
DoDSI_603:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6     		// Get load/store bit
	addis	r1,r1,0x4000			// Set bit 1 -> PTE not found

10:	mtspr	DSISR,r1
	mtctr	r0				// Restore CTR
	andi.	r2,r3,0xFFFF			// Clear upper bits of SRR1
	mtspr	SRR1,r2
	mfspr	r1,DMISS			// Get failing address
	mtspr	DAR,r1				// Set fault address
	mfmsr	r0				// Restore "normal" registers
	xoris   r0,r0,MSR_TGPR>>16
	mtcrf   0x80,r3				// Restore CR0
	sync					// Some chip revs have problems here...
	mtmsr   r0
	SOFT_VECTOR_ENTRY_603( 0x300 )		// Jump to the DSI vector

DoDSI_603_P:
	mfspr	r3,SRR1
	rlwinm	r1,r3,9,6,6			// get load/store bit
	addis	r1,r1,0x800			// Set bit 4 (prot. violation)
	b	10b
