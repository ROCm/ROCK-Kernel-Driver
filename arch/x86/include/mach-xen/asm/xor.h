#ifndef _ASM_X86_XEN_XOR_H
#define _ASM_X86_XEN_XOR_H

#include_next <asm/xor.h>

#undef XOR_SELECT_TEMPLATE

#ifdef CONFIG_X86_64

/* Also try the generic routines.  */
#undef XOR_TRY_TEMPLATES
#include <asm-generic/xor.h>

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES			\
do {						\
	xor_speed(&xor_block_8regs);		\
	xor_speed(&xor_block_8regs_p);		\
	xor_speed(&xor_block_32regs);		\
	xor_speed(&xor_block_32regs_p);		\
	xor_speed(&xor_block_sse);		\
	AVX_XOR_SPEED;				\
} while (0)

#endif

#endif /* _ASM_X86_XEN_XOR_H */
