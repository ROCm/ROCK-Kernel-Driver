/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 by Ralf Baechle
 * Copyright (C) 2000 by Silicon Graphics, Inc.
 *
 * On SGI IP27 the ARC memory configuration data is completly bogus but
 * alternate easier to use mechanisms are available.
 */
#include <linux/init.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/swap.h>

#include <asm/page.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/hub.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/arch.h>
#include <asm/mmzone.h>
#include <asm/sections.h>

/* ip27-klnuma.c   */
extern pfn_t node_getfirstfree(cnodeid_t cnode);

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define SLOT_IGNORED	0xffff

short slot_lastfilled_cache[MAX_COMPACT_NODES];
unsigned short slot_psize_cache[MAX_COMPACT_NODES][MAX_MEM_SLOTS];

struct bootmem_data plat_node_bdata[MAX_COMPACT_NODES];
struct pglist_data *node_data[MAX_COMPACT_NODES];
struct hub_data *hub_data[MAX_COMPACT_NODES];

int numa_debug(void)
{
	printk("NUMA debug\n");
	*(int *)0 = 0;
	return(0);
}

/*
 * Return the number of pages of memory provided by the given slot
 * on the specified node.
 */
static pfn_t slot_getsize(cnodeid_t node, int slot)
{
	return (pfn_t) slot_psize_cache[node][slot];
}

/*
 * Return highest slot filled
 */
static int node_getlastslot(cnodeid_t node)
{
	return (int) slot_lastfilled_cache[node];
}

/*
 * Return the pfn of the last free page of memory on a node.
 */
static pfn_t node_getmaxclick(cnodeid_t node)
{
	pfn_t	slot_psize;
	int	slot;

	/*
	 * Start at the top slot. When we find a slot with memory in it,
	 * that's the winner.
	 */
	for (slot = (node_getnumslots(node) - 1); slot >= 0; slot--) {
		if ((slot_psize = slot_getsize(node, slot))) {
			if (slot_psize == SLOT_IGNORED)
				continue;
			/* Return the basepfn + the slot size, minus 1. */
			return slot_getbasepfn(node, slot) + slot_psize - 1;
		}
	}

	/*
	 * If there's no memory on the node, return 0. This is likely
	 * to cause problems.
	 */
	return 0;
}

static pfn_t slot_psize_compute(cnodeid_t node, int slot)
{
	nasid_t nasid;
	lboard_t *brd;
	klmembnk_t *banks;
	unsigned long size;

	nasid = COMPACT_TO_NASID_NODEID(node);
	/* Find the node board */
	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_IP27);
	if (!brd)
		return 0;

	/* Get the memory bank structure */
	banks = (klmembnk_t *) find_first_component(brd, KLSTRUCT_MEMBNK);
	if (!banks)
		return 0;

	/* Size in _Megabytes_ */
	size = (unsigned long)banks->membnk_bnksz[slot/4];

	/* hack for 128 dimm banks */
	if (size <= 128) {
		if (slot % 4 == 0) {
			size <<= 20;		/* size in bytes */
			return(size >> PAGE_SHIFT);
		} else
			return 0;
	} else {
		size /= 4;
		size <<= 20;
		return size >> PAGE_SHIFT;
	}
}

static pfn_t szmem(void)
{
	cnodeid_t node;
	int slot, numslots;
	pfn_t num_pages = 0, slot_psize;
	pfn_t slot0sz = 0, nodebytes;	/* Hack to detect problem configs */
	int ignore;

	for (node = 0; node < numnodes; node++) {
		numslots = node_getnumslots(node);
		ignore = nodebytes = 0;
		for (slot = 0; slot < numslots; slot++) {
			slot_psize = slot_psize_compute(node, slot);
			if (slot == 0) slot0sz = slot_psize;
			/*
			 * We need to refine the hack when we have replicated
			 * kernel text.
			 */
			nodebytes += SLOT_SIZE;
			if ((nodebytes >> PAGE_SHIFT) * (sizeof(struct page)) >
						(slot0sz << PAGE_SHIFT))
				ignore = 1;
			if (ignore && slot_psize) {
				printk("Ignoring slot %d onwards on node %d\n",
								slot, node);
				slot_psize_cache[node][slot] = SLOT_IGNORED;
				slot = numslots;
				continue;
			}
			num_pages += slot_psize;
			slot_psize_cache[node][slot] =
					(unsigned short) slot_psize;
			if (slot_psize)
				slot_lastfilled_cache[node] = slot;
		}
	}

	return num_pages;
}

