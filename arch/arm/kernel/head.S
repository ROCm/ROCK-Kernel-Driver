/*
 *  linux/arch/arm/kernel/head-armv.S
 *
 *  Copyright (C) 1994-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Kernel startup code for all 32-bit CPUs
 */
#include <linux/config.h>
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/mach-types.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>
#include <asm/constants.h>

/*
 * We place the page tables 16K below TEXTADDR.  Therefore, we must make sure
 * that TEXTADDR is correctly set.  Currently, we expect the least significant
 * 16 bits to be 0x8000, but we could probably relax this restriction to
 * TEXTADDR > PAGE_OFFSET + 0x4000
 *
 * Note that swapper_pg_dir is the virtual address of the page tables, and
 * pgtbl gives us a position-independent reference to these tables.  We can
 * do this because stext == TEXTADDR
 *
 * swapper_pg_dir, pgtbl and krnladr are all closely related.
 */
#if (TEXTADDR & 0xffff) != 0x8000
#error TEXTADDR must start at 0xXXXX8000
#endif

	.globl	swapper_pg_dir
	.equ	swapper_pg_dir, TEXTADDR - 0x4000

	.macro	pgtbl, reg
	adr	\reg, stext
	sub	\reg, \reg, #0x4000
	.endm

/*
 * Since the page table is closely related to the kernel start address, we
 * can convert the page table base address to the base address of the section
 * containing both.
 */
	.macro	krnladr, rd, pgtable
	bic	\rd, \pgtable, #0x000ff000
	.endm

/*
 * Kernel startup entry point.
 * ---------------------------
 *
 * This is normally called from the decompressor code.  The requirements
 * are: MMU = off, D-cache = off, I-cache = dont care, r0 = 0,
 * r1 = machine nr.
 *
 * This code is mostly position independent, so if you link the kernel at
 * 0xc0008000, you call this at __pa(0xc0008000).
 *
 * See linux/arch/arm/tools/mach-types for the complete list of machine
 * numbers for r1.
 *
 * We're trying to keep crap to a minimum; DO NOT add any machine specific
 * crap here - that's what the boot loader (or in extreme, well justified
 * circumstances, zImage) is for.
 */
	__INIT
	.type	stext, #function
ENTRY(stext)
	mov	r12, r0
	mov	r0, #PSR_F_BIT | PSR_I_BIT | MODE_SVC	@ make sure svc mode
	msr	cpsr_c, r0			@ and all irqs disabled
	bl	__lookup_processor_type
	teq	r10, #0				@ invalid processor?
	moveq	r0, #'p'			@ yes, error 'p'
	beq	__error
	bl	__lookup_architecture_type
	teq	r7, #0				@ invalid architecture?
	moveq	r0, #'a'			@ yes, error 'a'
	beq	__error
	bl	__create_page_tables

	/*
	 * The following calls CPU specific code in a position independent
	 * manner.  See arch/arm/mm/proc-*.S for details.  r10 = base of
	 * xxx_proc_info structure selected by __lookup_architecture_type
	 * above.  On return, the CPU will be ready for the MMU to be
	 * turned on, and r0 will hold the CPU control register value.
	 */
	adr	lr, __turn_mmu_on		@ return (PIC) address
	add	pc, r10, #12

	.type	__switch_data, %object
__switch_data:
	.long	__mmap_switched
	.long	__bss_start			@ r4
	.long	_end				@ r5
	.long	processor_id			@ r6
	.long	__machine_arch_type		@ r7
	.long	cr_alignment			@ r8
	.long	init_thread_union+8192		@ sp

/*
 * Enable the MMU.  This completely changes the structure of the visible
 * memory space.  You will not be able to trace execution through this.
 * If you have an enquiry about this, *please* check the linux-arm-kernel
 * mailing list archives BEFORE sending another post to the list.
 */
	.align	5
	.type	__turn_mmu_on, %function
__turn_mmu_on:
	ldr	lr, __switch_data
#ifdef CONFIG_ALIGNMENT_TRAP
	orr	r0, r0, #2			@ ...........A.
