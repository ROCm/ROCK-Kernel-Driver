/*
 *  linux/arch/arm/mm/proc-xscale.S
 *
 *  Author:	Nicolas Pitre
 *  Created:	November 2000
 *  Copyright:	(C) 2000, 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MMU functions for the Intel XScale CPUs
 *
 * 2001 Aug 21:
 *	some contributions by Brett Gaines <brett.w.gaines@intel.com>
 *	Copyright 2001 by Intel Corp.
 *
 * 2001 Sep 08:
 *	Completely revisited, many important fixes
 *	Nicolas Pitre <nico@cam.org>
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/constants.h>
#include <asm/procinfo.h>
#include <asm/hardware.h>
#include <asm/proc/pgtable.h>

/*
 * This is the maximum size of an area which will be flushed.  If the area
 * is larger than this, then we flush the whole cache
 */
#define MAX_AREA_SIZE	32768

/*
 * the cache line size of the I and D cache
 */
#define CACHELINESIZE	32

/*
 * the size of the data cache
 */
#define CACHESIZE	32768

/*
 * and the page size
 */
#define PAGESIZE	4096

/*
 * Virtual address used to allocate the cache when flushed
 *
 * This must be an address range which is _never_ used.  It should
 * apparently have a mapping in the corresponding page table for
 * compatibility with future CPUs that _could_ require it.  For instance we
 * don't care.
 *
 * This must be aligned on a 2*CACHESIZE boundary.  The code selects one of
 * the 2 areas in alternance each time the clean_d_cache macro is used.
 * Without this the XScale core exhibits cache eviction problems and no one
 * knows why.
 *
 * Reminder: the vector table is located at 0xffff0000-0xffff0fff.
 */
#define CLEAN_ADDR	0xfffe0000

/*
 * This macro is used to wait for a CP15 write and is needed
 * when we have to ensure that the last operation to the co-pro
 * was completed before continuing with operation.
 */
	.macro	cpwait, rd
	mrc	p15, 0, \rd, c2, c0, 0		@ arbitrary read of cp15
	mov	\rd, \rd			@ wait for completion
	sub 	pc, pc, #4			@ flush instruction pipeline
	.endm

	.macro	cpwait_ret, lr, rd
	mrc	p15, 0, \rd, c2, c0, 0		@ arbitrary read of cp15
	sub	pc, \lr, \rd, LSR #32		@ wait for completion and
						@ flush instruction pipeline
	.endm

/*
 * This macro cleans the entire dcache using line allocate.
 * The main loop has been unrolled to reduce loop overhead.
 * rd and rs are two scratch registers.
 */
	.macro  clean_d_cache, rd, rs
	ldr	\rs, =clean_addr
	ldr	\rd, [\rs]
	eor	\rd, \rd, #CACHESIZE
	str	\rd, [\rs]
	add	\rs, \rd, #CACHESIZE
1:	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	teq	\rd, \rs
	bne	1b
	.endm

	.data
clean_addr:	.word	CLEAN_ADDR

	.text

/*
 * cpu_xscale_check_bugs()
 */
ENTRY(cpu_xscale_check_bugs)
	mrs	ip, cpsr
	bic	ip, ip, #PSR_F_BIT
	msr	cpsr, ip
	mov	pc, lr

/*
 * cpu_xscale_proc_init()
 *
 * Nothing too exciting at the moment
 */
ENTRY(cpu_xscale_proc_init)
	mov	pc, lr

/*
 * cpu_xscale_proc_fin()
 */
ENTRY(cpu_xscale_proc_fin)
	str	lr, [sp, #-4]!
	mov	r0, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r0
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1800			@ ...IZ...........
	bic	r0, r0, #0x0006			@ .............CA.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	bl	cpu_xscale_cache_clean_invalidate_all	@ clean caches
	ldr	pc, [sp], #4

/*
 * cpu_xscale_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_xscale_reset)
	mov	r1, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r1			@ reset CPSR
	mrc	p15, 0, r1, c1, c0, 0		@ ctrl register
	bic	r1, r1, #0x0086			@ ........B....CA.
	bic	r1, r1, #0x3900			@ ..VIZ..S........
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches & BTB
	bic	r1, r1, #0x0001			@ ...............M
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	@ CAUTION: MMU turned off from this point. We count on the pipeline
	@ already containing those two last instructions to survive.
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	mov	pc, r0

/*
 * cpu_xscale_do_idle(type)
 *
 * Cause the processor to idle
 *
 * type:
 *   0 = slow idle
 *   1 = fast idle
 *   2 = switch to slow processor clock
 *   3 = switch to fast processor clock
 *
 * For now we do nothing but go to idle mode for every case
 *
 * XScale supports clock switching, but using idle mode support
 * allows external hardware to react to system state changes.
 */
	.align	5

ENTRY(cpu_xscale_do_idle)
	mov	r0, #1
	mcr	p14, 0, r0, c7, c0, 0		@ Go to IDLE
	mov	pc, lr

/* ================================= CACHE ================================ */

/*
 * cpu_xscale_cache_clean_invalidate_all (void)
 *
 * clean and invalidate all cache lines
 *
 * Note:
 *  1. We should preserve r0 at all times.
 *  2. Even if this function implies cache "invalidation" by its name,
 *     we don't need to actually use explicit invalidation operations
 *     since the goal is to discard all valid references from the cache
 *     and the cleaning of it already has that effect.
 *  3. Because of 2 above and the fact that kernel space memory is always
 *     coherent across task switches there is no need to worry about
 *     inconsistencies due to interrupts, ence no irq disabling.
 */
	.align	5
ENTRY(cpu_xscale_cache_clean_invalidate_all)
	mov	r2, #1
cpu_xscale_cache_clean_invalidate_all_r2:
	clean_d_cache r0, r1
	teq	r2, #0
	mcrne	p15, 0, ip, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 * cpu_xscale_cache_clean_invalidate_range(start, end, flags)
 *
 * clean and invalidate all cache lines associated with this area of memory
 *
 * start: Area start address
 * end:   Area end address
 * flags: nonzero for I cache as well
 */
	.align	5
ENTRY(cpu_xscale_cache_clean_invalidate_range)
	bic	r0, r0, #CACHELINESIZE - 1	@ round down to cache line
	sub	r3, r1, r0
	cmp	r3, #MAX_AREA_SIZE
	bhi	cpu_xscale_cache_clean_invalidate_all_r2
1:	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	mcr	p15, 0, r0, c7, c6, 1		@ Invalidate D cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	teq	r2, #0
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	moveq	pc, lr
	sub	r0, r0, r3
1:	mcr	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, ip, c7, c5, 6		@ Invalidate BTB
	mov	pc, lr

/*
 * cpu_xscale_flush_ram_page(page)
 *
 * clean all cache lines associated with this memory page
 *
 * page: page to clean
 */
	.align	5
ENTRY(cpu_xscale_flush_ram_page)
	mov	r1, #PAGESIZE
1:	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	add	r0, r0, #CACHELINESIZE
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	add	r0, r0, #CACHELINESIZE
	subs	r1, r1, #2 * CACHELINESIZE
	bne	1b
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/* ================================ D-CACHE =============================== */

/*
 * cpu_xscale_dcache_invalidate_range(start, end)
 *
 * throw away all D-cached data in specified region without an obligation
 * to write them back.  Note however that on XScale we must clean all
 * entries also due to hardware errata (80200 A0 & A1 only).
 *
 * start: virtual start address
 * end:   virtual end address
 */
	.align	5
ENTRY(cpu_xscale_dcache_invalidate_range)
	mrc	p15, 0, r2, c0, c0, 0		@ Read part no.
	eor	r2, r2, #0x69000000
	eor	r2, r2, #0x00052000		@ 80200 XX part no.
	bics	r2, r2, #0x1			@ Clear LSB in revision field
	moveq	r2, #0
	beq	cpu_xscale_cache_clean_invalidate_range	@ An 80200 A0 or A1

	tst	r0, #CACHELINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	tst	r1, #CACHELINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ Clean D cache line
	bic	r0, r0, #CACHELINESIZE - 1	@ round down to cache line
1:	mcr	p15, 0, r0, c7, c6, 1		@ Invalidate D cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	pc, lr

/*
 * cpu_xscale_dcache_clean_range(start, end)
 *
 * For the specified virtual address range, ensure that all caches contain
 * clean data, such that peripheral accesses to the physical RAM fetch
 * correct data.
 *
 * start: virtual start address
 * end:   virtual end address
 */
	.align	5
ENTRY(cpu_xscale_dcache_clean_range)
	bic	r0, r0, #CACHELINESIZE - 1
	sub	r2, r1, r0
	cmp	r2, #MAX_AREA_SIZE
	movhi	r2, #0
	bhi	cpu_xscale_cache_clean_invalidate_all_r2

1:	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	add	r0, r0, #CACHELINESIZE
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 * cpu_xscale_clean_dcache_page(page)
 *
 * Cleans a single page of dcache so that if we have any future aliased
 * mappings, they will be consistent at the time that they are created.
 *
 * Note:
 *  1. we don't need to flush the write buffer in this case. [really? -Nico]
 *  2. we don't invalidate the entries since when we write the page
 *     out to disk, the entries may get reloaded into the cache.
 */
	.align	5
ENTRY(cpu_xscale_dcache_clean_page)
	mov	r1, #PAGESIZE
1:	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	add	r0, r0, #CACHELINESIZE
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	add	r0, r0, #CACHELINESIZE
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	add	r0, r0, #CACHELINESIZE
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	add	r0, r0, #CACHELINESIZE
	subs	r1, r1, #4 * CACHELINESIZE
	bne	1b
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 * cpu_xscale_dcache_clean_entry(addr)
 *
 * Clean the specified entry of any caches such that the MMU
 * translation fetches will obtain correct data.
 *
 * addr: cache-unaligned virtual address
 */
	.align	5
ENTRY(cpu_xscale_dcache_clean_entry)
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/* ================================ I-CACHE =============================== */

/*
 * cpu_xscale_icache_invalidate_range(start, end)
 *
 * invalidate a range of virtual addresses from the Icache
 *
 * start: virtual start address
 * end:   virtual end address
 *
 * Note: This is vaguely defined as supposed to bring the dcache and the
 *       icache in sync by the way this function is used.
 */
	.align	5
ENTRY(cpu_xscale_icache_invalidate_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	mcr	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, ip, c7, c5, 6		@ Invalidate BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 * cpu_xscale_icache_invalidate_page(page)
 *
 * invalidate all Icache lines associated with this area of memory
 *
 * page: page to invalidate
 */
	.align	5
ENTRY(cpu_xscale_icache_invalidate_page)
	mov	r1, #PAGESIZE
1:	mcr	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	add	r0, r0, #CACHELINESIZE
	mcr	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	add	r0, r0, #CACHELINESIZE
	mcr	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	add	r0, r0, #CACHELINESIZE
	mcr	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	add	r0, r0, #CACHELINESIZE
	subs	r1, r1, #4 * CACHELINESIZE
	bne	1b
	mcr	p15, 0, r0, c7, c5, 6		@ Invalidate BTB
	mov	pc, lr

/* ================================ CACHE LOCKING============================
 *
 * The XScale MicroArchitecture implements support for locking entries into
 * the data and instruction cache.  The following functions implement the core
 * low level instructions needed to accomplish the locking.  The developer's
 * manual states that the code that performs the locking must be in non-cached
 * memory.  To accomplish this, the code in xscale-cache-lock.c copies the
 * following functions from the cache into a non-cached memory region that
 * is allocated through consistent_alloc().
 *
 */
	.align	5
/*
 * xscale_icache_lock
 *
 * r0: starting address to lock
 * r1: end address to lock
 */
ENTRY(xscale_icache_lock)

iLockLoop:
	bic	r0, r0, #CACHELINESIZE - 1
	mcr	p15, 0, r0, c9, c1, 0	@ lock into cache
	cmp	r0, r1			@ are we done?
	add	r0, r0, #CACHELINESIZE	@ advance to next cache line
	bls	iLockLoop
	mov	pc, lr

/*
 * xscale_icache_unlock
 */
ENTRY(xscale_icache_unlock)
	mcr	p15, 0, r0, c9, c1, 1	@ Unlock icache
	mov	pc, lr

/*
 * xscale_dcache_lock
 *
 * r0: starting address to lock
 * r1: end address to lock
 */
ENTRY(xscale_dcache_lock)
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	r2, #1
	mcr	p15, 0, r2, c9, c2, 0	@ Put dcache in lock mode
	cpwait	ip			@ Wait for completion

	mrs	r2, cpsr
	orr	r3, r2, #PSR_F_BIT | PSR_I_BIT
dLockLoop:
	msr	cpsr_c, r3
	mcr	p15, 0, r0, c7, c10, 1	@ Write back line if it is dirty
	mcr	p15, 0, r0, c7, c6, 1	@ Flush/invalidate line
	msr	cpsr_c, r2
	ldr	ip, [r0], #CACHELINESIZE @ Preload 32 bytes into cache from
					@ location [r0]. Post-increment
					@ r3 to next cache line
	cmp	r0, r1			@ Are we done?
	bls	dLockLoop

	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	r2, #0
	mcr	p15, 0, r2, c9, c2, 0	@ Get out of lock mode
	cpwait_ret lr, ip

/*
 * xscale_dcache_unlock
 */
ENTRY(xscale_dcache_unlock)
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mcr	p15, 0, ip, c9, c2, 1	@ Unlock cache
	mov	pc, lr

/*
 * Needed to determine the length of the code that needs to be copied.
 */
	.align	5
ENTRY(xscale_cache_dummy)
	mov	pc, lr

/* ================================ TLB LOCKING==============================
 *
 * The XScale MicroArchitecture implements support for locking entries into
 * the Instruction and Data TLBs.  The following functions provide the
 * low level support for supporting these under Linux.  xscale-lock.c
 * implements some higher level management code.  Most of the following
 * is taken straight out of the Developer's Manual.
 */

/*
 * Lock I-TLB entry
 *
 * r0: Virtual address to translate and lock
 */
	.align	5
ENTRY(xscale_itlb_lock)
	mrs	r2, cpsr
	orr	r3, r2, #PSR_F_BIT | PSR_I_BIT
	msr	cpsr_c, r3			@ Disable interrupts
	mcr	p15, 0, r0, c8, c5, 1		@ Invalidate I-TLB entry
	mcr	p15, 0, r0, c10, c4, 0		@ Translate and lock
	msr	cpsr_c, r2			@ Restore interrupts
	cpwait_ret lr, ip

/*
 * Lock D-TLB entry
 *
 * r0: Virtual address to translate and lock
 */
	.align	5
ENTRY(xscale_dtlb_lock)
	mrs	r2, cpsr
	orr	r3, r2, #PSR_F_BIT | PSR_I_BIT
	msr	cpsr_c, r3			@ Disable interrupts
	mcr	p15, 0, r0, c8, c6, 1		@ Invalidate D-TLB entry
	mcr	p15, 0, r0, c10, c8, 0		@ Translate and lock
	msr	cpsr_c, r2			@ Restore interrupts
	cpwait_ret lr, ip

/*
 * Unlock all I-TLB entries
 */
	.align	5
ENTRY(xscale_itlb_unlock)
	mcr	p15, 0, ip, c10, c4, 1		@ Unlock I-TLB
	mcr	p15, 0, ip, c8, c5, 0		@ Invalidate I-TLB
	cpwait_ret lr, ip

/*
 * Unlock all D-TLB entries
 */
ENTRY(xscale_dtlb_unlock)
	mcr	p15, 0, ip, c10, c8, 1		@ Unlock D-TBL
	mcr	p15, 0, ip, c8, c6, 0		@ Invalidate D-TLB
	cpwait_ret lr, ip

/* =============================== PageTable ============================== */

#define PMD_CACHE_WRITE_ALLOCATE 0
#define PTE_CACHE_WRITE_ALLOCATE 0

/*
 * cpu_xscale_set_pgd(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_xscale_set_pgd)
	clean_d_cache r1, r2
	mcr	p15, 0, ip, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	cpwait_ret lr, ip

/*
 * cpu_xscale_flush_pmd(pmdp)
 *
 * Set a level 1 translation table entry, and clean it out of
 * any caches such that the MMUs can load it correctly.
 *
 * pmdp: pointer to PMD entry
 */
	.align	5
ENTRY(cpu_xscale_flush_pmd)
	mov	ip, #0
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 * cpu_xscale_set_pte(ptep, pte)
 *
 * Set a PTE and flush it out
 *
 * Errata 40: must set memory to write-through for user read-only pages.
 */
	.align	5
ENTRY(cpu_xscale_set_pte)
	str	r1, [r0], #-2048		@ linux version

	bic	r2, r1, #0xff0
	orr	r2, r2, #PTE_TYPE_EXT		@ extended page

	eor	r3, r1, #L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_WRITE | L_PTE_DIRTY

	tst	r3, #L_PTE_USER | L_PTE_EXEC	@ User or Exec?
	orrne	r2, r2, #PTE_EXT_AP_URO_SRW	@ yes -> user r/o, system r/w

	tst	r3, #L_PTE_WRITE | L_PTE_DIRTY	@ Write and Dirty?
	orreq	r2, r2, #PTE_EXT_AP_UNO_SRW	@ yes -> user n/a, system r/w
						@ combined with user -> user r/w

	@
	@ Handle the X bit.  We want to set this bit for the minicache
	@ (U = E = B = W = 0, C = 1) or when write allocate is enabled,
	@ and we have a writeable, cacheable region.  If we ignore the
	@ U and E bits, we can allow user space to use the minicache as
	@ well.
	@
	@  X = (C & ~W & ~B) | (C & W & B & write_allocate)
	@
	eor	ip, r1, #L_PTE_CACHEABLE
	tst	ip, #L_PTE_CACHEABLE | L_PTE_WRITE | L_PTE_BUFFERABLE
#if PTE_CACHE_WRITE_ALLOCATE
	eorne	ip, r1, #L_PTE_CACHEABLE | L_PTE_WRITE | L_PTE_BUFFERABLE
	tstne	ip, #L_PTE_CACHEABLE | L_PTE_WRITE | L_PTE_BUFFERABLE
#endif
	orreq	r2, r2, #PTE_EXT_TEX(1)

	@
	@ Erratum 40: The B bit must be cleared for a user read-only
	@ cacheable page.
	@
	@  B = B & ~((U|E) & C & ~W)
	@
	and	ip, r1, #L_PTE_USER | L_PTE_EXEC | L_PTE_WRITE | L_PTE_CACHEABLE
	teq	ip, #L_PTE_USER | L_PTE_CACHEABLE
	teqne	ip, #L_PTE_EXEC | L_PTE_CACHEABLE
	teqne	ip, #L_PTE_USER | L_PTE_EXEC | L_PTE_CACHEABLE
	biceq	r2, r2, #PTE_BUFFERABLE

	tst	r3, #L_PTE_PRESENT | L_PTE_YOUNG	@ Present and Young?
	movne	r2, #0				@ no -> fault

	str	r2, [r0]			@ hardware version
	mov	ip, #0
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr


	.ltorg

cpu_80200_name:
	.asciz	"XScale-80200"

cpu_80321_name:
	.asciz	"XScale-IOP80321"

cpu_pxa250_name:
	.asciz	"XScale-PXA250"

cpu_pxa210_name:
	.asciz	"XScale-PXA210"

	.align

	__INIT

__xscale_setup:
	mov	r0, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I, D caches & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I, D TLBs
	mcr	p15, 0, r4, c2, c0, 0		@ load page table pointer
	mov	r0, #0x1f			@ Domains 0, 1 = client
	mcr	p15, 0, r0, c3, c0, 0		@ load domain access register
	mov	r0, #1				@ Allow access to CP0 and CP13
	orr	r0, r0, #1 << 13		@ Its undefined whether this
	mcr	p15, 0, r0, c15, c1, 0		@ affects USR or SVC modes
	mrc	p15, 0, r0, c1, c0, 0		@ get control register
	bic	r0, r0, #0x0200			@ .... ..R. .... ....
	bic	r0, r0, #0x0002			@ .... .... .... ..A.
	orr	r0, r0, #0x0005			@ .... .... .... .C.M
	orr	r0, r0, #0x3900			@ ..VI Z..S .... ....
	mov	pc, lr

	.text

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */

	.type	xscale_processor_functions, #object
ENTRY(xscale_processor_functions)
	.word	xscale_abort
	.word	cpu_xscale_check_bugs
	.word	cpu_xscale_proc_init
	.word	cpu_xscale_proc_fin
	.word	cpu_xscale_reset
	.word	cpu_xscale_do_idle

	/* cache */
	.word	cpu_xscale_cache_clean_invalidate_all
	.word	cpu_xscale_cache_clean_invalidate_range
	.word	cpu_xscale_flush_ram_page

	/* dcache */
	.word	cpu_xscale_dcache_invalidate_range
	.word	cpu_xscale_dcache_clean_range
	.word	cpu_xscale_dcache_clean_page
	.word	cpu_xscale_dcache_clean_entry

	/* icache */
	.word	cpu_xscale_icache_invalidate_range
	.word	cpu_xscale_icache_invalidate_page

	/* pgtable */
	.word	cpu_xscale_set_pgd
	.word	cpu_xscale_flush_pmd
	.word	cpu_xscale_set_pte
	.size	xscale_processor_functions, . - xscale_processor_functions

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv5te"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v5"
	.size	cpu_elf_name, . - cpu_elf_name
	.align

	.section ".proc.info", #alloc, #execinstr

	.type	__80200_proc_info,#object
__80200_proc_info:
	.long	0x69052000
	.long	0xfffffff0
	.long	0x00000c0e
	b	__xscale_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_80200_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.size	__80200_proc_info, . - __80200_proc_info

	.type	__80321_proc_info,#object
__80321_proc_info:
	.long	0x69052420
	.long	0xfffffff0
	.long	0x00000c0e
	b	__xscale_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_80321_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.long	xscale_cache_fns
	.size	__80321_proc_info, . - __80321_proc_info

	.type	__pxa250_proc_info,#object
__pxa250_proc_info:
	.long	0x69052100
	.long	0xfffff3f0
	.long	0x00000c0e
	b	__xscale_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_pxa250_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.size	__pxa250_proc_info, . - __pxa250_proc_info

	.type	__pxa210_proc_info,#object
__pxa210_proc_info:
	.long	0x69052120
	.long	0xfffff3f0
	.long	0x00000c0e
	b	__xscale_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_pxa210_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.long	xscale_cache_fns
	.size	__pxa210_proc_info, . - __pxa210_proc_info

