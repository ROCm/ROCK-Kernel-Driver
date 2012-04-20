#ifndef _ASM_X86_XEN_CMPXCHG_H
#define _ASM_X86_XEN_CMPXCHG_H

#include_next <asm/cmpxchg.h>
#ifdef CONFIG_X86_32
# include "cmpxchg_32.h"
#else
# include "cmpxchg_64.h"
#endif

#endif	/* _ASM_X86_XEN_CMPXCHG_H */
