/*
 * Written by: Patricia Gaughen, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <gone@us.ibm.com>
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/highmem.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <asm/e820.h>
#include <asm/setup.h>

struct pfns {
	unsigned long start_pfn;
	unsigned long max_pfn;
};

struct plat_pglist_data *plat_node_data[MAX_NUMNODES];
bootmem_data_t plat_node_bdata;
struct pfns plat_node_bootpfns[MAX_NUMNODES];

extern unsigned long find_max_low_pfn(void);
extern void find_max_pfn(void);
extern void one_highpage_init(struct page *, int, int);

extern u64 nodes_mem_start[], nodes_mem_size[];
extern struct e820map e820;
extern char _end;
extern unsigned long highend_pfn, highstart_pfn;
extern unsigned long max_low_pfn;
extern unsigned long totalram_pages;
extern unsigned long totalhigh_pages;

/*
 * Find the highest page frame number we have available for the node
 */
static void __init find_max_pfn_node(int nid)
{
	unsigned long node_datasz;
	unsigned long start, end;

	start = plat_node_bootpfns[nid].start_pfn = PFN_UP(nodes_mem_start[nid]);
	end = PFN_DOWN(nodes_mem_start[nid]) + PFN_DOWN(nodes_mem_size[nid]);

	if (start >= end) {
		BUG();
	}
	if (end > max_pfn) {
		end = max_pfn;
	}
	plat_node_bootpfns[nid].max_pfn = end;

	node_datasz = PFN_UP(sizeof(struct plat_pglist_data));
	PLAT_NODE_DATA(nid) = (struct plat_pglist_data *)(__va(min_low_pfn << PAGE_SHIFT));
	min_low_pfn += node_datasz;
}

/*
 * Register fully available low RAM pages with the bootmem allocator.
 */
static void __init register_bootmem_low_pages(unsigned long system_max_low_pfn)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		unsigned long curr_pfn, last_pfn, size;
		/*
		 * Reserve usable low memory
		 */
		if (e820.map[i].type != E820_RAM)
			continue;
		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(e820.map[i].addr);
		if (curr_pfn >= system_max_low_pfn)
			continue;
		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(e820.map[i].addr + e820.map[i].size);

		if (last_pfn > system_max_low_pfn)
			last_pfn = system_max_low_pfn;

		/*
		 * .. finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		size = last_pfn - curr_pfn;
		free_bootmem_node(NODE_DATA(0), PFN_PHYS(curr_pfn), PFN_PHYS(size));
	}
}

unsigned long __init setup_memory(void)
{
	int nid;
	unsigned long bootmap_size, system_start_pfn, system_max_low_pfn;

	get_memcfg_numa();

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	system_start_pfn = min_low_pfn = PFN_UP(__pa(&_end));

	find_max_pfn();
	system_max_low_pfn = max_low_pfn = find_max_low_pfn();

#ifdef CONFIG_HIGHMEM
		highstart_pfn = highend_pfn = max_pfn;
		if (max_pfn > system_max_low_pfn) {
			highstart_pfn = system_max_low_pfn;
		}
		printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		       pages_to_mb(highend_pfn - highstart_pfn));
#endif
	printk(KERN_NOTICE "%ldMB LOWMEM available.\n",
			pages_to_mb(system_max_low_pfn));
	
	for (nid = 0; nid < numnodes; nid++)
	{	
		find_max_pfn_node(nid);

	}

	NODE_DATA(0)->bdata = &plat_node_bdata;

	/*
	 * Initialize the boot-time allocator (with low memory only):
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0), min_low_pfn, 0, system_max_low_pfn);

	register_bootmem_low_pages(system_max_low_pfn);

	/*
	 * Reserve the bootmem bitmap itself as well. We do this in two
	 * steps (first step was init_bootmem()) because this catches
	 * the (very unlikely) case of us accidentally initializing the
	 * bootmem allocator with an invalid RAM area.
	 */
	reserve_bootmem_node(NODE_DATA(0), HIGH_MEMORY, (PFN_PHYS(min_low_pfn) +
		 bootmap_size + PAGE_SIZE-1) - (HIGH_MEMORY));

	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem_node(NODE_DATA(0), 0, PAGE_SIZE);

	/*
	 * But first pinch a few for the stack/trampoline stuff
	 * FIXME: Don't need the extra page at 4K, but need to fix
	 * trampoline before removing it. (see the GDT stuff)
	 */
	reserve_bootmem_node(NODE_DATA(0), PAGE_SIZE, PAGE_SIZE);

