#ifndef __ASSEMBLY__

void xen_start_kernel(void);
void xen_arch_setup(void);

#ifdef CONFIG_X86_64
void reserve_pfn_range(unsigned long pfn, unsigned long nr, char *);
void reserve_pgtable_low(void);
#endif

extern unsigned long xen_initrd_start;

#endif

#include_next <asm/setup.h>
