/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (c) 2002 NEC Corp.
 * Copyright (c) 2002 Erich Focht <efocht@ess.nec.de>
 * Copyright (c) 2002 Kimio Suganuma <k-suganuma@da.jp.nec.com>
 */
#ifndef _ASM_IA64_MMZONE_H
#define _ASM_IA64_MMZONE_H

#include <linux/config.h>
#include <linux/init.h>

/*
 * Given a kaddr, find the base mem_map address for the start of the mem_map
 * entries for the bank containing the kaddr.
 */
#define BANK_MEM_MAP_BASE(kaddr) local_node_data->bank_mem_map_base[BANK_MEM_MAP_INDEX(kaddr)]

/*
 * Given a kaddr, this macro return the relative map number 
 * within the bank.
 */
#define BANK_MAP_NR(kaddr) 	(BANK_OFFSET(kaddr) >> PAGE_SHIFT)

/*
 * Given a pte, this macro returns a pointer to the page struct for the pte.
 */
#define pte_page(pte)	virt_to_page(PAGE_OFFSET | (pte_val(pte)&_PFN_MASK))

/*
 * Determine if a kaddr is a valid memory address of memory that
 * actually exists. 
 *
 * The check consists of 2 parts:
 *	- verify that the address is a region 7 address & does not 
 *	  contain any bits that preclude it from being a valid platform
 *	  memory address
 *	- verify that the chunk actually exists.
 *
 * Note that IO addresses are NOT considered valid addresses.
 *
 * Note, many platforms can simply check if kaddr exceeds a specific size.  
 *	(However, this won't work on SGI platforms since IO space is embedded 
 * 	within the range of valid memory addresses & nodes have holes in the 
 *	address range between banks). 
 */
#define kern_addr_valid(kaddr)		({long _kav=(long)(kaddr);	\
					VALID_MEM_KADDR(_kav);})

/*
 * Given a kaddr, return a pointer to the page struct for the page.
 * If the kaddr does not represent RAM memory that potentially exists, return
 * a pointer the page struct for max_mapnr. IO addresses will
 * return the page for max_nr. Addresses in unpopulated RAM banks may
 * return undefined results OR may panic the system.
 *
 */
#define virt_to_page(kaddr)	({long _kvtp=(long)(kaddr);	\
				(VALID_MEM_KADDR(_kvtp))	\
					? BANK_MEM_MAP_BASE(_kvtp) + BANK_MAP_NR(_kvtp)	\
					: NULL;})

/*
 * Given a page struct entry, return the physical address that the page struct represents.
 * Since IA64 has all memory in the DMA zone, the following works:
 */
#define page_to_phys(page)	__pa(page_address(page))

#define node_mem_map(nid)	(NODE_DATA(nid)->node_mem_map)

#define node_localnr(pfn, nid)	((pfn) - NODE_DATA(nid)->node_start_pfn)

#define pfn_to_page(pfn)	(struct page *)(node_mem_map(pfn_to_nid(pfn)) + node_localnr(pfn, pfn_to_nid(pfn)))

#define pfn_to_nid(pfn)		 local_node_data->node_id_map[(pfn << PAGE_SHIFT) >> BANKSHIFT]

#define page_to_pfn(page)	(long)((page - page_zone(page)->zone_mem_map) + page_zone(page)->zone_start_pfn)


/*
 * pfn_valid should be made as fast as possible, and the current definition
 * is valid for machines that are NUMA, but still contiguous, which is what
 * is currently supported. A more generalised, but slower definition would
 * be something like this - mbligh:
 * ( pfn_to_pgdat(pfn) && (pfn < node_end_pfn(pfn_to_nid(pfn))) )
 */
#define pfn_valid(pfn)          (pfn < max_low_pfn)
extern unsigned long max_low_pfn;


#ifdef CONFIG_IA64_DIG

/*
 * Platform definitions for DIG platform with contiguous memory.
 */
#define MAX_PHYSNODE_ID	8	/* Maximum node number +1 */
#define NR_NODES	8	/* Maximum number of nodes in SSI */

#define MAX_PHYS_MEMORY	(1UL << 40)	/* 1 TB */

/*
 * Bank definitions.
 * Configurable settings for DIG: 512MB/bank:  16GB/node,
 *                               2048MB/bank:  64GB/node,
 *                               8192MB/bank: 256GB/node.
 */
#define NR_BANKS_PER_NODE	32
#if defined(CONFIG_IA64_NODESIZE_16GB)
# define BANKSHIFT		29
#elif defined(CONFIG_IA64_NODESIZE_64GB)
# define BANKSHIFT		31
#elif defined(CONFIG_IA64_NODESIZE_256GB)
# define BANKSHIFT		33
#else
# error Unsupported bank and nodesize!
#endif
#define BANKSIZE		(1UL << BANKSHIFT)
#define BANK_OFFSET(addr)	((unsigned long)(addr) & (BANKSIZE-1))
#define NR_BANKS		(NR_BANKS_PER_NODE * NR_NODES)

/*
 * VALID_MEM_KADDR returns a boolean to indicate if a kaddr is
 * potentially a valid cacheable identity mapped RAM memory address.
 * Note that the RAM may or may not actually be present!!
 */
#define VALID_MEM_KADDR(kaddr)	1

/*
 * Given a nodeid & a bank number, find the address of the mem_map
 * entry for the first page of the bank.
 */
#define BANK_MEM_MAP_INDEX(kaddr) \
	(((unsigned long)(kaddr) & (MAX_PHYS_MEMORY-1)) >> BANKSHIFT)

#elif defined(CONFIG_IA64_SGI_SN2)
/*
 * SGI SN2 discontig definitions
 */
#define MAX_PHYSNODE_ID	2048	/* 2048 node ids (also called nasid) */
#define NR_NODES	128	/* Maximum number of nodes in SSI */
#define MAX_PHYS_MEMORY	(1UL << 49)

#define BANKSHIFT		38
#define NR_BANKS_PER_NODE	4
#define SN2_NODE_SIZE		(64UL*1024*1024*1024)	/* 64GB per node */
#define BANKSIZE		(SN2_NODE_SIZE/NR_BANKS_PER_NODE)
#define BANK_OFFSET(addr)	((unsigned long)(addr) & (BANKSIZE-1))
#define NR_BANKS		(NR_BANKS_PER_NODE * NR_NODES)
#define VALID_MEM_KADDR(kaddr)	1

/*
 * Given a nodeid & a bank number, find the address of the mem_map
 * entry for the first page of the bank.
 */
#define BANK_MEM_MAP_INDEX(kaddr) \
	(((unsigned long)(kaddr) & (MAX_PHYS_MEMORY-1)) >> BANKSHIFT)

#endif /* CONFIG_IA64_DIG */
#endif /* _ASM_IA64_MMZONE_H */
