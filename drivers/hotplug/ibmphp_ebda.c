/*
 * IBM Hot Plug Controller Driver
 *
 * Written By: Tong Yu, IBM Corporation
 *
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (c) 2001,2002 IBM Corp.
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
 * Send feedback to <gregkh@us.ibm.com>
 *
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/list.h>
#include "ibmphp.h"

/*
 * POST builds data blocks(in this data block definition, a char-1
 * byte, short(or word)-2 byte, long(dword)-4 byte) in the Extended
 * BIOS Data Area which describe the configuration of the hot-plug
 * controllers and resources used by the PCI Hot-Plug devices.
 *
 * This file walks EBDA, maps data block from physical addr,
 * reconstruct linked lists about all system resource(MEM, PFM, IO)
 * already assigned by POST, as well as linked lists about hot plug
 * controllers (ctlr#, slot#, bus&slot features...)
 */

/* Global lists */
LIST_HEAD (ibmphp_ebda_pci_rsrc_head);
LIST_HEAD (ibmphp_slot_head);

/* Local variables */
static struct ebda_hpc_list *hpc_list_ptr;
static struct ebda_rsrc_list *rsrc_list_ptr;
static struct rio_table_hdr *rio_table_ptr;
static LIST_HEAD (ebda_hpc_head);
static LIST_HEAD (bus_info_head);
static void *io_mem;

/* Local functions */
static int ebda_rsrc_controller (void);
static int ebda_rsrc_rsrc (void);
static int ebda_rio_table (void);

static struct slot *alloc_ibm_slot (void)
{
	struct slot *slot;

	slot = kmalloc (sizeof (struct slot), GFP_KERNEL);
	if (!slot)
		return NULL;
	memset (slot, 0, sizeof (*slot));
	return slot;
}

static struct ebda_hpc_list *alloc_ebda_hpc_list (void)
{
	struct ebda_hpc_list *list;

	list = kmalloc (sizeof (struct ebda_hpc_list), GFP_KERNEL);
	if (!list)
		return NULL;
	memset (list, 0, sizeof (*list));
	return list;
}

static struct controller *alloc_ebda_hpc (u32 slot_count, u32 bus_count)
{
	struct controller *controller;
	struct ebda_hpc_slot *slots;
	struct ebda_hpc_bus *buses;

	controller = kmalloc (sizeof (struct controller), GFP_KERNEL);
	if (!controller)
		return NULL;
	memset (controller, 0, sizeof (*controller));

	slots = kmalloc (sizeof (struct ebda_hpc_slot) * slot_count, GFP_KERNEL);
	if (!slots) {
		kfree (controller);
		return NULL;
	}
	memset (slots, 0, sizeof (*slots) * slot_count);
	controller->slots = slots;

	buses = kmalloc (sizeof (struct ebda_hpc_bus) * bus_count, GFP_KERNEL);
	if (!buses) {
		kfree (controller->slots);
		kfree (controller);
		return NULL;
	}
	memset (buses, 0, sizeof (*buses) * bus_count);
	controller->buses = buses;

	return controller;
}

static void free_ebda_hpc (struct controller *controller)
{
	kfree (controller->slots);
	controller->slots = NULL;
	kfree (controller->buses);
	controller->buses = NULL;
	kfree (controller);
}

static struct ebda_rsrc_list *alloc_ebda_rsrc_list (void)
{
	struct ebda_rsrc_list *list;

	list = kmalloc (sizeof (struct ebda_rsrc_list), GFP_KERNEL);
	if (!list)
		return NULL;
	memset (list, 0, sizeof (*list));
	return list;
}

static struct ebda_pci_rsrc *alloc_ebda_pci_rsrc (void)
{
	struct ebda_pci_rsrc *resource;

	resource = kmalloc (sizeof (struct ebda_pci_rsrc), GFP_KERNEL);
	if (!resource)
		return NULL;
	memset (resource, 0, sizeof (*resource));
	return resource;
}

