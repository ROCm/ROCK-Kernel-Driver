/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * r4xx0.c: R4000 processor variant specific MMU/Cache routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997,  1998,  1999,  2000 Ralf Baechle ralf@gnu.org
 */
#include <linux/config.h>
#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/cacheops.h>
#include <asm/mipsregs.h>
#include <asm/offset.h>

#ifdef CONFIG_64BIT_PHYS_ADDR
#define PGD_SIZE	0x2000
#else
#define PGD_SIZE	0x1000
#endif

	.text
	.set	noat

/*
 * Zero an entire page.  Basically a simple unrolled loop should do the
 * job but we want more performance by saving memory bus bandwidth.  We
 * have five flavours of the routine available for:
 *
 * - 16byte cachelines and no second level cache
 * - 32byte cachelines second level cache
 * - a version which handles the buggy R4600 v1.x
 * - a version which handles the buggy R4600 v2.0
 * - Finally a last version without fancy cache games for the SC and MC
 *   versions of R4000 and R4400.
 */

LEAF(r4k_clear_page32_d16)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_D, (a0)
	sw	zero, (a0)
	sw	zero, 4(a0)
	sw	zero, 8(a0)
	sw	zero, 12(a0)
	addiu	a0, 32
	cache	Create_Dirty_Excl_D, -16(a0)
	sw	zero, -16(a0)
	sw	zero, -12(a0)
	sw	zero, -8(a0)
	sw	zero, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page32_d16)

LEAF(r4k_clear_page32_d32)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_D, (a0)
	sw	zero, (a0)
	sw	zero, 4(a0)
	sw	zero, 8(a0)
	sw	zero, 12(a0)
	addiu	a0, 32
	sw	zero, -16(a0)
	sw	zero, -12(a0)
	sw	zero, -8(a0)
	sw	zero, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page32_d32)

LEAF(r4k_clear_page_d16)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_D, (a0)
	sd	zero, (a0)
	sd	zero, 8(a0)
	cache	Create_Dirty_Excl_D, 16(a0)
	sd	zero, 16(a0)
	sd	zero, 24(a0)
	addiu	a0, 64
	cache	Create_Dirty_Excl_D, -32(a0)
	sd	zero, -32(a0)
	sd	zero, -24(a0)
	cache	Create_Dirty_Excl_D, -16(a0)
	sd	zero, -16(a0)
	sd	zero, -8(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page_d16)

LEAF(r4k_clear_page_d32)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_D, (a0)
	sd	zero, (a0)
	sd	zero, 8(a0)
	sd	zero, 16(a0)
	sd	zero, 24(a0)
	addiu	a0, 64
	cache	Create_Dirty_Excl_D, -32(a0)
	sd	zero, -32(a0)
	sd	zero, -24(a0)
	sd	zero, -16(a0)
	sd	zero, -8(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page_d32)

/*
 * This flavour of r4k_clear_page is for the R4600 V1.x.  Cite from the
 * IDT R4600 V1.7 errata:
 *
 *  18. The CACHE instructions Hit_Writeback_Invalidate_D, Hit_Writeback_D,
 *      Hit_Invalidate_D and Create_Dirty_Excl_D should only be
 *      executed if there is no other dcache activity. If the dcache is
 *      accessed for another instruction immeidately preceding when these
 *      cache instructions are executing, it is possible that the dcache
 *      tag match outputs used by these cache instructions will be
 *      incorrect. These cache instructions should be preceded by at least
 *      four instructions that are not any kind of load or store
 *      instruction.
 *
 *      This is not allowed:    lw
 *                              nop
 *                              nop
 *                              nop
 *                              cache       Hit_Writeback_Invalidate_D
 *
 *      This is allowed:        lw
 *                              nop
 *                              nop
 *                              nop
 *                              nop
 *                              cache       Hit_Writeback_Invalidate_D
 */

