#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>

#if !defined(HAVE_KMALLOC_SIZE_ROUNDUP)
#ifdef CONFIG_SLOB
/* copy from mm/slob.c */
size_t kmalloc_size_roundup(size_t size)
{
	/* Short-circuit the 0 size case. */
	if (unlikely(size == 0))
		return 0;
	/* Short-circuit saturated "too-large" case. */
	if (unlikely(size == SIZE_MAX))
		return SIZE_MAX;

	return ALIGN(size, ARCH_KMALLOC_MINALIGN);
}

EXPORT_SYMBOL(kmalloc_size_roundup);
#else
/* copy from mm/slab_common.c and modified for KCL usage. */
struct kmem_cache *(*_kcl_kmalloc_slab)(size_t size, gfp_t flags);
size_t kmalloc_size_roundup(size_t size)
{
	struct kmem_cache *c;

	/* Short-circuit the 0 size case. */
	if (unlikely(size == 0))
		return 0;
	/* Short-circuit saturated "too-large" case. */
	if (unlikely(size == SIZE_MAX))
		return SIZE_MAX;
	/* Above the smaller buckets, size is a multiple of page size. */
	if (size > KMALLOC_MAX_CACHE_SIZE)
		return PAGE_SIZE << get_order(size);

	/* The flags don't matter since size_index is common to all. */
	c = _kcl_kmalloc_slab(size, GFP_KERNEL);
	return c ? kmem_cache_size(c) : 0;
}
EXPORT_SYMBOL(kmalloc_size_roundup);
#endif
#endif
