/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com)
 */
#include <linux/config.h>
#include <linux/threads.h>
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/pgtable.h>
#include <asm/stackframe.h>

	.data
	.comm	pgd_current, NR_CPUS * 8, 8

	/*
	 * After this macro runs we have a pointer to the pte of the address
	 * that caused the fault in in PTR.
	 */

	.macro	LOAD_PTE2, ptr, tmp
#ifdef CONFIG_SMP
	mfc0	\tmp, CP0_CONTEXT
	dla	\ptr, pgd_current
	dsrl	\tmp, 23
	daddu	\ptr, \tmp
#else
	dla	\ptr, pgd_current
#endif
	dmfc0	\tmp, CP0_BADVADDR
	ld	\ptr, (\ptr)
	bltz	\tmp, kaddr
	 dsrl	\tmp, (PGDIR_SHIFT-3)		# get pgd offset in bytes
	andi	\tmp, ((PTRS_PER_PGD - 1)<<3)
	daddu	\ptr, \tmp			# add in pgd offset
	dmfc0	\tmp, CP0_BADVADDR
	ld	\ptr, (\ptr)			# get pmd pointer
	dsrl	\tmp, (PMD_SHIFT-3)		# get pmd offset in bytes
	andi	\tmp, ((PTRS_PER_PMD - 1)<<3)
	daddu	\ptr, \tmp			# add in pmd offset
	dmfc0	\tmp, CP0_XCONTEXT
	ld	\ptr, (\ptr)			# get pte pointer
	andi	\tmp, 0xff0			# get pte offset
	daddu	\ptr, \tmp
	.endm

	/*
	 * This places the even/odd pte pair in the page table at the pte
	 * entry pointed to by PTE into ENTRYLO0 and ENTRYLO1.
	 */
	.macro	PTE_RELOAD, pte0, pte1
	dsrl	\pte0, 6			# convert to entrylo0
	dmtc0	\pte0, CP0_ENTRYLO0		# load it
	dsrl	\pte1, 6			# convert to entrylo1
	dmtc0	\pte1, CP0_ENTRYLO1		# load it
	.endm

	.text
	.set	noreorder
	.set	mips3

	.align	5
FEXPORT(except_vec0)
	.set	noat
1:	b	1b
	 nop

	/* TLB refill handler for the R10000.
	 * Attention:  We may only use 32 instructions.
	 */

	.align	5
FEXPORT(except_vec1_r10k)
	.set	noat
	LOAD_PTE2 k1 k0
	ld	k0, 0(k1)			# get even pte
	ld	k1, 8(k1)			# get odd pte
	PTE_RELOAD k0 k1
	nop
	tlbwr
	eret
kaddr:
	dla	k0, handle_vmalloc_address	# MAPPED kernel needs this
	jr	k0
	 nop

	.align	5
FEXPORT(handle_vmalloc_address)
	.set	noat
	/*
	 * First, determine that the address is in/above vmalloc range.
	 */
	dmfc0	k0, CP0_BADVADDR
	dli	k1, VMALLOC_START
	sltu	k1, k0, k1
	bne	k1, zero, not_vmalloc
	 dli	k1, VMALLOC_START

	/*
	 * Now find offset into kptbl.
	 */
	dsubu	k0, k0, k1
	dla	k1, kptbl
	dsrl	k0, (PAGE_SHIFT+1)		# get vpn2
	dsll	k0, 4				# byte offset of pte
	daddu	k1, k1, k0

	/*
	 * Determine that fault address is within vmalloc range.
	 */
	dla	k0, ekptbl
	sltu	k0, k1, k0
	beq	k0, zero, not_vmalloc

	/*
	 * Load cp0 registers.
	 */
	ld	k0, 0(k1)			# get even pte
	ld	k1, 8(k1)			# get odd pte
1:
	PTE_RELOAD k0 k1
	nop
	tlbwr
	eret
not_vmalloc:
	daddu	k0, zero, zero
	daddu	k1, zero, zero
	j	1b
	nop