LEAF(r4k_clear_page_r4600_v1)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	nop
	nop
	nop
	nop
	cache	Create_Dirty_Excl_D, (a0)
	sd	zero, (a0)
	sd	zero, 8(a0)
	sd	zero, 16(a0)
	sd	zero, 24(a0)
	addiu	a0, 64
	nop
	nop
	nop
	cache	Create_Dirty_Excl_D, -32(a0)
	sd	zero, -32(a0)
	sd	zero, -24(a0)
	sd	zero, -16(a0)
	sd	zero, -8(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page_r4600_v1)

LEAF(r4k_clear_page_r4600_v2)
	.set	mips3
	mfc0	a1, CP0_STATUS
	ori	AT, a1, 1
	xori	AT, 1
	mtc0	AT, CP0_STATUS
	nop
	nop
	nop

	.set	volatile
	la	AT, KSEG1
	lw	zero, (AT)
	.set	novolatile

	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_D, (a0)
	sd	zero, (a0)
	sd	zero, 8(a0)
	sd	zero, 16(a0)
	sd	zero, 24(a0)
	addiu	a0, 64
	cache	Create_Dirty_Excl_D, -32(a0)
	sd	zero, -32(a0)
	sd	zero, -24(a0)
	sd	zero, -16(a0)
	sd	zero, -8(a0)
	bne	AT, a0, 1b

	mfc0	AT, CP0_STATUS			# local_irq_restore
	andi	a1, 1
	ori	AT, 1
	xori	AT, 1
	or	a1, AT
	mtc0	a1, CP0_STATUS
	nop
	nop
	nop

	jr	ra
	END(r4k_clear_page_r4600_v2)

/*
 * The next 4 versions are optimized for all possible scache configurations
 * of the SC / MC versions of R4000 and R4400 ...
 *
 * Todo: For even better performance we should have a routine optimized for
 * every legal combination of dcache / scache linesize.  When I (Ralf) tried
 * this the kernel crashed shortly after mounting the root filesystem.  CPU
 * bug?  Weirdo cache instruction semantics?
 */

LEAF(r4k_clear_page_s16)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_SD, (a0)
	sd	zero, (a0)
	sd	zero, 8(a0)
	cache	Create_Dirty_Excl_SD, 16(a0)
	sd	zero, 16(a0)
	sd	zero, 24(a0)
	addiu	a0, 64
	cache	Create_Dirty_Excl_SD, -32(a0)
	sd	zero, -32(a0)
	sd	zero, -24(a0)
	cache	Create_Dirty_Excl_SD, -16(a0)
	sd	zero, -16(a0)
	sd	zero, -8(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page_s16)

LEAF(r4k_clear_page_s32)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_SD, (a0)
	sd	zero, (a0)
	sd	zero, 8(a0)
	sd	zero, 16(a0)
	sd	zero, 24(a0)
	addiu	a0, 64
	cache	Create_Dirty_Excl_SD, -32(a0)
	sd	zero, -32(a0)
	sd	zero, -24(a0)
	sd	zero, -16(a0)
	sd	zero, -8(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page_s32)

LEAF(r4k_clear_page_s64)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_SD, (a0)
	sd	zero, (a0)
	sd	zero, 8(a0)
	sd	zero, 16(a0)
	sd	zero, 24(a0)
	addiu	a0, 64
	sd	zero, -32(a0)
	sd	zero, -24(a0)
	sd	zero, -16(a0)
	sd	zero, -8(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page_s64)

LEAF(r4k_clear_page_s128)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_SD, (a0)
	sd	zero, (a0)
	sd	zero, 8(a0)
	sd	zero, 16(a0)
	sd	zero, 24(a0)
	sd	zero, 32(a0)
	sd	zero, 40(a0)
	sd	zero, 48(a0)
	sd	zero, 56(a0)
	addiu	a0, 128
	sd	zero, -64(a0)
	sd	zero, -56(a0)
	sd	zero, -48(a0)
	sd	zero, -40(a0)
	sd	zero, -32(a0)
	sd	zero, -24(a0)
	sd	zero, -16(a0)
	sd	zero, -8(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_clear_page_s128)