static void print_bus_info (void)
{
	struct bus_info *ptr;
	struct list_head *ptr1;
	
	list_for_each (ptr1, &bus_info_head) {
		ptr = list_entry (ptr1, struct bus_info, bus_info_list);
		debug ("%s - slot_min = %x\n", __FUNCTION__, ptr->slot_min);
		debug ("%s - slot_max = %x\n", __FUNCTION__, ptr->slot_max);
		debug ("%s - slot_count = %x\n", __FUNCTION__, ptr->slot_count);
		debug ("%s - bus# = %x\n", __FUNCTION__, ptr->busno);
		debug ("%s - current_speed = %x\n", __FUNCTION__, ptr->current_speed);
		debug ("%s - supported_speed = %x\n", __FUNCTION__, ptr->supported_speed);
		debug ("%s - controller_id = %x\n", __FUNCTION__, ptr->controller_id);
		debug ("%s - bus_mode = %x\n", __FUNCTION__, ptr->supported_bus_mode);
	}
}

static void print_ebda_pci_rsrc (void)
{
	struct ebda_pci_rsrc *ptr;
	struct list_head *ptr1;

	list_for_each (ptr1, &ibmphp_ebda_pci_rsrc_head) {
		ptr = list_entry (ptr1, struct ebda_pci_rsrc, ebda_pci_rsrc_list);
		debug ("%s - rsrc type: %x bus#: %x dev_func: %x start addr: %lx end addr: %lx\n", 
			__FUNCTION__, ptr->rsrc_type ,ptr->bus_num, ptr->dev_fun,ptr->start_addr, ptr->end_addr);
	}
}

static void print_ebda_hpc (void)
{
	struct controller *hpc_ptr;
	struct list_head *ptr1;
	u16 index;

	list_for_each (ptr1, &ebda_hpc_head) {

		hpc_ptr = list_entry (ptr1, struct controller, ebda_hpc_list); 

		for (index = 0; index < hpc_ptr->slot_count; index++) {
			debug ("%s - physical slot#: %x\n", __FUNCTION__, hpc_ptr->slots[index].slot_num);
			debug ("%s - pci bus# of the slot: %x\n", __FUNCTION__, hpc_ptr->slots[index].slot_bus_num);
			debug ("%s - index into ctlr addr: %x\n", __FUNCTION__, hpc_ptr->slots[index].ctl_index);
			debug ("%s - cap of the slot: %x\n", __FUNCTION__, hpc_ptr->slots[index].slot_cap);
		}

		for (index = 0; index < hpc_ptr->bus_count; index++) {
			debug ("%s - bus# of each bus controlled by this ctlr: %x\n", __FUNCTION__, hpc_ptr->buses[index].bus_num);
		}

		debug ("%s - type of hpc: %x\n", __FUNCTION__, hpc_ptr->ctlr_type);
		switch (hpc_ptr->ctlr_type) {
		case 1:
			debug ("%s - bus: %x\n", __FUNCTION__, hpc_ptr->u.pci_ctlr.bus);
			debug ("%s - dev_fun: %x\n", __FUNCTION__, hpc_ptr->u.pci_ctlr.dev_fun);
			debug ("%s - irq: %x\n", __FUNCTION__, hpc_ptr->irq);
			break;

		case 0:
			debug ("%s - io_start: %x\n", __FUNCTION__, hpc_ptr->u.isa_ctlr.io_start);
			debug ("%s - io_end: %x\n", __FUNCTION__, hpc_ptr->u.isa_ctlr.io_end);
			debug ("%s - irq: %x\n", __FUNCTION__, hpc_ptr->irq);
			break;

		case 2:
			debug ("%s - wpegbbar: %lx\n", __FUNCTION__, hpc_ptr->u.wpeg_ctlr.wpegbbar);
			debug ("%s - i2c_addr: %x\n", __FUNCTION__, hpc_ptr->u.wpeg_ctlr.i2c_addr);
			debug ("%s - irq: %x\n", __FUNCTION__, hpc_ptr->irq);
			break;
		}
	}
}

