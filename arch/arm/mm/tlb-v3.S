/*
 *  linux/arch/arm/mm/tlbv3.S
 *
 *  Copyright (C) 1997-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ARM architecture version 3 TLB handling functions.
 *
 * Processors: ARM610, ARM710.
 */
#include <linux/linkage.h>
#include <asm/constants.h>
#include "proc-macros.S"

	.align	5
/*
 *	v3_flush_user_tlb_mm(mm)
 *
 *	Invalidate all TLB entries in a particular address space
 *
 *	- mm    - mm_struct describing address space
 */
ENTRY(v3_flush_user_tlb_mm)
	act_mm	r1				@ get current->active_mm
	teq	r0, r1				@ == mm ?
	movne	pc, lr				@ no, we dont do anything

/*
 *	v3_flush_kern_tlb_all()
 *
 *	Invalidate the entire TLB
 */
ENTRY(v3_flush_kern_tlb_all)
	mov	r0, #0
	mcr	p15, 0, r0, c5, c0, 0		@ invalidate TLB
	mov	pc, lr

/*
 *	v3_flush_user_tlb_range(start, end, mm)
 *
 *	Invalidate a range of TLB entries in the specified address space.
 *
 *	- start - range start address
 *	- end   - range end address
 *	- mm    - mm_struct describing address space
 */
	.align	5
ENTRY(v3_flush_user_tlb_range)
	vma_vm_mm r2, r2
	act_mm	r3				@ get current->active_mm
	teq	r2, r3				@ == mm ?
	movne	pc, lr				@ no, we dont do anything
ENTRY(v3_flush_kern_tlb_range)
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
1:	mcr	p15, 0, r0, c6, c0, 0		@ invalidate TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mov	pc, lr

/*
 *	v3_flush_user_tlb_page(vaddr,vma)
 *
 *	Invalidate the specified page in the specified address space.
 *
 *	- vaddr - virtual address (may not be aligned)
 *	- vma   - vma_struct describing address range
 */
	.align	5
ENTRY(v3_flush_user_tlb_page)
	vma_vm_mm r2, r1			@ get vma->vm_mm
	act_mm	r3				@ get current->active_mm
	teq	r2, r3				@ equal
	movne	pc, lr				@ no
ENTRY(v3_flush_kern_tlb_page)
	mcr	p15, 0, r0, c6, c0, 0		@ invalidate TLB entry
	mov	pc, lr

	.section ".text.init", #alloc, #execinstr

	.type	v3_tlb_fns, #object
ENTRY(v3_tlb_fns)
	.long	v3_flush_kern_tlb_all
	.long	v3_flush_user_tlb_mm
	.long	v3_flush_user_tlb_range
	.long	v3_flush_user_tlb_page
	.long	v3_flush_kern_tlb_range
	.long	v3_flush_kern_tlb_page
	.size	v3_tlb_fns, . - v3_tlb_fns
