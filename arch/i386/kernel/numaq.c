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
#include <asm/numaq.h>

/* These are needed before the pgdat's are created */
unsigned long node_start_pfn[MAX_NUMNODES];
unsigned long node_end_pfn[MAX_NUMNODES];

#define	MB_TO_PAGES(addr) ((addr) << (20 - PAGE_SHIFT))

/*
 * Function: smp_dump_qct()
 *
 * Description: gets memory layout from the quad config table.  This
 * function also increments numnodes with the number of nodes (quads)
 * present.
 */
static void __init smp_dump_qct(void)
{
	int node;
	struct eachquadmem *eq;
	struct sys_cfg_data *scd =
		(struct sys_cfg_data *)__va(SYS_CFG_DATA_PRIV_ADDR);

	numnodes = 0;
	for(node = 0; node < MAX_NUMNODES; node++) {
		if(scd->quads_present31_0 & (1 << node)) {
			numnodes++;
			eq = &scd->eq[node];
			/* Convert to pages */
			node_start_pfn[node] = MB_TO_PAGES(
				eq->hi_shrd_mem_start - eq->priv_mem_size);
			node_end_pfn[node] = MB_TO_PAGES(
				eq->hi_shrd_mem_start + eq->hi_shrd_mem_size);
		}
	}
}

/*
 * -----------------------------------------
 *
 * functions related to physnode_map
 *
 * -----------------------------------------
 */
/*
 * physnode_map keeps track of the physical memory layout of the
 * numaq nodes on a 256Mb break (each element of the array will
 * represent 256Mb of memory and will be marked by the node id.  so,
 * if the first gig is on node 0, and the second gig is on node 1
 * physnode_map will contain:
 * physnode_map[0-3] = 0;
 * physnode_map[4-7] = 1;
 * physnode_map[8- ] = -1;
 */
int physnode_map[MAX_ELEMENTS] = { [0 ... (MAX_ELEMENTS - 1)] = -1};

#define PFN_TO_ELEMENT(pfn) (pfn / PAGES_PER_ELEMENT)
#define PA_TO_ELEMENT(pa) (PFN_TO_ELEMENT(pa >> PAGE_SHIFT))

int pfn_to_nid(unsigned long pfn)
{
	int nid = physnode_map[PFN_TO_ELEMENT(pfn)];

	if (nid == -1)
		BUG(); /* address is not present */

	return nid;
}

/*
 * for each node mark the regions
 *        TOPOFMEM = hi_shrd_mem_start + hi_shrd_mem_size
 *
 * need to be very careful to not mark 1024+ as belonging
 * to node 0. will want 1027 to show as belonging to node 1
 * example:
 *  TOPOFMEM = 1024
 * 1024 >> 8 = 4 (subtract 1 for starting at 0]
 * tmpvar = TOPOFMEM - 256 = 768
 * 1024 >> 8 = 4 (subtract 1 for starting at 0]
 * 
 */
static void __init initialize_physnode_map(void)
{
	int nid;
	unsigned int topofmem, cur;
	struct eachquadmem *eq;
 	struct sys_cfg_data *scd =
		(struct sys_cfg_data *)__va(SYS_CFG_DATA_PRIV_ADDR);

	
	for(nid = 0; nid < numnodes; nid++) {
		if(scd->quads_present31_0 & (1 << nid)) {
			eq = &scd->eq[nid];
			cur = eq->hi_shrd_mem_start;
			topofmem = eq->hi_shrd_mem_start + eq->hi_shrd_mem_size;
			while (cur < topofmem) {
				physnode_map[cur >> 8] = nid;
				cur ++;
			}
		}
	}
}

void __init get_memcfg_numaq(void)
{
	smp_dump_qct();
	initialize_physnode_map();
}
