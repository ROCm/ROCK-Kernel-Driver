/* include/asm-arm/arch-lh7a40x/memory.h
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H


#define BANKS_PER_NODE 1	/* Define as either 1 or 2 */

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	(0xc0000000UL)

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 */
#define __virt_to_bus(x)	 __virt_to_phys(x)
#define __bus_to_virt(x)	 __phys_to_virt(x)

#ifdef CONFIG_DISCONTIGMEM
/*
 * Because of the wide memory address space between physical RAM
 * banks, it's convenient to use Linux's NUMA support to represent our
 * memory map.  Assuming all memory nodes have equal access
 * characteristics, we then have a generic discontiguous memory setup.
 *
 * Of course, all this isn't mandatory for implementations with only
 * one used memory bank.  For those, simply undefine
 * CONFIG_DISCONTIGMEM.  However, keep in mind that a featurefull
 * system will need more than 4MiB of RAM.
 *
 * The contiguous memory blocks are small enough that it pays to
 * aggregate two banks into one node.  Setting BANKS_PER_NODE to 2
 * puts pairs of banks into a node.
 *
 * A typical layout would start like this:
 *
 *  node 0: 0xc0000000
 *          0xc1000000
 *  node 1: 0xc4000000
 *          0xc5000000
 *  node 2: 0xc8000000
 *          0xc9000000
 *
 * The proximity of the pairs of blocks makes it feasible to combine them.
 *
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */

#if BANKS_PER_NODE==1
#define KVADDR_TO_NID(addr) \
  (  ((((unsigned long) (addr) - PAGE_OFFSET) >> 24) &  1)\
   | ((((unsigned long) (addr) - PAGE_OFFSET) >> 25) & ~1))
#else  /* 2 banks per node */
#define KVADDR_TO_NID(addr) \
       ((unsigned long) (addr) - PAGE_OFFSET) >> 26)
#endif

/*
 * Given a page frame number, convert it to a node id.
 */

#if BANKS_PER_NODE==1
#define PFN_TO_NID(pfn) \
  (((((pfn) - PHYS_PFN_OFFSET) >> (24 - PAGE_SHIFT)) &  1)\
 | ((((pfn) - PHYS_PFN_OFFSET) >> (25 - PAGE_SHIFT)) & ~1))
#else  /* 2 banks per node */
#define PFN_TO_NID(addr) \
    (((pfn) - PHYS_PFN_OFFSET) >> (26 - PAGE_SHIFT))
#endif

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and return the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr)	NODE_MEM_MAP(KVADDR_TO_NID(kaddr))

/*
 * Given a page frame number, find the owning node of the memory
 * and return the mem_map of that node.
 */
#define PFN_TO_MAPBASE(pfn)	NODE_MEM_MAP(PFN_TO_NID(pfn))

/*
 * Given a kaddr, LOCAL_MEM_MAP finds the owning node of the memory
 * and returns the index corresponding to the appropriate page in the
 * node's mem_map.
 */

#if BANKS_PER_NODE==1
#define LOCAL_MAP_NR(addr) \
       (((unsigned long)(addr) & 0x003fffff) >> PAGE_SHIFT)
#else  /* 2 banks per node */
#define LOCAL_MAP_NR(addr) \
       (((unsigned long)(addr) & 0x01ffffff) >> PAGE_SHIFT)
#endif

#else

#define PFN_TO_NID(addr)	(0)

#endif

#endif
