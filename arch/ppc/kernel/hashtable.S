/*
 *  arch/ppc/kernel/hashtable.S
 *
 *  $Id: hashtable.S,v 1.6 1999/10/08 01:56:15 paulus Exp $
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *
 *  This file contains low-level assembler routines for managing
 *  the PowerPC MMU hash table.  (PPC 8xx processors don't use a
 *  hash table, so this file is not used on them.)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *	
 */

#include "ppc_asm.h"
#include <asm/processor.h>
#include <asm/page.h>
#include <linux/config.h>
#include "mol.h"

/*
 * Load a PTE into the hash table, if possible.
 * The address is in r3, and r4 contains access flags:
 * _PAGE_USER (4) if a user-mode access, ored with
 * _PAGE_RW (2) if a write.  r20 contains DSISR or SRR1,
 * so bit 1 (0x40000000) is set if the exception was due
 * to no matching PTE being found in the hash table.
 * SPRG3 contains the physical address of the current task's thread.
 *
 * Returns to the caller if the access is illegal or there is no
 * mapping for the address.  Otherwise it places an appropriate PTE
 * in the hash table and returns from the exception.
 * Uses r0, r2 - r6, ctr, lr.
 *
 * For speed, 4 of the instructions get patched once the size and
 * physical address of the hash table are known.  These definitions
 * of Hash_base and Hash_bits below are just an example.
 */
Hash_base = 0x180000
Hash_bits = 12				/* e.g. 256kB hash table */
Hash_msk = (((1 << Hash_bits) - 1) * 64)
	
	.globl	hash_page
hash_page:
#ifdef CONFIG_PPC64BRIDGE
	mfmsr	r0
	clrldi	r0,r0,1		/* make sure it's in 32-bit mode */
	sync
	MTMSRD(r0)
	isync
#endif
#ifdef CONFIG_SMP
	SAVE_2GPRS(7,r21)
	eieio
	lis	r2,hash_table_lock@h
	ori	r2,r2,hash_table_lock@l
 	tophys(r2,r2)
	lis	r6,0x0fff0000@h
	mtctr	r6
	mfspr	r5,SPRG3
	lwz	r0,PROCESSOR-THREAD(r5)
	or	r0,r0,r6
10:	lwarx	r6,0,r2
	cmpi	0,r6,0
	bne-	12f
	stwcx.	r0,0,r2
	beq+	11f
	/* spin here a bit */
12:	mfctr	r7
	li	r8,1000
	mtctr	r8
13:
	bdnz	13b
	mtctr	r7
	cmpw	r6,r0
	bdnzf	2,10b
	tw	31,31,31
11:	eieio
	REST_2GPRS(7, r21)
#endif
	/* Get PTE (linux-style) and check access */
	lis	r0,KERNELBASE@h		/* check if kernel address */
	cmplw	0,r3,r0
	mfspr	r2,SPRG3		/* current task's THREAD (phys) */
	lwz	r5,PGDIR(r2)		/* virt page-table root */
	blt+	112f			/* assume user more likely */
	lis	r5,swapper_pg_dir@ha	/* if kernel address, use */
	addi	r5,r5,swapper_pg_dir@l	/* kernel page table */
112:	tophys(r5,r5)			/* convert to phys addr */
	rlwimi	r5,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r5,0(r5)		/* get pmd entry */
	rlwinm.	r5,r5,0,0,19		/* extract address of pte page */
#ifdef CONFIG_SMP
	beq-	hash_page_out		/* return if no mapping */
#else
	/* XXX it seems like the 601 will give a machine fault on the
	   rfi if its alignment is wrong (bottom 4 bits of address are
	   8 or 0xc) and we have had a not-taken conditional branch
	   to the address following the rfi. */
	beqlr-
#endif
	tophys(r2,r5)
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r6,0(r2)		/* get linux-style pte */
	ori	r4,r4,1			/* set _PAGE_PRESENT bit in access */
	andc.	r0,r4,r6		/* check access & ~permission */
