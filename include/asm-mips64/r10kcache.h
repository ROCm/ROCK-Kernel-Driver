/* $Id: r10kcache.h,v 1.1 2000/01/16 01:27:14 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Inline assembly cache operations.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 *
 * FIXME: Handle split L2 caches.
 */
#ifndef _ASM_R10KCACHE_H
#define _ASM_R10KCACHE_H

#include <asm/asm.h>
#include <asm/r10kcacheops.h>

/* These are fixed for the current R10000.  */
#define icache_size	0x8000
#define dcache_size	0x8000
#define icache_way_size	0x4000
#define dcache_way_size	0x4000
#define ic_lsize	64
#define dc_lsize	32

/* These are configuration dependant.  */
#define scache_size()	({						\
	unsigned long __res;						\
	__res = (read_32bit_cp0_register(CP0_CONFIG) >> 16) & 3;	\
	__res = 1 << (__res + 19);					\
	__res;								\
})

#define sc_lsize()	({						\
	unsigned long __res;						\
	__res = (read_32bit_cp0_register(CP0_CONFIG) >> 13) & 1;	\
	__res = 1 << (__res + 6);					\
	__res;								\
})

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
		: "r" (addr), "i" (Index_Writeback_Inv_S));
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
		: "r" (addr), "i" (Hit_Invalidate_S));
}

extern inline void flush_scache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"cache %1, (%0)\n\t"
		".set reorder"
		:
		: "r" (addr), "i" (Hit_Writeback_Inv_S));
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
		: "r" (addr), "i" (Hit_Writeback_Inv_D));
}

#define cache32_unroll16(base,op)				\
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
		.set reorder"					\
		:						\
		: "r" (base),					\
		  "i" (op));

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
	unsigned long way0 = KSEG0;
	unsigned long way1 = way0 ^ 1;
	unsigned long end = (way0 + dcache_way_size);

	while (way0 < end) {
		cache32_unroll16(way0, Index_Writeback_Inv_D);
		cache32_unroll16(way1, Index_Writeback_Inv_D);
		way0 += 0x200;
		way1 += 0x200;
	}
}

extern inline void blast_dcache32_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while (start < end) {
		cache32_unroll32(start, Hit_Writeback_Inv_D);
		start += 0x400;
	}
}

extern inline void blast_dcache32_page_indexed(unsigned long page)
{
	unsigned long way0 = page;
	unsigned long way1 = page ^ 1;
	unsigned long end = page + PAGE_SIZE;

	while (way0 < end) {
		cache32_unroll16(way0, Index_Writeback_Inv_D);
		cache32_unroll16(way1, Index_Writeback_Inv_D);
		way0 += 0x200;
		way1 += 0x200;
	}
}

#define cache64_unroll16(base,op)				\
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
		.set reorder"					\
		:						\
		: "r" (base),					\
		  "i" (op));

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

extern inline void blast_icache64(void)
{
	unsigned long way0 = KSEG0;
	unsigned long way1 = way0 ^ 1;
	unsigned long end = way0 + icache_way_size;

	while (way0 < end) {
		cache64_unroll16(way0,Index_Invalidate_I);
		cache64_unroll16(way1,Index_Invalidate_I);
		way0 += 0x400;
		way1 += 0x400;
	}
}

extern inline void blast_icache64_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while (start < end) {
		cache64_unroll32(start,Hit_Invalidate_I);
		start += 0x800;
	}
}

extern inline void blast_icache64_page_indexed(unsigned long page)
{
	unsigned long way0 = page;
	unsigned long way1 = page ^ 1;
	unsigned long end = page + PAGE_SIZE;

	while (way0 < end) {
		cache64_unroll16(way0,Index_Invalidate_I);
		cache64_unroll16(way1,Index_Invalidate_I);
		way0 += 0x400;
		way1 += 0x400;
	}
}

extern inline void blast_scache64(void)
{
	unsigned long way0 = KSEG0;
	unsigned long way1 = way0 ^ 1;
	unsigned long end = KSEG0 + scache_size();

	while (way0 < end) {
		cache64_unroll16(way0,Index_Writeback_Inv_S);
		cache64_unroll16(way1,Index_Writeback_Inv_S);
		way0 += 0x400;
		way1 += 0x400;
	}
}

extern inline void blast_scache64_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while (start < end) {
		cache64_unroll32(start,Hit_Writeback_Inv_S);
		start += 0x800;
	}
}

extern inline void blast_scache64_page_indexed(unsigned long page)
{
	unsigned long way0 = page;
	unsigned long way1 = page ^ 1;
	unsigned long end = page + PAGE_SIZE;

	while (way0 < end) {
		cache64_unroll16(way0,Index_Writeback_Inv_S);
		cache64_unroll16(way1,Index_Writeback_Inv_S);
		way0 += 0x400;
		way1 += 0x400;
	}
}

#define cache128_unroll16(base,op)				\
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
		.set reorder"					\
		:						\
		: "r" (base),					\
		  "i" (op));

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
	unsigned long way0 = KSEG0;
	unsigned long way1 = way0 ^ 1;
	unsigned long end = way0 + scache_size();

	while (way0 < end) {
		cache128_unroll16(way0, Index_Writeback_Inv_S);
		cache128_unroll16(way1, Index_Writeback_Inv_S);
		way0 += 0x800;
		way1 += 0x800;
	}
}

extern inline void blast_scache128_page(unsigned long page)
{
	cache128_unroll32(page, Hit_Writeback_Inv_S);
}

extern inline void blast_scache128_page_indexed(unsigned long page)
{
	cache128_unroll32(page    , Index_Writeback_Inv_S);
	cache128_unroll32(page ^ 1, Index_Writeback_Inv_S);
}

#endif /* _ASM_R10KCACHE_H */
