/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Jan 2000
 * Copyright, 2000, Silicon Graphics, sprasad@engr.sgi.com
 */
#ifndef _LINUX_ASM_SN_MMZONE_H
#define _LINUX_ASM_SN_MMZONE_H

#include <linux/config.h>
#include <asm/sn/mmzone_sn1.h>
#include <asm/sn/sn_cpuid.h>

/*
 * Memory is conceptually divided into chunks. A chunk is either
 * completely present, or else the kernel assumes it is completely
 * absent. Each node consists of a number of contiguous chunks.
 */

#define CHUNKMASK       	(~(CHUNKSZ - 1))
#define CHUNKNUM(vaddr)        	(__pa(vaddr) >> CHUNKSHIFT)
#define PCHUNKNUM(paddr)        ((paddr) >> CHUNKSHIFT)

#define MAXCHUNKS      		(MAXNODES * MAX_CHUNKS_PER_NODE)

extern int chunktonid[];
#define CHUNKTONID(cnum)       (chunktonid[cnum])

typedef struct plat_pglist_data {
       pg_data_t       gendata;		/* try to keep this first. */
       unsigned long   virtstart;
       unsigned long   size;
} plat_pg_data_t;

extern plat_pg_data_t plat_node_data[];

extern int numa_debug(void);

/*
 * The foll two will move into linux/mmzone.h RSN.
 */
#define NODE_START(n)  plat_node_data[(n)].virtstart
#define NODE_SIZE(n)   plat_node_data[(n)].size

#define KVADDR_TO_NID(kaddr) \
       ((CHUNKTONID(CHUNKNUM((kaddr))) != -1) ? (CHUNKTONID(CHUNKNUM((kaddr)))) : \
       (printk("DISCONTIGBUG: %s line %d addr 0x%lx", __FILE__, __LINE__, \
       (unsigned long)(kaddr)), numa_debug()))
#if 0
#define KVADDR_TO_NID(kaddr) CHUNKTONID(CHUNKNUM((kaddr)))
#endif

/* These 2 macros should never be used if KVADDR_TO_NID(kaddr) is -1 */
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
#define LOCAL_BASE_ADDR(kaddr) NODE_START(KVADDR_TO_NID(kaddr))

#ifdef CONFIG_DISCONTIGMEM

/*
 * Return a pointer to the node data for node n.
 * Assume that n is the compact node id.
 */
#define NODE_DATA(n)   (&((plat_node_data + (n))->gendata))

/*
 * NODE_MEM_MAP gives the kaddr for the mem_map of the node.
 */
#define NODE_MEM_MAP(nid)      (NODE_DATA((nid))->node_mem_map)

/* This macro should never be used if KVADDR_TO_NID(kaddr) is -1 */
#define LOCAL_MAP_NR(kvaddr) \
        (((unsigned long)(kvaddr)-LOCAL_BASE_ADDR((kvaddr))) >> PAGE_SHIFT)
#define MAP_NR_SN1(kaddr)   (LOCAL_MAP_NR((kaddr)) + \
                (((unsigned long)ADDR_TO_MAPBASE((kaddr)) - PAGE_OFFSET) / \
                sizeof(mem_map_t)))
#if 0
#define MAP_NR_VALID(kaddr)   (LOCAL_MAP_NR((kaddr)) + \
                (((unsigned long)ADDR_TO_MAPBASE((kaddr)) - PAGE_OFFSET) / \
                sizeof(mem_map_t)))
#define MAP_NR_SN1(kaddr)	((KVADDR_TO_NID(kaddr) == -1) ? (max_mapnr + 1) :\
				MAP_NR_VALID(kaddr))
#endif

/* FIXME */
#define sn1_pte_pagenr(x)		MAP_NR_SN1(PAGE_OFFSET + (unsigned long)((pte_val(x)&_PFN_MASK) & PAGE_MASK))
#define pte_page(pte)			(mem_map + sn1_pte_pagenr(pte))
/* FIXME */

#define kern_addr_valid(addr)   ((KVADDR_TO_NID((unsigned long)addr) >= \
        numnodes) ? 0 : (test_bit(LOCAL_MAP_NR((addr)), \
        NODE_DATA(KVADDR_TO_NID((unsigned long)addr))->valid_addr_bitmap)))

#define virt_to_page(kaddr)	(mem_map + MAP_NR_SN1(kaddr))

#else /* CONFIG_DISCONTIGMEM */

#define MAP_NR_SN1(addr)	(((unsigned long) (addr) - PAGE_OFFSET) >> PAGE_SHIFT)

#endif /* CONFIG_DISCONTIGMEM */

#define numa_node_id()		cpuid_to_cnodeid(smp_processor_id())

#endif /* !_LINUX_ASM_SN_MMZONE_H */
