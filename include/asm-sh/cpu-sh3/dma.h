#ifndef __ASM_CPU_SH3_DMA_H
#define __ASM_CPU_SH3_DMA_H

#define SAR ((unsigned long[]){0xa4000020,0xa4000030,0xa4000040,0xa4000050})
#define DAR ((unsigned long[]){0xa4000024,0xa4000034,0xa4000044,0xa4000054})
#define DMATCR ((unsigned long[]){0xa4000028,0xa4000038,0xa4000048,0xa4000058})
#define CHCR ((unsigned long[]){0xa400002c,0xa400003c,0xa400004c,0xa400005c})
#define DMAOR 0xa4000060UL

#endif /* __ASM_CPU_SH3_DMA_H */

