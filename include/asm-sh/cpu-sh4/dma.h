#ifndef __ASM_CPU_SH4_DMA_H
#define __ASM_CPU_SH4_DMA_H

#define SAR ((unsigned long[]){0xbfa00000,0xbfa00010,0xbfa00020,0xbfa00030})
#define DAR ((unsigned long[]){0xbfa00004,0xbfa00014,0xbfa00024,0xbfa00034})
#define DMATCR ((unsigned long[]){0xbfa00008,0xbfa00018,0xbfa00028,0xbfa00038})
#define CHCR ((unsigned long[]){0xbfa0000c,0xbfa0001c,0xbfa0002c,0xbfa0003c})
#define DMAOR 0xbfa00040UL

#endif /* __ASM_CPU_SH4_DMA_H */