int ibmphp_access_ebda (void)
{
	u8 format, num_ctlrs, rio_complete, hs_complete;
	u16 ebda_seg, num_entries, next_offset, offset, blk_id, sub_addr, rc, re, rc_id, re_id, base;


	rio_complete = 0;
	hs_complete = 0;

	io_mem = ioremap ((0x40 << 4) + 0x0e, 2);
	if (!io_mem )
		return -ENOMEM;
	ebda_seg = readw (io_mem);
	iounmap (io_mem);
	debug ("returned ebda segment: %x\n", ebda_seg);
	
	io_mem = ioremap (ebda_seg<<4, 65000);
	if (!io_mem )
		return -ENOMEM;
	next_offset = 0x180;

	for (;;) {
		offset = next_offset;
		next_offset = readw (io_mem + offset);	/* offset of next blk */

		offset += 2;
		if (next_offset == 0)	/* 0 indicate it's last blk */
			break;
		blk_id = readw (io_mem + offset);	/* this blk id */

		offset += 2;
		/* check if it is hot swap block or rio block */
		if (blk_id != 0x4853 && blk_id != 0x4752)
			continue;
		/* found hs table */
		if (blk_id == 0x4853) {
			debug ("now enter hot swap block---\n");
			debug ("hot blk id: %x\n", blk_id);
			format = readb (io_mem + offset);

			offset += 1;
			if (format != 4) {
				iounmap (io_mem);
				return -ENODEV;
			}
			debug ("hot blk format: %x\n", format);
			/* hot swap sub blk */
			base = offset;

			sub_addr = base;
			re = readw (io_mem + sub_addr);	/* next sub blk */

			sub_addr += 2;
			rc_id = readw (io_mem + sub_addr); 	/* sub blk id */

			sub_addr += 2;
			if (rc_id != 0x5243) {
				iounmap (io_mem);
				return -ENODEV;
			}
			/* rc sub blk signature  */
			num_ctlrs = readb (io_mem + sub_addr);

			sub_addr += 1;
			hpc_list_ptr = alloc_ebda_hpc_list ();
			if (!hpc_list_ptr) {
				iounmap (io_mem);
				return -ENOMEM;
			}
			hpc_list_ptr->format = format;
			hpc_list_ptr->num_ctlrs = num_ctlrs;
			hpc_list_ptr->phys_addr = sub_addr;	/*  offset of RSRC_CONTROLLER blk */
			debug ("info about hpc descriptor---\n");
			debug ("hot blk format: %x\n", format);
			debug ("num of controller: %x\n", num_ctlrs);
			debug ("offset of hpc data structure enteries: %x\n ", sub_addr);

			sub_addr = base + re;	/* re sub blk */
			rc = readw (io_mem + sub_addr);	/* next sub blk */

			sub_addr += 2;
			re_id = readw (io_mem + sub_addr);	/* sub blk id */

			sub_addr += 2;
			if (re_id != 0x5245) {
				iounmap (io_mem);
				return -ENODEV;
			}

			/* signature of re */
			num_entries = readw (io_mem + sub_addr);

			sub_addr += 2;	/* offset of RSRC_ENTRIES blk */
			rsrc_list_ptr = alloc_ebda_rsrc_list ();
			if (!rsrc_list_ptr ) {
				iounmap (io_mem);
				return -ENOMEM;
			}
			rsrc_list_ptr->format = format;
			rsrc_list_ptr->num_entries = num_entries;
			rsrc_list_ptr->phys_addr = sub_addr;

			debug ("info about rsrc descriptor---\n");
			debug ("format: %x\n", format);
			debug ("num of rsrc: %x\n", num_entries);
			debug ("offset of rsrc data structure enteries: %x\n ", sub_addr);

			hs_complete = 1;
		}
		/* found rio table */
		else if (blk_id == 0x4752) {
			debug ("now enter io table ---\n");
			debug ("rio blk id: %x\n", blk_id);

			rio_table_ptr = kmalloc (sizeof (struct rio_table_hdr), GFP_KERNEL);
			if (!rio_table_ptr)
				return -ENOMEM; 
			memset (rio_table_ptr, 0, sizeof (struct rio_table_hdr) );
			rio_table_ptr->ver_num = readb (io_mem + offset);
			rio_table_ptr->scal_count = readb (io_mem + offset + 1);
			rio_table_ptr->riodev_count = readb (io_mem + offset + 2);
			rio_table_ptr->offset = offset +3 ;
			
			debug ("info about rio table hdr ---\n");
			debug ("ver_num: %x\nscal_count: %x\nriodev_count: %x\noffset of rio table: %x\n ", rio_table_ptr->ver_num, rio_table_ptr->scal_count, rio_table_ptr->riodev_count, rio_table_ptr->offset);

			rio_complete = 1;
		}

		if (hs_complete && rio_complete) {
			rc = ebda_rsrc_controller ();
			if (rc) {
				iounmap(io_mem);
				return rc;
			}
			rc = ebda_rsrc_rsrc ();
			if (rc) {
				iounmap(io_mem);
				return rc;
			}
			rc = ebda_rio_table ();
			if (rc) {
				iounmap(io_mem);
				return rc;
			}	
			iounmap (io_mem);
			return 0;
		}
	}
	iounmap (io_mem);
	return -ENODEV;
}


