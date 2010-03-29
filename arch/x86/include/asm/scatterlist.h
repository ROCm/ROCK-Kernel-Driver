#ifndef _ASM_X86_SCATTERLIST_H
#define _ASM_X86_SCATTERLIST_H

#define ISA_DMA_THRESHOLD (0x00ffffff)

#ifdef CONFIG_X86_XEN
# define sg_dma_len(sg)		((sg)->dma_length)
#endif

#include <asm-generic/scatterlist.h>

#endif /* _ASM_X86_SCATTERLIST_H */
