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
#include <linux/module.h>
#include <asm/numaq.h>

/* These are needed before the pgdat's are created */
extern long node_start_pfn[], node_end_pfn[];

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
			node_set_online(node);
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

/*
 * Unlike Summit, we don't really care to let the NUMA-Q
 * fall back to flat mode.  Don't compile for NUMA-Q
 * unless you really need it!
 */
int __init get_memcfg_numaq(void)
{
	smp_dump_qct();
	initialize_physnode_map();
	return 1;
}