#ifdef CONFIG_SMP
	bne-	hash_page_out		/* return if access not permitted */
#else
	bnelr-
#endif

	ori	r6,r6,0x100		/* set _PAGE_ACCESSED in pte */
	rlwinm	r5,r4,5,24,24		/* _PAGE_RW access -> _PAGE_DIRTY */
	rlwimi	r5,r4,7,22,22		/* _PAGE_RW -> _PAGE_HWWRITE */
	or	r6,r6,r5
	stw	r6,0(r2)		/* update PTE (accessed/dirty bits) */

	/* Convert linux-style PTE to low word of PPC-style PTE */
	rlwinm	r4,r6,32-9,31,31	/* _PAGE_HWWRITE -> PP lsb */
	rlwimi	r6,r6,32-1,31,31	/* _PAGE_USER -> PP (both bits now) */
	ori	r4,r4,0xe04		/* clear out reserved bits */
	andc	r6,r6,r4		/* PP=2 or 0, when _PAGE_HWWRITE */

#ifdef CONFIG_POWER4
	/*
	 * XXX hack hack hack - translate 32-bit "physical" addresses
	 * in the linux page tables to 42-bit real addresses in such
	 * a fashion that we can get at the I/O we need to access.
	 *	-- paulus
	 */
	cmpwi	0,r6,0
	rlwinm	r4,r6,16,16,30
	bge	57f
	cmplwi	0,r4,0xfe00
	li	r5,0x3fd
	bne	56f
	li	r5,0x3ff
56:	sldi	r5,r5,32
	or	r6,r6,r5
57:
#endif

#ifdef CONFIG_PPC64BRIDGE
	/* Construct the high word of the PPC-style PTE */
	mfsrin	r5,r3			/* get segment reg for segment */
	rlwinm	r5,r5,0,5,31
	sldi	r5,r5,12
	ori	r5,r5,1			/* set V (valid) bit */
	rlwimi	r5,r3,16,20,24		/* put in API (abbrev page index) */
	/* Get the address of the primary PTE group in the hash table */
	.globl	hash_page_patch_A
hash_page_patch_A:
	lis	r4,Hash_base@h		/* base address of hash table */
	rlwimi	r4,r5,32-5,25-Hash_bits,24	/* (VSID & hash_mask) << 7 */
	rlwinm	r0,r3,32-5,25-Hash_bits,24	/* (PI & hash_mask) << 7 */
	xor	r4,r4,r0		/* make primary hash */

	/* See whether it was a PTE not found exception or a
	   protection violation. */
	andis.	r0,r20,0x4000
	li	r2,8			/* PTEs/group */
	bne	10f			/* no PTE: go look for an empty slot */
	tlbie	r3			/* invalidate TLB entry */

	/* Search the primary PTEG for a PTE whose 1st dword matches r5 */
	mtctr	r2
	addi	r3,r4,-16
1:	ldu	r0,16(r3)		/* get next PTE */
	cmpd	0,r0,r5
	bdnzf	2,1b			/* loop while ctr != 0 && !cr0.eq */
	beq+	found_slot

	/* Search the secondary PTEG for a matching PTE */
	ori	r5,r5,0x2		/* set H (secondary hash) bit */
	.globl	hash_page_patch_B
hash_page_patch_B:
	xoris	r3,r4,Hash_msk>>16	/* compute secondary hash */
	xori	r3,r3,0xff80
	addi	r3,r3,-16
	mtctr	r2
2:	ldu	r0,16(r3)
	cmpd	0,r0,r5
	bdnzf	2,2b
	beq+	found_slot
	xori	r5,r5,0x2		/* clear H bit again */

	/* Search the primary PTEG for an empty slot */
10:	mtctr	r2
	addi	r3,r4,-16		/* search primary PTEG */
