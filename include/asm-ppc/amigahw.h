/*
 * BK Id: SCCS/s.amigahw.h 1.5 05/17/01 18:14:24 cort
 */
#ifdef __KERNEL__
#ifndef __ASMPPC_AMIGAHW_H
#define __ASMPPC_AMIGAHW_H

#include <linux/config.h>
#include <asm-m68k/amigahw.h>

#undef CHIP_PHYSADDR
#ifdef CONFIG_APUS_FAST_EXCEPT
#define CHIP_PHYSADDR      (0x000000)
#else
#define CHIP_PHYSADDR      (0x004000)
#endif


#endif /* __ASMPPC_AMIGAHW_H */
#endif /* __KERNEL__ */
