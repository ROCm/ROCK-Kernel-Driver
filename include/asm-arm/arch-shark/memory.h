/*
 * linux/include/asm-arm/arch-shark/memory.h
 *
 * by Alexander Schulz
 *
 * derived from:
 * linux/include/asm-arm/arch-ebsa110/memory.h
 * Copyright (c) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET     (0x08000000UL)

#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

#endif
