/*
 *  linux/include/asm-arm/memory.h
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Note: this file should not be included by non-asm/.h files
 *
 *  Modifications:
 */
#ifndef __ASM_ARM_MEMORY_H
#define __ASM_ARM_MEMORY_H

#include <asm/arch/memory.h>

extern __inline__ unsigned long virt_to_phys(volatile void *x)
{
	return __virt_to_phys((unsigned long)(x));
}

extern __inline__ void *phys_to_virt(unsigned long x)
{
	return (void *)(__phys_to_virt((unsigned long)(x)));
}

/*
 * Virtual <-> DMA view memory address translations
 */
#define virt_to_bus(x)		(__virt_to_bus((unsigned long)(x)))
#define bus_to_virt(x)		((void *)(__bus_to_virt((unsigned long)(x))))

#endif
