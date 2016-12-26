#include <kcl/kcl_io.h>
#include "kcl_common.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
#ifdef CONFIG_X86_PAT
static int (*_kcl_io_reserve_memtype)(resource_size_t start, resource_size_t end,
			enum page_cache_mode *type);
static void (*_kcl_io_free_memtype)(resource_size_t start, resource_size_t end);

int arch_io_reserve_memtype_wc(resource_size_t start, resource_size_t size)
{
	enum page_cache_mode type = _PAGE_CACHE_MODE_WC;

	return _kcl_io_reserve_memtype(start, start + size, &type);
}
EXPORT_SYMBOL(arch_io_reserve_memtype_wc);

void arch_io_free_memtype_wc(resource_size_t start, resource_size_t size)
{
	_kcl_io_free_memtype(start, start + size);
}
EXPORT_SYMBOL(arch_io_free_memtype_wc);

void amdkcl_io_init(void)
{
	_kcl_io_reserve_memtype = amdkcl_fp_setup("io_reserve_memtype", NULL);
	_kcl_io_free_memtype = amdkcl_fp_setup("io_free_memtype", NULL);
}
#endif
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0) */
