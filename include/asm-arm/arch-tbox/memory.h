/*
 * linux/include/asm-arm/arch-tbox/memory.h
 *
 * Copyright (c) 1996-1999 Russell King.
 * Copyright (c) 1998-1999 Phil Blundell
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET		(0x80000000UL)

/*
 * Bus view is the same as physical view
 */
#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

#endif
