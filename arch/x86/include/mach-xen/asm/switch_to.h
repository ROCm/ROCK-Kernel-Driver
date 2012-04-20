#ifndef _ASM_X86_SWITCH_TO_H

#define __switch_to_xtra(prev, next, tss) __switch_to_xtra(prev, next)

#include_next <asm/switch_to.h>

#undef __switch_to_xtra

#endif /* _ASM_X86_SWITCH_TO_H */
