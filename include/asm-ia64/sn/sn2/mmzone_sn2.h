#ifndef _ASM_IA64_SN_MMZONE_SN2_H
#define _ASM_IA64_SN_MMZONE_SN2_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */

#include <linux/config.h>


/*
 * SGI SN2 Arch defined values
 *
 * 	An SN2 physical address is broken down as follows:
 *
 *             +-----------------------------------------+
 *             |         |      |    |   node offset     |
 *             | unused  | node | AS |-------------------|
 *             |         |      |    | cn | clump offset |
 *             +-----------------------------------------+
 *              6       4 4    3 3  3 3  3 3            0
 *              3       9 8    8 7  6 5  4 3            0
 *
 *		bits 63-49	Unused - must be zero
 *		bits 48-38	Node number. Note that some configurations do NOT
 *				have a node zero.
 *		bits 37-36	Address space ID. Cached memory has a value of 3 (!!!).
 *				Chipset & IO addresses have other values.
 *				  (Yikes!! The hardware folks hate us...)
 *		bits 35-0	Node offset.
 *
 *	The node offset can be further broken down as:
 *		bits 35-34	Clump (bank) number.
 *		bits 33-0	Clump (bank) offset.
 *
 *	A node consists of up to 4 clumps (banks) of memory. A clump may be empty, or may be
 *	populated with a single contiguous block of memory starting at clump
 *	offset 0. The size of the block is (2**n) * 64MB, where 0<n<9.
 *
 *	Important notes:
 *		- IO space addresses are embedded with the range of valid memory addresses.
 *		- All cached memory addresses have bits 36 & 37 set to 1's.
 *		- There is no physical address 0.
 *
 * NOTE: This file exports symbols prefixed with "PLAT_". Symbols prefixed with
 *	 "SN_" are intended for internal use only and should not be used in
 *	 any platform independent code. 
 *
 *	 This file is also responsible for exporting the following definitions:
 *		cnodeid_t	Define a compact node id. 
 */

typedef signed short cnodeid_t;

#define SN2_BANKS_PER_NODE		4
#define SN2_NODE_SIZE			(64UL*1024*1024*1024)	/* 64GB per node */
#define SN2_BANK_SIZE			(SN2_NODE_SIZE/SN2_BANKS_PER_NODE)
#define SN2_NODE_SHIFT			38
#define SN2_NODE_MASK			0x7ffUL
#define SN2_NODE_OFFSET_MASK		(SN2_NODE_SIZE-1)
#define SN2_NODE_NUMBER(addr)		(((unsigned long)(addr) >> SN2_NODE_SHIFT) & SN2_NODE_MASK)
#define SN2_NODE_CLUMP_NUMBER(kaddr)	(((unsigned long)(kaddr) >>34) & 3)
#define SN2_NODE_OFFSET(addr)		(((unsigned long)(addr)) & SN2_NODE_OFFSET_MASK)
#define SN2_KADDR(nasid, offset)	(((unsigned long)(nasid)<<SN2_NODE_SHIFT) | (offset) | SN2_PAGE_OFFSET)
#define SN2_PAGE_OFFSET			0xe000003000000000UL      /* Cacheable memory space */


#define PLAT_MAX_NODE_NUMBER		2048			/* Maximum node number + 1 */
#define PLAT_MAX_COMPACT_NODES		128			/* Maximum number of nodes in SSI system */

#define PLAT_MAX_PHYS_MEMORY		(1UL << 49)



/*
 * On the SN platforms, a clump is the same as a memory bank.
 */
#define PLAT_CLUMPS_PER_NODE		SN2_BANKS_PER_NODE
#define PLAT_CLUMP_OFFSET(addr)		((unsigned long)(addr) & 0x3ffffffffUL)
#define PLAT_CLUMPSIZE			(SN2_NODE_SIZE/PLAT_CLUMPS_PER_NODE)
#define PLAT_MAXCLUMPS			(PLAT_CLUMPS_PER_NODE * PLAT_MAX_COMPACT_NODES)