/*
 * This is suboptimal for 32-bit kernels; we assume that R10000 is only used
 * with 64-bit kernels.  The prefetch offsets have been experimentally tuned
 * an Origin 200.
 */
LEAF(andes_clear_page)
	.set		mips4
	LONG_ADDIU	AT, a0, _PAGE_SIZE
1:	pref		7, 512(a0)
	sd		zero, 0*SZREG(a0)
	sd		zero, 1*SZREG(a0)
	sd		zero, 2*SZREG(a0)
	sd		zero, 3*SZREG(a0)
	LONG_ADDIU	a0, a0, 8*SZREG
	sd		zero, -4*SZREG(a0)
	sd		zero, -3*SZREG(a0)
	sd		zero, -2*SZREG(a0)
	sd		zero, -1*SZREG(a0)
	bne		AT, a0, 1b
	j		ra
	END(andes_clear_page)
	.set		mips0

/*
 * This is still inefficient.  We only can do better if we know the
 * virtual address where the copy will be accessed.
 */

LEAF(r4k_copy_page_d16)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_D, (a0)
	lw	a3, (a1)
	lw	a2, 4(a1)
	lw	v1, 8(a1)
	lw	v0, 12(a1)
	sw	a3, (a0)
	sw	a2, 4(a0)
	sw	v1, 8(a0)
	sw	v0, 12(a0)
	cache	Create_Dirty_Excl_D, 16(a0)
	lw	a3, 16(a1)
	lw	a2, 20(a1)
	lw	v1, 24(a1)
	lw	v0, 28(a1)
	sw	a3, 16(a0)
	sw	a2, 20(a0)
	sw	v1, 24(a0)
	sw	v0, 28(a0)
	cache	Create_Dirty_Excl_D, 32(a0)
	addiu	a0, 64
	addiu	a1, 64
	lw	a3, -32(a1)
	lw	a2, -28(a1)
	lw	v1, -24(a1)
	lw	v0, -20(a1)
	sw	a3, -32(a0)
	sw	a2, -28(a0)
	sw	v1, -24(a0)
	sw	v0, -20(a0)
	cache	Create_Dirty_Excl_D, -16(a0)
	lw	a3, -16(a1)
	lw	a2, -12(a1)
	lw	v1, -8(a1)
	lw	v0, -4(a1)
	sw	a3, -16(a0)
	sw	a2, -12(a0)
	sw	v1, -8(a0)
	sw	v0, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_copy_page_d16)

LEAF(r4k_copy_page_d32)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_D, (a0)
	lw	a3, (a1)
	lw	a2, 4(a1)
	lw	v1, 8(a1)
	lw	v0, 12(a1)
	sw	a3, (a0)
	sw	a2, 4(a0)
	sw	v1, 8(a0)
	sw	v0, 12(a0)
	lw	a3, 16(a1)
	lw	a2, 20(a1)
	lw	v1, 24(a1)
	lw	v0, 28(a1)
	sw	a3, 16(a0)
	sw	a2, 20(a0)
	sw	v1, 24(a0)
	sw	v0, 28(a0)
	cache	Create_Dirty_Excl_D, 32(a0)
	addiu	a0, 64
	addiu	a1, 64
	lw	a3, -32(a1)
	lw	a2, -28(a1)
	lw	v1, -24(a1)
	lw	v0, -20(a1)
	sw	a3, -32(a0)
	sw	a2, -28(a0)
	sw	v1, -24(a0)
	sw	v0, -20(a0)
	lw	a3, -16(a1)
	lw	a2, -12(a1)
	lw	v1, -8(a1)
	lw	v0, -4(a1)
	sw	a3, -16(a0)
	sw	a2, -12(a0)
	sw	v1, -8(a0)
	sw	v0, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_copy_page_d32)