1:	ldu	r0,16(r3)		/* get next PTE */
	andi.	r0,r0,1
	bdnzf	2,1b			/* loop while ctr != 0 && !cr0.eq */
	beq+	found_empty

	/* Search the secondary PTEG for an empty slot */
	ori	r5,r5,0x2		/* set H (secondary hash) bit */
	.globl	hash_page_patch_C
hash_page_patch_C:
	xoris	r3,r4,Hash_msk>>16	/* compute secondary hash */
	xori	r3,r3,0xff80
	addi	r3,r3,-16
	mtctr	r2
2:	ldu	r0,16(r3)
	andi.	r0,r0,1
	bdnzf	2,2b
	beq+	found_empty

	/*
	 * Choose an arbitrary slot in the primary PTEG to overwrite.
	 * Since both the primary and secondary PTEGs are full, and we
	 * have no information that the PTEs in the primary PTEG are
	 * more important or useful than those in the secondary PTEG,
	 * and we know there is a definite (although small) speed
	 * advantage to putting the PTE in the primary PTEG, we always
	 * put the PTE in the primary PTEG.
	 */
	xori	r5,r5,0x2		/* clear H bit again */
	lis	r3,next_slot@ha
	tophys(r3,r3)
	lwz	r2,next_slot@l(r3)
	addi	r2,r2,16
	andi.	r2,r2,0x70
#ifdef CONFIG_POWER4
	/*
	 * Since we don't have BATs on POWER4, we rely on always having
	 * PTEs in the hash table to map the hash table and the code
	 * that manipulates it in virtual mode, namely flush_hash_page and
	 * flush_hash_segments.  Otherwise we can get a DSI inside those
	 * routines which leads to a deadlock on the hash_table_lock on
	 * SMP machines.  We avoid this by never overwriting the first
	 * PTE of each PTEG if it is already valid.
	 *	-- paulus.
	 */
	bne	102f
	li	r2,0x10
102:
#endif /* CONFIG_POWER4 */
	stw	r2,next_slot@l(r3)
	add	r3,r4,r2
11:
	/* update counter of evicted pages */
	lis	r2,htab_evicts@ha
	tophys(r2,r2)
	lwz	r4,htab_evicts@l(r2)
	addi	r4,r4,1
	stw	r4,htab_evicts@l(r2)

#ifndef CONFIG_SMP
	/* Store PTE in PTEG */
found_empty:
	std	r5,0(r3)
found_slot:
	std	r6,8(r3)
	sync

#else /* CONFIG_SMP */
/*
 * Between the tlbie above and updating the hash table entry below,
 * another CPU could read the hash table entry and put it in its TLB.
 * There are 3 cases:
 * 1. using an empty slot
 * 2. updating an earlier entry to change permissions (i.e. enable write)
 * 3. taking over the PTE for an unrelated address
 *
 * In each case it doesn't really matter if the other CPUs have the old
 * PTE in their TLB.  So we don't need to bother with another tlbie here,
 * which is convenient as we've overwritten the register that had the
 * address. :-)  The tlbie above is mainly to make sure that this CPU comes
 * and gets the new PTE from the hash table.
 *
 * We do however have to make sure that the PTE is never in an invalid
 * state with the V bit set.
 */
found_empty:
found_slot:
	li	r0,1
	andc	r5,r5,r0	/* clear V (valid) bit in PTE */
	std	r5,0(r3)
	sync
	tlbsync
	sync
	std	r6,8(r3)	/* put in correct RPN, WIMG, PP bits */
	sync
	ori	r5,r5,1
	std	r5,0(r3)	/* finally set V bit in PTE */
#endif /* CONFIG_SMP */

#else /* CONFIG_PPC64BRIDGE */

	/* Construct the high word of the PPC-style PTE */
	mfsrin	r5,r3			/* get segment reg for segment */
	rlwinm	r5,r5,7,1,24		/* put VSID in 0x7fffff80 bits */
	oris	r5,r5,0x8000		/* set V (valid) bit */
	rlwimi	r5,r3,10,26,31		/* put in API (abbrev page index) */
	/* Get the address of the primary PTE group in the hash table */
	.globl	hash_page_patch_A
