/*
 * arch/mips/kernel/head.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1995 - 1999 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Modified for DECStation and hence R3000 support by Paul M. Antoine
 * Further modifications by David S. Miller and Harald Koerfgen
 * Copyright (C) 1999 Silicon Graphics, Inc.
 *
 * Head.S contains the MIPS exception handler and startup code.
 *
 **************************************************************************
 *  9 Nov, 2000.
 *  Added Cache Error exception handler and SBDDP EJTAG debug exception.
 *
 *  Kevin Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 *  Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 **************************************************************************
 */
#include <linux/config.h>
#include <linux/threads.h>

#include <asm/asm.h>
#include <asm/cacheops.h>
#include <asm/current.h>
#include <asm/offset.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/bootinfo.h>

	.text
	/*
	 * Reserved space for exception handlers.
	 * Necessary for machines which link their kernels at KSEG0.
	 * FIXME: Use the initcode feature to get rid of unused handler
	 * variants.
	 */
	.fill	0x280
/*	
 * This is space for the interrupt handlers.
 * After trap_init() they are located at virtual address KSEG0.
 *
 * These handlers much be written in a relocatable manner
 * because based upon the cpu type an arbitrary one of the
 * following pieces of code will be copied to the KSEG0
 * vector location.
 */
	/* TLB refill, EXL == 0, R4xx0, non-R4600 version */
	.set	noreorder
	.set	noat
	LEAF(except_vec0_r4000)
	.set	mips3
#ifdef CONFIG_SMP
	mfc0	k1, CP0_CONTEXT
	la	k0, current_pgd
	srl	k1, 23
	sll	k1, 2
	addu	k1, k0, k1
	lw	k1, (k1)
#else 
	lw	k1, current_pgd			# get pgd pointer
#endif	
	mfc0	k0, CP0_BADVADDR		# Get faulting address
	srl	k0, k0, 22			# get pgd only bits

	sll	k0, k0, 2
	addu	k1, k1, k0			# add in pgd offset
	mfc0	k0, CP0_CONTEXT			# get context reg
	lw	k1, (k1)
#if defined(CONFIG_CPU_VR41XX)
	srl	k0, k0, 3			# get pte offset
#else
	srl	k0, k0, 1			# get pte offset
#endif
	and	k0, k0, 0xff8
	addu	k1, k1, k0			# add in offset
	lw	k0, 0(k1)			# get even pte
	lw	k1, 4(k1)			# get odd pte
	srl	k0, k0, 6			# convert to entrylo0
	mtc0	k0, CP0_ENTRYLO0		# load it
	srl	k1, k1, 6			# convert to entrylo1
	mtc0	k1, CP0_ENTRYLO1		# load it
	b	1f
	tlbwr					# write random tlb entry
1:
	nop
	eret					# return from trap
	END(except_vec0_r4000)

	/* TLB refill, EXL == 0, R4600 version */
	LEAF(except_vec0_r4600)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	srl	k0, k0, 22
	lw	k1, current_pgd			# get pgd pointer
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mtc0	k1, CP0_ENTRYLO1
	nop
	tlbwr
	nop
	eret
	END(except_vec0_r4600)

	/* TLB refill, EXL == 0, R52x0 "Nevada" version */
        /*
         * This version has a bug workaround for the Nevada.  It seems
         * as if under certain circumstances the move from cp0_context
         * might produce a bogus result when the mfc0 instruction and
         * it's consumer are in a different cacheline or a load instruction,
         * probably any memory reference, is between them.  This is
         * potencially slower than the R4000 version, so we use this
         * special version.
         */
	.set	noreorder
	.set	noat
	LEAF(except_vec0_nevada)
	.set	mips3
	mfc0	k0, CP0_BADVADDR		# Get faulting address
	srl	k0, k0, 22			# get pgd only bits
	lw	k1, current_pgd			# get pgd pointer
	sll	k0, k0, 2
	addu	k1, k1, k0			# add in pgd offset
	lw	k1, (k1)
	mfc0	k0, CP0_CONTEXT			# get context reg
	srl	k0, k0, 1			# get pte offset
	and	k0, k0, 0xff8
	addu	k1, k1, k0			# add in offset
	lw	k0, 0(k1)			# get even pte
	lw	k1, 4(k1)			# get odd pte
	srl	k0, k0, 6			# convert to entrylo0
	mtc0	k0, CP0_ENTRYLO0		# load it
	srl	k1, k1, 6			# convert to entrylo1
	mtc0	k1, CP0_ENTRYLO1		# load it
	nop					# QED specified nops
	nop
	tlbwr					# write random tlb entry
	nop					# traditional nop
	eret					# return from trap
	END(except_vec0_nevada)

	/* TLB refill, EXL == 0, R4[40]00/R5000 badvaddr hwbug version */
	LEAF(except_vec0_r45k_bvahwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	srl	k0, k0, 22
	lw	k1, current_pgd			# get pgd pointer
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mfc0	k0, CP0_INDEX
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r45k_bvahwbug)