#endif
	mcr	p15, 0, r0, c1, c0, 0		@ write control reg
	mrc	p15, 0, r3, c0, c0, 0		@ read id reg
	mov	r3, r3
	mov	r3, r3
	mov	pc, lr

/*
 * The following fragment of code is executed with the MMU on, and uses
 * absolute addresses; this is not position independent.
 *
 *  r0  = processor control register
 *  r1  = machine ID
 *  r9  = processor ID
 *  r12 = value of r0 when kernel was called (currently always zero)
 */
	.align	5
__mmap_switched:
	adr	r3, __switch_data + 4
	ldmia	r3, {r4, r5, r6, r7, r8, sp}
	mov	fp, #0				@ Clear BSS (and zero fp)
1:	cmp	r4, r5
	strcc	fp, [r4],#4
	bcc	1b
	str	r9, [r6]			@ Save processor ID
	str	r1, [r7]			@ Save machine type
	bic	r2, r0, #2			@ Clear 'A' bit
	stmia	r8, {r0, r2}			@ Save control register values
	b	start_kernel




/*
 * Setup the initial page tables.  We only setup the barest
 * amount which are required to get the kernel running, which
 * generally means mapping in the kernel code.
 *
 * We only map in 4MB of RAM, which should be sufficient in
 * all cases.
 *
 * r5 = physical address of start of RAM
 * r6 = physical IO address
 * r7 = byte offset into page tables for IO
 * r8 = page table flags
 */
__create_page_tables:
	pgtbl	r4				@ page table address

	/*
	 * Clear the 16K level 1 swapper page table
	 */
	mov	r0, r4
	mov	r3, #0
	add	r2, r0, #0x4000