/*
 * Again a special version for the R4600 V1.x
 */

LEAF(r4k_copy_page_r4600_v1)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	nop
	nop
	nop
	nop
	cache	Create_Dirty_Excl_D, (a0)
	lw	a3, (a1)
	lw	a2, 4(a1)
	lw	v1, 8(a1)
	lw	v0, 12(a1)
	sw	a3, (a0)
	sw	a2, 4(a0)
	sw	v1, 8(a0)
	sw	v0, 12(a0)
	lw	a3, 16(a1)
	lw	a2, 20(a1)
	lw	v1, 24(a1)
	lw	v0, 28(a1)
	sw	a3, 16(a0)
	sw	a2, 20(a0)
	sw	v1, 24(a0)
	sw	v0, 28(a0)
	nop
	nop
	nop
	nop
	cache	Create_Dirty_Excl_D, 32(a0)
	addiu	a0, 64
	addiu	a1, 64
	lw	a3, -32(a1)
	lw	a2, -28(a1)
	lw	v1, -24(a1)
	lw	v0, -20(a1)
	sw	a3, -32(a0)
	sw	a2, -28(a0)
	sw	v1, -24(a0)
	sw	v0, -20(a0)
	lw	a3, -16(a1)
	lw	a2, -12(a1)
	lw	v1, -8(a1)
	lw	v0, -4(a1)
	sw	a3, -16(a0)
	sw	a2, -12(a0)
	sw	v1, -8(a0)
	sw	v0, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_copy_page_r4600_v1)

LEAF(r4k_copy_page_r4600_v2)
	.set	mips3
	mfc0	v1, CP0_STATUS
	ori	AT, v1, 1
	xori	AT, 1

	mtc0	AT, CP0_STATUS
	nop
	nop
	nop

	addiu	AT, a0, _PAGE_SIZE
1:	nop
	nop
	nop
	nop
	cache	Create_Dirty_Excl_D, (a0)
	lw	t1, (a1)
	lw	t0, 4(a1)
	lw	a3, 8(a1)
	lw	a2, 12(a1)
	sw	t1, (a0)
	sw	t0, 4(a0)
	sw	a3, 8(a0)
	sw	a2, 12(a0)
	lw	t1, 16(a1)
	lw	t0, 20(a1)
	lw	a3, 24(a1)
	lw	a2, 28(a1)
	sw	t1, 16(a0)
	sw	t0, 20(a0)
	sw	a3, 24(a0)
	sw	a2, 28(a0)
	nop
	nop
	nop
	nop
	cache	Create_Dirty_Excl_D, 32(a0)
	addiu	a0, 64
	addiu	a1, 64
	lw	t1, -32(a1)
	lw	t0, -28(a1)
	lw	a3, -24(a1)
	lw	a2, -20(a1)
	sw	t1, -32(a0)
	sw	t0, -28(a0)
	sw	a3, -24(a0)
	sw	a2, -20(a0)
	lw	t1, -16(a1)
	lw	t0, -12(a1)
	lw	a3, -8(a1)
	lw	a2, -4(a1)
	sw	t1, -16(a0)
	sw	t0, -12(a0)
	sw	a3, -8(a0)
	sw	a2, -4(a0)
	bne	AT, a0, 1b

	mfc0	AT, CP0_STATUS			# local_irq_restore
	andi	v1, 1
	ori	AT, 1
	xori	AT, 1
	or	v1, AT
	mtc0	v1, CP0_STATUS
	nop
	nop
	nop
	jr	ra
	END(r4k_copy_page_r4600_v2)

/*
 * These are for R4000SC / R4400MC
 */

