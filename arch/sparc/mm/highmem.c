/*
 *  highmem.c: virtual kernel memory mappings for high memory
 *
 *  Provides kernel-static versions of atomic kmap functions originally
 *  found as inlines in include/asm-sparc/highmem.h.  These became
 *  needed as kmap_atomic() and kunmap_atomic() started getting
 *  called from within modules.
 *  -- Tomas Szepe <szepe@pinerecords.com>, September 2002
 *
 *  But kmap_atomic() and kunmap_atomic() cannot be inlined in
 *  modules because they are loaded with btfixup-ped functions.
 */

/*
 * The use of kmap_atomic/kunmap_atomic is discouraged - kmap/kunmap
 * gives a more generic (and caching) interface. But kmap_atomic can
 * be used in IRQ contexts, so in some (very limited) cases we need it.
 *
 * XXX This is an old text. Actually, it's good to use atomic kmaps,
 * provided you remember that they are atomic and not try to sleep
 * with a kmap taken, much like a spinlock. Non-atomic kmaps are
 * shared by CPUs, and so precious, and establishing them requires IPI.
 * Atomic kmaps are lightweight and we may have NCPUS more of them.
 */
#include <linux/mm.h>
#include <linux/highmem.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

void *kmap_atomic(struct page *page, enum km_type type)
{
	unsigned long idx;
	unsigned long vaddr;

	inc_preempt_count();
	if (page < highmem_start_page)
		return page_address(page);

	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = fix_kmap_begin + idx * PAGE_SIZE;

/* XXX Fix - Anton */
#if 0
	__flush_cache_one(vaddr);
#else
	flush_cache_all();
#endif

#if HIGHMEM_DEBUG
	if (!pte_none(*(kmap_pte+idx)))
		BUG();
#endif
	set_pte(kmap_pte+idx, mk_pte(page, kmap_prot));
/* XXX Fix - Anton */
#if 0
	__flush_tlb_one(vaddr);
#else
	flush_tlb_all();
#endif

	return (void*) vaddr;
}

void kunmap_atomic(void *kvaddr, enum km_type type)
{
	unsigned long vaddr = (unsigned long) kvaddr;
	unsigned long idx = type + KM_TYPE_NR*smp_processor_id();

	if (vaddr < fix_kmap_begin) { // FIXME
		dec_preempt_count();
		return;
	}

	if (vaddr != fix_kmap_begin + idx * PAGE_SIZE)
		BUG();

/* XXX Fix - Anton */
#if 0
	__flush_cache_one(vaddr);
#else
	flush_cache_all();
#endif

#ifdef HIGHMEM_DEBUG
	/*
	 * force other mappings to Oops if they'll try to access
	 * this pte without first remap it
	 */
	pte_clear(kmap_pte+idx);
/* XXX Fix - Anton */
#if 0
	__flush_tlb_one(vaddr);
#else
	flush_tlb_all();
#endif
#endif
	dec_preempt_count();
}
