#ifndef _ASM_X86_XEN_EFI_H
#define _ASM_X86_XEN_EFI_H

#include_next <asm/efi.h>

int efi_set_rtc_mmss(const struct timespec *);

#endif /* _ASM_X86_XEN_EFI_H */