#ifdef CONFIG_ACPI_SLEEP
	/*
	 * Reserve low memory region for sleep support.
	 */
	acpi_reserve_bootmem();
#endif

	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();

	/*insert other nodes into pgdat_list*/
	for (nid = 1; nid < numnodes; nid++){       
		NODE_DATA(nid)->pgdat_next = pgdat_list;
		pgdat_list = NODE_DATA(nid);
	}
       

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (system_max_low_pfn << PAGE_SHIFT)) {
			reserve_bootmem_node(NODE_DATA(0), INITRD_START, INITRD_SIZE);
			initrd_start =
				INITRD_START ? INITRD_START + PAGE_OFFSET : 0;
			initrd_end = initrd_start+INITRD_SIZE;
		}
		else {
			printk(KERN_ERR "initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    INITRD_START + INITRD_SIZE,
			    system_max_low_pfn << PAGE_SHIFT);
			initrd_start = 0;
		}
	}
#endif
	return system_max_low_pfn;
}

void __init zone_sizes_init(void)
{
	int nid;

	for (nid = 0; nid < numnodes; nid++) {
		unsigned long zones_size[MAX_NR_ZONES] = {0, 0, 0};
		unsigned int max_dma;

		unsigned long low = max_low_pfn;
		unsigned long high = plat_node_bootpfns[nid].max_pfn;
		unsigned long start = plat_node_bootpfns[nid].start_pfn;
		
		max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;

		if (start > low) {
#ifdef CONFIG_HIGHMEM
		  zones_size[ZONE_HIGHMEM] = high - start;
#endif
		} else {
			if (low < max_dma)
				zones_size[ZONE_DMA] = low;
			else {
				zones_size[ZONE_DMA] = max_dma;
				zones_size[ZONE_NORMAL] = low - max_dma;
#ifdef CONFIG_HIGHMEM
				zones_size[ZONE_HIGHMEM] = high - low;
#endif
			}
		}
		free_area_init_node(nid, NODE_DATA(nid), 0, zones_size, start, 0);
	}
	return;
}

void __init set_highmem_pages_init(int bad_ppro) 
{
#ifdef CONFIG_HIGHMEM
	int nid;

	for (nid = 0; nid < numnodes; nid++) {
		unsigned long node_pfn, node_high_size, zone_start_pfn;
		struct page * zone_mem_map;
		
		node_high_size = NODE_DATA(nid)->node_zones[ZONE_HIGHMEM].size;
		zone_mem_map = NODE_DATA(nid)->node_zones[ZONE_HIGHMEM].zone_mem_map;
		zone_start_pfn = NODE_DATA(nid)->node_zones[ZONE_HIGHMEM].zone_start_pfn;

		printk("Initializing highpages for node %d\n", nid);
		for (node_pfn = 0; node_pfn < node_high_size; node_pfn++) {
			one_highpage_init((struct page *)(zone_mem_map + node_pfn),
					  zone_start_pfn + node_pfn, bad_ppro);
		}
	}
	totalram_pages += totalhigh_pages;
#endif
}

void __init set_max_mapnr_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long lmax_mapnr;
	int nid;
	
	highmem_start_page = mem_map + NODE_DATA(0)->node_zones[ZONE_HIGHMEM].zone_start_mapnr;
	num_physpages = highend_pfn;

	for (nid = 0; nid < numnodes; nid++) {
		lmax_mapnr = PLAT_NODE_DATA_STARTNR(nid) + PLAT_NODE_DATA_SIZE(nid);
		if (lmax_mapnr > max_mapnr) {
			max_mapnr = lmax_mapnr;
		}
	}
	
#else
	max_mapnr = num_physpages = max_low_pfn;
#endif
}