/*
 * map info (ctlr-id, slot count, slot#.. bus count, bus#, ctlr type...) of
 * each hpc from physical address to a list of hot plug controllers based on
 * hpc descriptors.
 */
static int ebda_rsrc_controller (void)
{
	u16 addr, addr_slot, addr_bus;
	u8 ctlr_id, temp, bus_index;
	u16 ctlr, slot, bus;
	u16 slot_num, bus_num, index;
	struct hotplug_slot *hp_slot_ptr;
	struct controller *hpc_ptr;
	struct ebda_hpc_bus *bus_ptr;
	struct ebda_hpc_slot *slot_ptr;
	struct bus_info *bus_info_ptr1, *bus_info_ptr2;
	int rc;

	addr = hpc_list_ptr->phys_addr;
	for (ctlr = 0; ctlr < hpc_list_ptr->num_ctlrs; ctlr++) {
		bus_index = 1;
		ctlr_id = readb (io_mem + addr);
		addr += 1;
		slot_num = readb (io_mem + addr);

		addr += 1;
		addr_slot = addr;	/* offset of slot structure */
		addr += (slot_num * 4);

		bus_num = readb (io_mem + addr);

		addr += 1;
		addr_bus = addr;	/* offset of bus */
		addr += (bus_num * 9);	/* offset of ctlr_type */
		temp = readb (io_mem + addr);

		addr += 1;
		/* init hpc structure */
		hpc_ptr = alloc_ebda_hpc (slot_num, bus_num);
		if (!hpc_ptr ) {
			iounmap (io_mem);
			return -ENOMEM;
		}
		hpc_ptr->ctlr_id = ctlr_id;
		hpc_ptr->ctlr_relative_id = ctlr;
		hpc_ptr->slot_count = slot_num;
		hpc_ptr->bus_count = bus_num;
		debug ("now enter ctlr data struture ---\n");
		debug ("ctlr id: %x\n", ctlr_id);
		debug ("ctlr_relative_id: %x\n", hpc_ptr->ctlr_relative_id);
		debug ("count of slots controlled by this ctlr: %x\n", slot_num);
		debug ("count of buses controlled by this ctlr: %x\n", bus_num);

		/* init slot structure, fetch slot, bus, cap... */
		slot_ptr = hpc_ptr->slots;
		for (slot = 0; slot < slot_num; slot++) {
			slot_ptr->slot_num = readb (io_mem + addr_slot);
			slot_ptr->slot_bus_num = readb (io_mem + addr_slot + slot_num);
			slot_ptr->ctl_index = readb (io_mem + addr_slot + 2*slot_num);
			slot_ptr->slot_cap = readb (io_mem + addr_slot + 3*slot_num);

			// create bus_info lined list --- if only one slot per bus: slot_min = slot_max 

			bus_info_ptr2 = ibmphp_find_same_bus_num (slot_ptr->slot_bus_num);
			if (!bus_info_ptr2) {
				bus_info_ptr1 = (struct bus_info *) kmalloc (sizeof (struct bus_info), GFP_KERNEL);
				if (!bus_info_ptr1) {
					iounmap (io_mem);
					return -ENOMEM;
				}
				memset (bus_info_ptr1, 0, sizeof (struct bus_info));
				bus_info_ptr1->slot_min = slot_ptr->slot_num;
				bus_info_ptr1->slot_max = slot_ptr->slot_num;
				bus_info_ptr1->slot_count += 1;
				bus_info_ptr1->busno = slot_ptr->slot_bus_num;
				bus_info_ptr1->index = bus_index++;
				bus_info_ptr1->current_speed = 0xff;
				bus_info_ptr1->current_bus_mode = 0xff;
				if ( ((slot_ptr->slot_cap) & EBDA_SLOT_133_MAX) == EBDA_SLOT_133_MAX )
					bus_info_ptr1->supported_speed =  3;
				else if ( ((slot_ptr->slot_cap) & EBDA_SLOT_100_MAX) == EBDA_SLOT_100_MAX )
					bus_info_ptr1->supported_speed =  2;
				else if ( ((slot_ptr->slot_cap) & EBDA_SLOT_66_MAX) == EBDA_SLOT_66_MAX )
					bus_info_ptr1->supported_speed =  1;
				bus_info_ptr1->controller_id = hpc_ptr->ctlr_id;
				if ( ((slot_ptr->slot_cap) & EBDA_SLOT_PCIX_CAP) == EBDA_SLOT_PCIX_CAP )
					bus_info_ptr1->supported_bus_mode = 1;
				else
					bus_info_ptr1->supported_bus_mode =0;


				list_add_tail (&bus_info_ptr1->bus_info_list, &bus_info_head);

			} else {
				bus_info_ptr2->slot_min = min (bus_info_ptr2->slot_min, slot_ptr->slot_num);
				bus_info_ptr2->slot_max = max (bus_info_ptr2->slot_max, slot_ptr->slot_num);
				bus_info_ptr2->slot_count += 1;

			}

			// end of creating the bus_info linked list

			slot_ptr++;
			addr_slot += 1;
		}

		/* init bus structure */
		bus_ptr = hpc_ptr->buses;
		for (bus = 0; bus < bus_num; bus++) {
			bus_ptr->bus_num = readb (io_mem + addr_bus);
			bus_ptr++;
			addr_bus += 1;
		}

		hpc_ptr->ctlr_type = temp;

		switch (hpc_ptr->ctlr_type) {
			case 1:
				hpc_ptr->u.pci_ctlr.bus = readb (io_mem + addr);
				hpc_ptr->u.pci_ctlr.dev_fun = readb (io_mem + addr + 1);
				hpc_ptr->irq = readb (io_mem + addr + 2);
				addr += 3; 
				break;

			case 0:
				hpc_ptr->u.isa_ctlr.io_start = readw (io_mem + addr);
				hpc_ptr->u.isa_ctlr.io_end = readw (io_mem + addr + 2);
				hpc_ptr->irq = readb (io_mem + addr + 4);
				addr += 5;
				break;

			case 2:
				hpc_ptr->u.wpeg_ctlr.wpegbbar = readl (io_mem + addr);
				hpc_ptr->u.wpeg_ctlr.i2c_addr = readb (io_mem + addr + 4);
				/* following 2 lines for testing purpose */
				if (hpc_ptr->u.wpeg_ctlr.i2c_addr == 0) 
					hpc_ptr->ctlr_type = 4;	
				

				hpc_ptr->irq = readb (io_mem + addr + 5);
				addr += 6;
				break;
			case 4:
				hpc_ptr->u.wpeg_ctlr.wpegbbar = readl (io_mem + addr);
				hpc_ptr->u.wpeg_ctlr.i2c_addr = readb (io_mem + addr + 4);
				hpc_ptr->irq = readb (io_mem + addr + 5);
				addr += 6;
				break;
			default:
				iounmap (io_mem);
				return -ENODEV;
		}
		/* following 3 line: Now our driver only supports I2c ctlrType */
		if ((hpc_ptr->ctlr_type != 2) && (hpc_ptr->ctlr_type != 4)) {
			err ("Please run this driver on ibm xseries440\n ");
			return -ENODEV;
		}

		hpc_ptr->revision = 0xff;
		hpc_ptr->options = 0xff;

		// register slots with hpc core as well as create linked list of ibm slot
		for (index = 0; index < hpc_ptr->slot_count; index++) {

			hp_slot_ptr = (struct hotplug_slot *) kmalloc (sizeof (struct hotplug_slot), GFP_KERNEL);
			if (!hp_slot_ptr) {
				iounmap (io_mem);
				return -ENOMEM;
			}
			memset (hp_slot_ptr, 0, sizeof (struct hotplug_slot));

			hp_slot_ptr->info = (struct hotplug_slot_info *) kmalloc (sizeof (struct hotplug_slot_info), GFP_KERNEL);
			if (!hp_slot_ptr->info) {
				iounmap (io_mem);
				kfree (hp_slot_ptr);
				return -ENOMEM;
			}
			memset (hp_slot_ptr->info, 0, sizeof (struct hotplug_slot_info));

			hp_slot_ptr->name = (char *) kmalloc (10, GFP_KERNEL);
			if (!hp_slot_ptr->name) {
				iounmap (io_mem);
				kfree (hp_slot_ptr->info);
				kfree (hp_slot_ptr);
				return -ENOMEM;
			}

			hp_slot_ptr->private = alloc_ibm_slot ();
			if (!hp_slot_ptr->private) {
				iounmap (io_mem);
				kfree (hp_slot_ptr->name);
				kfree (hp_slot_ptr->info);
				kfree (hp_slot_ptr);
				return -ENOMEM;
			}

			((struct slot *)hp_slot_ptr->private)->flag = TRUE;
			snprintf (hp_slot_ptr->name, 10, "%d", hpc_ptr->slots[index].slot_num);

			((struct slot *) hp_slot_ptr->private)->capabilities = hpc_ptr->slots[index].slot_cap;
			((struct slot *) hp_slot_ptr->private)->bus = hpc_ptr->slots[index].slot_bus_num;

			bus_info_ptr1 = ibmphp_find_same_bus_num (hpc_ptr->slots[index].slot_bus_num);
			if (!bus_info_ptr1) {
				iounmap (io_mem);
				return -ENODEV;
			}
			((struct slot *) hp_slot_ptr->private)->bus_on = bus_info_ptr1;
			bus_info_ptr1 = NULL;
			((struct slot *) hp_slot_ptr->private)->ctrl = hpc_ptr;


			((struct slot *) hp_slot_ptr->private)->ctlr_index = hpc_ptr->slots[index].ctl_index;
			((struct slot *) hp_slot_ptr->private)->number = hpc_ptr->slots[index].slot_num;

			((struct slot *) hp_slot_ptr->private)->hotplug_slot = hp_slot_ptr;

			rc = ibmphp_hpc_fillhpslotinfo (hp_slot_ptr);
			if (rc) {
				iounmap (io_mem);
				return rc;
			}

			rc = ibmphp_init_devno ((struct slot **) &hp_slot_ptr->private);
			if (rc) {
				iounmap (io_mem);
				return rc;
			}
			hp_slot_ptr->ops = &ibmphp_hotplug_slot_ops;

			pci_hp_register (hp_slot_ptr);

			// end of registering ibm slot with hotplug core

			list_add (& ((struct slot *)(hp_slot_ptr->private))->ibm_slot_list, &ibmphp_slot_head);
		}

		print_bus_info ();
		list_add (&hpc_ptr->ebda_hpc_list, &ebda_hpc_head );

	}			/* each hpc  */
	print_ebda_hpc ();
	return 0;
}