/*
 * PLAT_VALID_MEM_KADDR returns a boolean to indicate if a kaddr is potentially a
 * valid cacheable identity mapped RAM memory address.
 * Note that the RAM may or may not actually be present!!
 */
#define SN2_VALID_KERN_ADDR_MASK	0xffff003000000000UL
#define SN2_VALID_KERN_ADDR_VALUE	0xe000003000000000UL
#define PLAT_VALID_MEM_KADDR(kaddr)	(((unsigned long)(kaddr) & SN2_VALID_KERN_ADDR_MASK) == SN2_VALID_KERN_ADDR_VALUE)



/*
 * Memory is conceptually divided into chunks. A chunk is either
 * completely present, or else the kernel assumes it is completely
 * absent. Each node consists of a number of possibly contiguous chunks.
 */
#define SN2_CHUNKSHIFT			25			/* 32 MB */
#define PLAT_CHUNKSIZE			(1UL << SN2_CHUNKSHIFT)
#define PLAT_CHUNKNUM(addr)		({unsigned long _p=(unsigned long)(addr);		\
						(((_p&SN2_NODE_MASK)>>2) | 			\
						(_p&SN2_NODE_OFFSET_MASK)) >>SN2_CHUNKSHIFT;})

/*
 * Given a kaddr, find the nid (compact nodeid)
 */
#ifdef CONFIG_IA64_SGI_SN_DEBUG
#define DISCONBUG(kaddr)		panic("DISCONTIG BUG: line %d, %s. kaddr 0x%lx",	 	\
						__LINE__, __FILE__, (long)(kaddr))

#define KVADDR_TO_NID(kaddr)		({long _ktn=(long)(kaddr);					\
						kern_addr_valid(_ktn) ? 				\
						local_node_data->physical_node_map[SN2_NODE_NUMBER(_ktn)] :	\
						(DISCONBUG(_ktn), 0UL);})
#else
#define KVADDR_TO_NID(kaddr)		(local_node_data->physical_node_map[SN2_NODE_NUMBER(kaddr)])
#endif



/*
 * Given a kaddr, find the index into the clump_mem_map_base array of the page struct entry 
 * for the first page of the clump.
 */
#define PLAT_CLUMP_MEM_MAP_INDEX(kaddr)		({long _kmmi=(long)(kaddr);				\
							KVADDR_TO_NID(_kmmi) * PLAT_CLUMPS_PER_NODE +	\
							SN2_NODE_CLUMP_NUMBER(_kmmi);})



/*
 * Calculate a "goal" value to be passed to __alloc_bootmem_node for allocating structures on
 * nodes so that they dont alias to the same line in the cache as the previous allocated structure.
 * This macro takes an address of the end of previous allocation, rounds it to a page boundary & 
 * changes the node number.
 */
#define PLAT_BOOTMEM_ALLOC_GOAL(cnode,kaddr)	__pa(SN2_KADDR(PLAT_PXM_TO_PHYS_NODE_NUMBER(nid_to_pxm_map[cnode]),	       \
						 (SN2_NODE_OFFSET(kaddr) + PAGE_SIZE - 1) >> PAGE_SHIFT << PAGE_SHIFT))




/*
 * Convert a proximity domain number (from the ACPI tables) into a physical node number.
 *	Note: on SN2, the promity domain number is the same as bits [8:1] of the NASID. The following
 *	algorithm relies on:
 *		- bit 0 of the NASID for cpu nodes is always 0
 *		- bits [10:9] of all NASIDs in a partition are always the same
 *		- hard_smp_processor_id return the SAPIC of the current cpu &
 *			bits 0..11 contain the NASID.
 *
 *	All of this complexity is because MS architectually limited proximity domain numbers to
 *	8 bits. 
 */

#define PLAT_PXM_TO_PHYS_NODE_NUMBER(pxm)	(((pxm)<<1) | (hard_smp_processor_id() & 0x300))

#endif /* _ASM_IA64_SN_MMZONE_SN2_H */
