/*
 * include/asm-v850/nb85e_cache_cache.h -- Cache control for NB85E_CACHE212 and
 * 	NB85E_CACHE213 cache memories
 *
 *  Copyright (C) 2001,03  NEC Electronics Corporation
 *  Copyright (C) 2001,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_NB85E_CACHE_H__
#define __V850_NB85E_CACHE_H__

#include <asm/types.h>


/* Cache control registers.  */
#define NB85E_CACHE_BHC_ADDR	0xFFFFF06A
#define NB85E_CACHE_BHC		(*(volatile u16 *)NB85E_CACHE_BHC_ADDR)
#define NB85E_CACHE_ICC_ADDR	0xFFFFF070
#define NB85E_CACHE_ICC		(*(volatile u16 *)NB85E_CACHE_ICC_ADDR)
#define NB85E_CACHE_ISI_ADDR	0xFFFFF072
#define NB85E_CACHE_ISI		(*(volatile u16 *)NB85E_CACHE_ISI_ADDR)
#define NB85E_CACHE_DCC_ADDR	0xFFFFF078
#define NB85E_CACHE_DCC		(*(volatile u16 *)NB85E_CACHE_DCC_ADDR)

/* Size of a cache line in bytes.  */
#define NB85E_CACHE_LINE_SIZE	16

/* For <asm/cache.h> */
#define L1_CACHE_BYTES				NB85E_CACHE_LINE_SIZE


#ifndef __ASSEMBLY__

/* Set caching params via the BHC and DCC registers.  */
void nb85e_cache_enable (u16 bhc, u16 dcc);

struct page;
struct mm_struct;
struct vm_area_struct;

extern void nb85e_cache_flush_all (void);
extern void nb85e_cache_flush_mm (struct mm_struct *mm);
extern void nb85e_cache_flush_range (struct mm_struct *mm,
				     unsigned long start,
				     unsigned long end);
extern void nb85e_cache_flush_page (struct vm_area_struct *vma,
				    unsigned long page_addr);
extern void nb85e_cache_flush_dcache_page (struct page *page);
extern void nb85e_cache_flush_icache (void);
extern void nb85e_cache_flush_icache_range (unsigned long start,
					    unsigned long end);
extern void nb85e_cache_flush_icache_page (struct vm_area_struct *vma,
					   struct page *page);
extern void nb85e_cache_flush_icache_user_range (struct vm_area_struct *vma,
						 struct page *page,
						 unsigned long adr, int len);
extern void nb85e_cache_flush_sigtramp (unsigned long addr);

#define flush_page_to_ram(x)	((void)0)
#define flush_cache_all		nb85e_cache_flush_all
#define flush_cache_mm		nb85e_cache_flush_mm
#define flush_cache_range	nb85e_cache_flush_range
#define flush_cache_page	nb85e_cache_flush_page
#define flush_dcache_page	nb85e_cache_flush_dcache_page
#define flush_icache		nb85e_cache_flush_icache
#define flush_icache_range	nb85e_cache_flush_icache_range
#define flush_icache_page	nb85e_cache_flush_icache_page
#define flush_icache_user_range	nb85e_cache_flush_icache_user_range
#define flush_cache_sigtramp	nb85e_cache_flush_sigtramp

#endif /* !__ASSEMBLY__ */

#endif /* __V850_NB85E_CACHE_H__ */