LEAF(r4k_copy_page_s16)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_SD, (a0)
	lw	a3, (a1)
	lw	a2, 4(a1)
	lw	v1, 8(a1)
	lw	v0, 12(a1)
	sw	a3, (a0)
	sw	a2, 4(a0)
	sw	v1, 8(a0)
	sw	v0, 12(a0)
	cache	Create_Dirty_Excl_SD, 16(a0)
	lw	a3, 16(a1)
	lw	a2, 20(a1)
	lw	v1, 24(a1)
	lw	v0, 28(a1)
	sw	a3, 16(a0)
	sw	a2, 20(a0)
	sw	v1, 24(a0)
	sw	v0, 28(a0)
	cache	Create_Dirty_Excl_SD, 32(a0)
	addiu	a0, 64
	addiu	a1, 64
	lw	a3, -32(a1)
	lw	a2, -28(a1)
	lw	v1, -24(a1)
	lw	v0, -20(a1)
	sw	a3, -32(a0)
	sw	a2, -28(a0)
	sw	v1, -24(a0)
	sw	v0, -20(a0)
	cache	Create_Dirty_Excl_SD, -16(a0)
	lw	a3, -16(a1)
	lw	a2, -12(a1)
	lw	v1, -8(a1)
	lw	v0, -4(a1)
	sw	a3, -16(a0)
	sw	a2, -12(a0)
	sw	v1, -8(a0)
	sw	v0, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_copy_page_s16)

LEAF(r4k_copy_page_s32)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_SD, (a0)
	lw	a3, (a1)
	lw	a2, 4(a1)
	lw	v1, 8(a1)
	lw	v0, 12(a1)
	sw	a3, (a0)
	sw	a2, 4(a0)
	sw	v1, 8(a0)
	sw	v0, 12(a0)
	lw	a3, 16(a1)
	lw	a2, 20(a1)
	lw	v1, 24(a1)
	lw	v0, 28(a1)
	sw	a3, 16(a0)
	sw	a2, 20(a0)
	sw	v1, 24(a0)
	sw	v0, 28(a0)
	cache	Create_Dirty_Excl_SD, 32(a0)
	addiu	a0, 64
	addiu	a1, 64
	lw	a3, -32(a1)
	lw	a2, -28(a1)
	lw	v1, -24(a1)
	lw	v0, -20(a1)
	sw	a3, -32(a0)
	sw	a2, -28(a0)
	sw	v1, -24(a0)
	sw	v0, -20(a0)
	lw	a3, -16(a1)
	lw	a2, -12(a1)
	lw	v1, -8(a1)
	lw	v0, -4(a1)
	sw	a3, -16(a0)
	sw	a2, -12(a0)
	sw	v1, -8(a0)
	sw	v0, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_copy_page_s32)

LEAF(r4k_copy_page_s64)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_SD, (a0)
	lw	a3, (a1)
	lw	a2, 4(a1)
	lw	v1, 8(a1)
	lw	v0, 12(a1)
	sw	a3, (a0)
	sw	a2, 4(a0)
	sw	v1, 8(a0)
	sw	v0, 12(a0)
	lw	a3, 16(a1)
	lw	a2, 20(a1)
	lw	v1, 24(a1)
	lw	v0, 28(a1)
	sw	a3, 16(a0)
	sw	a2, 20(a0)
	sw	v1, 24(a0)
	sw	v0, 28(a0)
	addiu	a0, 64
	addiu	a1, 64
	lw	a3, -32(a1)
	lw	a2, -28(a1)
	lw	v1, -24(a1)
	lw	v0, -20(a1)
	sw	a3, -32(a0)
	sw	a2, -28(a0)
	sw	v1, -24(a0)
	sw	v0, -20(a0)
	lw	a3, -16(a1)
	lw	a2, -12(a1)
	lw	v1, -8(a1)
	lw	v0, -4(a1)
	sw	a3, -16(a0)
	sw	a2, -12(a0)
	sw	v1, -8(a0)
	sw	v0, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_copy_page_s64)

LEAF(r4k_copy_page_s128)
	.set	mips3
	addiu	AT, a0, _PAGE_SIZE
