#ifndef _PARISC_CACHEFLUSH_H
#define _PARISC_CACHEFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>

/* The usual comment is "Caches aren't brain-dead on the <architecture>".
 * Unfortunately, that doesn't apply to PA-RISC. */

/* Cache flush operations */

#ifdef CONFIG_SMP
#define flush_cache_mm(mm) flush_cache_all()
#else
#define flush_cache_mm(mm) flush_cache_all_local()
#endif

#define flush_kernel_dcache_range(start,size) \
	flush_kernel_dcache_range_asm((start), (start)+(size));

extern void flush_cache_all_local(void);

static inline void cacheflush_h_tmp_function(void *dummy)
{
	flush_cache_all_local();
}

static inline void flush_cache_all(void)
{
	on_each_cpu(cacheflush_h_tmp_function, NULL, 1, 1);
}

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

/* The following value needs to be tuned and probably scaled with the
 * cache size.
 */

#define FLUSH_THRESHOLD 0x80000

static inline void
flush_user_dcache_range(unsigned long start, unsigned long end)
{
#ifdef CONFIG_SMP
	flush_user_dcache_range_asm(start,end);
#else
	if ((end - start) < FLUSH_THRESHOLD)
		flush_user_dcache_range_asm(start,end);
	else
		flush_data_cache();
#endif
}

static inline void
flush_user_icache_range(unsigned long start, unsigned long end)
{
#ifdef CONFIG_SMP
	flush_user_icache_range_asm(start,end);
#else
	if ((end - start) < FLUSH_THRESHOLD)
		flush_user_icache_range_asm(start,end);
	else
		flush_instruction_cache();
#endif
}

extern void __flush_dcache_page(struct page *page);

static inline void flush_dcache_page(struct page *page)
{
	if (page->mapping && list_empty(&page->mapping->i_mmap) &&
			list_empty(&page->mapping->i_mmap_shared)) {
		set_bit(PG_dcache_dirty, &page->flags);
	} else {
		__flush_dcache_page(page);
	}
}

#define flush_icache_page(vma,page)	do { flush_kernel_dcache_page(page_address(page)); flush_kernel_icache_page(page_address(page)); } while (0)

#define flush_icache_range(s,e)		do { flush_kernel_dcache_range_asm(s,e); flush_kernel_icache_range_asm(s,e); } while (0)

#define flush_icache_user_range(vma, page, addr, len) do { \
        flush_user_dcache_range(addr, addr + len); \
	flush_user_icache_range(addr, addr + len); } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { memcpy(dst, src, len); \
     flush_icache_user_range(vma, page, vaddr, len); \
} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

static inline void flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	int sr3;

	if (!vma->vm_mm->context) {
		BUG();
		return;
	}

	sr3 = mfsp(3);
	if (vma->vm_mm->context == sr3) {
		flush_user_dcache_range(start,end);
		flush_user_icache_range(start,end);
	} else {
		flush_cache_all();
	}
}

static inline void
flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	int sr3;

	if (!vma->vm_mm->context) {
		BUG();
		return;
	}

	sr3 = mfsp(3);
	if (vma->vm_mm->context == sr3) {
		flush_user_dcache_range(vmaddr,vmaddr + PAGE_SIZE);
		if (vma->vm_flags & VM_EXEC)
			flush_user_icache_range(vmaddr,vmaddr + PAGE_SIZE);
	} else {
		if (vma->vm_flags & VM_EXEC)
			flush_cache_all();
		else
			flush_data_cache();
	}
}
#endif

