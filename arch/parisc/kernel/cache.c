/* $Id: cache.c,v 1.4 2000/01/25 00:11:38 prumpf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Helge Deller (07-13-1999)
 * Copyright (C) 1999 SuSE GmbH Nuernberg
 * Copyright (C) 2000 Philipp Rumpf (prumpf@tux.org)
 *
 * Cache and TLB management
 *
 */
 
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>

#include <asm/pdc.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>

int split_tlb;
int dcache_stride;
int icache_stride;
EXPORT_SYMBOL(dcache_stride);

struct pdc_cache_info cache_info;
#ifndef CONFIG_PA20
static struct pdc_btlb_info btlb_info;
#endif

#ifdef CONFIG_SMP
void
flush_data_cache(void)
{
	on_each_cpu((void (*)(void *))flush_data_cache_local, NULL, 1, 1);
}
#endif

void
flush_cache_all_local(void)
{
	flush_instruction_cache_local();
	flush_data_cache_local();
}
EXPORT_SYMBOL(flush_cache_all_local);

/* flushes EVERYTHING (tlb & cache) */

void
flush_all_caches(void)
{
	flush_cache_all();
	flush_tlb_all();
}
EXPORT_SYMBOL(flush_all_caches);

void
update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	struct page *page = pte_page(pte);

	if (VALID_PAGE(page) && page_mapping(page) &&
	    test_bit(PG_dcache_dirty, &page->flags)) {

		flush_kernel_dcache_page(page_address(page));
		clear_bit(PG_dcache_dirty, &page->flags);
	}
}

void
show_cache_info(struct seq_file *m)
{
	seq_printf(m, "I-cache\t\t: %ld KB\n", 
		cache_info.ic_size/1024 );
	seq_printf(m, "D-cache\t\t: %ld KB (%s)%s\n", 
		cache_info.dc_size/1024,
		(cache_info.dc_conf.cc_wt ? "WT":"WB"),
		(cache_info.dc_conf.cc_sh ? " - shared I/D":"")
	);

	seq_printf(m, "ITLB entries\t: %ld\n" "DTLB entries\t: %ld%s\n",
		cache_info.it_size,
		cache_info.dt_size,
		cache_info.dt_conf.tc_sh ? " - shared with ITLB":""
	);
		
#ifndef CONFIG_PA20
	/* BTLB - Block TLB */
	if (btlb_info.max_size==0) {
		seq_printf(m, "BTLB\t\t: not supported\n" );
	} else {
		seq_printf(m, 
		"BTLB fixed\t: max. %d pages, pagesize=%d (%dMB)\n"
		"BTLB fix-entr.\t: %d instruction, %d data (%d combined)\n"
		"BTLB var-entr.\t: %d instruction, %d data (%d combined)\n",
		btlb_info.max_size, (int)4096,
		btlb_info.max_size>>8,
		btlb_info.fixed_range_info.num_i,
		btlb_info.fixed_range_info.num_d,
		btlb_info.fixed_range_info.num_comb, 
		btlb_info.variable_range_info.num_i,
		btlb_info.variable_range_info.num_d,
		btlb_info.variable_range_info.num_comb
		);
	}
#endif
}

