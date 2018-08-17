#ifndef AMDKCL_HIGHMEM_BACKPORT_H
#define AMDKCL_HIGHMEM_BACKPORT_H

#include <linux/highmem.h>

#if !defined(HAVE_KMAP_ATOMIC_ONE_ARG)
static inline void *_kcl_kmap_atomic(struct page *page)
{
	return kmap_atomic(page, KM_USER0);
}
static inline void _kcl_kunmap_atomic(void *addr)
{
	kunmap_atomic(addr, KM_USER0);
}
#ifdef kmap_atomic_prot
#undef kmap_atomic_prot
#endif
#ifdef kunmap_atomic
#undef kunmap_atomic
#endif
#define kmap_atomic _kcl_kmap_atomic
#define kmap_atomic_prot(page, prot) kmap_atomic(page)
#define kunmap_atomic _kcl_kunmap_atomic
#endif
#endif
