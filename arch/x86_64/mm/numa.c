/* 
 * Generic VM initialization for x86-64 NUMA setups.
 * Copyright 2002,2003 Andi Kleen, SuSE Labs.
 */ 
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/ctype.h>
#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/numa.h>

#define Dprintk(x...)

struct pglist_data *node_data[MAXNODE];
bootmem_data_t plat_node_bdata[MAX_NUMNODES];

int memnode_shift;
u8  memnodemap[NODEMAPSIZE];

static int numa_off __initdata; 

unsigned long nodes_present; 

int __init compute_hash_shift(struct node *nodes)
{
	int i; 
	int shift = 24;
	u64 addr;
	
	/* When in doubt use brute force. */
	while (shift < 48) { 
		memset(memnodemap,0xff,sizeof(*memnodemap) * NODEMAPSIZE); 
		for (i = 0; i < numnodes; i++) { 
			if (nodes[i].start == nodes[i].end) 
				continue;
			for (addr = nodes[i].start; 
			     addr < nodes[i].end; 
			     addr += (1UL << shift)) {
				if (memnodemap[addr >> shift] != 0xff && 
				    memnodemap[addr >> shift] != i) { 
					printk(KERN_INFO 
					    "node %d shift %d addr %Lx conflict %d\n", 
					       i, shift, addr, memnodemap[addr>>shift]);
					goto next; 
				} 
				memnodemap[addr >> shift] = i; 
			} 
		} 
		return shift; 
	next:
		shift++; 
	} 
	memset(memnodemap,0,sizeof(*memnodemap) * NODEMAPSIZE); 
	return -1; 
}

/* Initialize bootmem allocator for a node */
void __init setup_node_bootmem(int nodeid, unsigned long start, unsigned long end)
{ 
	unsigned long start_pfn, end_pfn, bootmap_pages, bootmap_size, bootmap_start; 
	unsigned long nodedata_phys;
	const int pgdat_size = round_up(sizeof(pg_data_t), PAGE_SIZE);

	start = round_up(start, ZONE_ALIGN); 

	printk("Bootmem setup node %d %016lx-%016lx\n", nodeid, start, end);

	start_pfn = start >> PAGE_SHIFT;
	end_pfn = end >> PAGE_SHIFT;

	nodedata_phys = find_e820_area(start, end, pgdat_size); 
	if (nodedata_phys == -1L) 
		panic("Cannot find memory pgdat in node %d\n", nodeid);

	Dprintk("nodedata_phys %lx\n", nodedata_phys); 

	node_data[nodeid] = phys_to_virt(nodedata_phys);
	memset(NODE_DATA(nodeid), 0, sizeof(pg_data_t));
	NODE_DATA(nodeid)->bdata = &plat_node_bdata[nodeid];
	NODE_DATA(nodeid)->node_start_pfn = start_pfn;
	NODE_DATA(nodeid)->node_spanned_pages = end_pfn - start_pfn;

	/* Find a place for the bootmem map */
	bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn); 
	bootmap_start = round_up(nodedata_phys + pgdat_size, PAGE_SIZE);
	bootmap_start = find_e820_area(bootmap_start, end, bootmap_pages<<PAGE_SHIFT);
	if (bootmap_start == -1L) 
		panic("Not enough continuous space for bootmap on node %d", nodeid); 
	Dprintk("bootmap start %lu pages %lu\n", bootmap_start, bootmap_pages); 
	
	bootmap_size = init_bootmem_node(NODE_DATA(nodeid),
					 bootmap_start >> PAGE_SHIFT, 
					 start_pfn, end_pfn); 

	e820_bootmem_free(NODE_DATA(nodeid), start, end);

	reserve_bootmem_node(NODE_DATA(nodeid), nodedata_phys, pgdat_size); 
	reserve_bootmem_node(NODE_DATA(nodeid), bootmap_start, bootmap_pages<<PAGE_SHIFT);
	if (nodeid + 1 > numnodes)
		numnodes = nodeid + 1;
	nodes_present |= (1UL << nodeid); 
	node_set_online(nodeid);
} 

/* Initialize final allocator for a zone */
void __init setup_node_zones(int nodeid)
{ 
	unsigned long start_pfn, end_pfn; 
	unsigned long zones[MAX_NR_ZONES];
	unsigned long dma_end_pfn;

	memset(zones, 0, sizeof(unsigned long) * MAX_NR_ZONES); 

	start_pfn = node_start_pfn(nodeid);
	end_pfn = node_end_pfn(nodeid);

	printk(KERN_INFO "setting up node %d %lx-%lx\n", nodeid, start_pfn, end_pfn); 
	
	/* All nodes > 0 have a zero length zone DMA */ 
	dma_end_pfn = __pa(MAX_DMA_ADDRESS) >> PAGE_SHIFT; 
	if (start_pfn < dma_end_pfn) { 
		zones[ZONE_DMA] = dma_end_pfn - start_pfn;
		zones[ZONE_NORMAL] = end_pfn - dma_end_pfn; 
	} else { 
		zones[ZONE_NORMAL] = end_pfn - start_pfn; 
	} 
    
	free_area_init_node(nodeid, NODE_DATA(nodeid), NULL, zones, 
			    start_pfn, NULL); 
} 

int fake_node;

int __init numa_initmem_init(unsigned long start_pfn, unsigned long end_pfn)
{ 
#ifdef CONFIG_K8_NUMA
	if (!numa_off && !k8_scan_nodes(start_pfn<<PAGE_SHIFT, end_pfn<<PAGE_SHIFT))
		return 0; 
#endif
	printk(KERN_INFO "%s\n",
	       numa_off ? "NUMA turned off" : "No NUMA configuration found");

	printk(KERN_INFO "Faking a node at %016lx-%016lx\n", 
	       start_pfn << PAGE_SHIFT,
	       end_pfn << PAGE_SHIFT); 
		/* setup dummy node covering all memory */ 
	fake_node = 1; 	
	memnode_shift = 63; 
	memnodemap[0] = 0;
	numnodes = 1;
	setup_node_bootmem(0, start_pfn<<PAGE_SHIFT, end_pfn<<PAGE_SHIFT);
	return -1; 
} 

unsigned long __init numa_free_all_bootmem(void) 
{ 
	int i;
	unsigned long pages = 0;
	for_all_nodes(i) {
		pages += free_all_bootmem_node(NODE_DATA(i));
	}
	return pages;
} 

void __init paging_init(void)
{ 
	int i;
	for_all_nodes(i) { 
		setup_node_zones(i); 
	}
} 

/* [numa=off] */
__init int numa_setup(char *opt) 
{ 
	if (!strncmp(opt,"off",3))
		numa_off = 1;
	return 1;
} 


