#ifndef __X86_MM_INTERNAL_H
#define __X86_MM_INTERNAL_H

void *alloc_low_pages(unsigned int num);
static inline void *alloc_low_page(void)
{
	return alloc_low_pages(1);
}

void early_ioremap_page_table_range_init(void);

unsigned long kernel_physical_mapping_init(unsigned long start,
					     unsigned long end,
					     unsigned long page_size_mask);
#ifdef CONFIG_X86_64_XEN
void xen_finish_init_mapping(void);
#else
static inline void xen_finish_init_mapping(void) {}
#endif
void zone_sizes_init(void);

bool in_pgt_buf(unsigned long paddr);

extern int after_bootmem;

#endif	/* __X86_MM_INTERNAL_H */
