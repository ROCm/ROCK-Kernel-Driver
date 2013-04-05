#ifndef __ASSEMBLY__

void xen_start_kernel(void);
void xen_arch_setup(void);

extern unsigned long xen_initrd_start;

#ifdef CONFIG_EFI
void efi_probe(void);
#else
#define efi_probe() ((void)0)
#endif

#endif

#include_next <asm/setup.h>
