/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <linux/config.h>
#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/klkernvars.h>

typedef struct plat_pglist_data {
	pg_data_t	gendata;
	kern_vars_t	kern_vars;
} plat_pg_data_t;

/*
 * Following are macros that are specific to this numa platform.
 */

extern int numa_debug(void);
extern plat_pg_data_t *plat_node_data[];

#define PHYSADDR_TO_NID(pa)		NASID_TO_COMPACT_NODEID(NASID_GET(pa))
#define PLAT_NODE_DATA(n)		(plat_node_data[n])
#define PLAT_NODE_DATA_SIZE(n)	     (PLAT_NODE_DATA(n)->gendata.node_spanned_pages)
#define PLAT_NODE_DATA_LOCALNR(p, n) \
		(((p) >> PAGE_SHIFT) - PLAT_NODE_DATA(n)->gendata.node_start_pfn)

#ifdef CONFIG_DISCONTIGMEM

/*
 * Following are macros that each numa implmentation must define.
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(kaddr) \
	((NASID_TO_COMPACT_NODEID(NASID_GET(__pa(kaddr))) != -1) ? \
	(NASID_TO_COMPACT_NODEID(NASID_GET(__pa(kaddr)))) : \
	(printk("NUMABUG: %s line %d addr 0x%lx", __FILE__, __LINE__, kaddr), \
	numa_debug(), -1))

/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(n)	(&((PLAT_NODE_DATA(n))->gendata))

/*
 * NODE_MEM_MAP gives the kaddr for the mem_map of the node.
 */
#define NODE_MEM_MAP(nid)	(NODE_DATA(nid)->node_mem_map)

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and returns the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr) \
			NODE_MEM_MAP(KVADDR_TO_NID((unsigned long)(kaddr)))

/*
 * Given a kaddr, LOCAL_BASE_ADDR finds the owning node of the memory
 * and returns the kaddr corresponding to first physical page in the
 * node's mem_map.
 */
#define LOCAL_BASE_ADDR(kaddr)	((unsigned long)(kaddr) & ~(NODE_MAX_MEM_SIZE-1))

#define LOCAL_MAP_NR(kvaddr) \
	(((unsigned long)(kvaddr)-LOCAL_BASE_ADDR((kvaddr))) >> PAGE_SHIFT)

#define MIPS64_NR(kaddr) (((unsigned long)(kaddr) > (unsigned long)high_memory)\
		? (max_mapnr + 1) : (LOCAL_MAP_NR((kaddr)) + \
		(((unsigned long)ADDR_TO_MAPBASE((kaddr)) - PAGE_OFFSET) / \
		sizeof(struct page))))

#define kern_addr_valid(addr)	((KVADDR_TO_NID((unsigned long)addr) > \
	-1) ? 0 : (test_bit(LOCAL_MAP_NR((addr)), \
	NODE_DATA(KVADDR_TO_NID((unsigned long)addr))->valid_addr_bitmap)))

#define pfn_to_page(pfn)	(mem_map + (pfn))
#define page_to_pfn(page) \
	((((page)-(page)->zone->zone_mem_map) + (page)->zone->zone_start_pfn) \
	 << PAGE_SHIFT)
#define virt_to_page(kaddr)	pfn_to_page(MIPS64_NR(kaddr))

#define pfn_valid(pfn)		((pfn) < max_mapnr)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)
#define pte_pfn(x)		((unsigned long)((x).pte >> PAGE_SHIFT))
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

#endif /* CONFIG_DISCONTIGMEM */

#endif /* _ASM_MMZONE_H_ */