#ifdef CONFIG_SMP
	/* TLB refill, EXL == 0, R4000 MP badvaddr hwbug version */
	LEAF(except_vec0_r4k_mphwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	srl	k0, k0, 22
	lw	k1, current_pgd			# get pgd pointer
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mfc0	k0, CP0_INDEX
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_mphwbug)
#endif

	/* TLB refill, EXL == 0, R4000 UP 250MHZ entrylo[01] hwbug version */
	LEAF(except_vec0_r4k_250MHZhwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	srl	k0, k0, 22
	lw	k1, current_pgd			# get pgd pointer
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	srl	k0, k0, 6
	mtc0	zero, CP0_ENTRYLO0
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mtc0	zero, CP0_ENTRYLO1
	mtc0	k1, CP0_ENTRYLO1
	b	1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_250MHZhwbug)

#ifdef CONFIG_SMP
	/* TLB refill, EXL == 0, R4000 MP 250MHZ entrylo[01]+badvaddr bug version */
	LEAF(except_vec0_r4k_MP250MHZhwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	srl	k0, k0, 22
	lw	k1, current_pgd			# get pgd pointer
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	zero, CP0_ENTRYLO0
	mtc0	k0, CP0_ENTRYLO0
	mfc0	k0, CP0_INDEX
	srl	k1, k1, 6
	mtc0	zero, CP0_ENTRYLO1
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_MP250MHZhwbug)
#endif

	/* TLB refill, R[23]00 version */
	LEAF(except_vec0_r2300)
	.set	noat
	.set	mips1
	mfc0	k0, CP0_BADVADDR
	lw	k1, current_pgd			# get pgd pointer
	srl	k0, k0, 22
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	and	k0, k0, 0xffc
	addu	k1, k1, k0
	lw	k0, (k1)
	nop
	mtc0	k0, CP0_ENTRYLO0
	mfc0	k1, CP0_EPC
	tlbwr
	jr	k1
	rfe
	END(except_vec0_r2300)


	/* XTLB refill, EXL == 0, R4xx0 cpus only use this... */
	NESTED(except_vec1_generic, 0, sp)
	.set	noat
	.set	mips3
	/* Register saving is delayed as long as we don't know
	 * which registers really need to be saved.
	 */
	mfc0	k1, CP0_CONTEXT
	dsra	k1, 1
	lwu	k0,  (k1)		# May cause another exception
	lwu	k1, 4(k1)
	dsrl	k0, 6			# Convert to EntryLo format
	dsrl	k1, 6			# Convert to EntryLo format
	dmtc0	k0, CP0_ENTRYLO0
	dmtc0	k1, CP0_ENTRYLO1
	nop				# Needed for R4[04]00 pipeline
	tlbwr
	nop				# Needed for R4[04]00 pipeline
	nop
	nop
	eret
	nop				/* Workaround for R4000 bug. */
	eret
	END(except_vec1_generic)

	/* Cache Error */
	LEAF(except_vec2_generic)
	.set	noat
	.set    mips0
	/*
	 * This is a very bad place to be.  Our cache error
	 * detection has triggered.  If we have write-back data
	 * in the cache, we may not be able to recover.  As a
	 * first-order desperate measure, turn off KSEG0 cacheing.
	 */
	mfc0	k0,CP0_CONFIG
	li	k1,~CONF_CM_CMASK
	and	k0,k0,k1
	ori	k0,k0,CONF_CM_UNCACHED
	mtc0	k0,CP0_CONFIG
	/* Give it a few cycles to sink in... */
	nop
	nop
	nop

	j	cache_parity_error
	nop
	END(except_vec2_generic)

	/* General exception vector R4000 version. */
	NESTED(except_vec3_r4000, 0, sp)
	.set	noat
	mfc0	k1, CP0_CAUSE
	andi	k1, k1, 0x7c
	li	k0, 31<<2
	beq	k1, k0, handle_vced
	 li	k0, 14<<2
	beq	k1, k0, handle_vcei
	 la	k0, exception_handlers
	addu	k0, k0, k1
	lw	k0, (k0)
	nop
	jr	k0
	 nop

/*
 * Big shit, we now may have two dirty primary cache lines for the same
 * physical address.  We can savely invalidate the line pointed to by
 * c0_badvaddr because after return from this exception handler the load /
 * store will be re-executed.
 */
	.set    mips3
handle_vced:
	mfc0	k0, CP0_BADVADDR
 li k1, -4
 and k0, k1
	mtc0	zero, CP0_TAGLO
	cache	Index_Store_Tag_D,(k0)
	cache	Hit_Writeback_Inv_SD,(k0)
#ifdef CONFIG_PROC_FS
	lui	k0, %hi(vced_count)
	lw	k1, %lo(vced_count)(k0)
	addiu	k1, 1
	sw	k1, %lo(vced_count)(k0)
#endif
	eret

handle_vcei:
	mfc0	k0, CP0_BADVADDR
	cache	Hit_Writeback_Inv_SD,(k0)		# also cleans pi
