/*
 *  linux/arch/x86_64/kernel/head.S -- start in 32bit and switch to 64bit
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2000 Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2000 Karsten Keil <kkeil@suse.de>
 *  Copyright (C) 2001 2002 Andi Kleen <ak@suse.de>
 *
 *  $Id: head.S,v 1.41 2001/07/05 23:43:45 ak Exp $
 */
.code64
.text

#include <linux/linkage.h>
#include <linux/threads.h>
#include <asm/desc.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/offset.h>
	
/* we don't able to switch in one step to final KERNEL ADDRESS SPACE
 * because we need identity-mapped pages on setup so define __START_KERNEL to
 * 0x100000 for this stage
 * 
 */


startup_32:
.code32
	/*
	 * At this point the CPU runs in 32bit protected mode (CS.D = 1) with
	 * paging disabled and the point of this file is to switch to 64bit
	 * long mode with a kernel mapping for kerneland to jump into the
	 * kernel virtual addresses.
 	 * There is no stack until we set one up.
	 */

	/* As first check if extended functions are implemented */
	movl	$0x80000000, %eax
	cpuid
	cmpl	$0x80000000, %eax
	jbe	no_long_mode
	/* Check if long mode is implemented */
	mov	$0x80000001, %eax
	cpuid
	btl	$29, %edx
	jnc	no_long_mode

	/*
	 * Prepare for entering 64bits mode
	 */

	/* Enable PAE mode and PGE */
	xorl	%eax, %eax
	btsl	$5, %eax
	btsl	$7, %eax
	movl	%eax, %cr4

	/* Setup early boot stage 4 level pagetables */
	movl	$0x101000, %eax
	movl	%eax, %cr3

	/* Setup EFER (Extended Feature Enable Register) */
	movl	$0xc0000080, %ecx
	rdmsr
	/* Fool rdmsr and reset %eax to avoid dependences */
	xorl	%eax, %eax
	/* Enable Long Mode */
	btsl	$8, %eax
	/* Enable System Call */
	btsl	$0, %eax
	/* Make changes effective */
	wrmsr

	xorl	%eax, %eax
	/* Enable paging and in turn activate Long Mode */
	btsl	$31, %eax
	/* Enable protected mode */
	btsl	$0, %eax
	/* Enable MP */
	btsl	$1, %eax
	/* Enable ET */
	btsl	$4, %eax
	/* Enable NE */
	btsl	$5, %eax
	/* Enable WP */
	btsl	$16, %eax
	/* Enable AM */
	btsl	$18, %eax
	/* Make changes effective */
	movl	%eax, %cr0
	jmp	reach_compatibility_mode
reach_compatibility_mode:
	
	/*
	 * At this point we're in long mode but in 32bit compatibility mode
	 * with EFER.LME = 1, CS.L = 0, CS.D = 1 (and in turn
	 * EFER.LMA = 1). Now we want to jump in 64bit mode, to do that we load
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 */

	/* Load new GDT with the 64bit segment using 32bit descriptor */
	/* to avoid 32bit relocations we use fixed adresses here */
	movl	$0x100F00, %eax
	lgdt	(%eax)
	movl    $0x100F10, %eax
	/* Finally jump in 64bit mode */
	ljmp	*(%eax)

.code64
reach_long64:
	/*
	 * Where we're running at 0x0000000000100000, and yes, finally
	 * in 64bit mode.
	 */
	.globl	init_rsp

	/* Setup the first kernel stack (this instruction is modified by smpboot) */
	.byte 0x48, 0xb8	/* movq *init_rsp,%rax */ 
init_rsp:
	.quad init_thread_union+THREAD_SIZE
	movq	%rax, %rsp

	/* zero EFLAGS after setting rsp */
	pushq $0
	popfq

	/*
	 * We must switch to a new descriptor in kernel space for the GDT
	 * because soon the kernel won't have access anymore to the userspace
	 * addresses where we're currently running on. We have to do that here
	 * because in 32bit we couldn't load a 64bit linear address.
	 */
	lgdt	pGDT64

	/* esi is pointer to real mode structure with interesting info.
	   pass it to C */
	movl	%esi, %edi

	movl $__KERNEL_DS,%eax
	movl %eax,%ss
	movl %eax,%ds	
	movl %eax,%es
			
	/* Finally jump to run C code and to be on real kernel address
	 * Since we are running on identity-mapped space we have to jump
	 * to the full 64bit address , this is only possible as indirect
	 * jump
	 */
	movq	initial_code(%rip),%rax
	jmp	*%rax


	/* SMP bootup changes this */	
	.globl	initial_code
initial_code:
	.quad	x86_64_start_kernel

.code32
ENTRY(no_long_mode)
	/* This isn't an x86-64 CPU so hang */
1:
	jmp	1b

.org 0xf00
pGDT32:
	.word	gdt32_end-gdt_table32
	.quad	gdt_table32-__START_KERNEL+0x100000

.org 0xf10	
ljumpvector:
	.long	reach_long64-__START_KERNEL+0x100000
	.word	__KERNEL_CS

ENTRY(stext)
ENTRY(_stext)

	/*
	 * This default setting generates an ident mapping at address 0x100000
	 * and a mapping for the kernel that precisely maps virtual address
	 * 0xffffffff80000000 to physical address 0x000000. (always using
	 * 2Mbyte large pages provided by PAE mode)
	 */
