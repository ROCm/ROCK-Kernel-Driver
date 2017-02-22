#ifndef AMDKCL_IO_H
#define AMDKCL_IO_H
#include <linux/version.h>
#include <linux/types.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)

#ifdef CONFIG_X86_PAT
extern int arch_io_reserve_memtype_wc(resource_size_t start, resource_size_t size);
extern void arch_io_free_memtype_wc(resource_size_t start, resource_size_t size);
#define arch_io_reserve_memtype_wc arch_io_reserve_memtype_wc
#endif

/*
 * On x86 PAT systems we have memory tracking that keeps track of
 * the allowed mappings on memory ranges. This tracking works for
 * all the in-kernel mapping APIs (ioremap*), but where the user
 * wishes to map a range from a physical device into user memory
 * the tracking won't be updated. This API is to be used by
 * drivers which remap physical device pages into userspace,
 * and wants to make sure they are mapped WC and not UC.
 */
#ifndef arch_io_reserve_memtype_wc
static inline int arch_io_reserve_memtype_wc(resource_size_t base,
					     resource_size_t size)
{
	return 0;
}

static inline void arch_io_free_memtype_wc(resource_size_t base,
					   resource_size_t size)
{
}
#endif

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0) */

#endif /* AMDKCL_IO_H */