1:	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
	teq	r0, r2
	bne	1b

	/*
	 * Create identity mapping for first MB of kernel to
	 * cater for the MMU enable.  This identity mapping
	 * will be removed by paging_init()
	 */
	krnladr	r2, r4				@ start of kernel
	add	r3, r8, r2			@ flags + kernel base
	str	r3, [r4, r2, lsr #18]		@ identity mapping

	/*
	 * Now setup the pagetables for our kernel direct
	 * mapped region.  We round TEXTADDR down to the
	 * nearest megabyte boundary.
	 */
	add	r0, r4, #(TEXTADDR & 0xff000000) >> 18 @ start of kernel
	bic	r2, r3, #0x00f00000
	str	r2, [r0]			@ PAGE_OFFSET + 0MB
	add	r0, r0, #(TEXTADDR & 0x00f00000) >> 18
	str	r3, [r0], #4			@ KERNEL + 0MB
	add	r3, r3, #1 << 20
	str	r3, [r0], #4			@ KERNEL + 1MB
	add	r3, r3, #1 << 20
	str	r3, [r0], #4			@ KERNEL + 2MB
	add	r3, r3, #1 << 20
	str	r3, [r0], #4			@ KERNEL + 3MB

	bic	r8, r8, #0x0c			@ turn off cacheable
						@ and bufferable bits
#ifdef CONFIG_DEBUG_LL
	/*
	 * Map in IO space for serial debugging.
	 * This allows debug messages to be output
	 * via a serial console before paging_init.
	 */
	add	r0, r4, r7
	rsb	r3, r7, #0x4000			@ PTRS_PER_PGD*sizeof(long)
	cmp	r3, #0x0800
	addge	r2, r0, #0x0800
	addlt	r2, r0, r3
	orr	r3, r6, r8
1:	str	r3, [r0], #4
	add	r3, r3, #1 << 20
	teq	r0, r2
	bne	1b
#if defined(CONFIG_ARCH_NETWINDER) || defined(CONFIG_ARCH_CATS)
	/*
	 * If we're using the NetWinder, we need to map in
	 * the 16550-type serial port for the debug messages
	 */
	teq	r1, #MACH_TYPE_NETWINDER
	teqne	r1, #MACH_TYPE_CATS
	bne	1f
	add	r0, r4, #0x3fc0			@ ff000000
	mov	r3, #0x7c000000
	orr	r3, r3, r8
	str	r3, [r0], #4
	add	r3, r3, #1 << 20
	str	r3, [r0], #4
1:
#endif
#endif
#ifdef CONFIG_ARCH_RPC
	/*
	 * Map in screen at 0x02000000 & SCREEN2_BASE
	 * Similar reasons here - for debug.  This is
	 * only for Acorn RiscPC architectures.
	 */
	add	r0, r4, #0x80			@ 02000000
	mov	r3, #0x02000000
	orr	r3, r3, r8
	str	r3, [r0]
	add	r0, r4, #0x3600			@ d8000000
	str	r3, [r0]
#endif
	mov	pc, lr



/*
 * Exception handling.  Something went wrong and we can't proceed.  We
 * ought to tell the user, but since we don't have any guarantee that
 * we're even running on the right architecture, we do virtually nothing.
 *
 * r0 = ascii error character:
 *	a = invalid architecture
 *	p = invalid processor
 *	i = invalid calling convention
 *
 * Generally, only serious errors cause this.
 */
__error:
#ifdef CONFIG_DEBUG_LL
	mov	r8, r0				@ preserve r0
	adr	r0, err_str
	bl	printascii
	mov	r0, r8
	bl	printch
#endif
#ifdef CONFIG_ARCH_RPC
/*
 * Turn the screen red on a error - RiscPC only.
 */
	mov	r0, #0x02000000
	mov	r3, #0x11
	orr	r3, r3, r3, lsl #8
	orr	r3, r3, r3, lsl #16
	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
#endif
1:	mov	r0, r0
	b	1b

#ifdef CONFIG_DEBUG_LL
err_str:
	.asciz	"\nError: "
	.align
#endif

/*
 * Read processor ID register (CP#15, CR0), and look up in the linker-built
 * supported processor list.  Note that we can't use the absolute addresses
 * for the __proc_info lists since we aren't running with the MMU on
 * (and therefore, we are not in the correct address space).  We have to
 * calculate the offset.
 *
 * Returns:
 *	r5, r6, r7 corrupted
 *	r8  = page table flags
 *	r9  = processor ID
 *	r10 = pointer to processor structure
 */
__lookup_processor_type:
	adr	r5, 2f
	ldmia	r5, {r7, r9, r10}
	sub	r5, r5, r10			@ convert addresses
	add	r7, r7, r5			@ to our address space
	add	r10, r9, r5
	mrc	p15, 0, r9, c0, c0		@ get processor id
1:	ldmia	r10, {r5, r6, r8}		@ value, mask, mmuflags
	and	r6, r6, r9			@ mask wanted bits
	teq	r5, r6
	moveq	pc, lr
	add	r10, r10, #PROC_INFO_SZ		@ sizeof(proc_info_list)
	cmp	r10, r7
	blt	1b
	mov	r10, #0				@ unknown processor
	mov	pc, lr

/*
 * Look in include/asm-arm/procinfo.h and arch/arm/kernel/arch.[ch] for
 * more information about the __proc_info and __arch_info structures.
 */
2:	.long	__proc_info_end
	.long	__proc_info_begin
	.long	2b
	.long	__arch_info_begin
	.long	__arch_info_end

/*
 * Lookup machine architecture in the linker-build list of architectures.
 * Note that we can't use the absolute addresses for the __arch_info
 * lists since we aren't running with the MMU on (and therefore, we are
 * not in the correct address space).  We have to calculate the offset.
 *
 *  r1 = machine architecture number
 * Returns:
 *  r2, r3, r4 corrupted
 *  r5 = physical start address of RAM
 *  r6 = physical address of IO
 *  r7 = byte offset into page tables for IO
 */
__lookup_architecture_type:
	adr	r4, 2b
	ldmia	r4, {r2, r3, r5, r6, r7}	@ throw away r2, r3
	sub	r5, r4, r5			@ convert addresses
	add	r4, r6, r5			@ to our address space
	add	r7, r7, r5
1:	ldr	r5, [r4]			@ get machine type
	teq	r5, r1				@ matches loader number?
	beq	2f				@ found
	add	r4, r4, #SIZEOF_MACHINE_DESC	@ next machine_desc
	cmp	r4, r7
	blt	1b
	mov	r7, #0				@ unknown architecture
	mov	pc, lr
2:	ldmib	r4, {r5, r6, r7}		@ found, get results
	mov	pc, lr
