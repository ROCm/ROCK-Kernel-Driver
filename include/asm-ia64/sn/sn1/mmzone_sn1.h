#ifndef _ASM_IA64_SN_MMZONE_SN1_H
#define _ASM_IA64_SN_MMZONE_SN1_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */

#include <linux/config.h>


/*
 * SGI SN1 Arch defined values
 *
 * 	An SN1 physical address is broken down as follows:
 *
 *             +-----------------------------------------+
 *             |         |    |      |   node offset     |
 *             | unused  | AS | node |-------------------|
 *             |         |    |      | cn | clump offset |
 *             +-----------------------------------------+
 *              6       4 4  4 3    3 3  3 2            0
 *              3       4 3  0 9    3 2  0 9            0
 *
 *		bits 63-44	Unused - must be zero
 *		bits 43-40	Address space ID. Cached memory has a value of 0.
 *				Chipset & IO addresses have non-zero values.
 *		bits 39-33	Node number. Note that some configurations do NOT
 *				have a node zero.
 *		bits 32-0	Node offset.
 *
 *	The node offset can be further broken down as:
 *		bits 32-30	Clump (bank) number.
 *		bits 29-0	Clump (bank) offset.
 *
 *	A node consists of up to 8 clumps (banks) of memory. A clump may be empty, or may be
 *	populated with a single contiguous block of memory starting at clump
 *	offset 0. The size of the block is (2**n) * 64MB, where 0<n<5.
 *
 *
 * NOTE: This file exports symbols prefixed with "PLAT_". Symbols prefixed with
 *	 "SN_" are intended for internal use only and should not be used in
 *	 any platform independent code. 
 *
 *	 This file is also responsible for exporting the following definitions:
 *		cnodeid_t	Define a compact node id. 
 */

typedef signed short cnodeid_t;

#define SN1_BANKS_PER_NODE		8
#define SN1_NODE_SIZE			(8UL*1024*1024*1024)	/* 8 GB per node */
#define SN1_BANK_SIZE			(SN1_NODE_SIZE/SN1_BANKS_PER_NODE)
#define SN1_NODE_SHIFT			33
#define SN1_NODE_MASK			0x7fUL
#define SN1_NODE_OFFSET_MASK		(SN1_NODE_SIZE-1)
#define SN1_NODE_NUMBER(addr)		(((unsigned long)(addr) >> SN1_NODE_SHIFT) & SN1_NODE_MASK)
#define SN1_NODE_CLUMP_NUMBER(addr)	(((unsigned long)(addr) >>30) & 7)
#define SN1_NODE_OFFSET(addr)		(((unsigned long)(addr)) & SN1_NODE_OFFSET_MASK)
#define SN1_KADDR(nasid, offset)	(((unsigned long)(nasid)<<SN1_NODE_SHIFT) | (offset) | PAGE_OFFSET)


#define PLAT_MAX_NODE_NUMBER		128			/* Maximum node number +1 */
#define PLAT_MAX_COMPACT_NODES		128			/* Maximum number of nodes in SSI */

#define PLAT_MAX_PHYS_MEMORY		(1UL << 40)



/*
 * On the SN platforms, a clump is the same as a memory bank.
 */
#define PLAT_CLUMPS_PER_NODE		SN1_BANKS_PER_NODE
#define PLAT_CLUMP_OFFSET(addr)		((unsigned long)(addr) & 0x3fffffffUL)
#define PLAT_CLUMPSIZE                  (SN1_NODE_SIZE/PLAT_CLUMPS_PER_NODE)
#define PLAT_MAXCLUMPS			(PLAT_CLUMPS_PER_NODE*PLAT_MAX_COMPACT_NODES)




/*
 * PLAT_VALID_MEM_KADDR returns a boolean to indicate if a kaddr is potentially a
 * valid cacheable identity mapped RAM memory address.
 * Note that the RAM may or may not actually be present!!
 */
#define SN1_VALID_KERN_ADDR_MASK	0xffffff0000000000UL
#define SN1_VALID_KERN_ADDR_VALUE	0xe000000000000000UL
#define PLAT_VALID_MEM_KADDR(kaddr)	(((unsigned long)(kaddr) & SN1_VALID_KERN_ADDR_MASK) == SN1_VALID_KERN_ADDR_VALUE)



/*
 * Memory is conceptually divided into chunks. A chunk is either
 * completely present, or else the kernel assumes it is completely
 * absent. Each node consists of a number of possibly discontiguous chunks.
 */
#define SN1_CHUNKSHIFT			26			/* 64 MB */
#define PLAT_CHUNKSIZE			(1UL << SN1_CHUNKSHIFT)
#define PLAT_CHUNKNUM(addr)		(((addr) & (PLAT_MAX_PHYS_MEMORY-1)) >> SN1_CHUNKSHIFT)


/*
 * Given a kaddr, find the nid (compact nodeid)
 */
#ifdef CONFIG_IA64_SGI_SN_DEBUG
#define DISCONBUG(kaddr)		panic("DISCONTIG BUG: line %d, %s. kaddr 0x%lx",	 	\
						__LINE__, __FILE__, (long)(kaddr))

#define KVADDR_TO_NID(kaddr)		({long _ktn=(long)(kaddr);					\
						kern_addr_valid(_ktn) ? 				\
						local_node_data->physical_node_map[SN1_NODE_NUMBER(_ktn)] :\
						(DISCONBUG(_ktn), 0UL);})
#else
#define KVADDR_TO_NID(kaddr)		(local_node_data->physical_node_map[SN1_NODE_NUMBER(kaddr)])
#endif



/*
 * Given a kaddr, find the index into the clump_mem_map_base array of the page struct entry
 * for the first page of the clump.
 */
#define PLAT_CLUMP_MEM_MAP_INDEX(kaddr)		({long _kmmi=(long)(kaddr);				\
							KVADDR_TO_NID(_kmmi) * PLAT_CLUMPS_PER_NODE +	\
							SN1_NODE_CLUMP_NUMBER(_kmmi);})


/*
 * Calculate a "goal" value to be passed to __alloc_bootmem_node for allocating structures on
 * nodes so that they dont alias to the same line in the cache as the previous allocated structure.
 * This macro takes an address of the end of previous allocation, rounds it to a page boundary & 
 * changes the node number.
 */
#define PLAT_BOOTMEM_ALLOC_GOAL(cnode,kaddr)	__pa(SN1_KADDR(PLAT_PXM_TO_PHYS_NODE_NUMBER(nid_to_pxm_map[cnode]),		\
						  (SN1_NODE_OFFSET(kaddr) + PAGE_SIZE - 1) >> PAGE_SHIFT << PAGE_SHIFT))




/*
 * Convert a proximity domain number (from the ACPI tables) into a physical node number.
 */

#define PLAT_PXM_TO_PHYS_NODE_NUMBER(pxm)	(pxm)

#endif /* _ASM_IA64_SN_MMZONE_SN1_H */