/* 
 * map info (bus, devfun, start addr, end addr..) of i/o, memory,
 * pfm from the physical addr to a list of resource.
 */
static int ebda_rsrc_rsrc (void)
{
	u16 addr;
	short rsrc;
	u8 type, rsrc_type;
	struct ebda_pci_rsrc *rsrc_ptr;

	addr = rsrc_list_ptr->phys_addr;
	debug ("now entering rsrc land\n");
	debug ("offset of rsrc: %x\n", rsrc_list_ptr->phys_addr);

	for (rsrc = 0; rsrc < rsrc_list_ptr->num_entries; rsrc++) {
		type = readb (io_mem + addr);

		addr += 1;
		rsrc_type = type & EBDA_RSRC_TYPE_MASK;

		if (rsrc_type == EBDA_IO_RSRC_TYPE) {
			rsrc_ptr = alloc_ebda_pci_rsrc ();
			if (!rsrc_ptr) {
				iounmap (io_mem);
				return -ENOMEM;
			}
			rsrc_ptr->rsrc_type = type;

			rsrc_ptr->bus_num = readb (io_mem + addr);
			rsrc_ptr->dev_fun = readb (io_mem + addr + 1);
			rsrc_ptr->start_addr = readw (io_mem + addr + 2);
			rsrc_ptr->end_addr = readw (io_mem + addr + 4);
			addr += 6;

			debug ("rsrc from io type ----\n");
			debug ("rsrc type: %x bus#: %x dev_func: %x start addr: %lx end addr: %lx\n",
				rsrc_ptr->rsrc_type, rsrc_ptr->bus_num, rsrc_ptr->dev_fun, rsrc_ptr->start_addr, rsrc_ptr->end_addr);

			list_add (&rsrc_ptr->ebda_pci_rsrc_list, &ibmphp_ebda_pci_rsrc_head);
		}

		if (rsrc_type == EBDA_MEM_RSRC_TYPE || rsrc_type == EBDA_PFM_RSRC_TYPE) {
			rsrc_ptr = alloc_ebda_pci_rsrc ();
			if (!rsrc_ptr ) {
				iounmap (io_mem);
				return -ENOMEM;
			}
			rsrc_ptr->rsrc_type = type;

			rsrc_ptr->bus_num = readb (io_mem + addr);
			rsrc_ptr->dev_fun = readb (io_mem + addr + 1);
			rsrc_ptr->start_addr = readl (io_mem + addr + 2);
			rsrc_ptr->end_addr = readl (io_mem + addr + 6);
			addr += 10;

			debug ("rsrc from mem or pfm ---\n");
			debug ("rsrc type: %x bus#: %x dev_func: %x start addr: %lx end addr: %lx\n", 
				rsrc_ptr->rsrc_type, rsrc_ptr->bus_num, rsrc_ptr->dev_fun, rsrc_ptr->start_addr, rsrc_ptr->end_addr);

			list_add (&rsrc_ptr->ebda_pci_rsrc_list, &ibmphp_ebda_pci_rsrc_head);
		}
	}
	kfree (rsrc_list_ptr);
	rsrc_list_ptr = NULL;
	print_ebda_pci_rsrc ();
	return 0;
}

