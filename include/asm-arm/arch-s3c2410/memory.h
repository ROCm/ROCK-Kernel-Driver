/*
 *  linux/include/asm-arm/arch-s3c2410/memory.h
 *
 *  from linux/include/asm-arm/arch-rpc/memory.h
 *
 *  Copyright (C) 1996,1997,1998 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   20-Oct-1996 RMK	Created
 *   31-Dec-1997 RMK	Fixed definitions to reduce warnings
 *   11-Jan-1998 RMK	Uninlined to reduce hits on cache
 *   08-Feb-1998 RMK	Added __virt_to_bus and __bus_to_virt
 *   21-Mar-1999 RMK	Renamed to memory.h
 *		 RMK	Added TASK_SIZE and PAGE_OFFSET
 *   05-Apr-2004 BJD    Copied and altered for arch-s3c2410
*/

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Task size: 3GB
 */
#define TASK_SIZE	(0xbf000000UL)
#define TASK_SIZE_26	(0x04000000UL)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE (0x40000000)

/*
 * Page offset: 3GB
 *
 * DRAM starts at 0x30000000
*/

#define PAGE_OFFSET	(0xc0000000UL)
#define PHYS_OFFSET	(0x30000000UL)

#define __virt_to_phys(vpage) ((vpage) - PAGE_OFFSET + PHYS_OFFSET)
#define __phys_to_virt(ppage) ((ppage) + PAGE_OFFSET - PHYS_OFFSET)

/*
 * These are exactly the same on the S3C2410 as the
 * physical memory view.
*/

#define __virt_to_bus(x) __virt_to_phys(x)
#define __bus_to_virt(x) __phys_to_virt(x)

#endif
