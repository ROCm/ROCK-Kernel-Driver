/*
 * arch/i386/kernel/summit.c - IBM Summit-Specific Code
 *
 * Written By: Matthew Dobson, IBM Corporation
 *
 * Copyright (c) 2003 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
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
 * Send feedback to <colpatch@us.ibm.com>
 *
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <asm/io.h>
#include <mach_mpparse.h>

#ifdef CONFIG_NUMA
static void __init setup_pci_node_map_for_wpeg(int wpeg_num, struct rio_table_hdr *rth, 
		struct scal_detail **scal_nodes, struct rio_detail **rio_nodes){
	int twst_num = 0, node = 0, first_bus = 0;
	int i, bus, num_busses;

	for(i = 0; i < rth->num_rio_dev; i++){
		if (rio_nodes[i]->node_id == rio_nodes[wpeg_num]->owner_id){
			twst_num = rio_nodes[i]->owner_id;
			break;
		}
	}
	if (i == rth->num_rio_dev){
		printk("%s: Couldn't find owner Cyclone for Winnipeg!\n", __FUNCTION__);
		return;
	}

	for(i = 0; i < rth->num_scal_dev; i++){
		if (scal_nodes[i]->node_id == twst_num){
			node = scal_nodes[i]->node_id;
			break;
		}
	}
	if (i == rth->num_scal_dev){
		printk("%s: Couldn't find owner Twister for Cyclone!\n", __FUNCTION__);
		return;
	}

	switch (rio_nodes[wpeg_num]->type){
	case CompatWPEG:
		/* The Compatability Winnipeg controls the legacy busses
		   (busses 0 & 1), the 66MHz PCI bus [2 slots] (bus 2), 
		   and the "extra" busses in case a PCI-PCI bridge card is 
		   used in either slot (busses 3 & 4): total 5 busses. */
		num_busses = 5;
		/* The BIOS numbers the busses starting at 1, and in a 
		   slightly wierd manner.  You'll have to trust that 
		   the math used below to determine the number of the 
		   first bus works. */
		first_bus = (rio_nodes[wpeg_num]->first_slot - 1) * 2;
		break;
	case AltWPEG:
		/* The Alternate/Secondary Winnipeg controls the 1st 133MHz 
		   bus [1 slot] & its "extra" bus (busses 0 & 1), the 2nd 
		   133MHz bus [1 slot] & its "extra" bus (busses 2 & 3), the 
		   100MHz bus [2 slots] (bus 4), and the "extra" busses for 
		   the 2 100MHz slots (busses 5 & 6): total 7 busses. */
		num_busses = 7;
		first_bus = (rio_nodes[wpeg_num]->first_slot * 2) - 1;
		break;
	case LookOutAWPEG:
	case LookOutBWPEG:
		printk("%s: LookOut Winnipegs not supported yet!\n", __FUNCTION__);
		return;
	default:
		printk("%s: Unsupported Winnipeg type!\n", __FUNCTION__);
		return;
	}

	for(bus = first_bus; bus < first_bus + num_busses; bus++)
		mp_bus_id_to_node[bus] = node;
}

static int __init build_detail_arrays(struct rio_table_hdr *rth,
		struct scal_detail **sd, struct rio_detail **rd){
	unsigned long ptr;
	int i, scal_detail_size, rio_detail_size;

	if ((rth->num_scal_dev > MAX_NUMNODES) ||
	    (rth->num_rio_dev > MAX_NUMNODES * 2)){
		printk("%s: MAX_NUMNODES too low!  Defined as %d, but system has %d nodes.\n", __FUNCTION__, MAX_NUMNODES, rth->num_scal_dev);
		return 1;
	}

	switch (rth->version){
	default:
		printk("%s: Bad Rio Grande Table Version: %d\n", __FUNCTION__, rth->version);
		return 1;
	case 2:
		scal_detail_size = 11;
		rio_detail_size = 13;
		break;
	case 3:
		scal_detail_size = 12;
		rio_detail_size = 15;
		break;
	}

	ptr = (unsigned long)rth + 3;
	for(i = 0; i < rth->num_scal_dev; i++)
		sd[i] = (struct scal_detail *)(ptr + (scal_detail_size * i));

	ptr += scal_detail_size * rth->num_scal_dev;
	for(i = 0; i < rth->num_rio_dev; i++)
		rd[i] = (struct rio_detail *)(ptr + (rio_detail_size * i));

	return 0;
}

void __init setup_summit(void)
{
	struct rio_table_hdr	*rio_table_hdr = NULL;
	struct scal_detail	*scal_devs[MAX_NUMNODES];
	struct rio_detail	*rio_devs[MAX_NUMNODES*2];
	unsigned long		ptr;
	unsigned short		offset;
	int			i;

	memset(mp_bus_id_to_node, -1, sizeof(mp_bus_id_to_node));

	/* The pointer to the EBDA is stored in the word @ phys 0x40E(40:0E) */
	ptr = *(unsigned short *)phys_to_virt(0x40Eul);
	ptr = (unsigned long)phys_to_virt(ptr << 4);

	offset = 0x180;
	while (offset){
		/* The block id is stored in the 2nd word */
		if (*((unsigned short *)(ptr + offset + 2)) == 0x4752){
			/* set the pointer past the offset & block id */
			rio_table_hdr = (struct rio_table_hdr *)(ptr + offset + 4);
			break;
		}
		/* The next offset is stored in the 1st word.  0 means no more */
		offset = *((unsigned short *)(ptr + offset));
	}
	if (!rio_table_hdr){
		printk("%s: Unable to locate Rio Grande Table in EBDA - bailing!\n", __FUNCTION__);
		return;
	}

	if (build_detail_arrays(rio_table_hdr, scal_devs, rio_devs))
		return;

	for(i = 0; i < rio_table_hdr->num_rio_dev; i++)
		if (is_WPEG(rio_devs[i]->type))
			/* It's a Winnipeg, it's got PCI Busses */
			setup_pci_node_map_for_wpeg(i, rio_table_hdr, scal_devs, rio_devs);
}
#endif /* CONFIG_NUMA */
