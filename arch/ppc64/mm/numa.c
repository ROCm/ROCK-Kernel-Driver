/*
 * NUMA support
 *
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/threads.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <asm/lmb.h>

#if 0
#define dbg(format, arg...) udbg_printf(format, arg)
#else
#define dbg(format, arg...)
#endif

int numa_cpu_lookup_table[NR_CPUS] = { [ 0 ... (NR_CPUS - 1)] = -1};
int numa_memory_lookup_table[MAX_MEMORY >> MEMORY_INCREMENT_SHIFT] =
	{ [ 0 ... ((MAX_MEMORY >> MEMORY_INCREMENT_SHIFT) - 1)] = -1};
int numa_node_exists[MAX_NUMNODES];

struct pglist_data node_data[MAX_NUMNODES];
bootmem_data_t plat_node_bdata[MAX_NUMNODES];

static int __init parse_numa_properties(void)
{
	/* XXX implement */
	return -1;
}

void __init do_init_bootmem(void)
{
	int nid;

	min_low_pfn = 0;
	max_low_pfn = lmb_end_of_DRAM() >> PAGE_SHIFT;

	if (parse_numa_properties())
		BUG();

	for (nid = 0; nid < MAX_NUMNODES; nid++) {
		unsigned long start, end;
		unsigned long start_paddr, end_paddr;
		int i;
		unsigned long bootmem_paddr;
		unsigned long bootmap_size;

		if (!numa_node_exists[nid])
			continue;

		/* Find start and end of this zone */
		start = 0;
		while (numa_memory_lookup_table[start] != nid)
			start++;

		end = (MAX_MEMORY >> MEMORY_INCREMENT_SHIFT) - 1;
		while (numa_memory_lookup_table[end] != nid)
			end--;
		end++;

		start_paddr = start << MEMORY_INCREMENT_SHIFT;
		end_paddr = end << MEMORY_INCREMENT_SHIFT;

		dbg("node %d\n", nid);
		dbg("start_paddr = %lx\n", start_paddr);
		dbg("end_paddr = %lx\n", end_paddr);

		NODE_DATA(nid)->bdata = &plat_node_bdata[nid];

		/* XXX FIXME: first bitmap hardwired to 1G */
		if (start_paddr == 0)
			bootmem_paddr = (1 << 30);
		else
			bootmem_paddr = start_paddr;

		dbg("bootmap_paddr = %lx\n", bootmem_paddr);

		bootmap_size = init_bootmem_node(NODE_DATA(nid),
						 bootmem_paddr >> PAGE_SHIFT,
						 start_paddr >> PAGE_SHIFT,
						 end_paddr >> PAGE_SHIFT);

		dbg("bootmap_size = %lx\n", bootmap_size);

		for (i = 0; i < lmb.memory.cnt; i++) {
			unsigned long physbase, size;
			unsigned long type = lmb.memory.region[i].type;

			if (type != LMB_MEMORY_AREA)
				continue;

			physbase = lmb.memory.region[i].physbase;
			size = lmb.memory.region[i].size;

			if (physbase < end_paddr &&
			    (physbase+size) > start_paddr) {
				/* overlaps */
				if (physbase < start_paddr) {
					size -= start_paddr - physbase;
					physbase = start_paddr;
				}

				if (size > end_paddr - start_paddr)
					size = end_paddr - start_paddr;

				dbg("free_bootmem %lx %lx\n", physbase, size);
				free_bootmem_node(NODE_DATA(nid), physbase,
						  size);
			}
		}

		for (i = 0; i < lmb.reserved.cnt; i++) {
			unsigned long physbase = lmb.reserved.region[i].physbase;
			unsigned long size = lmb.reserved.region[i].size;

			if (physbase < end_paddr &&
			    (physbase+size) > start_paddr) {
				/* overlaps */
				if (physbase < start_paddr) {
					size -= start_paddr - physbase;
					physbase = start_paddr;
				}

				if (size > end_paddr - start_paddr)
					size = end_paddr - start_paddr;

				dbg("reserve_bootmem %lx %lx\n", physbase,
				    size);
				reserve_bootmem_node(NODE_DATA(nid), physbase,
						     size);
			}
		}

		dbg("reserve_bootmem %lx %lx\n", bootmem_paddr, bootmap_size);
		reserve_bootmem_node(NODE_DATA(nid), bootmem_paddr,
				     bootmap_size);
	}
}

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	int i, nid;

	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

	for (nid = 0; nid < MAX_NUMNODES; nid++) {
		unsigned long start_pfn;
		unsigned long end_pfn;

		if (!numa_node_exists[nid])
			continue;

		start_pfn = plat_node_bdata[nid].node_boot_start >> PAGE_SHIFT;
		end_pfn = plat_node_bdata[nid].node_low_pfn;

		zones_size[ZONE_DMA] = end_pfn - start_pfn;
		dbg("free_area_init node %d %lx %lx\n", nid, zones_size,
		    start_pfn);
		free_area_init_node(nid, NODE_DATA(nid), NULL, zones_size,
				    start_pfn, NULL);
	}
}