/*
 * map info of scalability details and rio details from physical address
 */
static int ebda_rio_table(void)
{
	u16 offset;
	u8 i;
	struct scal_detail *scal_detail_ptr;
	struct rio_detail *rio_detail_ptr;

	offset = rio_table_ptr->offset;
	for (i = 0; i < rio_table_ptr->scal_count; i++) {
		
		scal_detail_ptr = kmalloc (sizeof (struct scal_detail), GFP_KERNEL );
		if (!scal_detail_ptr )
			return -ENOMEM;
		memset (scal_detail_ptr, 0, sizeof (struct scal_detail) );
		scal_detail_ptr->node_id = readb (io_mem + offset);
		scal_detail_ptr->cbar = readl (io_mem+ offset + 1);
		scal_detail_ptr->port0_node_connect = readb (io_mem + 5);
		scal_detail_ptr->port0_port_connect = readb (io_mem + 6);
		scal_detail_ptr->port1_node_connect = readb (io_mem + 7);
		scal_detail_ptr->port1_port_connect = readb (io_mem + 8);
		scal_detail_ptr->port2_node_connect = readb (io_mem + 9);
		scal_detail_ptr->port2_port_connect = readb (io_mem + 10);
		debug ("node_id: %x\ncbar: %x\nport0_node: %x\nport0_port: %x\nport1_node: %x\nport1_port: %x\nport2_node: %x\nport2_port: %x\n", scal_detail_ptr->node_id, scal_detail_ptr->cbar, scal_detail_ptr->port0_node_connect, scal_detail_ptr->port0_port_connect, scal_detail_ptr->port1_node_connect, scal_detail_ptr->port1_port_connect, scal_detail_ptr->port2_node_connect, scal_detail_ptr->port2_port_connect);
//		list_add (&scal_detail_ptr->scal_detail_list, &scal_detail_head);
		offset += 11;
	}
	for (i=0; i < rio_table_ptr->riodev_count; i++) {
		rio_detail_ptr = kmalloc (sizeof (struct rio_detail), GFP_KERNEL );
		if (!rio_detail_ptr )
			return -ENOMEM;
		memset (rio_detail_ptr, 0, sizeof (struct rio_detail) );
		rio_detail_ptr->rio_node_id = readb (io_mem + offset );
		rio_detail_ptr->bbar = readl (io_mem + offset + 1);
		rio_detail_ptr->rio_type = readb (io_mem + offset + 5);
		rio_detail_ptr->owner_id = readb (io_mem + offset + 6);
		rio_detail_ptr->port0_node_connect = readb (io_mem + offset + 7);
		rio_detail_ptr->port0_port_connect = readb (io_mem + offset + 8);
		rio_detail_ptr->port1_node_connect = readb (io_mem + offset + 9);
		rio_detail_ptr->port1_port_connect = readb (io_mem + offset + 10);
		rio_detail_ptr->first_slot_num = readb (io_mem + offset + 11);
		rio_detail_ptr->status = readb (io_mem + offset + 12);
		debug ("rio_node_id: %x\nbbar: %x\nrio_type: %x\nowner_id: %x\nport0_node: %x\nport0_port: %x\nport1_node: %x\nport1_port: %x\nfirst_slot_num: %x\nstatus: %x\n", rio_detail_ptr->rio_node_id, rio_detail_ptr->bbar, rio_detail_ptr->rio_type, rio_detail_ptr->owner_id, rio_detail_ptr->port0_node_connect, rio_detail_ptr->port0_port_connect, rio_detail_ptr->port1_node_connect, rio_detail_ptr->port1_port_connect, rio_detail_ptr->first_slot_num, rio_detail_ptr->status);
		offset += 13;
	}
	return 0;
}

