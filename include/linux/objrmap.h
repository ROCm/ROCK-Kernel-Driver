#ifndef _LINUX_OBJRMAP_H
#define _LINUX_OBJRMAP_H
/*
 * Declarations for Object Reverse Mapping functions in mm/objrmap.c
 */
#include <linux/config.h>

#ifdef CONFIG_MMU

#include <linux/linkage.h>
#include <linux/slab.h>
#include <linux/kernel.h>

extern kmem_cache_t * anon_vma_cachep;

#define page_map_lock(page)	bit_spin_lock(PG_maplock, &page->flags)
#define page_map_unlock(page)	bit_spin_unlock(PG_maplock, &page->flags)

static inline void anon_vma_free(anon_vma_t * anon_vma)
{
	kmem_cache_free(anon_vma_cachep, anon_vma);
}

static inline anon_vma_t * anon_vma_alloc(void)
{
	return kmem_cache_alloc(anon_vma_cachep, SLAB_KERNEL);
}

static inline void anon_vma_lock(struct vm_area_struct * vma)
{
	anon_vma_t * anon_vma = vma->anon_vma;
	if (anon_vma)
		spin_lock(&anon_vma->anon_vma_lock);
}

static inline void anon_vma_unlock(struct vm_area_struct * vma)
{
	anon_vma_t * anon_vma = vma->anon_vma;
	if (anon_vma)
		spin_unlock(&anon_vma->anon_vma_lock);
}

/*
 * anon_vma helper functions. The one starting with __ requires
 * the caller to hold the anon_vma_lock, the other takes it
 * internally.
 */
extern int FASTCALL(anon_vma_prepare(struct vm_area_struct * vma));
extern void FASTCALL(anon_vma_merge(struct vm_area_struct * vma,
				    struct vm_area_struct * vma_dying));
extern void FASTCALL(anon_vma_unlink(struct vm_area_struct * vma));
extern void FASTCALL(anon_vma_link(struct vm_area_struct * vma));
extern void FASTCALL(__anon_vma_link(struct vm_area_struct * vma));

/* objrmap tracking functions */
void FASTCALL(page_add_rmap(struct page *, struct vm_area_struct *, unsigned long, int));
void FASTCALL(page_remove_rmap(struct page *));

/*
 * Called from mm/vmscan.c to handle paging out
 */
int FASTCALL(try_to_unmap(struct page *));
int FASTCALL(page_referenced(struct page *));

/*
 * Return values of try_to_unmap
 */
#define SWAP_SUCCESS	0
#define SWAP_AGAIN	1
#define SWAP_FAIL	2

#else	/* !CONFIG_MMU */

#define page_referenced(page)	TestClearPageReferenced(page)

#endif /* CONFIG_MMU */

#endif /* _LINUX_OBJRMAP_H */
