/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Head.S contains the MIPS exception handler and startup code.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#define __ASSEMBLY__
#include <linux/config.h>
#include <linux/init.h>
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/processor.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/pgtable.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/klkernvars.h>

	.macro	ARC64_TWIDDLE_PC
#if defined(CONFIG_ARC64) || defined(CONFIG_MAPPED_KERNEL)
	/* We get launched at a XKPHYS address but the kernel is linked to
	   run at a KSEG0 address, so jump there.  */
	la	t0, \@f
	jr	t0
\@:
#endif
	.endm

	/*
	 * outputs the local nasid into t1.
	 */
	.macro GET_NASID_ASM
	dli	t1, LOCAL_HUB_ADDR(NI_STATUS_REV_ID)
	ld	t1, (t1)
	and	t1, NSRI_NODEID_MASK
	dsrl	t1, NSRI_NODEID_SHFT
	.endm

	/*
	 * inputs are the text nasid in t1, data nasid in t2.
	 */
	.macro MAPPED_KERNEL_SETUP_TLB
#ifdef CONFIG_MAPPED_KERNEL
	/*
	 * This needs to read the nasid - assume 0 for now.
	 * Drop in 0xffffffffc0000000 in tlbhi, 0+VG in tlblo_0,
	 * 0+DVG in tlblo_1.
	 */
	dli	t0, 0xffffffffc0000000
	dmtc0	t0, CP0_ENTRYHI
	li	t0, 0x1c000		# Offset of text into node memory
	dsll	t1, NASID_SHFT		# Shift text nasid into place
	dsll	t2, NASID_SHFT		# Same for data nasid
	or	t1, t1, t0		# Physical load address of kernel text
	or	t2, t2, t0		# Physical load address of kernel data
	dsrl	t1, 12			# 4K pfn
	dsrl	t2, 12			# 4K pfn
	dsll	t1, 6			# Get pfn into place
	dsll	t2, 6			# Get pfn into place
	li	t0, ((_PAGE_GLOBAL|_PAGE_VALID| _CACHE_CACHABLE_COW) >> 6)
	or	t0, t0, t1
	mtc0	t0, CP0_ENTRYLO0	# physaddr, VG, cach exlwr
	li	t0, ((_PAGE_GLOBAL|_PAGE_VALID| _PAGE_DIRTY|_CACHE_CACHABLE_COW) >> 6)
	or	t0, t0, t2
	mtc0	t0, CP0_ENTRYLO1	# physaddr, DVG, cach exlwr
	li	t0, 0x1ffe000		# MAPPED_KERN_TLBMASK, TLBPGMASK_16M
	mtc0	t0, CP0_PAGEMASK
	li	t0, 0			# KMAP_INX
	mtc0	t0, CP0_INDEX
	li	t0, 1
	mtc0	t0, CP0_WIRED
	tlbwi
#else
	mtc0	zero, CP0_WIRED
#endif
	.endm

	.text

EXPORT(stext)					# used for profiling
EXPORT(_stext)

	__INIT

NESTED(kernel_entry, 16, sp)			# kernel entry point

	ori	sp, 0xf				# align stack on 16 byte.
	xori	sp, 0xf

	GET_NASID_ASM
	move	t2, t1				# text and data are here
	MAPPED_KERNEL_SETUP_TLB

	ARC64_TWIDDLE_PC

	CLI					# disable interrupts

	mfc0	t0, CP0_STATUS
	/*
	 * On IP27, I am seeing the TS bit set when the
	 * kernel is loaded. Maybe because the kernel is
	 * in ckseg0 and not xkphys? Clear it anyway ...
	 */
	li	t1, ~(ST0_TS|ST0_CU1|ST0_CU2|ST0_CU3)
	and	t0, t1
	or	t0, (ST0_CU0|ST0_KX|ST0_SX|ST0_FR) # Bogosity: cu0 indicates kernel
	mtc0	t0, CP0_STATUS			   # thread in copy_thread.

	la	$28, init_task_union		# init current pointer
	daddiu	t0, $28, KERNEL_STACK_SIZE-32
	sd	t0, kernelsp
	dsubu	sp, t0, 4*SZREG			# init stack pointer
	move	t0, $28
#ifdef CONFIG_SMP
	mtc0	t0, CP0_WATCHLO
	dsrl32	t0, t0, 0
	mtc0	t0, CP0_WATCHHI
#endif
	/* Note that all firmware passed argument registers still
	   have their values.  */
	jal	prom_init			# initialize firmware

	jal	start_kernel
1:	b	1b				# just in case ...
	END(kernel_entry)

NESTED(bootstrap, 16, sp)
	GET_NASID_ASM
	li	t0, KLDIR_OFFSET + (KLI_KERN_VARS * KLDIR_ENT_SIZE) + KLDIR_OFF_POINTER + K0BASE
	dsll	t1, NASID_SHFT
	or	t0, t0, t1
	ld	t0, 0(t0)			# t0 points to kern_vars struct
	lh	t1, KV_RO_NASID_OFFSET(t0)
	lh	t2, KV_RW_NASID_OFFSET(t0)
	MAPPED_KERNEL_SETUP_TLB
	ARC64_TWIDDLE_PC
	CLI
	mfc0	t0, CP0_STATUS
	li	t1, ~(ST0_CU1|ST0_CU2|ST0_CU3)
	and	t0, t1
	or	t0, (ST0_CU0|ST0_KX|ST0_SX|ST0_FR) # Bogosity: cu0 indicates kernel
	mtc0	t0, CP0_STATUS			   # thread in copy_thread.
	jal	cboot
	END(bootstrap)

	__FINIT

	.comm	kernelsp, 8, 8			# current stackpointer

#undef PAGE_SIZE
#define PAGE_SIZE	0x1000

	.macro	page name, order=0
	.globl	\name
\name:	.size	\name, (PAGE_SIZE << \order)
	.org	. + (PAGE_SIZE << \order)
	.type	\name, @object
	.endm

	.data
	.align	12

	page	swapper_pg_dir, 1
	page	invalid_pte_table, 0
	page	invalid_pmd_table, 1
	page	empty_bad_page_table, 0
	page	empty_bad_pmd_table, 1
	page	kptbl, KPTBL_PAGE_ORDER
	.globl	ekptbl
	page	kpmdtbl, 0
ekptbl:
