/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Inline assembly cache operations.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * FIXME: Handle split L2 caches.
 */
#ifndef _ASM_R4KCACHE_H
#define _ASM_R4KCACHE_H

#include <asm/asm.h>
#include <asm/r4kcacheops.h>

extern inline void flush_icache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Index_Invalidate_I));
}

extern inline void flush_dcache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Index_Writeback_Inv_D));
}

extern inline void flush_scache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Index_Writeback_Inv_SD));
}

extern inline void flush_icache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Hit_Invalidate_I));
}

extern inline void flush_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Hit_Writeback_Inv_D));
}

extern inline void invalidate_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Hit_Invalidate_D));
}

extern inline void invalidate_scache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Hit_Invalidate_SD));
}

extern inline void flush_scache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Hit_Writeback_Inv_SD));
}

/*
 * The next two are for badland addresses like signal trampolines.
 */
extern inline void protected_flush_icache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"1:\tcache %1,(%0)\n"
		"2:\t.set reorder\n\t"
		".section\t__ex_table,\"a\"\n\t"
		".dword\t1b,2b\n\t"
		".previous"
		:
		: "r" (addr), "i" (Hit_Invalidate_I));
}

extern inline void protected_writeback_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"1:\tcache %1,(%0)\n"
		"2:\t.set reorder\n\t"
		".section\t__ex_table,\"a\"\n\t"
		".dword\t1b,2b\n\t"
		".previous"
		:
		: "r" (addr), "i" (Hit_Writeback_D));
}

#define cache16_unroll32(base,op)				\
	__asm__ __volatile__("					\
		.set noreorder;					\
		cache %1, 0x000(%0); cache %1, 0x010(%0);	\
		cache %1, 0x020(%0); cache %1, 0x030(%0);	\
		cache %1, 0x040(%0); cache %1, 0x050(%0);	\
		cache %1, 0x060(%0); cache %1, 0x070(%0);	\
		cache %1, 0x080(%0); cache %1, 0x090(%0);	\
		cache %1, 0x0a0(%0); cache %1, 0x0b0(%0);	\
		cache %1, 0x0c0(%0); cache %1, 0x0d0(%0);	\
		cache %1, 0x0e0(%0); cache %1, 0x0f0(%0);	\
		cache %1, 0x100(%0); cache %1, 0x110(%0);	\
		cache %1, 0x120(%0); cache %1, 0x130(%0);	\
		cache %1, 0x140(%0); cache %1, 0x150(%0);	\
		cache %1, 0x160(%0); cache %1, 0x170(%0);	\
		cache %1, 0x180(%0); cache %1, 0x190(%0);	\
		cache %1, 0x1a0(%0); cache %1, 0x1b0(%0);	\
		cache %1, 0x1c0(%0); cache %1, 0x1d0(%0);	\
		cache %1, 0x1e0(%0); cache %1, 0x1f0(%0);	\
		.set reorder"					\
		:						\
		: "r" (base),					\
		  "i" (op));

extern inline void blast_dcache16(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + dcache_size);

	while(start < end) {
		cache16_unroll32(start,Index_Writeback_Inv_D);
		start += 0x200;
	}
}

extern inline void blast_dcache16_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache16_unroll32(start,Hit_Writeback_Inv_D);
		start += 0x200;
	}
}

extern inline void blast_dcache16_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache16_unroll32(start,Index_Writeback_Inv_D);
		start += 0x200;
	}
}

extern inline void blast_icache16(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + icache_size);

	while(start < end) {
		cache16_unroll32(start,Index_Invalidate_I);
		start += 0x200;
	}
}

extern inline void blast_icache16_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache16_unroll32(start,Hit_Invalidate_I);
		start += 0x200;
	}
}

extern inline void blast_icache16_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache16_unroll32(start,Index_Invalidate_I);
		start += 0x200;
	}
}

extern inline void blast_scache16(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		cache16_unroll32(start,Index_Writeback_Inv_SD);
		start += 0x200;
	}
}

extern inline void blast_scache16_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		cache16_unroll32(start,Hit_Writeback_Inv_SD);
		start += 0x200;
	}
}

extern inline void blast_scache16_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		cache16_unroll32(start,Index_Writeback_Inv_SD);
		start += 0x200;
	}
}

#define cache32_unroll32(base,op)				\
	__asm__ __volatile__("					\
		.set noreorder;					\
		cache %1, 0x000(%0); cache %1, 0x020(%0);	\
		cache %1, 0x040(%0); cache %1, 0x060(%0);	\
		cache %1, 0x080(%0); cache %1, 0x0a0(%0);	\
		cache %1, 0x0c0(%0); cache %1, 0x0e0(%0);	\
		cache %1, 0x100(%0); cache %1, 0x120(%0);	\
		cache %1, 0x140(%0); cache %1, 0x160(%0);	\
		cache %1, 0x180(%0); cache %1, 0x1a0(%0);	\
		cache %1, 0x1c0(%0); cache %1, 0x1e0(%0);	\
		cache %1, 0x200(%0); cache %1, 0x220(%0);	\
		cache %1, 0x240(%0); cache %1, 0x260(%0);	\
		cache %1, 0x280(%0); cache %1, 0x2a0(%0);	\
		cache %1, 0x2c0(%0); cache %1, 0x2e0(%0);	\
		cache %1, 0x300(%0); cache %1, 0x320(%0);	\
		cache %1, 0x340(%0); cache %1, 0x360(%0);	\
		cache %1, 0x380(%0); cache %1, 0x3a0(%0);	\
		cache %1, 0x3c0(%0); cache %1, 0x3e0(%0);	\
		.set reorder"					\
		:						\
		: "r" (base),					\
		  "i" (op));

extern inline void blast_dcache32(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + dcache_size);

	while(start < end) {
		cache32_unroll32(start,Index_Writeback_Inv_D);
		start += 0x400;
	}
}

/*
 * Call this function only with interrupts disabled or R4600 V2.0 may blow
 * up on you.
 *
 * R4600 v2.0 bug: "The CACHE instructions Hit_Writeback_Inv_D,
 * Hit_Writeback_D, Hit_Invalidate_D and Create_Dirty_Excl_D will only
 * operate correctly if the internal data cache refill buffer is empty.  These
 * CACHE instructions should be separated from any potential data cache miss
 * by a load instruction to an uncached address to empty the response buffer."
 * (Revision 2.0 device errata from IDT available on http://www.idt.com/
 * in .pdf format.)
 */
extern inline void blast_dcache32_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	/*
	 * Sigh ... workaround for R4600 v1.7 bug.  Explanation see above.
	 */
	*(volatile unsigned long *)KSEG1;

	__asm__ __volatile__("nop;nop;nop;nop");
	while(start < end) {
		cache32_unroll32(start,Hit_Writeback_Inv_D);
		start += 0x400;
	}
}

extern inline void blast_dcache32_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache32_unroll32(start,Index_Writeback_Inv_D);
		start += 0x400;
	}
}

extern inline void blast_icache32(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + icache_size);

	while(start < end) {
		cache32_unroll32(start,Index_Invalidate_I);
		start += 0x400;
	}
}

extern inline void blast_icache32_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache32_unroll32(start,Hit_Invalidate_I);
		start += 0x400;
	}
}

extern inline void blast_icache32_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache32_unroll32(start,Index_Invalidate_I);
		start += 0x400;
	}
}

extern inline void blast_scache32(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		cache32_unroll32(start,Index_Writeback_Inv_SD);
		start += 0x400;
	}
}

extern inline void blast_scache32_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		cache32_unroll32(start,Hit_Writeback_Inv_SD);
		start += 0x400;
	}
}

extern inline void blast_scache32_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		cache32_unroll32(start,Index_Writeback_Inv_SD);
		start += 0x400;
	}
}

#define cache64_unroll32(base,op)				\
	__asm__ __volatile__("					\
		.set noreorder;					\
		cache %1, 0x000(%0); cache %1, 0x040(%0);	\
		cache %1, 0x080(%0); cache %1, 0x0c0(%0);	\
		cache %1, 0x100(%0); cache %1, 0x140(%0);	\
		cache %1, 0x180(%0); cache %1, 0x1c0(%0);	\
		cache %1, 0x200(%0); cache %1, 0x240(%0);	\
		cache %1, 0x280(%0); cache %1, 0x2c0(%0);	\
		cache %1, 0x300(%0); cache %1, 0x340(%0);	\
		cache %1, 0x380(%0); cache %1, 0x3c0(%0);	\
		cache %1, 0x400(%0); cache %1, 0x440(%0);	\
		cache %1, 0x480(%0); cache %1, 0x4c0(%0);	\
		cache %1, 0x500(%0); cache %1, 0x540(%0);	\
		cache %1, 0x580(%0); cache %1, 0x5c0(%0);	\
		cache %1, 0x600(%0); cache %1, 0x640(%0);	\
		cache %1, 0x680(%0); cache %1, 0x6c0(%0);	\
		cache %1, 0x700(%0); cache %1, 0x740(%0);	\
		cache %1, 0x780(%0); cache %1, 0x7c0(%0);	\
		.set reorder"					\
		:						\
		: "r" (base),					\
		  "i" (op));

extern inline void blast_scache64(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		cache64_unroll32(start,Index_Writeback_Inv_SD);
		start += 0x800;
	}
}

extern inline void blast_scache64_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		cache64_unroll32(start,Hit_Writeback_Inv_SD);
		start += 0x800;
	}
}

extern inline void blast_scache64_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		cache64_unroll32(start,Index_Writeback_Inv_SD);
		start += 0x800;
	}
}

#define cache128_unroll32(base,op)				\
	__asm__ __volatile__("					\
		.set noreorder;					\
		cache %1, 0x000(%0); cache %1, 0x080(%0);	\
		cache %1, 0x100(%0); cache %1, 0x180(%0);	\
		cache %1, 0x200(%0); cache %1, 0x280(%0);	\
		cache %1, 0x300(%0); cache %1, 0x380(%0);	\
		cache %1, 0x400(%0); cache %1, 0x480(%0);	\
		cache %1, 0x500(%0); cache %1, 0x580(%0);	\
		cache %1, 0x600(%0); cache %1, 0x680(%0);	\
		cache %1, 0x700(%0); cache %1, 0x780(%0);	\
		cache %1, 0x800(%0); cache %1, 0x880(%0);	\
		cache %1, 0x900(%0); cache %1, 0x980(%0);	\
		cache %1, 0xa00(%0); cache %1, 0xa80(%0);	\
		cache %1, 0xb00(%0); cache %1, 0xb80(%0);	\
		cache %1, 0xc00(%0); cache %1, 0xc80(%0);	\
		cache %1, 0xd00(%0); cache %1, 0xd80(%0);	\
		cache %1, 0xe00(%0); cache %1, 0xe80(%0);	\
		cache %1, 0xf00(%0); cache %1, 0xf80(%0);	\
		.set reorder"					\
		:						\
		: "r" (base),					\
		  "i" (op));

extern inline void blast_scache128(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		cache128_unroll32(start,Index_Writeback_Inv_SD);
		start += 0x1000;
	}
}

extern inline void blast_scache128_page(unsigned long page)
{
	cache128_unroll32(page,Hit_Writeback_Inv_SD);
}

extern inline void blast_scache128_page_indexed(unsigned long page)
{
	cache128_unroll32(page,Index_Writeback_Inv_SD);
}

#endif /* __ASM_R4KCACHE_H */