#ifdef CONFIG_PROC_FS
	lui	k0, %hi(vcei_count)
	lw	k1, %lo(vcei_count)(k0)
	addiu	k1, 1
	sw	k1, %lo(vcei_count)(k0)
#endif
	eret

	END(except_vec3_r4000)
	.set	at

	/* General exception vector. */
	NESTED(except_vec3_generic, 0, sp)
	.set	noat
	.set	mips0
	mfc0	k1, CP0_CAUSE
	la	k0, exception_handlers
	andi	k1, k1, 0x7c
	addu	k0, k0, k1
	lw	k0, (k0)
	nop
	jr	k0
	 nop
	END(except_vec3_generic)
	.set	at

	/*
	 * Special interrupt vector for embedded MIPS.  This is a
	 * dedicated interrupt vector which reduces interrupt processing
	 * overhead.  The jump instruction will be inserted here at
	 * initialization time.  This handler may only be 8 bytes in size!
	 */
	NESTED(except_vec4, 0, sp)
1:	j	1b			/* Dummy, will be replaced */
	 nop
	END(except_vec4)

	/*
	 * SBDDP EJTAG debug exception handler.
	 * The EJTAG debug exception entry point is 0xbfc00480, which
	 * normally is in the boot PROM, so the boot PROM must do a
	 * unconditional jump to this vector.
	 */	
	NESTED(except_vec_ejtag_debug, 0, sp)
	j	ejtag_debug_handler
	 nop
	END(except_vec_ejtag_debug)

	/*
	 * EJTAG debug exception handler.
	 */
	NESTED(ejtag_debug_handler, PT_SIZE, sp)
	.set	noat
	.set	noreorder
	SAVE_ALL
	PRINT("SDBBP EJTAG debug exception - not handled yet, just ignored!\n");
	mfc0	k0, $23         # Get EJTAG Debug register.
	mfc0    k1, $24         # Get DEPC register.
	bgez	k0, 1f
	 addiu	k1, k1, 4	# SBDDP inst. in delay slot.
	addiu	k1, k1, 4
1:	mtc0	k1, $24
	RESTORE_ALL
	.word	0x4200001f      # deret, return EJTAG debug exception.
	 nop
	.set	at
	END(ejtag_debug_handler)

		
/*
 * Kernel entry point
 */
NESTED(kernel_entry, 16, sp)
	.set	noreorder
	/* The following two symbols are used for kernel profiling. */
	EXPORT(stext)
	EXPORT(_stext)

	/*
	 * Stack for kernel and init, current variable
	 */
	la	$28, init_task_union
	addiu	t0, $28, KERNEL_STACK_SIZE-32
	subu	sp, t0, 4*SZREG

	sw	t0, kernelsp
	/* The firmware/bootloader passes argc/argp/envp
	 * to us as arguments.  But clear bss first because
	 * the romvec and other important info is stored there
	 * by prom_init().
	 */
	la	t0, _edata
	sw	zero, (t0)
	la	t1, (_end - 4)
1:
	addiu	t0, 4
	bne	t0, t1, 1b
	 sw	zero, (t0)

	jal	init_arch
	 nop
	END(kernel_entry)


#ifdef CONFIG_SMP

/*
 * SMP slave cpus entry point.  Board specific code
 * for bootstrap calls this function after setting up
 * the stack and gp registers. 
 */
	LEAF(smp_bootstrap)
	.set push
	.set noreorder
	mtc0	zero, CP0_WIRED
	CLI
	mfc0	t0, CP0_STATUS
	li	t1, ~(ST0_CU1|ST0_CU2|ST0_CU3|ST0_BEV); 
	and	t0, t1
	or	t0, (ST0_CU0|ST0_KX|ST0_SX|ST0_FR);
	addiu	a0, zero, 0
	jal	start_secondary
         mtc0	t0, CP0_STATUS
	.set pop
	END(smp_bootstrap)
#endif
	
/*
 * This buffer is reserved for the use of the cache error handler.
 */
		.data
		EXPORT(cache_error_buffer)
		.fill	32*4,1,0

#ifndef CONFIG_SMP
EXPORT(kernelsp)
		PTR	0
EXPORT(current_pgd)
		PTR     0
#else
		/* There's almost certainly a better way to do this with the macros...*/
		.globl kernelsp
		.comm  kernelsp,     NR_CPUS * 8, 8
		.globl current_pgd
		.comm	current_pgd, NR_CPUS * 8, 8
#endif
		.text
		.org	0x1000
EXPORT(swapper_pg_dir)

		.org	0x2000
EXPORT(empty_bad_page)

		.org	0x3000
EXPORT(empty_bad_page_table)

		.org	0x4000
EXPORT(invalid_pte_table)

		.org	0x5000
/* XXX This label is required to keep GAS trying to be too clever ...
   Bug?  */
dummy:
/*
 * Align to 8kb boundary for init_task_union which follows in the
 * .text segment.
 */
		.align	13
