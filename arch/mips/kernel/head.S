/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2003 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Modified for DECStation and hence R3000 support by Paul M. Antoine
 * Further modifications by David S. Miller and Harald Koerfgen
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Kevin Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/threads.h>

#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#ifdef CONFIG_SGI_IP27
#include <asm/sn/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/klkernvars.h>
#endif

	.macro	ARC64_TWIDDLE_PC
#if defined(CONFIG_ARC64) || defined(CONFIG_MAPPED_KERNEL)
	/* We get launched at a XKPHYS address but the kernel is linked to
	   run at a KSEG0 address, so jump there.  */
	PTR_LA	t0, \@f
	jr	t0
\@:
#endif
	.endm

#ifdef CONFIG_SGI_IP27
	/*
	 * outputs the local nasid into res.  IP27 stuff.
	 */
	.macro GET_NASID_ASM res
	dli	\res, LOCAL_HUB_ADDR(NI_STATUS_REV_ID)
	ld	\res, (\res)
	and	\res, NSRI_NODEID_MASK
	dsrl	\res, NSRI_NODEID_SHFT
	.endm
#endif /* CONFIG_SGI_IP27 */

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

	/*
	 * Reserved space for exception handlers.
	 * Necessary for machines which link their kernels at KSEG0.
	 */
	.fill	0x400

EXPORT(stext)					# used for profiling
EXPORT(_stext)

	__INIT

NESTED(kernel_entry, 16, sp)			# kernel entry point
#ifdef CONFIG_SGI_IP27
	GET_NASID_ASM	t1
	move	t2, t1				# text and data are here
	MAPPED_KERNEL_SETUP_TLB
#endif /* IP27 */

	ARC64_TWIDDLE_PC

	CLI					# disable interrupts

	PTR_LA		$28, init_thread_union
	PTR_ADDIU	sp, $28, _THREAD_SIZE - 32
	set_saved_sp	sp, t0, t1
	PTR_SUBU	sp, 4 * SZREG		# init stack pointer

	/*
	 * The firmware/bootloader passes argc/argp/envp
	 * to us as arguments.  But clear bss first because
	 * the romvec and other important info is stored there
	 * by prom_init().
	 */
	PTR_LA		t0, __bss_start
	LONG_S		zero, (t0)
	PTR_LA		t1, __bss_stop - LONGSIZE
1:
	PTR_ADDIU	t0, LONGSIZE
	LONG_S		zero, (t0)
	bne		t0, t1, 1b

	jal		init_arch
	END(kernel_entry)

#ifdef CONFIG_SMP
/*
 * SMP slave cpus entry point.  Board specific code for bootstrap calls this
 * function after setting up the stack and gp registers.
 */
NESTED(smp_bootstrap, 16, sp)
#ifdef CONFIG_SGI_IP27
	GET_NASID_ASM	t1
	li	t0, KLDIR_OFFSET + (KLI_KERN_VARS * KLDIR_ENT_SIZE) + \
		    KLDIR_OFF_POINTER + K0BASE
	dsll	t1, NASID_SHFT
	or	t0, t0, t1
	ld	t0, 0(t0)			# t0 points to kern_vars struct
	lh	t1, KV_RO_NASID_OFFSET(t0)
	lh	t2, KV_RW_NASID_OFFSET(t0)
	MAPPED_KERNEL_SETUP_TLB
	ARC64_TWIDDLE_PC
#endif /* CONFIG_SGI_IP27 */

	CLI

#ifdef CONFIG_MIPS64
	/*
	 * For the moment set ST0_KU so the CPU will not spit fire when
	 * executing 64-bit instructions.  The full initialization of the
	 * CPU's status register is done later in per_cpu_trap_init().
	 */
	mfc0	t0, CP0_STATUS
	or	t0, ST0_KX
	mtc0	t0, CP0_STATUS
#endif
	jal	start_secondary
	END(smp_bootstrap)
#endif /* CONFIG_SMP */

	__FINIT

	.comm	kernelsp,    NR_CPUS * 8, 8
	.comm	pgd_current, NR_CPUS * 8, 8

	.macro	page name, order=0
	.globl	\name
\name:	.size	\name, (_PAGE_SIZE << \order)
	.org	. + (_PAGE_SIZE << \order)
	.type	\name, @object
	.endm

	.data
	.align	PAGE_SHIFT

#ifdef CONFIG_MIPS32
	/*
	 * Here we only have a two-level pagetable structure ...
	 */
	page	swapper_pg_dir, _PGD_ORDER
	page	invalid_pte_table, _PTE_ORDER
#endif
#ifdef CONFIG_MIPS64
	/*
	 * ... but on 64-bit we've got three-level pagetables with a
	 * slightly different layout ...
	 */
	page	swapper_pg_dir, _PGD_ORDER
	page	invalid_pmd_table, _PMD_ORDER
	page	invalid_pte_table, _PTE_ORDER

	/*
	 * 64-bit kernel mappings are really screwed up ...
	 */
	page	kptbl, _PGD_ORDER
	.globl	ekptbl
	page	kpmdtbl, 0
ekptbl:
#endif