.org 0x1000
ENTRY(level4_pgt)
	.quad	0x0000000000102007		/* -> level3_ident_pgt */
	.fill	255,8,0
	/* __PAGE_OFFSET 0xffff800000000000 */
	.quad	0x000000000010a007
	.fill	254,8,0
	/* (2^48-(2*1024*1024*1024))/(2^39) = 511 */
	.quad	0x0000000000103007		/* -> level3_kernel_pgt */

.org 0x2000
/* Kernel does not "know" about 4-th level of page tables. */
ENTRY(swapper_pg_dir)
ENTRY(level3_ident_pgt)
	.quad	0x0000000000104007
	.fill	511,8,0

.org 0x3000
ENTRY(level3_kernel_pgt)
	.fill	510,8,0
	/* (2^48-(2*1024*1024*1024)-((2^39)*511))/(2^30) = 510 */
	.quad	0x0000000000105007		/* -> level2_kernel_pgt */
	.fill	1,8,0

.org 0x4000
ENTRY(level2_ident_pgt)
	/* 2 Mbytes are enough, this is necessary only for head.S */
	.quad	0x0000000000000283
	/* .fill	511,8,0 */
	/* Jan needs more than 2Mbytes, so set a 40Mbyte mapping instead */
	.quad	0x0000000000200183
	.quad	0x0000000000400183
	.quad	0x0000000000600183
	.quad	0x0000000000800183
	.quad	0x0000000000A00183
	.quad	0x0000000000C00183
	.quad	0x0000000000E00183
	.quad	0x0000000001000183
	.quad	0x0000000001200183
	.quad	0x0000000001400183
	.quad	0x0000000001600183
	.quad	0x0000000001800183
	.quad	0x0000000001A00183
	.quad	0x0000000001C00183
	.quad	0x0000000001E00183
	.quad	0x0000000002000183
	.quad	0x0000000002200183
	.quad	0x0000000002400183
	.quad	0x0000000002600183
	.fill	492,8,0
	
.org 0x5000
ENTRY(level2_kernel_pgt)
	/* (2^48-(2*1024*1024*1024)-((2^39)*511)-((2^30)*510)) = 0 */
	.quad	0x0000000000000183
	.quad	0x0000000000200183
	.quad	0x0000000000400183
	.quad	0x0000000000600183
	.quad	0x0000000000800183
	.quad	0x0000000000A00183
	.quad	0x0000000000C00183
	.quad	0x0000000000E00183
	.quad	0x0000000001000183
	.quad	0x0000000001200183
	.quad	0x0000000001400183
	.quad	0x0000000001600183
	.quad	0x0000000001800183
	.quad	0x0000000001A00183
	.quad	0x0000000001C00183
	.quad	0x0000000001E00183
	.quad	0x0000000002000183
	.quad	0x0000000002200183
	.quad	0x0000000002400183
	.quad	0x0000000002600183
	/*
	 * We could go ahead without any downside (except typing programmer
	 * wise :) but 40Mbyte are just enough for the kernel statically
	 * linked part (and extending it is trivial).
	 */
	.fill	492,8,0

.org 0x6000
ENTRY(empty_zero_page)

.org 0x7000
ENTRY(empty_bad_page)

.org 0x8000
ENTRY(empty_bad_pte_table)

.org 0x9000
ENTRY(empty_bad_pmd_table)

.org 0xa000
ENTRY(level3_physmem_pgt)
	.quad	0x0000000000105007		/* -> level2_kernel_pgt (so that __va works even before pagetable_init) */



.org 0xb000


.data

.globl SYMBOL_NAME(gdt)

	.word 0
	.align 16
	.word 0
pGDT64:
	.word	gdt_end-gdt_table
SYMBOL_NAME_LABEL(gdt)
	.quad	gdt_table
	

.align 64 /* cacheline aligned */
ENTRY(gdt_table32)
	.quad	0x0000000000000000	/* This one is magic */
	.quad	0x0000000000000000	/* unused */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
gdt32_end:	
	
/* We need valid kernel segments for data and code in long mode too
 * IRET will check the segment types  kkeil 2000/10/28
 * Also sysret mandates a special GDT layout 
 */
		 		
.align 64 /* cacheline aligned, keep this synchronized with asm/desc.h */
ENTRY(gdt_table)
	.quad	0x0000000000000000	/* This one is magic */
	.quad	0x0000000000000000	/* unused */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x00cffe000000ffff	/* __USER32_CS */
	.quad	0x00cff2000000ffff	/* __USER_DS, __USER32_DS  */		
	.quad	0x00affa000000ffff	/* __USER_CS */

	.globl  tss_start
tss_start:
	.rept NR_CPUS	 	
	.quad   0,0					/* TSS descriptors. filled in later */
	.endr
	.globl ldt_start
ldt_start:	
	.rept NR_CPUS
	.quad   0,0					/* LDT descriptors. filled in later */ 
	.endr	
gdt_end:	
	.globl gdt_end

	.align  64
ENTRY(idt_table)	
	.rept   256
	.quad   0
	.quad 	0
	.endr
	
.section .text.lock
ENTRY(stext_lock)
