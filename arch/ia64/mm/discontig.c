/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Tony Luck <tony.luck@intel.com>
 * Copyright (c) 2002 NEC Corp.
 * Copyright (c) 2002 Kimio Suganuma <k-suganuma@da.jp.nec.com>
 */

/*
 * Platform initialization for Discontig Memory
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/acpi.h>
#include <linux/efi.h>


/*
 * Round an address upward to the next multiple of GRANULE size.
 */
#define GRANULEROUNDUP(n) (((n)+IA64_GRANULE_SIZE-1) & ~(IA64_GRANULE_SIZE-1))

static struct ia64_node_data	*node_data[NR_NODES];
static long			boot_pg_data[8*NR_NODES+sizeof(pg_data_t)]  __initdata;
static pg_data_t		*pg_data_ptr[NR_NODES] __initdata;
static bootmem_data_t		bdata[NR_NODES][NR_BANKS_PER_NODE+1] __initdata;

extern int  filter_rsvd_memory (unsigned long start, unsigned long end, void *arg);

/*
 * Return the compact node number of this cpu. Used prior to
 * setting up the cpu_data area.
 *	Note - not fast, intended for boot use only!!
 */
int
boot_get_local_nodeid(void)
{
	int	i;

	for (i = 0; i < NR_CPUS; i++)
		if (node_cpuid[i].phys_id == hard_smp_processor_id())
			return node_cpuid[i].nid;

	/* node info missing, so nid should be 0.. */
	return 0;
}

/*
 * Return a pointer to the pg_data structure for a node.
 * This function is used ONLY in early boot before the cpu_data
 * structure is available.
 */
pg_data_t* __init
boot_get_pg_data_ptr(long node)
{
	return pg_data_ptr[node];
}


/*
 * Return a pointer to the node data for the current node.
 *	(boottime initialization only)
 */
struct ia64_node_data *
get_node_data_ptr(void)
{
	return node_data[boot_get_local_nodeid()];
}

/*
 * We allocate one of the bootmem_data_t structs for each piece of memory
 * that we wish to treat as a contiguous block.  Each such block must start
 * on a BANKSIZE boundary.  Multiple banks per node is not supported.
 */
static int __init
build_maps(unsigned long pstart, unsigned long length, int node)
{
	bootmem_data_t	*bdp;
	unsigned long cstart, epfn;

	bdp = pg_data_ptr[node]->bdata;
	epfn = GRANULEROUNDUP(pstart + length) >> PAGE_SHIFT;
	cstart = pstart & ~(BANKSIZE - 1);

	if (!bdp->node_low_pfn) {
		bdp->node_boot_start = cstart;
		bdp->node_low_pfn = epfn;
	} else {
		bdp->node_boot_start = min(cstart, bdp->node_boot_start);
		bdp->node_low_pfn = max(epfn, bdp->node_low_pfn);
	}

	min_low_pfn = min(min_low_pfn, bdp->node_boot_start>>PAGE_SHIFT);
	max_low_pfn = max(max_low_pfn, bdp->node_low_pfn);

	return 0;
}

/*
 * Find space on each node for the bootmem map.
 *
 * Called by efi_memmap_walk to find boot memory on each node. Note that
 * only blocks that are free are passed to this routine (currently filtered by
 * free_available_memory).
 */
static int __init
find_bootmap_space(unsigned long pstart, unsigned long length, int node)
{
	unsigned long	mapsize, pages, epfn;
	bootmem_data_t	*bdp;

	epfn = (pstart + length) >> PAGE_SHIFT;
	bdp = &pg_data_ptr[node]->bdata[0];

	if (pstart < bdp->node_boot_start || epfn > bdp->node_low_pfn)
		return 0;

	if (!bdp->node_bootmem_map) {
		pages = bdp->node_low_pfn - (bdp->node_boot_start>>PAGE_SHIFT);
		mapsize = bootmem_bootmap_pages(pages) << PAGE_SHIFT;
		if (length > mapsize) {
			init_bootmem_node(
				BOOT_NODE_DATA(node),
				pstart>>PAGE_SHIFT, 
				bdp->node_boot_start>>PAGE_SHIFT,
				bdp->node_low_pfn);
		}

	}

	return 0;
}


/*
 * Free available memory to the bootmem allocator.
 *
 * Note that only blocks that are free are passed to this routine (currently 
 * filtered by free_available_memory).
 *
 */
static int __init
discontig_free_bootmem_node(unsigned long pstart, unsigned long length, int node)
{
	free_bootmem_node(BOOT_NODE_DATA(node), pstart, length);

	return 0;
}


/*
 * Reserve the space used by the bootmem maps.
 */
static void __init
discontig_reserve_bootmem(void)
{
	int		node;
	unsigned long	mapbase, mapsize, pages;
	bootmem_data_t	*bdp;

	for (node = 0; node < numnodes; node++) {
		bdp = BOOT_NODE_DATA(node)->bdata;

		pages = bdp->node_low_pfn - (bdp->node_boot_start>>PAGE_SHIFT);
		mapsize = bootmem_bootmap_pages(pages) << PAGE_SHIFT;
		mapbase = __pa(bdp->node_bootmem_map);
		reserve_bootmem_node(BOOT_NODE_DATA(node), mapbase, mapsize);
	}
}

