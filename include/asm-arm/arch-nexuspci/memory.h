/*
 * linux/include/asm-arm/arch-nexuspci/memory.h
 *
 * Copyright (c) 1997, 1998, 2000 FutureTV Labs Ltd.
 * Copyright (c) 1999 Russell King
 *
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	(0x40000000UL)
#define BUS_OFFSET	(0xe0000000UL)

/*
 * On the PCI bus the DRAM appears at address 0xe0000000
 */
#define __virt_to_bus(x) ((unsigned long)(x) - PAGE_OFFSET + BUS_OFFSET)
#define __bus_to_virt(x) ((unsigned long)(x) + PAGE_OFFSET - BUS_OFFSET)

#endif