1:	cache	Create_Dirty_Excl_SD, (a0)
	lw	a3, (a1)
	lw	a2, 4(a1)
	lw	v1, 8(a1)
	lw	v0, 12(a1)
	sw	a3, (a0)
	sw	a2, 4(a0)
	sw	v1, 8(a0)
	sw	v0, 12(a0)
	lw	a3, 16(a1)
	lw	a2, 20(a1)
	lw	v1, 24(a1)
	lw	v0, 28(a1)
	sw	a3, 16(a0)
	sw	a2, 20(a0)
	sw	v1, 24(a0)
	sw	v0, 28(a0)
	lw	a3, 32(a1)
	lw	a2, 36(a1)
	lw	v1, 40(a1)
	lw	v0, 44(a1)
	sw	a3, 32(a0)
	sw	a2, 36(a0)
	sw	v1, 40(a0)
	sw	v0, 44(a0)
	lw	a3, 48(a1)
	lw	a2, 52(a1)
	lw	v1, 56(a1)
	lw	v0, 60(a1)
	sw	a3, 48(a0)
	sw	a2, 52(a0)
	sw	v1, 56(a0)
	sw	v0, 60(a0)
	addiu	a0, 128
	addiu	a1, 128
	lw	a3, -64(a1)
	lw	a2, -60(a1)
	lw	v1, -56(a1)
	lw	v0, -52(a1)
	sw	a3, -64(a0)
	sw	a2, -60(a0)
	sw	v1, -56(a0)
	sw	v0, -52(a0)
	lw	a3, -48(a1)
	lw	a2, -44(a1)
	lw	v1, -40(a1)
	lw	v0, -36(a1)
	sw	a3, -48(a0)
	sw	a2, -44(a0)
	sw	v1, -40(a0)
	sw	v0, -36(a0)
	lw	a3, -32(a1)
	lw	a2, -28(a1)
	lw	v1, -24(a1)
	lw	v0, -20(a1)
	sw	a3, -32(a0)
	sw	a2, -28(a0)
	sw	v1, -24(a0)
	sw	v0, -20(a0)
	lw	a3, -16(a1)
	lw	a2, -12(a1)
	lw	v1, -8(a1)
	lw	v0, -4(a1)
	sw	a3, -16(a0)
	sw	a2, -12(a0)
	sw	v1, -8(a0)
	sw	v0, -4(a0)
	bne	AT, a0, 1b
	jr	ra
	END(r4k_copy_page_s128)


	.text
	.set	mips4
	.set	noat


/*
 * This is suboptimal for 32-bit kernels; we assume that R10000 is only used
 * with 64-bit kernels.  The prefetch offsets have been experimentally tuned
 * an Origin 200.
 */
LEAF(andes_copy_page)
	.set		mips4
	LONG_ADDIU	AT, a0, _PAGE_SIZE
1:	pref		0, 2*128(a1)
	pref		1, 2*128(a0)
	LONG_L		a3, 0*SZREG(a1)
	LONG_L		a2, 1*SZREG(a1)
	LONG_L		v1, 2*SZREG(a1)
	LONG_L		v0, 3*SZREG(a1)
	LONG_S		a3, 0*SZREG(a0)
	LONG_S		a2, 1*SZREG(a0)
	LONG_S		v1, 2*SZREG(a0)
	LONG_S		v0, 3*SZREG(a0)
	LONG_ADDIU	a0, a0, 8*SZREG
	LONG_ADDIU	a1, a1, 8*SZREG
	LONG_L		a3, -4*SZREG(a1)
	LONG_L		a2, -3*SZREG(a1)
	LONG_L		v1, -2*SZREG(a1)
	LONG_L		v0, -1*SZREG(a1)
	LONG_S		a3, -4*SZREG(a0)
	LONG_S		a2, -3*SZREG(a0)
	LONG_S		v1, -2*SZREG(a0)
	LONG_S		v0, -1*SZREG(a0)
	bne		AT, a0,1b
	j		ra
	END(andes_copy_page)
	.set		mips0