hash_page_patch_A:
	lis	r4,Hash_base@h		/* base address of hash table */
	rlwimi	r4,r5,32-1,26-Hash_bits,25	/* (VSID & hash_mask) << 6 */
	rlwinm	r0,r3,32-6,26-Hash_bits,25	/* (PI & hash_mask) << 6 */
	xor	r4,r4,r0		/* make primary hash */

	/* See whether it was a PTE not found exception or a
	   protection violation. */
	andis.	r0,r20,0x4000
	li	r2,8			/* PTEs/group */
	bne	10f			/* no PTE: go look for an empty slot */
	tlbie	r3			/* invalidate TLB entry */

	/* Search the primary PTEG for a PTE whose 1st word matches r5 */
	mtctr	r2
	addi	r3,r4,-8
1:	lwzu	r0,8(r3)		/* get next PTE */
	cmp	0,r0,r5
	bdnzf	2,1b			/* loop while ctr != 0 && !cr0.eq */
	beq+	found_slot

	/* Search the secondary PTEG for a matching PTE */
	ori	r5,r5,0x40		/* set H (secondary hash) bit */
	.globl	hash_page_patch_B
hash_page_patch_B:
	xoris	r3,r4,Hash_msk>>16	/* compute secondary hash */
	xori	r3,r3,0xffc0
	addi	r3,r3,-8
	mtctr	r2
2:	lwzu	r0,8(r3)
	cmp	0,r0,r5
	bdnzf	2,2b
	beq+	found_slot
	xori	r5,r5,0x40		/* clear H bit again */

	/* Search the primary PTEG for an empty slot */
10:	mtctr	r2
	addi	r3,r4,-8		/* search primary PTEG */
1:	lwzu	r0,8(r3)		/* get next PTE */
	rlwinm.	r0,r0,0,0,0		/* only want to check valid bit */
	bdnzf	2,1b			/* loop while ctr != 0 && !cr0.eq */
	beq+	found_empty

	/* Search the secondary PTEG for an empty slot */
	ori	r5,r5,0x40		/* set H (secondary hash) bit */
	.globl	hash_page_patch_C
hash_page_patch_C:
	xoris	r3,r4,Hash_msk>>16	/* compute secondary hash */
	xori	r3,r3,0xffc0
	addi	r3,r3,-8
	mtctr	r2
2:
	lwzu	r0,8(r3)
	rlwinm.	r0,r0,0,0,0		/* only want to check valid bit */
	bdnzf	2,2b
	beq+	found_empty

	/*
	 * Choose an arbitrary slot in the primary PTEG to overwrite.
	 * Since both the primary and secondary PTEGs are full, and we
	 * have no information that the PTEs in the primary PTEG are
	 * more important or useful than those in the secondary PTEG,
	 * and we know there is a definite (although small) speed
	 * advantage to putting the PTE in the primary PTEG, we always
	 * put the PTE in the primary PTEG.
	 */
	xori	r5,r5,0x40		/* clear H bit again */
	lis	r3,next_slot@ha
	tophys(r3,r3)
	lwz	r2,next_slot@l(r3)
	addi	r2,r2,8
	andi.	r2,r2,0x38
	stw	r2,next_slot@l(r3)
	add	r3,r4,r2
11:		
	/* update counter of evicted pages */
	lis	r2,htab_evicts@ha
	tophys(r2,r2)
	lwz	r4,htab_evicts@l(r2)
	addi	r4,r4,1
	stw	r4,htab_evicts@l(r2)

#ifndef CONFIG_SMP
	/* Store PTE in PTEG */
found_empty:
	stw	r5,0(r3)
found_slot:
	stw	r6,4(r3)
	sync

