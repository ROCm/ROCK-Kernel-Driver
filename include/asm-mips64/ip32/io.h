#ifndef __ASM_IP32_IO_H__
#define __ASM_IP32_IO_H__

#include <asm/ip32/mace.h>

/*#ifdef CONFIG_MIPS_UNCACHED*/
#define UNCACHEDADDR(x) (0x9000000000000000UL | (u64)(x))
/*#else
#define UNCACHEDADDR(x) (x)
#endif*/
/*#define UNCACHEDADDR(x) (KSEG1ADDR (x)) */
#define IO_SPACE_BASE UNCACHEDADDR (MACEPCI_HI_MEMORY)
#define IO_SPACE_LIMIT 0xffffffffUL

#endif
