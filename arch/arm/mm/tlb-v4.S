/*
 *  linux/arch/arm/mm/tlbv4.S
 *
 *  Copyright (C) 1997-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ARM architecture version 4 TLB handling functions.
 *  These assume a split I/D TLBs, and no write buffer.
 *
 * Processors: ARM720T
 */
#include <linux/linkage.h>
#include <asm/constants.h>
#include "proc-macros.S"

	.align	5
/*
 *	v4_flush_user_tlb_mm(mm)
 *
 *	Invalidate all TLB entries in a particular address space
 *
 *	- mm    - mm_struct describing address space
 */
ENTRY(v4_flush_user_tlb_mm)
	act_mm	r1				@ get current->active_mm
	teq	r0, r1				@ == mm ?
	movne	pc, lr				@ no, we dont do anything

/*
 *	v4_flush_kern_tlb_all()
 *
 *	Invalidate the entire TLB
 */
ENTRY(v4_flush_kern_tlb_all)
	mov	r0, #0
	mcr	p15, 0, r0, c8, c7, 0		@ invalidate I + D TLBs
	mov	pc, lr

/*
 *	v4_flush_user_tlb_range(start, end, mm)
 *
 *	Invalidate a range of TLB entries in the specified user address space.
 *
 *	- start - range start address
 *	- end   - range end address
 *	- mm    - mm_struct describing address space
 */
	.align	5
ENTRY(v4_flush_user_tlb_range)
	vma_vm_mm ip, r2
	act_mm	r3				@ get current->active_mm
	eors	r3, ip, r3				@ == mm ?
	movne	pc, lr				@ no, we dont do anything
	vma_vm_flags ip, r2
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
1:	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	tst	ip, #VM_EXEC
	mcrne	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
	mov	pc, lr

/*
 *	v4_flush_user_tlb_page(vaddr,vma)
 *
 *	Invalidate the specified page in the specified address space.
 *
 *	- vaddr - virtual address (may not be aligned)
 *	- vma   - vma_struct describing address range
 */
	.align	5
ENTRY(v4_flush_user_tlb_page)
	vma_vm_mm r2, r1			@ get vma->vm_mm
	act_mm	r3				@ get current->active_mm
	teq	r2, r3				@ equal
	movne	pc, lr				@ no
	vma_vm_flags r2, r1
	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	tst	r2, #VM_EXEC
	mcrne	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
	mov	pc, lr

/*
 *	v4_flush_kerm_tlb_range(start, end)
 *
 *	Invalidate a range of TLB entries in the specified kernel
 *	address range.
 *
 *	- start - virtual address (may not be aligned)
 *	- end   - virtual address (may not be aligned)
 */
	.align	5
ENTRY(v4_flush_kern_tlb_range)
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
1:	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
	mov	pc, lr


/*
 *	v4_flush_kern_tlb_page(kaddr)
 *
 *	Invalidate the TLB entry for the specified page.  The address
 *	will be in the kernels virtual memory space.  Current uses
 *	only require the D-TLB to be invalidated.
 *
 *	- kaddr - Kernel virtual memory address
 */
ENTRY(v4_flush_kern_tlb_page)
	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	mov	pc, lr

	.section ".text.init", #alloc, #execinstr

	.type	v4_tlb_fns, #object
ENTRY(v4_tlb_fns)
	.long	v4_flush_kern_tlb_all
	.long	v4_flush_user_tlb_mm
	.long	v4_flush_user_tlb_range
	.long	v4_flush_user_tlb_page
	.long	v4_flush_kern_tlb_range
	.long	v4_flush_kern_tlb_page
	.size	v4_tlb_fns, . - v4_tlb_fns