#else /* CONFIG_SMP */
/*
 * Between the tlbie above and updating the hash table entry below,
 * another CPU could read the hash table entry and put it in its TLB.
 * There are 3 cases:
 * 1. using an empty slot
 * 2. updating an earlier entry to change permissions (i.e. enable write)
 * 3. taking over the PTE for an unrelated address
 *
 * In each case it doesn't really matter if the other CPUs have the old
 * PTE in their TLB.  So we don't need to bother with another tlbie here,
 * which is convenient as we've overwritten the register that had the
 * address. :-)  The tlbie above is mainly to make sure that this CPU comes
 * and gets the new PTE from the hash table.
 *
 * We do however have to make sure that the PTE is never in an invalid
 * state with the V bit set.
 */
found_empty:
found_slot:
	rlwinm	r5,r5,0,1,31	/* clear V (valid) bit in PTE */
	stw	r5,0(r3)
	sync
	tlbsync
	sync
	stw	r6,4(r3)	/* put in correct RPN, WIMG, PP bits */
	sync
	oris	r5,r5,0x8000
	stw	r5,0(r3)	/* finally set V bit in PTE */
#endif /* CONFIG_SMP */
#endif /* CONFIG_PPC64BRIDGE */

/*
 * Update the hash table miss count.  We only want misses here
 * that _are_ valid addresses and have a pte otherwise we don't
 * count it as a reload.  do_page_fault() takes care of bad addrs
 * and entries that need linux-style pte's created.
 *
 * safe to use r2 here since we're not using it as current yet 
 * update the htab misses count
 *   -- Cort
 */
	lis	r2,htab_reloads@ha
	tophys(r2,r2)
	lwz	r3,htab_reloads@l(r2)
	addi	r3,r3,1
	stw	r3,htab_reloads@l(r2)

#ifdef CONFIG_SMP
	lis	r2,hash_table_lock@ha
	tophys(r2,r2)
	li	r0,0
	stw	r0,hash_table_lock@l(r2)
	eieio
#endif

	/* Return from the exception */
	lwz	r3,_CCR(r21)
	lwz	r4,_LINK(r21)
	lwz	r5,_CTR(r21)
	mtcrf	0xff,r3
	mtlr	r4
	mtctr	r5
	lwz	r0,GPR0(r21)
	lwz	r1,GPR1(r21)
	lwz	r2,GPR2(r21)
	lwz	r3,GPR3(r21)
	lwz	r4,GPR4(r21)
	lwz	r5,GPR5(r21)
	lwz	r6,GPR6(r21)
	/* we haven't used xer */
	mtspr	SRR1,r23
	mtspr	SRR0,r22
	lwz	r20,GPR20(r21)
	lwz	r22,GPR22(r21)
	lwz	r23,GPR23(r21)
	lwz	r21,GPR21(r21)
	RFI
	
#ifdef CONFIG_SMP
hash_page_out:
	lis	r2,hash_table_lock@ha
	tophys(r2,r2)
	li	r0,0
	stw	r0,hash_table_lock@l(r2)
	eieio
	blr

	.data
	.globl	hash_table_lock
hash_table_lock:
	.long	0
#endif /* CONFIG_SMP */

	.data
next_slot:
	.long	0

	.text
/*
 * Flush entries from the hash table with VSIDs in the range
 * given.
 */
_GLOBAL(flush_hash_segments)
	lis	r5,Hash@ha
	lwz	r5,Hash@l(r5)		/* base of hash table */
	cmpwi	0,r5,0
	bne+	99f
	tlbia
	sync
#ifdef CONFIG_SMP
	tlbsync
	sync
#endif
	blr
99:
#if defined(CONFIG_SMP) || defined(CONFIG_PPC64BRIDGE)
	/* Note - we had better not do anything which could generate
	   a hash table miss while we have the hash table locked,
	   or we'll get a deadlock.  -paulus */
	mfmsr	r10
	sync
	rlwinm	r0,r10,0,17,15	/* clear bit 16 (MSR_EE) */
	mtmsr	r0
	SYNC