u16 ibmphp_get_total_controllers (void)
{
	return hpc_list_ptr->num_ctlrs;
}

struct slot *ibmphp_get_slot_from_physical_num (u8 physical_num)
{
	struct slot *slot;
	struct list_head *list;

	list_for_each (list, &ibmphp_slot_head) {
		slot = list_entry (list, struct slot, ibm_slot_list);
		if (slot->number == physical_num)
			return slot;
	}
	return NULL;
}

/* To find:
 *	- the smallest slot number
 *	- the largest slot number
 *	- the total number of the slots based on each bus
 *	  (if only one slot per bus slot_min = slot_max )
 */
struct bus_info *ibmphp_find_same_bus_num (u32 num)
{
	struct bus_info *ptr;
	struct list_head  *ptr1;

	list_for_each (ptr1, &bus_info_head) {
		ptr = list_entry (ptr1, struct bus_info, bus_info_list); 
		if (ptr->busno == num) 
			 return ptr;
	}
	return NULL;
}

/*  Finding relative bus number, in order to map corresponding
 *  bus register
 */
int ibmphp_get_bus_index (u8 num)
{
	struct bus_info *ptr;
	struct list_head  *ptr1;

	list_for_each (ptr1, &bus_info_head) {
		ptr = list_entry (ptr1, struct bus_info, bus_info_list);
		if (ptr->busno == num)  
			return ptr->index;
	}
	return -ENODEV;
}