/*
 * Allocate per node tables.
 * 	- the pg_data structure is allocated on each node. This minimizes offnode 
 *	  memory references
 *	- the node data is allocated & initialized. Portions of this structure is read-only (after 
 *	  boot) and contains node-local pointers to usefuls data structures located on
 *	  other nodes.
 *
 * We also switch to using the "real" pg_data structures at this point. Earlier in boot, we
 * use a different structure. The only use for pg_data prior to the point in boot is to get 
 * the pointer to the bdata for the node.
 */
static void __init
allocate_pernode_structures(void)
{
	pg_data_t	*pgdat=0, *new_pgdat_list=0;
	int		node, mynode;

	mynode = boot_get_local_nodeid();
	for (node = numnodes - 1; node >= 0 ; node--) {
		node_data[node] = alloc_bootmem_node(BOOT_NODE_DATA(node), sizeof (struct ia64_node_data));
		pgdat = __alloc_bootmem_node(BOOT_NODE_DATA(node), sizeof(pg_data_t), SMP_CACHE_BYTES, 0);
		pgdat->bdata = &(bdata[node][0]);
		pg_data_ptr[node] = pgdat;
		pgdat->pgdat_next = new_pgdat_list;
		new_pgdat_list = pgdat;
	}
	
	memcpy(node_data[mynode]->pg_data_ptrs, pg_data_ptr, sizeof(pg_data_ptr));
	memcpy(node_data[mynode]->node_data_ptrs, node_data, sizeof(node_data));

	pgdat_list = new_pgdat_list;
}

/*
 * Called early in boot to setup the boot memory allocator, and to
 * allocate the node-local pg_data & node-directory data structures..
 */
void __init
discontig_mem_init(void)
{
	int	node;

	if (numnodes == 0) {
		printk(KERN_ERR "node info missing!\n");
		numnodes = 1;
	}

	for (node = 0; node < numnodes; node++) {
		pg_data_ptr[node] = (pg_data_t*) &boot_pg_data[node];
		pg_data_ptr[node]->bdata = &bdata[node][0];
	}

	min_low_pfn = -1;
	max_low_pfn = 0;

        efi_memmap_walk(filter_rsvd_memory, build_maps);
        efi_memmap_walk(filter_rsvd_memory, find_bootmap_space);
        efi_memmap_walk(filter_rsvd_memory, discontig_free_bootmem_node);
	discontig_reserve_bootmem();
	allocate_pernode_structures();
}

/*
 * Initialize the paging system.
 *	- determine sizes of each node
 *	- initialize the paging system for the node
 *	- build the nodedir for the node. This contains pointers to
 *	  the per-bank mem_map entries.
 *	- fix the page struct "virtual" pointers. These are bank specific
 *	  values that the paging system doesn't understand.
 *	- replicate the nodedir structure to other nodes	
 */ 

void __init
discontig_paging_init(void)
{
	int		node, mynode;
	unsigned long	max_dma, zones_size[MAX_NR_ZONES];
	unsigned long	kaddr, ekaddr, bid;
	struct page	*page;
	bootmem_data_t	*bdp;

	max_dma = virt_to_phys((void *) MAX_DMA_ADDRESS) >> PAGE_SHIFT;

	mynode = boot_get_local_nodeid();
	for (node = 0; node < numnodes; node++) {
		long pfn, startpfn;

		memset(zones_size, 0, sizeof(zones_size));

		startpfn = -1;
		bdp = BOOT_NODE_DATA(node)->bdata;
		pfn = bdp->node_boot_start >> PAGE_SHIFT;
		if (startpfn == -1)
			startpfn = pfn;
		if (pfn > max_dma)
			zones_size[ZONE_NORMAL] += (bdp->node_low_pfn - pfn);
		else if (bdp->node_low_pfn < max_dma)
			zones_size[ZONE_DMA] += (bdp->node_low_pfn - pfn);
		else {
			zones_size[ZONE_DMA] += (max_dma - pfn);
			zones_size[ZONE_NORMAL] += (bdp->node_low_pfn - max_dma);
		}

		free_area_init_node(node, NODE_DATA(node), NULL, zones_size, startpfn, 0);

		page = NODE_DATA(node)->node_mem_map;

		bdp = BOOT_NODE_DATA(node)->bdata;

		kaddr = (unsigned long)__va(bdp->node_boot_start);
		ekaddr = (unsigned long)__va(bdp->node_low_pfn << PAGE_SHIFT);
		while (kaddr < ekaddr) {
			if (paddr_to_nid(__pa(kaddr)) == node) {
				bid = BANK_MEM_MAP_INDEX(kaddr);
				node_data[mynode]->node_id_map[bid] = node;
				node_data[mynode]->bank_mem_map_base[bid] = page;
			}
			kaddr += BANKSIZE;
			page += BANKSIZE/PAGE_SIZE;
		}
	}

	/*
	 * Finish setting up the node data for this node, then copy it to the other nodes.
	 */
	for (node=0; node < numnodes; node++)
		if (mynode != node) {
			memcpy(node_data[node], node_data[mynode], sizeof(struct ia64_node_data));
			node_data[node]->node = node;
		}
}