void __init 
parisc_cache_init(void)
{
	if (pdc_cache_info(&cache_info) < 0)
		panic("parisc_cache_init: pdc_cache_info failed");

#if 0
	printk(KERN_DEBUG "ic_size %lx dc_size %lx it_size %lx pdc_cache_info %d*long pdc_cache_cf %d\n",
	    cache_info.ic_size,
	    cache_info.dc_size,
	    cache_info.it_size,
	    sizeof (struct pdc_cache_info) / sizeof (long),
	    sizeof (struct pdc_cache_cf)
	);

	printk(KERN_DEBUG "dc base %x dc stride %x dc count %x dc loop %d\n",
	    cache_info.dc_base,
	    cache_info.dc_stride,
	    cache_info.dc_count,
	    cache_info.dc_loop);

	printk(KERN_DEBUG "dc conf: alias %d block %d line %d wt %d sh %d cst %d assoc %d\n",
	    cache_info.dc_conf.cc_alias,
	    cache_info.dc_conf.cc_block,
	    cache_info.dc_conf.cc_line,
	    cache_info.dc_conf.cc_wt,
	    cache_info.dc_conf.cc_sh,
	    cache_info.dc_conf.cc_cst,
	    cache_info.dc_conf.cc_assoc);

	printk(KERN_DEBUG "ic conf: alias %d block %d line %d wt %d sh %d cst %d assoc %d\n",
	    cache_info.ic_conf.cc_alias,
	    cache_info.ic_conf.cc_block,
	    cache_info.ic_conf.cc_line,
	    cache_info.ic_conf.cc_wt,
	    cache_info.ic_conf.cc_sh,
	    cache_info.ic_conf.cc_cst,
	    cache_info.ic_conf.cc_assoc);

	printk(KERN_DEBUG "dt conf: sh %d page %d cst %d aid %d pad1 %d \n",
	    cache_info.dt_conf.tc_sh,
	    cache_info.dt_conf.tc_page,
	    cache_info.dt_conf.tc_cst,
	    cache_info.dt_conf.tc_aid,
	    cache_info.dt_conf.tc_pad1);

	printk(KERN_DEBUG "it conf: sh %d page %d cst %d aid %d pad1 %d \n",
	    cache_info.it_conf.tc_sh,
	    cache_info.it_conf.tc_page,
	    cache_info.it_conf.tc_cst,
	    cache_info.it_conf.tc_aid,
	    cache_info.it_conf.tc_pad1);
#endif

	split_tlb = 0;
	if (cache_info.dt_conf.tc_sh == 0 || cache_info.dt_conf.tc_sh == 2) {
		if (cache_info.dt_conf.tc_sh == 2)
			printk(KERN_WARNING "Unexpected TLB configuration. "
			"Will flush I/D separately (could be optimized).\n");

		split_tlb = 1;
	}

	dcache_stride = (1 << (cache_info.dc_conf.cc_block + 3)) *
						cache_info.dc_conf.cc_line;
	icache_stride = (1 << (cache_info.ic_conf.cc_block + 3)) *
						cache_info.ic_conf.cc_line;
#ifndef CONFIG_PA20
	if (pdc_btlb_info(&btlb_info) < 0) {
		memset(&btlb_info, 0, sizeof btlb_info);
	}
#endif

	if ((boot_cpu_data.pdc.capabilities & PDC_MODEL_NVA_MASK) ==
						PDC_MODEL_NVA_UNSUPPORTED) {
		printk(KERN_WARNING "Only equivalent aliasing supported\n");
#ifndef CONFIG_SMP
		panic("SMP kernel required to avoid non-equivalent aliasing");
#endif
	}
}

void disable_sr_hashing(void)
{
	int srhash_type;

	switch (boot_cpu_data.cpu_type) {
	case pcx: /* We shouldn't get this far.  setup.c should prevent it. */
		BUG();
		return;

	case pcxs:
	case pcxt:
	case pcxt_:
		srhash_type = SRHASH_PCXST;
		break;

	case pcxl:
		srhash_type = SRHASH_PCXL;
		break;

	case pcxl2: /* pcxl2 doesn't support space register hashing */
		return;

	default: /* Currently all PA2.0 machines use the same ins. sequence */
		srhash_type = SRHASH_PA20;
		break;
	}

	disable_sr_hashing_asm(srhash_type);
}

void __flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	struct vm_area_struct *mpnt = NULL;
	struct prio_tree_iter iter;
	unsigned long offset;
	unsigned long addr;
	pgoff_t pgoff;

	flush_kernel_dcache_page(page_address(page));

	if (!mapping)
		return;

	pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

	/* We have ensured in arch_get_unmapped_area() that all shared
	 * mappings are mapped at equivalent addresses, so we only need
	 * to flush one for them all to become coherent */

	while ((mpnt = vma_prio_tree_next(mpnt, &mapping->i_mmap_shared,
					&iter, pgoff, pgoff)) != NULL) {
		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		addr = mpnt->vm_start + offset;

		/* flush instructions produce non access tlb misses.
		 * On PA, we nullify these instructions rather than 
		 * taking a page fault if the pte doesn't exist, so we
		 * have to find a congruent address with an existing
		 * translation */

		if (!translation_exists(mpnt, addr))
			continue;

		__flush_cache_page(mpnt, addr);

		/* If we find an address to flush, that will also
		 * bring all the private mappings up to date (see
		 * comment below) */
		return;
	}

	/* we have carefully arranged in arch_get_unmapped_area() that
	 * *any* mappings of a file are always congruently mapped (whether
	 * declared as MAP_PRIVATE or MAP_SHARED), so we only need
	 * to flush one address here too */

	while ((mpnt = vma_prio_tree_next(mpnt, &mapping->i_mmap,
					&iter, pgoff, pgoff)) != NULL) {
		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		addr = mpnt->vm_start + offset;

		/* This is just for speed.  If the page translation isn't
		 * there there's no point exciting the nadtlb handler into
		 * a nullification frenzy */
		if(!translation_exists(mpnt, addr))
			continue;

		__flush_cache_page(mpnt, addr);

		return;
	}
}
EXPORT_SYMBOL(__flush_dcache_page);

/* Defined in arch/parisc/kernel/pacache.S */
EXPORT_SYMBOL(flush_kernel_dcache_range_asm);
EXPORT_SYMBOL(flush_kernel_dcache_page);
EXPORT_SYMBOL(flush_data_cache_local);
EXPORT_SYMBOL(flush_kernel_icache_range_asm);
