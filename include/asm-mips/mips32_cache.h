/*
 * mips32_cache.h
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Inline assembly cache operations.
 * 
 * This file is the original r4cache.c file with modification that makes the
 * cache handling more generic.
 *
 * FIXME: Handle split L2 caches.
 *
 */
#ifndef _MIPS_R4KCACHE_H
#define _MIPS_R4KCACHE_H

#include <asm/asm.h>
#include <asm/cacheops.h>

extern inline void flush_icache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Index_Invalidate_I));
}

extern inline void flush_dcache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Index_Writeback_Inv_D));
}

extern inline void flush_scache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Index_Writeback_Inv_SD));
}

extern inline void flush_icache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Invalidate_I));
}

extern inline void flush_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Writeback_Inv_D));
}

extern inline void invalidate_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Invalidate_D));
}

extern inline void invalidate_scache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Invalidate_SD));
}

extern inline void flush_scache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Writeback_Inv_SD));
}

/*
 * The next two are for badland addresses like signal trampolines.
 */
extern inline void protected_flush_icache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n"
		"1:\tcache %1,(%0)\n"
		"2:\t.set mips0\n\t"
		".set reorder\n\t"
		".section\t__ex_table,\"a\"\n\t"
		STR(PTR)"\t1b,2b\n\t"
		".previous"
		:
		: "r" (addr),
		  "i" (Hit_Invalidate_I));
}

extern inline void protected_writeback_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n"
		"1:\tcache %1,(%0)\n"
		"2:\t.set mips0\n\t"
		".set reorder\n\t"
		".section\t__ex_table,\"a\"\n\t"
		STR(PTR)"\t1b,2b\n\t"
		".previous"
		:
		: "r" (addr),
		  "i" (Hit_Writeback_D));
}

#define cache_unroll(base,op)	        	\
	__asm__ __volatile__("	         	\
		.set noreorder;		        \
		.set mips3;		        \
                cache %1, (%0);	                \
		.set mips0;			\
		.set reorder"			\
		:				\
		: "r" (base),			\
		  "i" (op));


extern inline void blast_dcache(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + dcache_size);

	while(start < end) {
		cache_unroll(start,Index_Writeback_Inv_D);
		start += dc_lsize;
	}
}

extern inline void blast_dcache_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache_unroll(start,Hit_Writeback_Inv_D);
		start += dc_lsize;
	}
}

extern inline void blast_dcache_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache_unroll(start,Index_Writeback_Inv_D);
		start += dc_lsize;
	}
}

extern inline void blast_icache(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + icache_size);

	while(start < end) {
		cache_unroll(start,Index_Invalidate_I);
		start += ic_lsize;
	}
}

extern inline void blast_icache_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache_unroll(start,Hit_Invalidate_I);
		start += ic_lsize;
	}
}

extern inline void blast_icache_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache_unroll(start,Index_Invalidate_I);
		start += ic_lsize;
	}
}

extern inline void blast_scache(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		cache_unroll(start,Index_Writeback_Inv_SD);
		start += sc_lsize;
	}
}

extern inline void blast_scache_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		cache_unroll(start,Hit_Writeback_Inv_SD);
		start += sc_lsize;
	}
}

extern inline void blast_scache_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		cache_unroll(start,Index_Writeback_Inv_SD);
		start += sc_lsize;
	}
}

#endif /* !(_MIPS_R4KCACHE_H) */
