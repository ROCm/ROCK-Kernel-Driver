/*
 *  linux/arch/arm/mm/proc-syms.c
 *
 *  Copyright (C) 2000-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/pgalloc.h>
#include <asm/proc-fns.h>
#include <asm/tlbflush.h>

EXPORT_SYMBOL(__flush_dcache_page);

#ifndef MULTI_CPU
EXPORT_SYMBOL(cpu_cache_clean_invalidate_all);
EXPORT_SYMBOL(cpu_cache_clean_invalidate_range);
EXPORT_SYMBOL(cpu_dcache_clean_page);
EXPORT_SYMBOL(cpu_dcache_clean_entry);
EXPORT_SYMBOL(cpu_dcache_clean_range);
EXPORT_SYMBOL(cpu_dcache_invalidate_range);
EXPORT_SYMBOL(cpu_icache_invalidate_range);
EXPORT_SYMBOL(cpu_icache_invalidate_page);
EXPORT_SYMBOL(cpu_set_pgd);
EXPORT_SYMBOL(cpu_set_pte);
#else
EXPORT_SYMBOL(processor);
#endif

/*
 * No module should need to touch the TLB (and currently
 * no modules do.  We export this for "loadkernel" support
 * (booting a new kernel from within a running kernel.)
 */
#ifdef MULTI_TLB
EXPORT_SYMBOL(cpu_tlb);
#endif