#endif
#ifdef CONFIG_SMP
	lis	r9,hash_table_lock@h
	ori	r9,r9,hash_table_lock@l
	lwz	r8,PROCESSOR(r2)
	oris	r8,r8,8
10:	lwarx	r6,0,r9
	cmpi	0,r6,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	eieio
#endif
#ifndef CONFIG_PPC64BRIDGE
	rlwinm	r3,r3,7,1,24		/* put VSID lower limit in position */
	oris	r3,r3,0x8000		/* set V bit */
	rlwinm	r4,r4,7,1,24		/* put VSID upper limit in position */
	oris	r4,r4,0x8000
	ori	r4,r4,0x7f
	lis	r6,Hash_size@ha
	lwz	r6,Hash_size@l(r6)	/* size in bytes */
	srwi	r6,r6,3			/* # PTEs */
	mtctr	r6
	addi	r5,r5,-8
	li	r0,0
1:	lwzu	r6,8(r5)		/* get next tag word */
	cmplw	0,r6,r3
	cmplw	1,r6,r4
	cror	0,0,5			/* set cr0.lt if out of range */
	blt	2f			/* branch if out of range */
	stw	r0,0(r5)		/* invalidate entry */
2:	bdnz	1b			/* continue with loop */
#else /* CONFIG_PPC64BRIDGE */
	rldic	r3,r3,12,20		/* put VSID lower limit in position */
	ori	r3,r3,1			/* set V bit */
	rldic	r4,r4,12,20		/* put VSID upper limit in position */
	ori	r4,r4,0xfff		/* set V bit, API etc. */
	lis	r6,Hash_size@ha
	lwz	r6,Hash_size@l(r6)	/* size in bytes */
	srwi	r6,r6,4			/* # PTEs */
	mtctr	r6
	addi	r5,r5,-16
	li	r0,0
1:	ldu	r6,16(r5)		/* get next tag word */
	cmpld	0,r6,r3
	cmpld	1,r6,r4
	cror	0,0,5			/* set cr0.lt if out of range */
	blt	2f			/* branch if out of range */
	std	r0,0(r5)		/* invalidate entry */
2:	bdnz	1b			/* continue with loop */
#endif /* CONFIG_PPC64BRIDGE */

	sync
	tlbia
	sync
#ifdef CONFIG_SMP
	tlbsync
	sync
	lis	r3,hash_table_lock@ha
	stw	r0,hash_table_lock@l(r3)
#endif
#if defined(CONFIG_SMP) || defined(CONFIG_PPC64BRIDGE)
	mtmsr	r10
	SYNC
#endif
	blr

/*
 * Flush the entry for a particular page from the hash table.
 *
 * flush_hash_page(unsigned context, unsigned long va)
 */
_GLOBAL(flush_hash_page)
#ifdef CONFIG_MOL
	mflr    r10
	MOL_HOOK_MMU(10, r6)
	mtlr    r10
#endif
	lis	r6,Hash@ha
	lwz	r6,Hash@l(r6)		/* hash table base */
	cmpwi	0,r6,0			/* hash table in use? */
	bne+	99f
	tlbie	r4			/* in hw tlb too */
	sync
#ifdef CONFIG_SMP
	tlbsync
	sync
#endif
	blr
99:
#if defined(CONFIG_SMP) || defined(CONFIG_PPC64BRIDGE)
	/* Note - we had better not do anything which could generate
	   a hash table miss while we have the hash table locked,
	   or we'll get a deadlock.  -paulus */
	mfmsr	r10
	sync
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	mtmsr	r0
	SYNC
#endif
#ifdef CONFIG_SMP
	lis	r9,hash_table_lock@h
	ori	r9,r9,hash_table_lock@l
	lwz	r8,PROCESSOR(r2)
	oris	r8,r8,9
