/*
 *  linux/arch/arm/mm/tlbv4wb.S
 *
 *  Copyright (C) 1997-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ARM architecture version 4 and version 5 TLB handling functions.
 *  These assume a split I/D TLBs, with a write buffer.
 *
 *  Processors: ARM920 ARM922 ARM926 SA110 SA1100 SA1110 XScale
 */
#include <linux/linkage.h>
#include <asm/constants.h>
#include "proc-macros.S"

	.align	5
/*
 *	v4wb_flush_user_tlb_mm(mm)
 *
 *	Invalidate all TLB entries in a particular address space
 *
 *	- mm    - mm_struct describing address space
 */
ENTRY(v4wb_flush_user_tlb_mm)
ENTRY(v4wbi_flush_user_tlb_mm)
	act_mm	r1				@ get current->active_mm
	teq	r0, r1				@ == mm ?
	movne	pc, lr				@ no, we dont do anything

/*
 *	v4wb_flush_tlb_all()
 *
 *	Invalidate the entire TLB
 */
ENTRY(v4wb_flush_kern_tlb_all)
ENTRY(v4wbi_flush_kern_tlb_all)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mcr	p15, 0, r0, c8, c7, 0		@ invalidate I + D TLBs
	mov	pc, lr

/*
 *	v4wb_flush_user_tlb_range(start, end, mm)
 *
 *	Invalidate a range of TLB entries in the specified address space.
 *
 *	- start - range start address
 *	- end   - range end address
 *	- mm    - mm_struct describing address space
 */
	.align	5
ENTRY(v4wb_flush_user_tlb_range)
	vma_vm_mm ip, r2
	act_mm	r3				@ get current->active_mm
	eors	r3, ip, r3				@ == mm ?
	movne	pc, lr				@ no, we dont do anything
	vma_vm_flags r2, r2
	mcr	p15, 0, r3, c7, c10, 4		@ drain WB
	tst	r2, #VM_EXEC
	mcrne	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
1:	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mov	pc, lr

/*
 *	v4wb_flush_user_tlb_page(vaddr,vma)
 *
 *	Invalidate the specified page in the specified address space.
 *
 *	- vaddr - virtual address (may not be aligned)
 *	- vma   - vma_struct describing address range
 */
	.align	5
ENTRY(v4wb_flush_user_tlb_page)
	vma_vm_mm r2, r1			@ get vma->vm_mm
	act_mm	r3				@ get current->active_mm
	teq	r2, r3				@ equal
	movne	pc, lr				@ no
	vma_vm_flags r2, r1
	mov	r3, #0
	mcr	p15, 0, r3, c7, c10, 4		@ drain WB
	tst	r2, #VM_EXEC
	mcrne	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
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
ENTRY(v4wb_flush_kern_tlb_range)
	mov	r3, #0
	mcr	p15, 0, r3, c7, c10, 4		@ drain WB
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
	mcr	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
1:	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
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
ENTRY(v4wb_flush_kern_tlb_page)
	mcr	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	mov	pc, lr

/*
 * These two are optimised for ARM920, ARM922, ARM926, Xscale
 */

/*
 *	v4wb_flush_user_tlb_range(start, end, mm)
 *
 *	Invalidate a range of TLB entries in the specified address space.
 *
 *	- start - range start address
 *	- end   - range end address
 *	- mm    - mm_struct describing address space
 */
	.align	5
ENTRY(v4wbi_flush_user_tlb_range)
	vma_vm_mm ip, r2
	act_mm	r3				@ get current->active_mm
	eors	r3, ip, r3			@ == mm ?
	movne	pc, lr				@ no, we dont do anything
	mov	r3, #0
	mcr	p15, 0, r3, c7, c10, 4		@ drain WB
	vma_vm_flags r2, r2
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
1:	tst	r2, #VM_EXEC
	mcrne	p15, 0, r0, c8, c5, 1		@ invalidate I TLB entry
	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mov	pc, lr

/*
 *	v4wb_flush_tlb_page(vaddr,vma)
 *
 *	Invalidate the specified page in the specified address space.
 *
 *	- vaddr - virtual address (may not be aligned)
 *	- vma   - vma_struct describing address range
 */
	.align	5
ENTRY(v4wbi_flush_user_tlb_page)
	vma_vm_mm r2, r1			@ get vma->vm_mm
	act_mm	r3				@ get current->active_mm
	teq	r2, r3				@ equal
	movne	pc, lr				@ no
	vma_vm_flags r2, r1
	mov	r3, #0
	mcr	p15, 0, r3, c7, c10, 4		@ drain WB
	tst	r2, #VM_EXEC
	mcrne	p15, 0, r0, c8, c5, 1		@ invalidate I TLB entry
	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	mov	pc, lr

ENTRY(v4wbi_flush_kern_tlb_range)
	mov	r3, #0
	mcr	p15, 0, r3, c7, c10, 4		@ drain WB
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
1:	mcr	p15, 0, r0, c8, c5, 1		@ invalidate I TLB entry
	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mov	pc, lr

ENTRY(v4wbi_flush_kern_tlb_page)
	mcr	p15, 0, r0, c8, c5, 1		@ invalidate I TLB entry
	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	mov	pc, lr

	.section ".text.init", #alloc, #execinstr

	.type	v4wb_tlb_fns, #object
ENTRY(v4wb_tlb_fns)
	.long	v4wb_flush_kern_tlb_all
	.long	v4wb_flush_user_tlb_mm
	.long	v4wb_flush_user_tlb_range
	.long	v4wb_flush_user_tlb_page
	.long	v4wb_flush_kern_tlb_range
	.long	v4wb_flush_kern_tlb_page
	.size	v4wb_tlb_fns, . - v4wb_tlb_fns

	.type	v4wbi_tlb_fns, #object
ENTRY(v4wbi_tlb_fns)
	.long	v4wbi_flush_kern_tlb_all
	.long	v4wbi_flush_user_tlb_mm
	.long	v4wbi_flush_user_tlb_range
	.long	v4wbi_flush_user_tlb_page
	.long	v4wbi_flush_kern_tlb_range
	.long	v4wbi_flush_kern_tlb_page
	.size	v4wbi_tlb_fns, . - v4wbi_tlb_fns
