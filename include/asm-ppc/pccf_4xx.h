#ifndef __ASM_PCCF_4XX_H
#define __ASM_PCCF_4XX_H

/* Areas that we ioremap() are mapped only as large as necessary to get the job
   done: Only a few locations of the macro space are used, and legacy IO space
   is only 64 KB. There is 1 memory window, and 2 virtual IO windows. */

#define PCCF_4XX_MACRO_OFFSET	0x00000000
#define PCCF_4XX_MACRO_PADDR	(PCCF_4XX_PADDR + PCCF_4XX_MACRO_OFFSET)
#define PCCF_4XX_MACRO_WINSIZE	PAGE_SIZE

#define PCCF_4XX_MEM_OFFSET	0x01000000
#define PCCF_4XX_MEM_PADDR	(PCCF_4XX_PADDR + PCCF_4XX_MEM_OFFSET)
#define PCCF_4XX_MEM_WINSIZE	(8 * 1024 * 1024)

#define PCCF_4XX_IO_OFFSET	0x01800000
#define PCCF_4XX_IO_PADDR	(PCCF_4XX_PADDR + PCCF_4XX_IO_OFFSET)
#define PCCF_4XX_IO_WINSIZE	0x00010000

/* These are declared here, since the pccf_4xx driver needs them, but
 * must be defined and initialized by the board setup code. */
extern volatile u16 *pccf_4xx_macro_vaddr;
extern unsigned long pccf_4xx_io_base;
extern unsigned long pccf_4xx_mem_base;

#endif /* __ASM_PCCF_4XX_H */