/*
 * Currently, the intranode memory hole support assumes that each slot
 * contains at least 32 MBytes of memory. We assume all bootmem data
 * fits on the first slot.
 */
extern void mlreset(void);
void __init prom_meminit(void)
{
	cnodeid_t node;

	mlreset();

	num_physpages = szmem();

	for (node = 0; node < numnodes; node++) {
		pfn_t slot_firstpfn = slot_getbasepfn(node, 0);
		pfn_t slot_lastpfn = slot_firstpfn + slot_getsize(node, 0);
		pfn_t slot_freepfn = node_getfirstfree(node);
		unsigned long bootmap_size;

		/*
		 * Allocate the node data structures on the node first.
		 */
		node_data[node] = __va(slot_freepfn << PAGE_SHIFT);
		node_data[node]->bdata = &plat_node_bdata[node];

		hub_data[node] = node_data[node] + 1;

		slot_freepfn += PFN_UP(sizeof(struct pglist_data) +
				       sizeof(struct hub_data));
	
	  	bootmap_size = init_bootmem_node(NODE_DATA(node), slot_freepfn,
						slot_firstpfn, slot_lastpfn);
		free_bootmem_node(NODE_DATA(node), slot_firstpfn << PAGE_SHIFT,
				(slot_lastpfn - slot_firstpfn) << PAGE_SHIFT);
		reserve_bootmem_node(NODE_DATA(node), slot_firstpfn << PAGE_SHIFT,
		  ((slot_freepfn - slot_firstpfn) << PAGE_SHIFT) + bootmap_size);
	}
}

unsigned long __init prom_free_prom_memory(void)
{
	/* We got nothing to free here ...  */
	return 0;
}

extern void pagetable_init(void);
extern unsigned long setup_zero_pages(void);

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, 0, 0};
	unsigned node;

	pagetable_init();

	for (node = 0; node < numnodes; node++) {
		pfn_t start_pfn = slot_getbasepfn(node, 0);
		pfn_t end_pfn = node_getmaxclick(node) + 1;

		zones_size[ZONE_DMA] = end_pfn - start_pfn;
		free_area_init_node(node, NODE_DATA(node), NULL,
				zones_size, start_pfn, NULL);

		if (end_pfn > max_low_pfn)
			max_low_pfn = end_pfn;
	}
}

void __init mem_init(void)
{
	unsigned long codesize, datasize, initsize, tmp;
	unsigned node;

	high_memory = (void *) __va(num_physpages << PAGE_SHIFT);

	for (node = 0; node < numnodes; node++) {
		unsigned slot, numslots;
		struct page *end, *p;
	
		/*
	 	 * This will free up the bootmem, ie, slot 0 memory.
	 	 */
		totalram_pages += free_all_bootmem_node(NODE_DATA(node));

		/*
		 * We need to manually do the other slots.
		 */
		numslots = node_getlastslot(node);
		for (slot = 1; slot <= numslots; slot++) {
			p = NODE_DATA(node)->node_mem_map +
				(slot_getbasepfn(node, slot) -
				 slot_getbasepfn(node, 0));

			/*
			 * Free valid memory in current slot.
			 */
			for (end = p + slot_getsize(node, slot); p < end; p++) {
				/* if (!page_is_ram(pgnr)) continue; */
				/* commented out until page_is_ram works */
				ClearPageReserved(p);
				set_page_count(p, 1);
				__free_page(p);
				totalram_pages++;
			}
		}
	}

	totalram_pages -= setup_zero_pages();	/* This comes from node 0 */

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	tmp = nr_free_pages();
	printk(KERN_INFO "Memory: %luk/%luk available (%ldk kernel code, "
	       "%ldk reserved, %ldk data, %ldk init, %ldk highmem)\n",
	       tmp << (PAGE_SHIFT-10),
	       num_physpages << (PAGE_SHIFT-10),
	       codesize >> 10,
	       (num_physpages - tmp) << (PAGE_SHIFT-10),
	       datasize >> 10,
	       initsize >> 10,
	       (unsigned long) (totalhigh_pages << (PAGE_SHIFT-10)));
}
