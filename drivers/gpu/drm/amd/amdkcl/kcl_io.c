/* SPDX-License-Identifier: MIT */
#include <linux/module.h>
#include <linux/io.h>
#include "kcl_common.h"

#if !defined(HAVE_ARCH_IO_RESERVE_FREE_MEMTYPE_WC) && \
	defined(CONFIG_X86)
#include <asm/pgtable_types.h>

static int (*_kcl_io_reserve_memtype)(resource_size_t start, resource_size_t end,
			enum page_cache_mode *type);
static void (*_kcl_io_free_memtype)(resource_size_t start, resource_size_t end);

int _kcl_arch_io_reserve_memtype_wc(resource_size_t start, resource_size_t size)
{
#ifdef _PAGE_CACHE_WC
	unsigned long type = _PAGE_CACHE_WC;
#else
	enum page_cache_mode type = _PAGE_CACHE_MODE_WC;
#endif

	return _kcl_io_reserve_memtype(start, start + size, &type);
}
EXPORT_SYMBOL(_kcl_arch_io_reserve_memtype_wc);

void _kcl_arch_io_free_memtype_wc(resource_size_t start, resource_size_t size)
{
	_kcl_io_free_memtype(start, start + size);
}
EXPORT_SYMBOL(_kcl_arch_io_free_memtype_wc);

void amdkcl_io_init(void)
{
	_kcl_io_reserve_memtype = amdkcl_fp_setup("io_reserve_memtype", NULL);
	_kcl_io_free_memtype = amdkcl_fp_setup("io_free_memtype", NULL);
}
#else
void amdkcl_io_init(void)
{

}
#endif /* HAVE_ARCH_IO_RESERVE_FREE_MEMTYPE_WC */