void ibmphp_free_bus_info_queue (void)
{
	struct bus_info *bus_info;
	struct list_head *list;
	struct list_head *next;

	list_for_each_safe (list, next, &bus_info_head ) {
		bus_info = list_entry (list, struct bus_info, bus_info_list);
		kfree (bus_info);
	}
}

/*
 * Calculate the total hot pluggable slots controlled by total hpcs
 */
/*
int ibmphp_get_total_hp_slots (void)
{
	struct ebda_hpc *ptr;
	int slot_num = 0;

	ptr = ebda_hpc_head;
	while (ptr != NULL) {
		slot_num += ptr->slot_count;
		ptr = ptr->next;
	}
	return slot_num;
}
*/

void ibmphp_free_ebda_hpc_queue (void)
{
	struct controller *controller;
	struct list_head *list;
	struct list_head *next;

	list_for_each_safe (list, next, &ebda_hpc_head) {
		controller = list_entry (list, struct controller, ebda_hpc_list);
		free_ebda_hpc (controller);
	}
}

void ibmphp_free_ebda_pci_rsrc_queue (void)
{
	struct ebda_pci_rsrc *resource;
	struct list_head *list;
	struct list_head *next;

	list_for_each_safe (list, next, &ibmphp_ebda_pci_rsrc_head) {
		resource = list_entry (list, struct ebda_pci_rsrc, ebda_pci_rsrc_list);
		kfree (resource);
		resource = NULL;
	}
}