10:	lwarx	r7,0,r9
	cmpi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	eieio
#endif
#ifndef CONFIG_PPC64BRIDGE
	rlwinm	r3,r3,11,1,20		/* put context into vsid */
	rlwimi	r3,r4,11,21,24		/* put top 4 bits of va into vsid */
	oris	r3,r3,0x8000		/* set V (valid) bit */
	rlwimi	r3,r4,10,26,31		/* put in API (abbrev page index) */
	rlwinm	r7,r4,32-6,10,25	/* get page index << 6 */
	rlwinm	r5,r3,32-1,7,25		/* vsid << 6 */
	xor	r7,r7,r5		/* primary hash << 6 */
	lis	r5,Hash_mask@ha
	lwz	r5,Hash_mask@l(r5)	/* hash mask */
	slwi	r5,r5,6			/*  << 6 */
	and	r7,r7,r5
	add	r6,r6,r7		/* address of primary PTEG */
	li	r8,8
	mtctr	r8
	addi	r7,r6,-8
1:	lwzu	r0,8(r7)		/* get next PTE */
	cmpw	0,r0,r3			/* see if tag matches */
	bdnzf	2,1b			/* while --ctr != 0 && !cr0.eq */
	beq	3f			/* if we found it */
	ori	r3,r3,0x40		/* set H (alt. hash) bit */
	xor	r6,r6,r5		/* address of secondary PTEG */
	mtctr	r8
	addi	r7,r6,-8
2:	lwzu	r0,8(r7)		/* get next PTE */
	cmpw	0,r0,r3			/* see if tag matches */
	bdnzf	2,2b			/* while --ctr != 0 && !cr0.eq */
	bne	4f			/* if we didn't find it */
3:	li	r0,0
	stw	r0,0(r7)		/* invalidate entry */
#else /* CONFIG_PPC64BRIDGE */
	rldic	r3,r3,16,16		/* put context into vsid (<< 12) */
	rlwimi	r3,r4,16,16,24		/* top 4 bits of va and API */
	ori	r3,r3,1			/* set V (valid) bit */
	rlwinm	r7,r4,32-5,9,24		/* get page index << 7 */
	srdi	r5,r3,5			/* vsid << 7 */
	rlwinm	r5,r5,0,1,24		/* vsid << 7 (limited to 24 bits) */
	xor	r7,r7,r5		/* primary hash << 7 */
	lis	r5,Hash_mask@ha
	lwz	r5,Hash_mask@l(r5)	/* hash mask */
	slwi	r5,r5,7			/*  << 7 */
	and	r7,r7,r5
	add	r6,r6,r7		/* address of primary PTEG */
	li	r8,8
	mtctr	r8
	addi	r7,r6,-16
1:	ldu	r0,16(r7)		/* get next PTE */
	cmpd	0,r0,r3			/* see if tag matches */
	bdnzf	2,1b			/* while --ctr != 0 && !cr0.eq */
	beq	3f			/* if we found it */
	ori	r3,r3,2			/* set H (alt. hash) bit */
	xor	r6,r6,r5		/* address of secondary PTEG */
	mtctr	r8
	addi	r7,r6,-16
2:	ldu	r0,16(r7)		/* get next PTE */
	cmpd	0,r0,r3			/* see if tag matches */
	bdnzf	2,2b			/* while --ctr != 0 && !cr0.eq */
	bne	4f			/* if we didn't find it */
3:	li	r0,0
	std	r0,0(r7)		/* invalidate entry */
#endif /* CONFIG_PPC64BRIDGE */
4:	sync
	tlbie	r4			/* in hw tlb too */
	sync
#ifdef CONFIG_SMP
	tlbsync
	sync
	li	r0,0
	stw	r0,0(r9)		/* clear hash_table_lock */
#endif
#if defined(CONFIG_SMP) || defined(CONFIG_PPC64BRIDGE)
	mtmsr	r10
	SYNC
#endif
	blr
