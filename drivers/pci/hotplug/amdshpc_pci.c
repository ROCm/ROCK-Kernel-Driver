/* 
 * AMD Standard Hot Plug Controller Driver
 *
 * Copyright (c) 1995,2001,2003 Compaq Computer Corporation
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2003 IBM Corp.
 * Copyright (C) 2002-2004 Advanced Micro Devices
 *
 * YOUR USE OF THIS CODE IS SUBJECT TO THE TERMS
 * AND CONDITIONS OF THE GNU GENERAL PUBLIC
 * LICENSE FOUND IN THE "GPL.TXT" FILE THAT IS
 * INCLUDED WITH THIS FILE AND POSTED AT
 * http://www.gnu.org/licenses/gpl.html
 *
 * Send feedback to <greg@kroah.com> <david.keck@amd.com>
 *
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include "amdshpc.h"
#include "../pci.h"

u8 amdshpc_nic_irq;
u8 amdshpc_disk_irq;

static u16 unused_IRQ;

extern struct controller *amdshpc_ctrl_list;	/* = NULL */
extern struct pci_func *amdshpc_slot_list[256];

static int bridge_slot_remove(struct pci_func *bridge);
static int is_bridge(struct pci_func * func);
static int update_slot_info (struct controller  *ctrl, struct slot *slot);
static int slot_remove(struct pci_func * old_slot);
static u32 configure_new_device(struct controller * ctrl, struct pci_func *func,u8 behind_bridge, struct resource_lists *resources);
static int configure_new_function(struct controller * ctrl, struct pci_func *func,u8 behind_bridge, struct resource_lists *resources);
int amdshpc_process_SI (struct controller *ctrl, struct pci_func *func);

static u16 unused_IRQ;

/**
 * board_added - Called after a board has been added to the system.
 *
 * Turns power on for the board
 * Configures board
 *
 */
static u32 board_added(struct pci_func * func, struct controller * ctrl)
{
	int index;
	u32 temp_register = 0xFFFFFFFF;
	u32 rc = 0;
	struct pci_func *new_slot = NULL;
	struct resource_lists res_lists;

	dbg("%s: func->device, slot_offset = %d, %d \n",__FUNCTION__,
	    func->device, ctrl->slot_device_offset);

	// Get vendor/device ID u32
	rc = pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(func->device, func->function), PCI_VENDOR_ID, &temp_register);
	dbg("%s: pci_bus_read_config_dword returns %d\n",__FUNCTION__, rc);
	dbg("%s: temp_register is %x\n",__FUNCTION__, temp_register);

	if (rc != 0) {
		// Something's wrong here
		temp_register = 0xFFFFFFFF;
		dbg("%s: temp register set to %x by error\n",__FUNCTION__, temp_register);
	}
	// Preset return code.  It will be changed later if things go okay.
	rc = NO_ADAPTER_PRESENT;

	// All F's is an empty slot or an invalid board
	if (temp_register != 0xFFFFFFFF) {	  // Check for a board in the slot
		res_lists.io_head       = ctrl->io_head;
		res_lists.mem_head      = ctrl->mem_head;
		res_lists.p_mem_head = ctrl->p_mem_head;
		res_lists.bus_head      = ctrl->bus_head;
		res_lists.irqs = NULL;

		rc = configure_new_device(ctrl, func, 0, &res_lists);

		dbg("%s: back from configure_new_device\n",__FUNCTION__);
		ctrl->io_head   = res_lists.io_head;
		ctrl->mem_head  = res_lists.mem_head;
		ctrl->p_mem_head = res_lists.p_mem_head;
		ctrl->bus_head  = res_lists.bus_head;

		amdshpc_resource_sort_and_combine(&(ctrl->mem_head));
		amdshpc_resource_sort_and_combine(&(ctrl->p_mem_head));
		amdshpc_resource_sort_and_combine(&(ctrl->io_head));
		amdshpc_resource_sort_and_combine(&(ctrl->bus_head));

		if (rc) {
			// Something went wrong; disable slot
			return(rc);
		} else {
			amdshpc_save_slot_config(ctrl, func);
		}


		func->status = 0;
		func->switch_save = 0x10;
		func->is_a_board = 0x01;

		//next, we will instantiate the linux pci_dev structures (with appropriate driver notification, if already present)
		dbg("%s: configure linux pci_dev structure\n",__FUNCTION__);
		index = 0;
		do {
			new_slot = amdshpc_slot_find(ctrl->bus, func->device, index++);
			if (new_slot && !new_slot->pci_dev) {
				amdshpc_configure_device(ctrl, new_slot);
			}
		} while (new_slot);
	} else {
		// Something went wrong; disable slot
		return(rc);
	}
	return 0;
}


/**
 * remove_board - Returns resources
 */
static u32 remove_board(struct pci_func * func, u32 replace_flag, struct controller  * ctrl)
{
	int index;
	u8 skip = 0;
	u8 device;
	u8 hp_slot;
	u32 rc;
	struct resource_lists res_lists;
	struct pci_func *temp_func;

	if (func == NULL)
		return(1);

	if (amdshpc_unconfigure_device(func))
		return(1);

	device = func->device;

	hp_slot = func->device - ctrl->slot_device_offset;
	dbg("In %s, hp_slot = %d\n",__FUNCTION__, hp_slot);

	// When we get here, it is safe to change base Address Registers.
	// We will attempt to save the base Address Register Lengths
	if (replace_flag || !ctrl->add_support)
		rc = amdshpc_save_base_addr_length(ctrl, func);
	else if (!func->bus_head && !func->mem_head &&
		 !func->p_mem_head && !func->io_head) {
		// Here we check to see if we've saved any of the board's
		// resources already.  If so, we'll skip the attempt to
		// determine what's being used.
		index = 0;
		temp_func = amdshpc_slot_find(func->bus, func->device, index++);
		while (temp_func) {
			if (temp_func->bus_head || temp_func->mem_head
			    || temp_func->p_mem_head || temp_func->io_head) {
				skip = 1;
				break;
			}
			temp_func = amdshpc_slot_find(temp_func->bus, temp_func->device, index++);
		}

		if (!skip)
			rc = amdshpc_save_used_resources(ctrl, func);
	}
	// Change status to shutdown
	if (func->is_a_board)
		func->status = 0x01;
	func->configured = 0;

//	TO_DO_amd_disable_slot(ctrl, hp_slot);

	if (!replace_flag && ctrl->add_support) {
		while (func) {
			res_lists.io_head = ctrl->io_head;
			res_lists.mem_head = ctrl->mem_head;
			res_lists.p_mem_head = ctrl->p_mem_head;
			res_lists.bus_head = ctrl->bus_head;

			amdshpc_return_board_resources(func, &res_lists);

			ctrl->io_head = res_lists.io_head;
			ctrl->mem_head = res_lists.mem_head;
			ctrl->p_mem_head = res_lists.p_mem_head;
			ctrl->bus_head = res_lists.bus_head;

			amdshpc_resource_sort_and_combine(&(ctrl->mem_head));
			amdshpc_resource_sort_and_combine(&(ctrl->p_mem_head));
			amdshpc_resource_sort_and_combine(&(ctrl->io_head));
			amdshpc_resource_sort_and_combine(&(ctrl->bus_head));

			if (is_bridge(func)) {
				bridge_slot_remove(func);
			} else
				slot_remove(func);

			func = amdshpc_slot_find(ctrl->bus, device, 0);
		}

		// Setup slot structure with entry for empty slot
		func = amdshpc_slot_create(ctrl->bus);

		if (func == NULL) {
			// Out of memory
			return(1);
		}

		func->bus = ctrl->bus;
		func->device = device;
		func->function = 0;
		func->configured = 0;
		func->switch_save = 0x10;
		func->is_a_board = 0;
		func->p_task_event = NULL;
	}
	return 0;
}


/*
 * find_slot
 */
static inline struct slot* find_slot (struct controller* ctrl, u8 device)
{
	struct slot *slot;

	dbg("%s", __FUNCTION__);
	if (!ctrl)
		return NULL;

	slot = ctrl->slot;

	while (slot && (slot->device != device)) {
		slot = slot->next;
	}

	return slot;
}

// board insertion
int amdshpc_process_SI (struct controller *ctrl, struct pci_func *func)
{
	u8 device, hp_slot;
	u16 temp_word;
	u32 tempdword;
	int rc;
	struct slot* p_slot;
	int physical_slot = 0;

	dbg("%s  0", __FUNCTION__);
	if (!ctrl)
		return(1);

	tempdword = 0;

	device = func->device;
	hp_slot = device - ctrl->slot_device_offset;
	p_slot = find_slot(ctrl, device);
	if (p_slot) {
		physical_slot = p_slot->number;
	}

	if (tempdword & (0x01 << hp_slot)) {
		dbg("%s  1", __FUNCTION__);
		return(1);
	}

	// add board
	slot_remove(func);

	func = amdshpc_slot_create(ctrl->bus);
	dbg("%s  2",__FUNCTION__);
	if (func == NULL) {
		dbg("%s 3",__FUNCTION__);
		return(1);
	}

	func->bus = ctrl->bus;
	func->device = device;
	func->function = 0;
	func->configured = 0;
	func->is_a_board = 1;

	// We have to save the presence info for these slots
	temp_word = ctrl->ctrl_int_comp >> 16;
	func->presence_save = (temp_word >> hp_slot) & 0x01;
	func->presence_save |= (temp_word >> (hp_slot + 7)) & 0x02;

	dbg("%s 4",__FUNCTION__);
	if (ctrl->ctrl_int_comp & (0x1L << hp_slot)) {
		dbg("%s 5",__FUNCTION__);
		func->switch_save = 0;
	} else {
		dbg("%s 6",__FUNCTION__);
		func->switch_save = 0x10;
	}

	rc = board_added(func, ctrl);
	dbg("%s 7 rc=%d",__FUNCTION__,rc);
	if (rc) {
		dbg("%s 8",__FUNCTION__);
		if (is_bridge(func)) {
			dbg("%s 9",__FUNCTION__);
			bridge_slot_remove(func);
		} else {
			dbg("%s 10",__FUNCTION__);
			slot_remove(func);
		}

		// Setup slot structure with entry for empty slot
		func = amdshpc_slot_create(ctrl->bus);

		dbg("%s 11",__FUNCTION__);
		if (func == NULL) {
			// Out of memory
			return(1);
		}

		func->bus = ctrl->bus;
		func->device = device;
		func->function = 0;
		func->configured = 0;
		func->is_a_board = 0;

		// We have to save the presence info for these slots
		temp_word = ctrl->ctrl_int_comp >> 16;
		func->presence_save = (temp_word >> hp_slot) & 0x01;
		func->presence_save |=
			(temp_word >> (hp_slot + 7)) & 0x02;

		if (ctrl->ctrl_int_comp & (0x1L << hp_slot)) {
			dbg("%s 12",__FUNCTION__);
			func->switch_save = 0;
		} else {
			dbg("%s 13",__FUNCTION__);
			func->switch_save = 0x10;
		}
	}

	if (rc) {
		dbg("%s: rc = %d\n",__FUNCTION__, rc);
	}

	if (p_slot) {
		dbg("%s 14",__FUNCTION__);
		update_slot_info(ctrl, p_slot);
	}

	return rc;
}

// Disable Slot
int amdshpc_process_SS (struct controller *ctrl, struct pci_func *func)
{
	u8 device, class_code, header_type, BCR;
	u8 index = 0;
	u8 replace_flag;
	u32 rc = 0;
	struct slot* p_slot;
	int physical_slot=0;

	dbg("%s 0",__FUNCTION__);
	device = func->device;
	func = amdshpc_slot_find(ctrl->bus, device, index++);
	p_slot = find_slot(ctrl, device);
	if (p_slot) {
		physical_slot = p_slot->number;
	}

	// Make sure there are no video controllers here
	while (func && !rc) {
		dbg("%s 1..",__FUNCTION__);
		// Check the Class Code
		rc = pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(func->device, func->function), 0x0B, &class_code);
		dbg("%s 1.1 rc = %d  class_code = %02x",__FUNCTION__, rc, class_code);
		if (rc) {
			dbg("%s 2",__FUNCTION__);
			return rc;
		}

		if (class_code == PCI_BASE_CLASS_DISPLAY) {
			/* Display/Video adapter (not supported) */
			dbg("%s 3",__FUNCTION__);
			rc = REMOVE_NOT_SUPPORTED;
		} else {
			dbg("%s 3.5",__FUNCTION__);
			// See if it's a bridge
			rc = pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(func->device, func->function), PCI_HEADER_TYPE, &header_type);
			if (rc) {
				dbg("%s 4",__FUNCTION__);
				return rc;
			}

			// If it's a bridge, check the VGA Enable bit
			if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
				dbg("%s 4.5",__FUNCTION__);
				rc = pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(func->device, func->function), PCI_BRIDGE_CONTROL, &BCR);
				if (rc) {
					dbg("%s 5",__FUNCTION__);
					return rc;
				}

				dbg("%s 5.5",__FUNCTION__);
				// If the VGA Enable bit is set, remove isn't supported
				if (BCR & PCI_BRIDGE_CTL_VGA) {
					dbg("%s 6",__FUNCTION__);
					rc = REMOVE_NOT_SUPPORTED;
				}
			}
		}

		func = amdshpc_slot_find(ctrl->bus, device, index++);
		dbg("%s 7",__FUNCTION__);
	}

	func = amdshpc_slot_find(ctrl->bus, device, 0);
	dbg("%s 8",__FUNCTION__);
	if ((func != NULL) && !rc) {
		dbg("%s 9",__FUNCTION__);
		//FIXME: Replace flag should be passed into process_SS
		replace_flag = !(ctrl->add_support);
		rc = remove_board(func, replace_flag, ctrl);
	} else if (!rc) {
		dbg("%s 10",__FUNCTION__);
		rc = 1;
	}

	if (p_slot) {
		dbg("%s 11",__FUNCTION__);
		update_slot_info(ctrl, p_slot);
	}

	dbg("%s 12",__FUNCTION__);
	return(rc);
}


/*
 * detect_HRT_floating_pointer
 *
 * find the Hot Plug Resource Table in the specified region of memory.
 *
 */
static void *detect_HRT_floating_pointer(void *begin, void *end)
{
	void *fp;
	void *endp;
	u8 temp1, temp2, temp3, temp4;
	int status = 0;

	endp = (end - sizeof(struct hrt) + 1);

	for (fp = begin; fp <= endp; fp += 16) {
		temp1 = readb(fp + SIG0);
		temp2 = readb(fp + SIG1);
		temp3 = readb(fp + SIG2);
		temp4 = readb(fp + SIG3);
		if (temp1 == '$' &&
		    temp2 == 'H' &&
		    temp3 == 'R' &&
		    temp4 == 'T') {
			status = 1;
			dbg("%s -->temp string----> %c%c%c%c  at----->  %p\n", __FUNCTION__, temp1,temp2,temp3,temp4,fp);
			break;
		}
	}

	if (!status) {
		fp = NULL;
		dbg("%s -->Did not discover Hotplug Resource Table between start:%p  end:%p\n", __FUNCTION__, begin, end);
		return fp;
	}

	dbg("%s -->Discovered Hotplug Resource Table at %p\n", __FUNCTION__, fp);
	return fp;
}

/**
 * amdshpc_slot_find - Looks for a node by bus, and device, multiple functions accessed
 * @bus: bus to find
 * @device: device to find
 * @index: is 0 for first function found, 1 for the second...
 *
 * Returns pointer to the node if successful, %NULL otherwise.
 */
struct pci_func *amdshpc_slot_find(u8 bus, u8 device, u8 index) {
	int found = -1;
	struct pci_func *func;

	func = amdshpc_slot_list[bus];
	dbg("%s  amdshpc_slot_list[%02x] = %p", __FUNCTION__, bus, amdshpc_slot_list[bus]);
	dbg("%s  bus, device, index  %x %d %d", __FUNCTION__, bus, device, index);

	if ((func == NULL) || ((func->device == device) && (index == 0)))
		return(func);

	if (func->device == device)
		found++;

	while (func->next != NULL) {
		func = func->next;

		if (func->device == device)
			found++;

		if (found == index)
			return(func);
	}

	return(NULL);
}


/*
 * amdshpc_resource_sort_and_combine
 *
 * Sorts all of the nodes in the list in ascending order by
 * their base addresses.  Also does garbage collection by
 * combining adjacent nodes.
 *
 * returns 0 if success
 */
int amdshpc_resource_sort_and_combine(struct pci_resource **head)
{
	struct pci_resource *node1;
	struct pci_resource *node2;
	int out_of_order = 1;

	dbg("%s: head = %p, *head = %p\n",__FUNCTION__, head, *head);

	if (!(*head))
		return(1);

	dbg("%s -->*head->next = %p\n", __FUNCTION__,(*head)->next);

	if (!(*head)->next)
		return(0);	/* only one item on the list, already sorted! */

	dbg("%s -->*head->base = 0x%x\n", __FUNCTION__,(*head)->base);
	dbg("%s -->*head->next->base = 0x%x\n", __FUNCTION__,(*head)->next->base);
	while (out_of_order) {
		out_of_order = 0;

		// Special case for swapping list head
		if (((*head)->next) &&
		    ((*head)->base > (*head)->next->base)) {
			node1 = *head;
			(*head) = (*head)->next;
			node1->next = (*head)->next;
			(*head)->next = node1;
			out_of_order++;
		}

		node1 = (*head);

		while (node1->next && node1->next->next) {
			if (node1->next->base > node1->next->next->base) {
				out_of_order++;
				node2 = node1->next;
				node1->next = node1->next->next;
				node1 = node1->next;
				node2->next = node1->next;
				node1->next = node2;
			} else
				node1 = node1->next;
		}
	}  // End of out_of_order loop

	node1 = *head;

	while (node1 && node1->next) {
		if ((node1->base + node1->length) == node1->next->base) {
			// Combine
			dbg("%s -->8..\n", __FUNCTION__);
			node1->length += node1->next->length;
			node2 = node1->next;
			node1->next = node1->next->next;
			kfree(node2);
		} else
			node1 = node1->next;
	}

	return(0);
}


/*
 * amdshpc_find_available_resources
 *
 * Finds available memory, IO, and IRQ resources for programming
 * devices which may be added to the system
 * this function is for hot plug ADD!
 *
 * returns 0 if success
 */
int amdshpc_find_available_resources (struct controller *ctrl, void *rom_start)
{
	u8 temp;
	u8 populated_slot=0;
	u8 bridged_slot;
	u8 slot_index;
	void *one_slot;
	struct pci_func *func = NULL;
	int i = 10, index;
	u32 temp_dword, rc;
	struct pci_resource *mem_node;
	struct pci_resource *p_mem_node;
	struct pci_resource *io_node;
	struct pci_resource *bus_node;
	void *rom_resource_table;
	struct shpc_context *shpc_context;

	slot_index=0;

	shpc_context = (struct shpc_context* ) ctrl->shpc_context;
	rom_resource_table = detect_HRT_floating_pointer(rom_start, rom_start+0xffff);
	dbg("%s -->rom_resource_table = %p\n", __FUNCTION__, rom_resource_table);

	if (rom_resource_table == NULL) {
		return -ENODEV;
	}
	// Sum all resources and setup resource maps
	unused_IRQ = readl(rom_resource_table + UNUSED_IRQ);
	dbg("%s -->unused_IRQ = %x\n", __FUNCTION__, unused_IRQ);
	dbg("%s -->PCI_IRQ = %x\n", __FUNCTION__, readl(rom_resource_table + PCIIRQ));

	temp = 0;

	while (unused_IRQ) {
		if (unused_IRQ & 1) {
			amdshpc_disk_irq = temp;
			break;
		}
		unused_IRQ = unused_IRQ >> 1;
		temp++;
	}

	dbg("%s -->amdshpc_disk_irq= %d\n", __FUNCTION__, amdshpc_disk_irq);
	unused_IRQ = unused_IRQ >> 1;
	temp++;

	while (unused_IRQ) {
		if (unused_IRQ & 1) {
			amdshpc_nic_irq = temp;
			break;
		}
		unused_IRQ = unused_IRQ >> 1;
		temp++;
	}

	dbg("%s -->amdshpc_nic_irq= %d\n", __FUNCTION__, amdshpc_nic_irq);
	unused_IRQ = readl(rom_resource_table + PCIIRQ);

	temp = 0;

	if (!amdshpc_nic_irq) {
		amdshpc_nic_irq = ctrl->interrupt;
	}

	if (!amdshpc_disk_irq) {
		amdshpc_disk_irq = ctrl->interrupt;
	}

	dbg("%s -->amdshpc_disk_irq, amdshpc_nic_irq= %d, %d\n", __FUNCTION__, amdshpc_disk_irq, amdshpc_nic_irq);

	one_slot = rom_resource_table + sizeof (struct hrt);

	i = readb(rom_resource_table + NUMBER_OF_ENTRIES);
	dbg("%s -->number_of_entries = %d\n", __FUNCTION__, i);

	if (!readb(one_slot + SECONDARY_BUS)) {
		return(1);
	}

	dbg("%s -->dev|IO base|length|Mem base|length|Pre base|length|PB SB MB\n", __FUNCTION__);

	while (i && readb(one_slot + SECONDARY_BUS)) {
		u8 dev_func =           readb(one_slot + DEV_FUNC);
		u8 primary_bus =        readb(one_slot + PRIMARY_BUS);
		u8 secondary_bus =      readb(one_slot + SECONDARY_BUS);
		u8 max_bus =            readb(one_slot + MAX_BUS);
		u16 io_base =           readw(one_slot + IO_BASE);
		u16 io_length =         readw(one_slot + IO_LENGTH);
		u16 mem_base =          readw(one_slot + MEM_BASE);
		u16 mem_length =        readw(one_slot + MEM_LENGTH);
		u16 pre_mem_base =      readw(one_slot + PRE_MEM_BASE);
		u16 pre_mem_length = readw(one_slot + PRE_MEM_LENGTH);

		dbg("%s -->%2.2x | %4.4x  | %4.4x | %4.4x   | %4.4x | %4.4x   | %4.4x |%2.2x %2.2x %2.2x\n", __FUNCTION__,
		    dev_func, io_base, io_length, mem_base, mem_length, pre_mem_base, pre_mem_length,
		    primary_bus, secondary_bus, max_bus);

		// If this entry isn't for our controller's bus, ignore it
		if (primary_bus != ctrl->bus) {
			i--;
			one_slot += sizeof (struct slot_rt);
			continue;
		}

		// find out if this entry is for an occupied slot
		pci_bus_read_config_dword(ctrl->pci_bus, dev_func, PCI_VENDOR_ID, &temp_dword);
		dbg("bus %p, pri-bus %08x, slot %d, function %d, vend ID %d, tempDW %p\n",
			ctrl->pci_bus, primary_bus, PCI_SLOT(dev_func), PCI_FUNC(dev_func), PCI_VENDOR_ID, &temp_dword);

		dbg("%s -->temp_D_word = %08X\n", __FUNCTION__, temp_dword);

		if (temp_dword != 0xFFFFFFFF) {
			index = 0;
			func = amdshpc_slot_find(primary_bus, dev_func >> 3, 0);
			dbg("%s -->func = %p",__FUNCTION__, (unsigned long*)func);
			while (func && (func->function != PCI_FUNC(dev_func))) {
				dbg("%s -->func = %p (bus, dev, fun) = (%d, %d, %d)\n",__FUNCTION__, func, primary_bus, dev_func >> 3, index);
				func = amdshpc_slot_find(primary_bus, PCI_SLOT(dev_func), index++);
			}

			// If we can't find a match, skip this table entry
			if (!func) {
				i--;
				one_slot += sizeof (struct slot_rt);
				continue;
			}
			// this may not work and shouldn't be used
			if (secondary_bus != primary_bus) {
				bridged_slot = 1;
			} else {
				bridged_slot = 0;
			}
			shpc_context->slot_context[slot_index].slot_occupied = 1;
		} else {

			populated_slot = 0;
			bridged_slot = 0;
		}
		slot_index++;

		// If we've got a valid IO base, use it

		temp_dword = io_base + io_length;
		dbg("%s -->temp_D_word for io base = %08x",__FUNCTION__, temp_dword);

		if ((io_base) && (temp_dword < 0x10000)) {
			io_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!io_node)
				return -ENOMEM;

			io_node->base = io_base;
			io_node->length = io_length;

			dbg("%s -->found io_node(base, length) = %x, %x\n",__FUNCTION__, io_node->base, io_node->length);
			dbg("%s -->populated slot =%d \n",__FUNCTION__, populated_slot);
			if (!populated_slot) {
				io_node->next = ctrl->io_head;
				ctrl->io_head = io_node;
			} else {
				io_node->next = func->io_head;
				func->io_head = io_node;
			}
		}

		// If we've got a valid memory base, use it
		temp_dword = mem_base + mem_length;
		dbg("%s -->temp_D_word for mem base = %08x",__FUNCTION__, temp_dword);
		if ((mem_base) && (temp_dword < 0x10000)) {
			mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!mem_node)
				return -ENOMEM;

			mem_node->base = mem_base << 16;

			mem_node->length = mem_length << 16;

			dbg("%s -->found mem_node(base, length) = %08x, %08x\n",__FUNCTION__, mem_node->base, mem_node->length);
			dbg("%s -->populated slot =%d \n",__FUNCTION__, populated_slot);
			if (!populated_slot) {
				mem_node->next = ctrl->mem_head;
				ctrl->mem_head = mem_node;
			} else {
				mem_node->next = func->mem_head;
				func->mem_head = mem_node;
			}
		}

		// If we've got a valid prefetchable memory base, and
		// the base + length isn't greater than 0xFFFF
		temp_dword = pre_mem_base + pre_mem_length;
		dbg("%s -->temp_D_word for pre mem base = %08x",__FUNCTION__, temp_dword);
		if ((pre_mem_base) && (temp_dword < 0x10000)) {
			p_mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!p_mem_node)
				return -ENOMEM;

			p_mem_node->base = pre_mem_base << 16;

			p_mem_node->length = pre_mem_length << 16;
			dbg("%s -->found p_mem_node(base, length) = %08x, %08x\n",__FUNCTION__, p_mem_node->base, p_mem_node->length);
			dbg("%s -->populated slot =%d \n",__FUNCTION__, populated_slot);

			if (!populated_slot) {
				p_mem_node->next = ctrl->p_mem_head;
				ctrl->p_mem_head = p_mem_node;
			} else {
				p_mem_node->next = func->p_mem_head;
				func->p_mem_head = p_mem_node;
			}
		}

		// If we've got a valid bus number, use it
		// The second condition is to ignore bus numbers on
		// populated slots that don't have PCI-PCI bridges
		if (secondary_bus && (secondary_bus != primary_bus)) {
			bus_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!bus_node)
				return -ENOMEM;

			bus_node->base = secondary_bus;
			bus_node->length = max_bus - secondary_bus + 1;
			dbg("%s -->found bus_node(base, length) = %08x, %08x\n",__FUNCTION__, bus_node->base, bus_node->length);
			dbg("%s -->populated slot =%d \n",__FUNCTION__, populated_slot);
			if (!populated_slot) {
				bus_node->next = ctrl->bus_head;
				ctrl->bus_head = bus_node;
			} else {
				bus_node->next = func->bus_head;
				func->bus_head = bus_node;
			}
		}

		i--;
		one_slot += sizeof (struct slot_rt);
	}

	// If all of the following fail, we don't have any resources for
	// hot plug add
	rc = 1;
	rc &= amdshpc_resource_sort_and_combine(&(ctrl->mem_head));
	dbg("%s -->rc =%d \n",__FUNCTION__, rc);
	rc &= amdshpc_resource_sort_and_combine(&(ctrl->p_mem_head));
	dbg("%s -->rc =%d \n",__FUNCTION__, rc);
	rc &= amdshpc_resource_sort_and_combine(&(ctrl->io_head));
	dbg("%s -->rc =%d \n",__FUNCTION__, rc);
	rc &= amdshpc_resource_sort_and_combine(&(ctrl->bus_head));
	dbg("%s -->rc =%d \n",__FUNCTION__, rc);

	return(rc);
}



/*
 * amdshpc_save_config
 *
 * Reads configuration for all slots in a PCI bus and saves info.
 *
 * Note:  For non-hot plug busses, the slot # saved is the device #
 *
 * returns 0 if success
 */
int amdshpc_save_config(struct controller *ctrl, int busnumber, struct slot_config_info* is_hot_plug)
{
	long rc;
	u8 class_code;
	u8 header_type;
	u32 ID;
	u8 secondary_bus;
	struct pci_func *new_slot;
	int sub_bus;
	int FirstSupported;
	int LastSupported;
	int max_functions;
	int function;
	u8 DevError;
	int device = 0;
	int cloop = 0;
	int stop_it;
	int index;

	//  Decide which slots are supported
	if (is_hot_plug) {
		FirstSupported  = ctrl->first_slot;
		LastSupported = (FirstSupported + is_hot_plug->lu_slots_implemented) - 1;
	} else {
		FirstSupported = 0;
		LastSupported = 0x1F;
	}

	// Save PCI configuration space for all devices in supported slots
	for (device = FirstSupported; device <= LastSupported; device++) {
		int devfn = PCI_DEVFN(device, 0);
		ID = 0xFFFFFFFF;
		rc = pci_bus_read_config_dword(ctrl->pci_bus, devfn, PCI_VENDOR_ID, &ID);
		if (rc)
			return rc;

		if (ID != 0xFFFFFFFF) {	  //  device in slot
			rc = pci_bus_read_config_byte(ctrl->pci_bus, devfn, 0x0B, &class_code);
			if (rc)
				return rc;

			rc = pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_HEADER_TYPE, &header_type);
			if (rc)
				return rc;

			// If multi-function device, set max_functions to 8
			if (header_type & 0x80)
				max_functions = 8;
			else
				max_functions = 1;

			function = 0;

			do {
				DevError = 0;

				if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {	// P-P Bridge
					//  Recurse the subordinate bus
					//  get the subordinate bus number
					rc = pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_SECONDARY_BUS, &secondary_bus);
					if (rc) {
						return rc;
					} else {
						sub_bus = (int) secondary_bus;

						// Save secondary bus cfg spc
						// with this recursive call.
						rc = amdshpc_save_config(ctrl, sub_bus, 0);

						if (rc)
							return rc;
					}
				}

				index = 0;
				new_slot = amdshpc_slot_find(busnumber, device, index++);
				while (new_slot &&
				       (new_slot->function != (u8) function))
					new_slot = amdshpc_slot_find(busnumber, device, index++);

				if (!new_slot) {
					// Setup slot structure.
					new_slot = amdshpc_slot_create(busnumber);

					if (new_slot == NULL)
						return(1);
				}

				new_slot->bus = (u8) busnumber;
				new_slot->device = (u8) device;
				new_slot->function = (u8) function;
				new_slot->is_a_board = 1;
				new_slot->switch_save = 0x10;
				// In case of unsupported board
				new_slot->status = DevError;
				new_slot->pci_dev = pci_find_slot(new_slot->bus, (new_slot->device << 3) | new_slot->function);
				dbg("%s EXISTING SLOT", __FUNCTION__);
				dbg("%s ns->bus         = %d", __FUNCTION__, new_slot->bus);
				dbg("%s ns->device      = %d", __FUNCTION__, new_slot->device);
				dbg("%s ns->function    = %d", __FUNCTION__, new_slot->function);
				dbg("%s ns->is_a_board  = %d", __FUNCTION__, new_slot->is_a_board);
				dbg("%s ns->switch_save = %02x", __FUNCTION__, new_slot->switch_save);
				dbg("%s ns->pci_dev     = %p", __FUNCTION__, new_slot->pci_dev);

				for (cloop = 0; cloop < 0x20; cloop++) {
					rc = pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), cloop << 2, (u32 *) & (new_slot-> config_space [cloop]));
					if (rc)
						return rc;
				}

				function++;
				stop_it = 0;

				//  this loop skips to the next present function
				//  reading in Class Code and Header type.
				while ((function < max_functions)&&(!stop_it)) {
					rc = pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_VENDOR_ID, &ID);
					if (ID == 0xFFFFFFFF) {	 // nothing there.
						function++;
					} else {  // Something there
						rc = pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(device, function), 0x0B, &class_code);
						if (rc)
							return rc;

						rc = pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_HEADER_TYPE, &header_type);
						if (rc)
							return rc;

						stop_it++;
					}
				}

			} while (function < max_functions);
		}		// End of IF (device in slot?)
		else if (is_hot_plug) {
			// Setup slot structure with entry for empty slot
			new_slot = amdshpc_slot_create(busnumber);

			if (new_slot == NULL) {
				return(1);
			}

			new_slot->bus = (u8) busnumber;
			new_slot->device = (u8) device;
			new_slot->function = 0;
			new_slot->is_a_board = 0;
			new_slot->presence_save = 0;
			new_slot->switch_save = 0;
		}
		dbg("%s NEW SLOT", __FUNCTION__);
		dbg("%s ns->bus         = %d", __FUNCTION__, new_slot->bus);
		dbg("%s ns->device      = %d", __FUNCTION__, new_slot->function);
		dbg("%s ns->function    = %d", __FUNCTION__, new_slot->function);
	}// End of FOR loop

	return 0;
}


/*
 * amdshpc_set_irq
 *
 * @bus_num: bus number of PCI device
 * @dev_num: device number of PCI device
 */
/*
int amdshpc_set_irq (u8 bus_num, u8 dev_num, u8 int_pin, u8 irq_num)
{
	int rc;
	u16 temp_word;
	struct pci_dev fakedev;
	struct pci_bus fakebus;

	fakedev.devfn = dev_num << 3;
	fakedev.bus = &fakebus;
	fakebus.number = bus_num;
	dbg("%s : dev %d, bus %d, pin %d, num %d\n",__FUNCTION__,
		dev_num, bus_num, int_pin, irq_num);
	rc = pcibios_set_irq_routing(&fakedev, int_pin - 0x0a, irq_num);
	dbg("%s:rc %d\n",__FUNCTION__, rc);
	if (rc)
		return rc;

	// set the Edge Level Control Register (ELCR)
	temp_word = inb(0x4d0);
	temp_word |= inb(0x4d1) << 8;

	temp_word |= 0x01 << irq_num;

	// This should only be for x86 as it sets the Edge Level Control Register
	outb((u8) (temp_word & 0xFF), 0x4d0);
	outb((u8) ((temp_word & 0xFF00) >> 8), 0x4d1);

	return 0;
}
*/

/*
 * do_pre_bridge_resource_split
 *
 *	Returns zero or one node of resources that aren't in use
 *
 */
static struct pci_resource *do_pre_bridge_resource_split (struct pci_resource **head, struct pci_resource **orig_head, u32 alignment) {
	struct pci_resource *prevnode = NULL;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u32 rc;
	u32 temp_dword;
	dbg("%s -->do_pre_bridge_resource_split\n",__FUNCTION__);

	if (!(*head) || !(*orig_head))
		return(NULL);

	rc = amdshpc_resource_sort_and_combine(head);

	if (rc)
		return(NULL);

	if ((*head)->base != (*orig_head)->base)
		return(NULL);

	if ((*head)->length == (*orig_head)->length)
		return(NULL);


	// If we got here, there the bridge requires some of the resource, but
	// we may be able to split some off of the front

	node = *head;

	if (node->length & (alignment -1)) {
		// this one isn't an aligned length, so we'll make a new entry
		// and split it up.
		split_node = (struct pci_resource*) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

		if (!split_node)
			return(NULL);

		temp_dword = (node->length | (alignment-1)) + 1 - alignment;

		split_node->base = node->base;
		split_node->length = temp_dword;

		node->length -= temp_dword;
		node->base += split_node->length;

		// Put it in the list
		*head = split_node;
		split_node->next = node;
	}

	if (node->length < alignment) {
		return(NULL);
	}

	// Now unlink it
	if (*head == node) {
		*head = node->next;
		node->next = NULL;
	} else {
		prevnode = *head;
		while (prevnode->next != node)
			prevnode = prevnode->next;

		prevnode->next = node->next;
		node->next = NULL;
	}

	return(node);
}


/*
 * do_bridge_resource_split
 *
 *	Returns zero or one node of resources that aren't in use
 *
 */
static struct pci_resource *do_bridge_resource_split (struct pci_resource **head, u32 alignment) {
	struct pci_resource *prevnode = NULL;
	struct pci_resource *node;
	u32 rc;
	u32 temp_dword;

	if (!(*head))
		return(NULL);

	rc = amdshpc_resource_sort_and_combine(head);

	if (rc)
		return(NULL);

	node = *head;

	while (node->next) {
		prevnode = node;
		node = node->next;
		kfree(prevnode);
	}

	if (node->length < alignment) {
		kfree(node);
		return(NULL);
	}

	if (node->base & (alignment - 1)) {
		// Short circuit if adjusted size is too small
		temp_dword = (node->base | (alignment-1)) + 1;
		if ((node->length - (temp_dword - node->base)) < alignment) {
			kfree(node);
			return(NULL);
		}

		node->length -= (temp_dword - node->base);
		node->base = temp_dword;
	}

	if (node->length & (alignment - 1)) {
		// There's stuff in use after this node
		kfree(node);
		return(NULL);
	}

	return(node);
}


/*
 * sort_by_size
 *
 * Sorts nodes on the list by their length.
 * Smallest first.
 *
 */
static int sort_by_size(struct pci_resource **head)
{
	struct pci_resource *current_res;
	struct pci_resource *next_res;
	int out_of_order = 1;

	if (!(*head))
		return(1);

	if (!((*head)->next))
		return(0);

	while (out_of_order) {
		out_of_order = 0;

		// Special case for swapping list head
		if (((*head)->next) &&
		    ((*head)->length > (*head)->next->length)) {
			out_of_order++;
			current_res = *head;
			*head = (*head)->next;
			current_res->next = (*head)->next;
			(*head)->next = current_res;
		}

		current_res = *head;

		while (current_res->next && current_res->next->next) {
			if (current_res->next->length > current_res->next->next->length) {
				out_of_order++;
				next_res = current_res->next;
				current_res->next = current_res->next->next;
				current_res = current_res->next;
				next_res->next = current_res->next;
				current_res->next = next_res;
			} else
				current_res	= current_res->next;
		}
	}  // End of out_of_order loop

	return(0);
}

/**
 * amdshpc_slot_create - Creates a node and adds it to the proper bus.
 * @busnumber - bus where new node is to be located
 *
 * Returns pointer to the new node or NULL if unsuccessful
 */
struct pci_func *amdshpc_slot_create(u8 busnumber) {
	struct pci_func *new_slot;
	struct pci_func *next;

	dbg("%s  busnumber = %02xh",__FUNCTION__, busnumber);
	new_slot = (struct pci_func *) kmalloc(sizeof(struct pci_func), GFP_KERNEL);

	if (new_slot == NULL) {
		// I'm not dead yet!
		// You will be.
		return(new_slot);
	}

	memset(new_slot, 0, sizeof(struct pci_func));

	new_slot->next = NULL;
	new_slot->configured = 1;

	if (amdshpc_slot_list[busnumber] == NULL) {
		amdshpc_slot_list[busnumber] = new_slot;
		dbg("%s   created new slot in amdshpc_slot_list  amdshpc_slot_list[%02X] = %p", __FUNCTION__,
		    busnumber, amdshpc_slot_list[busnumber]);
	} else {
		next = amdshpc_slot_list[busnumber];
		while (next->next != NULL)
			next = next->next;
		next->next = new_slot;
	}
	return(new_slot);
}


/*
 * return_resource
 *
 * Puts node back in the resource list pointed to by head
 *
 */
static inline void return_resource (struct pci_resource **head, struct pci_resource *node)
{
	dbg("%s",__FUNCTION__);
	if (!node || !head)
		return;
	node->next = *head;
	*head = node;
}


/*
 * sort_by_max_size
 *
 * Sorts nodes on the list by their length.
 * Largest first.
 *
 */
static int sort_by_max_size(struct pci_resource **head)
{
	struct pci_resource *current_res;
	struct pci_resource *next_res;
	int out_of_order = 1;

	if (!(*head))
		return(1);

	if (!((*head)->next))
		return(0);

	while (out_of_order) {
		out_of_order = 0;

		// Special case for swapping list head
		if (((*head)->next) &&
		    ((*head)->length < (*head)->next->length)) {
			out_of_order++;
			current_res = *head;
			*head = (*head)->next;
			current_res->next = (*head)->next;
			(*head)->next = current_res;
		}

		current_res = *head;

		while (current_res->next && current_res->next->next) {
			if (current_res->next->length < current_res->next->next->length) {
				out_of_order++;
				next_res = current_res->next;
				current_res->next = current_res->next->next;
				current_res = current_res->next;
				next_res->next = current_res->next;
				current_res->next = next_res;
			} else
				current_res	= current_res->next;
		}
	}  // End of out_of_order loop

	return(0);
}


/*
 * get_max_resource
 *
 * Gets the largest node that is at least "size" big from the
 * list pointed to by head.  It aligns the node on top and bottom
 * to "size" alignment before returning it.
 */
static struct pci_resource *get_max_resource (struct pci_resource **head, u32 size) {
	struct pci_resource *max;
	struct pci_resource *temp;
	struct pci_resource *split_node;
	u32 temp_dword;

	if (!(*head))
		return(NULL);

	if (amdshpc_resource_sort_and_combine(head))
		return(NULL);

	if (sort_by_max_size(head))
		return(NULL);

	for (max = *head;max; max = max->next) {

		// If not big enough we could probably just bail,
		// instead we'll continue to the next.
		if (max->length < size)
			continue;

		if (max->base & (size - 1)) {
			// this one isn't base aligned properly
			// so we'll make a new entry and split it up
			temp_dword = (max->base | (size-1)) + 1;

			// Short circuit if adjusted size is too small
			if ((max->length - (temp_dword - max->base)) < size)
				continue;

			split_node = (struct pci_resource*) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = max->base;
			split_node->length = temp_dword - max->base;
			max->base = temp_dword;
			max->length -= split_node->length;

			// Put it next in the list
			split_node->next = max->next;
			max->next = split_node;
		}

		if ((max->base + max->length) & (size - 1)) {
			// this one isn't end aligned properly at the top
			// so we'll make a new entry and split it up
			split_node = (struct pci_resource*) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!split_node)
				return(NULL);
			temp_dword = ((max->base + max->length) & ~(size - 1));
			split_node->base = temp_dword;
			split_node->length = max->length + max->base
					     - split_node->base;
			max->length -= split_node->length;

			// Put it in the list
			split_node->next = max->next;
			max->next = split_node;
		}

		// Make sure it didn't shrink too much when we aligned it
		if (max->length < size)
			continue;

		// Now take it out of the list
		temp = (struct pci_resource*) *head;
		if (temp == max) {
			*head = max->next;
		} else {
			while (temp && temp->next != max) {
				temp = temp->next;
			}

			temp->next = max->next;
		}

		max->next = NULL;
		return(max);
	}

	// If we get here, we couldn't find one
	return(NULL);
}


/*
 * get_io_resource
 *
 * this function sorts the resource list by size and then
 * returns the first node of "size" length that is not in the
 * ISA aliasing window.  If it finds a node larger than "size"
 * it will split it up.
 *
 * size must be a power of two.
 */
static struct pci_resource *get_io_resource (struct pci_resource **head, u32 size) {
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u32 temp_dword;

	if (!(*head))
		return(NULL);

	if ( amdshpc_resource_sort_and_combine(head) )
		return(NULL);

	if ( sort_by_size(head) )
		return(NULL);

	for (node = *head; node; node = node->next) {
		if (node->length < size)
			continue;

		if (node->base & (size - 1)) {
			// this one isn't base aligned properly
			// so we'll make a new entry and split it up
			temp_dword = (node->base | (size-1)) + 1;

			// Short circuit if adjusted size is too small
			if ((node->length - (temp_dword - node->base)) < size)
				continue;

			split_node = (struct pci_resource*) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = node->base;
			split_node->length = temp_dword - node->base;
			node->base = temp_dword;
			node->length -= split_node->length;

			// Put it in the list
			split_node->next = node->next;
			node->next = split_node;
		} // End of non-aligned base

		// Don't need to check if too small since we already did
		if (node->length > size) {
			// this one is longer than we need
			// so we'll make a new entry and split it up
			split_node = (struct pci_resource*) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = node->base + size;
			split_node->length = node->length - size;
			node->length = size;

			// Put it in the list
			split_node->next = node->next;
			node->next = split_node;
		}  // End of too big on top end

		// For IO make sure it's not in the ISA aliasing space
		if (node->base & 0x300L)
			continue;

		// If we got here, then it is the right size
		// Now take it out of the list
		if (*head == node) {
			*head = node->next;
		} else {
			prevnode = *head;
			while (prevnode->next != node)
				prevnode = prevnode->next;

			prevnode->next = node->next;
		}
		node->next = NULL;
		// Stop looping
		break;
	}

	return(node);
}


/*
 * get_resource
 *
 * this function sorts the resource list by size and then
 * returns the first node of "size" length.  If it finds a node
 * larger than "size" it will split it up.
 *
 * size must be a power of two.
 */
static struct pci_resource *get_resource (struct pci_resource **head, u32 size) {
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u32 temp_dword;

	if (!(*head))
		return(NULL);

	if ( amdshpc_resource_sort_and_combine(head) )
		return(NULL);

	if ( sort_by_size(head) )
		return(NULL);

	for (node = *head; node; node = node->next) {
		dbg("%s: req_size =%x node=%p, base=%x, length=%x\n",__FUNCTION__,
		    size, node, node->base, node->length);
		if (node->length < size)
			continue;

		if (node->base & (size - 1)) {
			dbg("%s: not aligned\n",__FUNCTION__);
			// this one isn't base aligned properly
			// so we'll make a new entry and split it up
			temp_dword = (node->base | (size-1)) + 1;

			// Short circuit if adjusted size is too small
			if ((node->length - (temp_dword - node->base)) < size)
				continue;

			split_node = (struct pci_resource*) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = node->base;
			split_node->length = temp_dword - node->base;
			node->base = temp_dword;
			node->length -= split_node->length;

			// Put it in the list
			split_node->next = node->next;
			node->next = split_node;
		} // End of non-aligned base

		// Don't need to check if too small since we already did
		if (node->length > size) {
			dbg("%s: too big\n",__FUNCTION__);
			// this one is longer than we need
			// so we'll make a new entry and split it up
			split_node = (struct pci_resource*) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = node->base + size;
			split_node->length = node->length - size;
			node->length = size;

			// Put it in the list
			split_node->next = node->next;
			node->next = split_node;
		}  // End of too big on top end

		dbg("%s: got one!!!\n",__FUNCTION__);
		// If we got here, then it is the right size
		// Now take it out of the list
		if (*head == node) {
			*head = node->next;
		} else {
			prevnode = *head;
			while (prevnode->next != node)
				prevnode = prevnode->next;

			prevnode->next = node->next;
		}
		node->next = NULL;
		// Stop looping
		break;
	}
	return(node);
}

/*
 * amdshpc_return_board_resources
 *
 * this routine returns all resources allocated to a board to
 * the available pool.
 *
 * returns 0 if success
 */
int amdshpc_return_board_resources(struct pci_func * func, struct resource_lists * resources)
{
	int rc = 1;
	struct pci_resource *node;
	struct pci_resource *t_node;
	dbg("%s",__FUNCTION__);

	if (!func)
		return(1);

	node = func->io_head;
	func->io_head = NULL;
	while (node) {
		t_node = node->next;
		return_resource(&(resources->io_head), node);
		node = t_node;
	}

	node = func->mem_head;
	func->mem_head = NULL;
	while (node) {
		t_node = node->next;
		return_resource(&(resources->mem_head), node);
		node = t_node;
	}

	node = func->p_mem_head;
	func->p_mem_head = NULL;
	while (node) {
		t_node = node->next;
		return_resource(&(resources->p_mem_head), node);
		node = t_node;
	}

	node = func->bus_head;
	func->bus_head = NULL;
	while (node) {
		t_node = node->next;
		return_resource(&(resources->bus_head), node);
		node = t_node;
	}

	rc |= amdshpc_resource_sort_and_combine(&(resources->mem_head));
	rc |= amdshpc_resource_sort_and_combine(&(resources->p_mem_head));
	rc |= amdshpc_resource_sort_and_combine(&(resources->io_head));
	rc |= amdshpc_resource_sort_and_combine(&(resources->bus_head));

	return(rc);
}


/*
 * amdshpc_destroy_resource_list
 *
 * Puts node back in the resource list pointed to by head
 */
void amdshpc_destroy_resource_list (struct resource_lists * resources)
{
	struct pci_resource *res, *tres;

	res = resources->io_head;
	resources->io_head = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}

	res = resources->mem_head;
	resources->mem_head = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}

	res = resources->p_mem_head;
	resources->p_mem_head = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}

	res = resources->bus_head;
	resources->bus_head = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}
}


/*
 * amdshpc_destroy_board_resources
 *
 * Puts node back in the resource list pointed to by head
 */
void amdshpc_destroy_board_resources (struct pci_func * func)
{
	struct pci_resource *res, *tres;

	res = func->io_head;
	func->io_head = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}

	res = func->mem_head;
	func->mem_head = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}

	res = func->p_mem_head;
	func->p_mem_head = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}

	res = func->bus_head;
	func->bus_head = NULL;

	while (res) {
		tres = res;
		res = res->next;
		kfree(tres);
	}
}

/**
 * configure_new_device - Configures the PCI header information of one board.
 *
 * @ctrl: pointer to controller structure
 * @func: pointer to function structure
 * @behind_bridge: 1 if this is a recursive call, 0 if not
 * @resources: pointer to set of resource lists
 *
 * Returns 0 if success
 *
 */
static u32 configure_new_device (struct controller * ctrl, struct pci_func * func,
				 u8 behind_bridge, struct resource_lists * resources)
{
	u8 temp_byte, function, max_functions, stop_it;
	int rc;
	u32 ID;
	struct pci_func *new_slot;
	int index;

	new_slot = func;

	dbg("%s",__FUNCTION__);
	// Check for Multi-function device
	rc = pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(func->device, func->function), 0x0E, &temp_byte);
	if (rc) {
		dbg("%s: rc = %d\n",__FUNCTION__, rc);
		return rc;
	}

	if (temp_byte & 0x80)	// Multi-function device
		max_functions = 8;
	else
		max_functions = 1;

	function = 0;

	do {
		rc = configure_new_function(ctrl, new_slot, behind_bridge, resources);

		if (rc) {
			dbg("%s -->configure_new_function failed %d\n",__FUNCTION__,rc);
			index = 0;

			while (new_slot) {
				new_slot = amdshpc_slot_find(new_slot->bus, new_slot->device, index++);

				if (new_slot)
					amdshpc_return_board_resources(new_slot, resources);
			}

			return(rc);
		}

		function++;

		stop_it = 0;

		//  The following loop skips to the next present function
		//  and creates a board structure

		while ((function < max_functions) && (!stop_it)) {
			pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(func->device, function), 0x00, &ID);

			if (ID == 0xFFFFFFFF) {	  // There's nothing there.
				function++;
			} else {  // There's something there
				// Setup slot structure.
				new_slot = amdshpc_slot_create(func->bus);

				if (new_slot == NULL) {
					// Out of memory
					return(1);
				}

				new_slot->bus = func->bus;
				new_slot->device = func->device;
				new_slot->function = function;
				new_slot->is_a_board = 1;
				new_slot->status = 0;

				stop_it++;
			}
		}

	} while (function < max_functions);
	dbg("%s -->returning from configure_new_device\n",__FUNCTION__);

	return 0;
}


/*
  Configuration logic that involves the hotplug data structures and
  their bookkeeping
 */


/**
 * configure_new_function - Configures the PCI header information of one device
 *
 * @ctrl: pointer to controller structure
 * @func: pointer to function structure
 * @behind_bridge: 1 if this is a recursive call, 0 if not
 * @resources: pointer to set of resource lists
 *
 * Calls itself recursively for bridged devices.
 * Returns 0 if success
 *
 */
static int configure_new_function (struct controller * ctrl, struct pci_func * func,
				   u8 behind_bridge, struct resource_lists * resources)
{
	int cloop;
	u8 IRQ;
	u8 temp_byte;
	u8 device;
	u8 class_code;
	u16 command;
	u16 temp_word;
	u32 temp_dword;
	u32 rc;
	u32 temp_register;
	u32 base;
	u32 ID;
	struct pci_resource *mem_node;
	struct pci_resource *p_mem_node;
	struct pci_resource *io_node;
	struct pci_resource *bus_node;
	struct pci_resource *hold_mem_node;
	struct pci_resource *hold_p_mem_node;
	struct pci_resource *hold_IO_node;
	struct pci_resource *hold_bus_node;
	struct irq_mapping irqs;
	struct pci_func *new_slot;
	struct resource_lists temp_resources;
	int devfn = PCI_DEVFN(func->device, func->function);

	dbg("%s", __FUNCTION__);
	// Check for Bridge
	rc = pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_HEADER_TYPE, &temp_byte);
	if (rc)
		return rc;

	if ((temp_byte & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {	// PCI-PCI Bridge
		// set Primary bus
		dbg("%s -->set Primary bus = %d\n",__FUNCTION__, func->bus);
		rc = pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_PRIMARY_BUS, func->bus);
		if (rc)
			return rc;

		// find range of busses to use
		dbg("%s -->find ranges of buses to use\n",__FUNCTION__);
		bus_node = get_max_resource(&resources->bus_head, 1);

		// If we don't have any busses to allocate, we can't continue
		if (!bus_node)
			return -ENOMEM;

		// set Secondary bus
		temp_byte = bus_node->base;
		dbg("%s -->set Secondary bus = %d\n",__FUNCTION__, bus_node->base);
		rc = pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_SECONDARY_BUS, temp_byte);
		if (rc)
			return rc;

		// set subordinate bus
		temp_byte = bus_node->base + bus_node->length - 1;
		dbg("%s -->set subordinate bus = %d\n",__FUNCTION__, bus_node->base + bus_node->length - 1);
		rc = pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_SUBORDINATE_BUS, temp_byte);
		if (rc)
			return rc;

		// set subordinate Latency Timer and base Latency Timer
		temp_byte = 0x40;
		rc = pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_SEC_LATENCY_TIMER, temp_byte);
		if (rc)
			return rc;
		rc = pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_LATENCY_TIMER, temp_byte);
		if (rc)
			return rc;

		// set Cache Line size
		temp_byte = 0x08;
		rc = pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_CACHE_LINE_SIZE, temp_byte);
		if (rc)
			return rc;

		// Setup the IO, memory, and prefetchable windows

		io_node = get_max_resource(&(resources->io_head), 0x1000);
		mem_node = get_max_resource(&(resources->mem_head), 0x100000);
		p_mem_node = get_max_resource(&(resources->p_mem_head), 0x100000);
		dbg("%s -->Setup the IO, memory, and prefetchable windows\n",__FUNCTION__);
		dbg("%s -->io_node\n",__FUNCTION__);
		dbg("%s -->(base, len, next) (%x, %x, %p)\n",__FUNCTION__, io_node->base, io_node->length, io_node->next);
		dbg("%s -->mem_node\n",__FUNCTION__);
		dbg("%s -->(base, len, next) (%x, %x, %p)\n",__FUNCTION__, mem_node->base, mem_node->length, mem_node->next);
		dbg("%s -->p_mem_node\n",__FUNCTION__);
		dbg("%s -->(base, len, next) (%x, %x, %p)\n",__FUNCTION__, p_mem_node->base, p_mem_node->length, p_mem_node->next);

		// set up the IRQ info
		if (!resources->irqs) {
			irqs.barber_pole = 0;
			irqs.interrupt[0] = 0;
			irqs.interrupt[1] = 0;
			irqs.interrupt[2] = 0;
			irqs.interrupt[3] = 0;
			irqs.valid_INT = 0;
		} else {
			irqs.barber_pole = resources->irqs->barber_pole;
			irqs.interrupt[0] = resources->irqs->interrupt[0];
			irqs.interrupt[1] = resources->irqs->interrupt[1];
			irqs.interrupt[2] = resources->irqs->interrupt[2];
			irqs.interrupt[3] = resources->irqs->interrupt[3];
			irqs.valid_INT = resources->irqs->valid_INT;
		}

		// set up resource lists that are now aligned on top and bottom
		// for anything behind the bridge.
		temp_resources.bus_head = bus_node;
		temp_resources.io_head = io_node;
		temp_resources.mem_head = mem_node;
		temp_resources.p_mem_head = p_mem_node;
		temp_resources.irqs = &irqs;

		// Make copies of the nodes we are going to pass down so that
		// if there is a problem,we can just use these to free resources
		hold_bus_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
		hold_IO_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
		hold_mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
		hold_p_mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);

		if (!hold_bus_node || !hold_IO_node || !hold_mem_node || !hold_p_mem_node) {
			if (hold_bus_node)
				kfree(hold_bus_node);
			if (hold_IO_node)
				kfree(hold_IO_node);
			if (hold_mem_node)
				kfree(hold_mem_node);
			if (hold_p_mem_node)
				kfree(hold_p_mem_node);

			return(1);
		}

		memcpy(hold_bus_node, bus_node, sizeof(struct pci_resource));

		bus_node->base += 1;
		bus_node->length -= 1;
		bus_node->next = NULL;

		// If we have IO resources copy them and fill in the bridge's
		// IO range registers
		if (io_node) {
			memcpy(hold_IO_node, io_node, sizeof(struct pci_resource));
			io_node->next = NULL;

			// set IO base and Limit registers
			temp_byte = io_node->base >> 8;
			rc = pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_IO_BASE, temp_byte);

			temp_byte = (io_node->base + io_node->length - 1) >> 8;
			rc = pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_IO_LIMIT, temp_byte);
		} else {
			kfree(hold_IO_node);
			hold_IO_node = NULL;
		}

		// If we have memory resources copy them and fill in the bridge's
		// memory range registers.  Otherwise, fill in the range
		// registers with values that disable them.
		if (mem_node) {
			memcpy(hold_mem_node, mem_node, sizeof(struct pci_resource));
			mem_node->next = NULL;

			// set Mem base and Limit registers
			temp_word = mem_node->base >> 16;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_BASE, temp_word);

			temp_word = (mem_node->base + mem_node->length - 1) >> 16;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);
		} else {
			temp_word = 0xFFFF;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_BASE, temp_word);

			temp_word = 0x0000;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

			kfree(hold_mem_node);
			hold_mem_node = NULL;
		}

		// If we have prefetchable memory resources copy them and
		// fill in the bridge's memory range registers.  Otherwise,
		// fill in the range registers with values that disable them.
		if (p_mem_node) {
			memcpy(hold_p_mem_node, p_mem_node, sizeof(struct pci_resource));
			p_mem_node->next = NULL;

			// set Pre Mem base and Limit registers
			temp_word = p_mem_node->base >> 16;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_BASE, temp_word);

			temp_word = (p_mem_node->base + p_mem_node->length - 1) >> 16;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);
		} else {
			temp_word = 0xFFFF;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_BASE, temp_word);

			temp_word = 0x0000;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

			kfree(hold_p_mem_node);
			hold_p_mem_node = NULL;
		}

		// Adjust this to compensate for extra adjustment in first loop
		irqs.barber_pole--;

		rc = 0;

		// Here we actually find the devices and configure them
		for (device = 0; (device <= 0x1F) && !rc; device++) {
			irqs.barber_pole = (irqs.barber_pole + 1) & 0x03;

			ID = 0xFFFFFFFF;
			pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, 0), 0x00, &ID);

			if (ID != 0xFFFFFFFF) {	  //  device Present
				// Setup slot structure.
				new_slot = amdshpc_slot_create(hold_bus_node->base);

				if (new_slot == NULL) {
					// Out of memory
					rc = -ENOMEM;
					continue;
				}

				new_slot->bus = hold_bus_node->base;
				new_slot->device = device;
				new_slot->function = 0;
				new_slot->is_a_board = 1;
				new_slot->status = 0;

				rc = configure_new_device(ctrl, new_slot, 1, &temp_resources);
				dbg("%s -->configure_new_device rc=0x%x\n",__FUNCTION__,rc);
			}	// End of IF (device in slot?)
		}		// End of FOR loop

		if (rc) {
			amdshpc_destroy_resource_list(&temp_resources);

			return_resource(&(resources->bus_head), hold_bus_node);
			return_resource(&(resources->io_head), hold_IO_node);
			return_resource(&(resources->mem_head), hold_mem_node);
			return_resource(&(resources->p_mem_head), hold_p_mem_node);
			return(rc);
		}
		// save the interrupt routing information
		if (resources->irqs) {
			resources->irqs->interrupt[0] = irqs.interrupt[0];
			resources->irqs->interrupt[1] = irqs.interrupt[1];
			resources->irqs->interrupt[2] = irqs.interrupt[2];
			resources->irqs->interrupt[3] = irqs.interrupt[3];
			resources->irqs->valid_INT = irqs.valid_INT;
		} else if (!behind_bridge) {
			// We need to hook up the interrupts here
			for (cloop = 0; cloop < 4; cloop++) {
				if (irqs.valid_INT & (0x01 << cloop)) {
					rc=0;
//					rc = amdshpc_set_irq(func->bus, func->device,
//									   0x0A + cloop, irqs.interrupt[cloop]);
					if (rc) {
						amdshpc_destroy_resource_list (&temp_resources);

						return_resource(&(resources-> bus_head), hold_bus_node);
						return_resource(&(resources-> io_head), hold_IO_node);
						return_resource(&(resources-> mem_head), hold_mem_node);
						return_resource(&(resources-> p_mem_head), hold_p_mem_node);
						return rc;
					}
				}
			}	// end of for loop
		}
		// Return unused bus resources
		// First use the temporary node to store information for the board
		if (hold_bus_node && bus_node && temp_resources.bus_head) {
			hold_bus_node->length = bus_node->base - hold_bus_node->base;

			hold_bus_node->next = func->bus_head;
			func->bus_head = hold_bus_node;

			temp_byte = temp_resources.bus_head->base - 1;

			// set subordinate bus
			pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_SUBORDINATE_BUS, temp_byte);

			if (temp_resources.bus_head->length == 0) {
				kfree(temp_resources.bus_head);
				temp_resources.bus_head = NULL;
			} else {
				return_resource(&(resources->bus_head), temp_resources.bus_head);
			}
		}

		// If we have IO space available and there is some left,
		// return the unused portion
		if (hold_IO_node && temp_resources.io_head) {
			io_node = do_pre_bridge_resource_split(&(temp_resources.io_head),
							       &hold_IO_node, 0x1000);

			// Check if we were able to split something off
			if (io_node) {
				hold_IO_node->base = io_node->base + io_node->length;

				temp_byte = (hold_IO_node->base) >> 8;
				pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_IO_BASE, temp_byte);

				return_resource(&(resources->io_head), io_node);
			}

			io_node = do_bridge_resource_split(&(temp_resources.io_head), 0x1000);

			// Check if we were able to split something off
			if (io_node) {
				// First use the temporary node to store information for the board
				hold_IO_node->length = io_node->base - hold_IO_node->base;

				// If we used any, add it to the board's list
				if (hold_IO_node->length) {
					hold_IO_node->next = func->io_head;
					func->io_head = hold_IO_node;

					temp_byte = (io_node->base - 1) >> 8;
					pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_IO_LIMIT, temp_byte);

					return_resource(&(resources->io_head), io_node);
				} else {
					// it doesn't need any IO
					temp_word = 0x0000;
					pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_IO_LIMIT, temp_word);

					return_resource(&(resources->io_head), io_node);
					kfree(hold_IO_node);
				}
			} else {
				// it used most of the range
				hold_IO_node->next = func->io_head;
				func->io_head = hold_IO_node;
			}
		} else if (hold_IO_node) {
			// it used the whole range
			hold_IO_node->next = func->io_head;
			func->io_head = hold_IO_node;
		}
		// If we have memory space available and there is some left,
		// return the unused portion
		if (hold_mem_node && temp_resources.mem_head) {
			mem_node = do_pre_bridge_resource_split(&(temp_resources.  mem_head),
								&hold_mem_node, 0x100000);

			// Check if we were able to split something off
			if (mem_node) {
				hold_mem_node->base = mem_node->base + mem_node->length;

				temp_word = (hold_mem_node->base) >> 16;
				pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_BASE, temp_word);

				return_resource(&(resources->mem_head), mem_node);
			}

			mem_node = do_bridge_resource_split(&(temp_resources.mem_head), 0x100000);

			// Check if we were able to split something off
			if (mem_node) {
				// First use the temporary node to store information for the board
				hold_mem_node->length = mem_node->base - hold_mem_node->base;

				if (hold_mem_node->length) {
					hold_mem_node->next = func->mem_head;
					func->mem_head = hold_mem_node;

					// configure end address
					temp_word = (mem_node->base - 1) >> 16;
					pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

					// Return unused resources to the pool
					return_resource(&(resources->mem_head), mem_node);
				} else {
					// it doesn't need any Mem
					temp_word = 0x0000;
					pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

					return_resource(&(resources->mem_head), mem_node);
					kfree(hold_mem_node);
				}
			} else {
				// it used most of the range
				hold_mem_node->next = func->mem_head;
				func->mem_head = hold_mem_node;
			}
		} else if (hold_mem_node) {
			// it used the whole range
			hold_mem_node->next = func->mem_head;
			func->mem_head = hold_mem_node;
		}
		// If we have prefetchable memory space available and there is some
		// left at the end, return the unused portion
		if (hold_p_mem_node && temp_resources.p_mem_head) {
			p_mem_node = do_pre_bridge_resource_split(&(temp_resources.p_mem_head),
								  &hold_p_mem_node, 0x100000);

			// Check if we were able to split something off
			if (p_mem_node) {
				hold_p_mem_node->base = p_mem_node->base + p_mem_node->length;

				temp_word = (hold_p_mem_node->base) >> 16;
				pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_BASE, temp_word);

				return_resource(&(resources->p_mem_head), p_mem_node);
			}

			p_mem_node = do_bridge_resource_split(&(temp_resources.p_mem_head), 0x100000);

			// Check if we were able to split something off
			if (p_mem_node) {
				// First use the temporary node to store information for the board
				hold_p_mem_node->length = p_mem_node->base - hold_p_mem_node->base;

				// If we used any, add it to the board's list
				if (hold_p_mem_node->length) {
					hold_p_mem_node->next = func->p_mem_head;
					func->p_mem_head = hold_p_mem_node;

					temp_word = (p_mem_node->base - 1) >> 16;
					pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

					return_resource(&(resources->p_mem_head), p_mem_node);
				} else {
					// it doesn't need any PMem
					temp_word = 0x0000;
					pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

					return_resource(&(resources->p_mem_head), p_mem_node);
					kfree(hold_p_mem_node);
				}
			} else {
				// it used the most of the range
				hold_p_mem_node->next = func->p_mem_head;
				func->p_mem_head = hold_p_mem_node;
			}
		} else if (hold_p_mem_node) {
			// it used the whole range
			hold_p_mem_node->next = func->p_mem_head;
			func->p_mem_head = hold_p_mem_node;
		}
		// We should be configuring an IRQ and the bridge's base address
		// registers if it needs them.  Although we have never seen such
		// a device

		// enable card
		command = 0x0157;	// = PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |  PCI_COMMAND_INVALIDATE | PCI_COMMAND_PARITY | PCI_COMMAND_SERR
		pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_COMMAND, command);

		// set Bridge Control Register
		command = 0x07;		// = PCI_BRIDGE_CTL_PARITY | PCI_BRIDGE_CTL_SERR | PCI_BRIDGE_CTL_NO_ISA
		pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_BRIDGE_CONTROL, command);
	} else if ((temp_byte & 0x7F) == PCI_HEADER_TYPE_NORMAL) {
		// Standard device
		pci_bus_read_config_byte(ctrl->pci_bus, devfn, 0x0B, &class_code);

		if (class_code == PCI_BASE_CLASS_DISPLAY) {
			// Display (video) adapter (not supported)
			return(DEVICE_TYPE_NOT_SUPPORTED);
		}
		// Figure out IO and memory needs
		for (cloop = 0x10; cloop <= 0x24; cloop += 4) {
			temp_register = 0xFFFFFFFF;

			dbg("%s -->CND: devfn=%x, offset=%d\n",__FUNCTION__, devfn, cloop);
			pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, temp_register);
			pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &temp_register);
			dbg("%s -->CND: base = 0x%x\n",__FUNCTION__, temp_register);

			if (temp_register) {	  // If this register is implemented
				if ((temp_register & 0x03L) == 0x01) {
					// Map IO

					// set base = amount of IO space
					base = temp_register & 0xFFFFFFFC;
					base = ~base + 1;

					dbg("%s -->CND:      length = 0x%x\n",__FUNCTION__, base);
					io_node = get_io_resource(&(resources->io_head), base);
					dbg("%s -->Got io_node start = %8.8x, length = %8.8x next (%p)\n",__FUNCTION__,
					    io_node->base, io_node->length, io_node->next);
					dbg("%s -->func (%p) io_head (%p)\n",__FUNCTION__, func, func->io_head);

					// allocate the resource to the board
					if (io_node) {
						base = io_node->base;

						io_node->next = func->io_head;
						func->io_head = io_node;
					} else
						return -ENOMEM;
				} else if ((temp_register & 0x0BL) == 0x08) {
					// Map prefetchable memory
					base = temp_register & 0xFFFFFFF0;
					base = ~base + 1;

					dbg("%s -->CND:      length = 0x%x\n",__FUNCTION__, base);
					p_mem_node = get_resource(&(resources->p_mem_head), base);

					// allocate the resource to the board
					if (p_mem_node) {
						base = p_mem_node->base;

						p_mem_node->next = func->p_mem_head;
						func->p_mem_head = p_mem_node;
					} else
						return -ENOMEM;
				} else if ((temp_register & 0x0BL) == 0x00) {
					// Map memory
					base = temp_register & 0xFFFFFFF0;
					base = ~base + 1;

					dbg("%s -->CND:      length = 0x%x\n",__FUNCTION__, base);
					mem_node = get_resource(&(resources->mem_head), base);

					// allocate the resource to the board
					if (mem_node) {
						base = mem_node->base;

						mem_node->next = func->mem_head;
						func->mem_head = mem_node;
					} else
						return -ENOMEM;
				} else if ((temp_register & 0x0BL) == 0x04) {
					// Map memory
					base = temp_register & 0xFFFFFFF0;
					base = ~base + 1;

					dbg("%s -->CND:      length = 0x%x\n",__FUNCTION__, base);
					mem_node = get_resource(&(resources->mem_head), base);

					// allocate the resource to the board
					if (mem_node) {
						base = mem_node->base;

						mem_node->next = func->mem_head;
						func->mem_head = mem_node;
					} else
						return -ENOMEM;
				} else if ((temp_register & 0x0BL) == 0x06) {
					// Those bits are reserved, we can't handle this
					return(1);
				} else {
					// Requesting space below 1M
					return(NOT_ENOUGH_RESOURCES);
				}

				pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, base);

				// Check for 64-bit base
				if ((temp_register & 0x07L) == 0x04) {
					cloop += 4;

					// Upper 32 bits of address always zero on today's systems
					// FIXME this is probably not true on Alpha and ia64???
					base = 0;
					pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, base);
				}
			}
		}		// End of base register loop

		// Figure out which interrupt pin this function uses
		pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_INTERRUPT_PIN, &temp_byte);
		dbg("%s temp_byte for interrupt pin = %x", __FUNCTION__, temp_byte);
		// If this function needs an interrupt and we are behind a bridge
		// and the pin is tied to something that's already mapped,
		// set this one the same
		if (temp_byte && resources->irqs &&
		    (resources->irqs->valid_INT &
		     (0x01 << ((temp_byte + resources->irqs->barber_pole - 1) & 0x03)))) {
			// We have to share with something already set up
			IRQ = resources->irqs->interrupt[(temp_byte + resources->irqs->barber_pole - 1) & 0x03];
			dbg("%s We're sharing the IRQ from some other device = %02x", __FUNCTION__, IRQ);
		} else {
			// Program IRQ based on card type
			pci_bus_read_config_byte(ctrl->pci_bus, devfn, 0x0B, &class_code);
			if (class_code == PCI_BASE_CLASS_STORAGE) {
				dbg("%s We're sharing the disk IRQ (maybe)", __FUNCTION__);
				IRQ = amdshpc_disk_irq;
			} else {
				dbg("%s We're sharing the NIC IRQ (maybe)", __FUNCTION__);
				IRQ = amdshpc_nic_irq;
			}
		}

		// IRQ Line
		pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_INTERRUPT_LINE, IRQ);
		if (!behind_bridge) {
//			rc = amdshpc_set_irq(func->bus, func->device, temp_byte + 0x09, IRQ);
//			rc = amdshpc_set_irq(func->bus, func->device, temp_byte + 20, IRQ);
			rc = 0;
			if (rc)
				return 1;
		} else {
			//TBD - this code may also belong in the other clause of this If statement
			resources->irqs->interrupt[(temp_byte + resources->irqs->barber_pole - 1) & 0x03] = IRQ;
			resources->irqs->valid_INT |= 0x01 << (temp_byte + resources->irqs->barber_pole - 1) & 0x03;
		}

		// Latency Timer
		temp_byte = 0x40;
		pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_LATENCY_TIMER, temp_byte);

		// Cache Line size
		temp_byte = 0x08;
		pci_bus_write_config_byte(ctrl->pci_bus, devfn, PCI_CACHE_LINE_SIZE, temp_byte);

		// disable ROM base Address
		temp_dword = 0x00L;
		pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_ROM_ADDRESS, temp_dword);

		// enable card
		temp_word = 0x0157;	// = PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |  PCI_COMMAND_INVALIDATE | PCI_COMMAND_PARITY | PCI_COMMAND_SERR
		pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_COMMAND, temp_word);
	}			// End of Not-A-Bridge else
	else {
		// It's some strange type of PCI adapter (Cardbus?)
		return DEVICE_TYPE_NOT_SUPPORTED;
	}

	func->configured = 1;

	return 0;
}




int amdshpc_configure_device (struct controller * ctrl, struct pci_func* func)
{
	unsigned char bus;
	struct pci_dev dev0;
	struct pci_bus *child;
	int num;

	memset(&dev0, 0, sizeof(struct pci_dev));
	dbg("%s", __FUNCTION__);

	if (func->pci_dev == NULL)
		func->pci_dev = pci_find_slot(func->bus, PCI_DEVFN(func->device, func->function));

	//Still NULL ? Well then scan for it !
	if (func->pci_dev == NULL) {
		num = pci_scan_slot(ctrl->pci_dev->bus, PCI_DEVFN(func->device, func->function));
		dbg("%s ctrl->pci_dev->bus = %p", __FUNCTION__, ctrl->pci_dev->bus);

		if (num)
			pci_bus_add_devices(ctrl->pci_dev->bus);

		func->pci_dev = pci_find_slot(func->bus, PCI_DEVFN(func->device, func->function));
		if (func->pci_dev == NULL) {
			dbg("ERROR: pci_dev still null\n");
			return 0;
		}
	}

	if (func->pci_dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		pci_read_config_byte(func->pci_dev, PCI_SECONDARY_BUS, &bus);
		child = (struct pci_bus*) pci_add_new_bus(func->pci_dev->bus, (func->pci_dev), bus);
		pci_do_scan_bus(child);

	}

	return 0;
}


int amdshpc_unconfigure_device(struct pci_func* func)
{
	int j;

	dbg("%s: bus/dev/func = %x/%x/%x\n",__FUNCTION__,func->bus, func->device, func->function);

	for (j=0; j<8 ; j++) {
		struct pci_dev* temp = pci_find_slot(func->bus, (func->device << 3) | j);
		if (temp)
			pci_remove_bus_device(temp);
	}
	return 0;
}

/*
static int PCI_RefinedAccessConfig(struct pci_ops *ops, u8 bus, u8 device, u8 function, u8 offset, u32 *value)
{
	u32 vendID = 0;

	dbg("%s", __FUNCTION__);
	if (pci_read_config_dword_nodev (ops, bus, device, function, PCI_VENDOR_ID, &vendID) == -1)
		return -1;
	if (vendID == 0xffffffff)
		return -1;
	return pci_read_config_dword_nodev (ops, bus, device, function, offset, value);
}


//
// WTF??? This function isn't in the code, yet a function calls it, but the
// compiler optimizes it away?  strange.  Here as a placeholder to keep the
// compiler happy.
//
static int PCI_ScanBusNonBridge (u8 bus, u8 device)
{
	return 0;
}

static int PCI_ScanBusForNonBridge(struct controller  *ctrl, u8 bus_num, u8 * dev_num)
{
	u8 tdevice;
	u32 work;
	u8 tbus;

	dbg("%s", __FUNCTION__);
	for (tdevice = 0; tdevice < 0x100; tdevice++) {
		//Scan for access first
		if (PCI_RefinedAccessConfig(ctrl->pci_ops, bus_num, tdevice >> 3, tdevice & 0x7, 0x08, &work) == -1)
			continue;
		dbg("Looking for nonbridge bus_num %d dev_num %d\n", bus_num, tdevice);
		//Yep we got one. Not a bridge ?
		if ((work >> 8) != PCI_TO_PCI_BRIDGE_CLASS) {
			*dev_num = tdevice;
			dbg("found it !\n");
			return 0;
		}
	}
	for (tdevice = 0; tdevice < 0x100; tdevice++) {
		//Scan for access first
		if (PCI_RefinedAccessConfig(ctrl->pci_ops, bus_num, tdevice >> 3, tdevice & 0x7, 0x08, &work) == -1)
			continue;
		dbg("Looking for bridge bus_num %d dev_num %d\n", bus_num, tdevice);
		//Yep we got one. bridge ?
		if ((work >> 8) == PCI_TO_PCI_BRIDGE_CLASS) {
			pci_read_config_byte_nodev (ctrl->pci_ops, tbus, tdevice, 0, PCI_SECONDARY_BUS, &tbus);
			dbg("Recurse on bus_num %d tdevice %d\n", tbus, tdevice);
			if (PCI_ScanBusNonBridge(tbus, tdevice) == 0)
				return 0;
		}
	}

	return -1;
}

static int PCI_GetBusDevHelper(struct controller  *ctrl, u8 *bus_num, u8 *dev_num, u8 slot, u8 nobridge)
{
	struct irq_routing_table *PCIIRQRoutingInfoLength;
	long len;
	long loop;
	u32 work;

	u8 tbus, tdevice, tslot;

	PCIIRQRoutingInfoLength = pcibios_get_irq_routing_table();

	len = (PCIIRQRoutingInfoLength->size -
	       sizeof(struct irq_routing_table)) / sizeof(struct irq_info);
	dbg("%s  len = %d",__FUNCTION__, (int)len);
	// Make sure I got at least one entry
	if (len == 0) {
		if (PCIIRQRoutingInfoLength != NULL)
			kfree(PCIIRQRoutingInfoLength );
		return -1;
	}

	for (loop = 0; loop < len; ++loop) {
		tbus = PCIIRQRoutingInfoLength->slots[loop].bus;
		tdevice = PCIIRQRoutingInfoLength->slots[loop].devfn;
		tslot = PCIIRQRoutingInfoLength->slots[loop].slot;
		dbg("%s  tbus = %02Xh  tdevice = %02Xh  device = %02Xh function = %d  tslot = %d",__FUNCTION__,
							tbus, tdevice, tdevice >>3, tdevice & 0x7, tslot);
		if (tslot == slot) {
			*bus_num = tbus;
			*dev_num = tdevice;
			pci_read_config_dword_nodev (ctrl->pci_ops, *bus_num, *dev_num >> 3, *dev_num & 0x7, PCI_VENDOR_ID, &work);
			if (!nobridge || (work == 0xffffffff)) {
				if (PCIIRQRoutingInfoLength != NULL)
					dbg("%s PCIIRQRoutingInfoLength != NULL  returning 0",__FUNCTION__);
					kfree(PCIIRQRoutingInfoLength );
				return 0;
			}

			dbg("bus_num %d dev_num %d func_num %d\n", *bus_num, *dev_num >> 3, *dev_num & 0x7);
			pci_read_config_dword_nodev (ctrl->pci_ops, *bus_num, *dev_num >> 3, *dev_num & 0x7, PCI_CLASS_REVISION, &work);
			dbg("work >> 8 (%x) = BRIDGE (%x)\n", work >> 8, PCI_TO_PCI_BRIDGE_CLASS);

			if ((work >> 8) == PCI_TO_PCI_BRIDGE_CLASS) {
				pci_read_config_byte_nodev (ctrl->pci_ops, *bus_num, *dev_num >> 3, *dev_num & 0x7, PCI_SECONDARY_BUS, &tbus);
				dbg("Scan bus for Non Bridge: bus %d\n", tbus);
				if (PCI_ScanBusForNonBridge(ctrl, tbus, dev_num) == 0) {
					*bus_num = tbus;
					if (PCIIRQRoutingInfoLength != NULL)
						kfree(PCIIRQRoutingInfoLength );
					return 0;
				}
			} else {
				if (PCIIRQRoutingInfoLength != NULL)
					kfree(PCIIRQRoutingInfoLength );
				return 0;
			}

		}
	}
	if (PCIIRQRoutingInfoLength != NULL)
		kfree(PCIIRQRoutingInfoLength );
	return -1;
}


int amdshpc_get_bus_dev (struct controller  *ctrl, u8 * bus_num, u8 * dev_num, u8 slot)
{
	dbg("%s", __FUNCTION__);
	return PCI_GetBusDevHelper(ctrl, bus_num, dev_num, slot, 0);	//plain (bridges allowed)
}
*/

/* More PCI configuration routines; this time centered around hotplug controller */


/*
 * amdshpc_save_slot_config
 *
 * Saves configuration info for all PCI devices in a given slot
 * including subordinate busses.
 *
 * returns 0 if success
 */
int amdshpc_save_slot_config (struct controller  *ctrl, struct pci_func * new_slot)
{
	long rc;
	u8 class_code;
	u8 header_type;
	u32 ID;
	u8 secondary_bus;
	int sub_bus;
	int max_functions;
	int function;
	int cloop = 0;
	int stop_it;

	ID = 0xFFFFFFFF;

	dbg("%s", __FUNCTION__);
	pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(new_slot->device, 0), PCI_VENDOR_ID, &ID);

	if (ID != 0xFFFFFFFF) {	  //  device in slot
		pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(new_slot->device, 0), 0x0B, &class_code);
		pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(new_slot->device, 0), PCI_HEADER_TYPE, &header_type);

		if (header_type & 0x80)	// Multi-function device
			max_functions = 8;
		else
			max_functions = 1;

		function = 0;

		do {
			if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {	  // PCI-PCI Bridge
				//  Recurse the subordinate bus
				pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(new_slot->device, function), PCI_SECONDARY_BUS, &secondary_bus);

				sub_bus = (int) secondary_bus;

				// Save the config headers for the secondary bus.
				rc = amdshpc_save_config(ctrl, sub_bus, 0);

				if (rc)
					return(rc);

			}	// End of IF

			new_slot->status = 0;

			for (cloop = 0; cloop < 0x20; cloop++) {
				pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(new_slot->device, function), cloop << 2, (u32 *) & (new_slot-> config_space [cloop]));
			}

			function++;

			stop_it = 0;

			//  this loop skips to the next present function
			//  reading in the Class Code and the Header type.

			while ((function < max_functions) && (!stop_it)) {
				pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(new_slot->device, function), PCI_VENDOR_ID, &ID);

				if (ID == 0xFFFFFFFF) {	 // nothing there.
					function++;
				} else {  // Something there
					pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(new_slot->device, function), 0x0B, &class_code);
					pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(new_slot->device, function), PCI_HEADER_TYPE, &header_type);

					stop_it++;
				}
			}

		} while (function < max_functions);
	}			// End of IF (device in slot?)
	else {
		return(2);
	}

	return(0);
}


/*
 * amdshpc_save_base_addr_length
 *
 * Saves the length of all base address registers for the
 * specified slot.  this is for hot plug REPLACE
 *
 * returns 0 if success
 */
int amdshpc_save_base_addr_length(struct controller  *ctrl, struct pci_func * func)
{
	u8 cloop;
	u8 header_type;
	u8 secondary_bus;
	u8 type;
	int sub_bus;
	u32 temp_register;
	u32 base;
	u32 rc;
	struct pci_func *next;
	int index = 0;

	dbg("%s", __FUNCTION__);
	func = amdshpc_slot_find(func->bus, func->device, index++);

	while (func != NULL) {
		int devfn = PCI_DEVFN(func->device, func->function);

		// Check for Bridge
		pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_HEADER_TYPE, &header_type);

		if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
			// PCI-PCI Bridge
			pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_SECONDARY_BUS, &secondary_bus);

			sub_bus = (int) secondary_bus;

			next = amdshpc_slot_list[sub_bus];

			while (next != NULL) {
				rc = amdshpc_save_base_addr_length(ctrl, next);

				if (rc)
					return(rc);

				next = next->next;
			}

			//FIXME: this loop is duplicated in the non-bridge case.  The two could be rolled together
			// Figure out IO and memory base lengths
			for (cloop = 0x10; cloop <= 0x14; cloop += 4) {
				temp_register = 0xFFFFFFFF;
				pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, temp_register);
				pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &base);

				if (base) {  // If this register is implemented
					if (base & 0x01L) {
						// IO base
						// set base = amount of IO space requested
						base = base & 0xFFFFFFFE;
						base = (~base) + 1;

						type = 1;
					} else {
						// memory base
						base = base & 0xFFFFFFF0;
						base = (~base) + 1;

						type = 0;
					}
				} else {
					base = 0x0L;
					type = 0;
				}

				// Save information in slot structure
				func->base_length[(cloop - 0x10) >> 2] = base;
				func->base_type[(cloop - 0x10) >> 2] = type;

			}	// End of base register loop


		} else if ((header_type & 0x7F) == 0x00) {	  // PCI-PCI Bridge
			// Figure out IO and memory base lengths
			for (cloop = 0x10; cloop <= 0x24; cloop += 4) {
				temp_register = 0xFFFFFFFF;
				pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, temp_register);
				pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &base);

				if (base) {  // If this register is implemented
					if (base & 0x01L) {
						// IO base
						// base = amount of IO space requested
						base = base & 0xFFFFFFFE;
						base = (~base) + 1;

						type = 1;
					} else {
						// memory base
						// base = amount of memory space requested
						base = base & 0xFFFFFFF0;
						base = (~base) + 1;

						type = 0;
					}
				} else {
					base = 0x0L;
					type = 0;
				}

				// Save information in slot structure
				func->base_length[(cloop - 0x10) >> 2] = base;
				func->base_type[(cloop - 0x10) >> 2] = type;

			}	// End of base register loop

		} else {	  // Some other unknown header type
		}

		// find the next device in this slot
		func = amdshpc_slot_find(func->bus, func->device, index++);
	}

	return(0);
}


/*
 * amdshpc_save_used_resources
 *
 * Stores used resource information for existing boards.  this is
 * for boards that were in the system when this driver was loaded.
 * this function is for hot plug ADD
 *
 * returns 0 if success
 */
int amdshpc_save_used_resources (struct controller  *ctrl, struct pci_func * func)
{
	u8 cloop;
	u8 header_type;
	u8 secondary_bus;
	u8 temp_byte;
	u8 b_base;
	u8 b_length;
	u16 command;
	u16 save_command;
	u16 w_base;
	u16 w_length;
	u32 temp_register;
	u32 save_base;
	u32 base;
	int index = 0;
	struct pci_resource *mem_node;
	struct pci_resource *p_mem_node;
	struct pci_resource *io_node;
	struct pci_resource *bus_node;

	dbg("%s", __FUNCTION__);
	func = amdshpc_slot_find(func->bus, func->device, index++);

	while ((func != NULL) && func->is_a_board) {
		int devfn = PCI_DEVFN(func->device, func->function);
		// Save the command register
		pci_bus_read_config_word(ctrl->pci_bus, devfn, PCI_COMMAND, &save_command);

		// disable card
		command = 0x00;
		pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_COMMAND, command);

		// Check for Bridge
		pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_HEADER_TYPE, &header_type);

		if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {	  // PCI-PCI Bridge
			// Clear Bridge Control Register
			command = 0x00;
			pci_bus_write_config_word(ctrl->pci_bus, devfn, PCI_BRIDGE_CONTROL, command);
			pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_SECONDARY_BUS, &secondary_bus);
			pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_SUBORDINATE_BUS, &temp_byte);

			bus_node =(struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!bus_node)
				return -ENOMEM;

			bus_node->base = secondary_bus;
			bus_node->length = temp_byte - secondary_bus + 1;

			bus_node->next = func->bus_head;
			func->bus_head = bus_node;

			// Save IO base and Limit registers
			pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_IO_BASE, &b_base);
			pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_IO_LIMIT, &b_length);

			if ((b_base <= b_length) && (save_command & 0x01)) {
				io_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
				if (!io_node)
					return -ENOMEM;

				io_node->base = (b_base & 0xF0) << 8;
				io_node->length = (b_length - b_base + 0x10) << 8;

				io_node->next = func->io_head;
				func->io_head = io_node;
			}
			// Save memory base and Limit registers
			pci_bus_read_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_BASE, &w_base);
			pci_bus_read_config_word(ctrl->pci_bus, devfn, PCI_MEMORY_LIMIT, &w_length);

			if ((w_base <= w_length) && (save_command & 0x02)) {
				mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
				if (!mem_node)
					return -ENOMEM;

				mem_node->base = w_base << 16;
				mem_node->length = (w_length - w_base + 0x10) << 16;

				mem_node->next = func->mem_head;
				func->mem_head = mem_node;
			}
			// Save prefetchable memory base and Limit registers
			pci_bus_read_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_BASE, &w_base);
			pci_bus_read_config_word(ctrl->pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, &w_length);

			if ((w_base <= w_length) && (save_command & 0x02)) {
				p_mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
				if (!p_mem_node)
					return -ENOMEM;

				p_mem_node->base = w_base << 16;
				p_mem_node->length = (w_length - w_base + 0x10) << 16;

				p_mem_node->next = func->p_mem_head;
				func->p_mem_head = p_mem_node;
			}
			// Figure out IO and memory base lengths
			for (cloop = 0x10; cloop <= 0x14; cloop += 4) {
				pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &save_base);

				temp_register = 0xFFFFFFFF;
				pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, temp_register);
				pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &base);

				temp_register = base;

				if (base) {  // If this register is implemented
					if (((base & 0x03L) == 0x01)
					    && (save_command & 0x01)) {
						// IO base
						// set temp_register = amount of IO space requested
						temp_register = base & 0xFFFFFFFE;
						temp_register = (~temp_register) + 1;

						io_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
						if (!io_node)
							return -ENOMEM;

						io_node->base =
						save_base & (~0x03L);
						io_node->length = temp_register;

						io_node->next = func->io_head;
						func->io_head = io_node;
					} else
						if (((base & 0x0BL) == 0x08)
						    && (save_command & 0x02)) {
						// prefetchable memory base
						temp_register = base & 0xFFFFFFF0;
						temp_register = (~temp_register) + 1;

						p_mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
						if (!p_mem_node)
							return -ENOMEM;

						p_mem_node->base = save_base & (~0x0FL);
						p_mem_node->length = temp_register;

						p_mem_node->next = func->p_mem_head;
						func->p_mem_head = p_mem_node;
					} else
						if (((base & 0x0BL) == 0x00)
						    && (save_command & 0x02)) {
						// prefetchable memory base
						temp_register = base & 0xFFFFFFF0;
						temp_register = (~temp_register) + 1;

						mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
						if (!mem_node)
							return -ENOMEM;

						mem_node->base = save_base & (~0x0FL);
						mem_node->length = temp_register;

						mem_node->next = func->mem_head;
						func->mem_head = mem_node;
					} else
						return(1);
				}
			}	// End of base register loop
		} else if ((header_type & 0x7F) == 0x00) {	  // Standard header
			// Figure out IO and memory base lengths
			for (cloop = 0x10; cloop <= 0x24; cloop += 4) {
				pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &save_base);

				temp_register = 0xFFFFFFFF;
				pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, temp_register);
				pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &base);

				temp_register = base;

				if (base) {	  // If this register is implemented
					if (((base & 0x03L) == 0x01)
					    && (save_command & 0x01)) {
						// IO base
						// set temp_register = amount of IO space requested
						temp_register = base & 0xFFFFFFFE;
						temp_register = (~temp_register) + 1;

						io_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
						if (!io_node)
							return -ENOMEM;

						io_node->base = save_base & (~0x01L);
						io_node->length = temp_register;

						io_node->next = func->io_head;
						func->io_head = io_node;
					} else
						if (((base & 0x0BL) == 0x08)
						    && (save_command & 0x02)) {
						// prefetchable memory base
						temp_register = base & 0xFFFFFFF0;
						temp_register = (~temp_register) + 1;

						p_mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
						if (!p_mem_node)
							return -ENOMEM;

						p_mem_node->base = save_base & (~0x0FL);
						p_mem_node->length = temp_register;

						p_mem_node->next = func->p_mem_head;
						func->p_mem_head = p_mem_node;
					} else
						if (((base & 0x0BL) == 0x00)
						    && (save_command & 0x02)) {
						// prefetchable memory base
						temp_register = base & 0xFFFFFFF0;
						temp_register = (~temp_register) + 1;

						mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
						if (!mem_node)
							return -ENOMEM;

						mem_node->base = save_base & (~0x0FL);
						mem_node->length = temp_register;

						mem_node->next = func->mem_head;
						func->mem_head = mem_node;
					} else
						return(1);
				}
			}	// End of base register loop
		} else {	  // Some other unknown header type
		}

		// find the next device in this slot
		func = amdshpc_slot_find(func->bus, func->device, index++);
	}

	return(0);
}


/*
 * amdshpc_configure_board
 *
 * Copies saved configuration information to one slot.
 * this is called recursively for bridge devices.
 * this is for hot plug REPLACE!
 *
 * returns 0 if success
 */
int amdshpc_configure_board(struct controller  *ctrl, struct pci_func * func)
{
	int cloop;
	u8 header_type;
	u8 secondary_bus;
	int sub_bus;
	struct pci_func *next;
	u32 temp;
	u32 rc;
	int index = 0;

	dbg("%s", __FUNCTION__);
	func = amdshpc_slot_find(func->bus, func->device, index++);

	while (func != NULL) {
		int devfn = PCI_DEVFN(func->device, func->function);
		// Start at the top of config space so that the control
		// registers are programmed last
		for (cloop = 0x3C; cloop > 0; cloop -= 4) {
			pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, func->config_space[cloop >> 2]);
		}

		pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_HEADER_TYPE, &header_type);

		// If this is a bridge device, restore subordinate devices
		if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {	  // PCI-PCI Bridge
			pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_SECONDARY_BUS, &secondary_bus);

			sub_bus = (int) secondary_bus;

			next = amdshpc_slot_list[sub_bus];

			while (next != NULL) {
				rc = amdshpc_configure_board(ctrl, next);
				if (rc)
					return rc;

				next = next->next;
			}
		} else {
			// Check all the base Address Registers to make sure
			// they are the same.  If not, the board is different.
			for (cloop = 16; cloop < 40; cloop += 4) {
				pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &temp);
				if (temp != func->config_space[cloop >> 2]) {
					dbg("Config space compare failure!!! offset = %x\n", cloop);
					dbg("bus = %x, device = %x, function = %x\n", func->bus, func->device, func->function);
					dbg("temp = %x, config space = %x\n\n", temp, func->config_space[cloop]);
					return 1;
				}
			}
		}

		func->configured = 1;

		func = amdshpc_slot_find(func->bus, func->device, index++);
	}

	return 0;
}


/*
 * amdshpc_valid_replace
 *
 * this function checks to see if a board is the same as the
 * one it is replacing.  this check will detect if the device's
 * vendor or device id's are the same
 *
 * returns 0 if the board is the same nonzero otherwise
 */
int amdshpc_valid_replace(struct controller  *ctrl, struct pci_func * func)
{
	u8 cloop;
	u8 header_type;
	u8 secondary_bus;
	u8 type;
	u32 temp_register = 0;
	u32 base;
	u32 rc;
	struct pci_func *next;
	int index = 0;

	dbg("%s", __FUNCTION__);
	if (!func->is_a_board)
		return(ADD_NOT_SUPPORTED);

	func = amdshpc_slot_find(func->bus, func->device, index++);

	while (func != NULL) {
		int devfn = PCI_DEVFN(func->device, func->function);

		pci_bus_read_config_dword(ctrl->pci_bus, devfn, PCI_VENDOR_ID, &temp_register);

		// No adapter present
		if (temp_register == 0xFFFFFFFF)
			return(NO_ADAPTER_PRESENT);

		if (temp_register != func->config_space[0])
			return(ADAPTER_NOT_SAME);

		// Check for same revision number and class code
		pci_bus_read_config_dword(ctrl->pci_bus, devfn, PCI_CLASS_REVISION, &temp_register);

		// Adapter not the same
		if (temp_register != func->config_space[0x08 >> 2])
			return(ADAPTER_NOT_SAME);

		// Check for Bridge
		pci_bus_read_config_byte(ctrl->pci_bus, devfn, PCI_HEADER_TYPE, &header_type);

		if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {	  // PCI-PCI Bridge
			// In order to continue checking, we must program the
			// bus registers in the bridge to respond to accesses
			// for it's subordinate bus(es)

			temp_register = func->config_space[0x18 >> 2];
			pci_bus_write_config_dword(ctrl->pci_bus, devfn, PCI_PRIMARY_BUS, temp_register);

			secondary_bus = (temp_register >> 8) & 0xFF;

			next = amdshpc_slot_list[secondary_bus];

			while (next != NULL) {
				rc = amdshpc_valid_replace(ctrl, next);
				if (rc)
					return(rc);

				next = next->next;
			}

		}
		// Check to see if it is a standard config header
		else if ((header_type & 0x7F) == PCI_HEADER_TYPE_NORMAL) {
			// Check subsystem vendor and ID
			pci_bus_read_config_dword(ctrl->pci_bus, devfn, PCI_SUBSYSTEM_VENDOR_ID, &temp_register);

			if (temp_register != func->config_space[0x2C >> 2]) {
				// If it's a SMART-2 and the register isn't filled
				// in, ignore the difference because
				// they just have an old rev of the firmware

				if (!((func->config_space[0] == 0xAE100E11)
				      && (temp_register == 0x00L)))
					return(ADAPTER_NOT_SAME);
			}
			// Figure out IO and memory base lengths
			for (cloop = 0x10; cloop <= 0x24; cloop += 4) {
				temp_register = 0xFFFFFFFF;
				pci_bus_write_config_dword(ctrl->pci_bus, devfn, cloop, temp_register);
				pci_bus_read_config_dword(ctrl->pci_bus, devfn, cloop, &base);

				if (base) {	  // If this register is implemented
					if (base & 0x01L) {
						// IO base
						// set base = amount of IO space requested
						base = base & 0xFFFFFFFE;
						base = (~base) + 1;

						type = 1;
					} else {
						// memory base
						base = base & 0xFFFFFFF0;
						base = (~base) + 1;

						type = 0;
					}
				} else {
					base = 0x0L;
					type = 0;
				}

				// Check information in slot structure
				if (func->base_length[(cloop - 0x10) >> 2] != base)
					return(ADAPTER_NOT_SAME);

				if (func->base_type[(cloop - 0x10) >> 2] != type)
					return(ADAPTER_NOT_SAME);

			}	// End of base register loop

		}		// End of (type 0 config space) else
		else {
			// this is not a type 0 or 1 config space header so
			// we don't know how to do it
			return(DEVICE_TYPE_NOT_SUPPORTED);
		}

		// Get the next function
		func = amdshpc_slot_find(func->bus, func->device, index++);
	}

	return(0);
}



static int update_slot_info (struct controller  *ctrl, struct slot *slot)
{
//	TO_DO_amd_update_slot_info();
	dbg("%s   THIS FUNCTION IS STUBBED OUT!!!!!!!!!!!",__FUNCTION__);
	return 0;
	/*	struct hotplug_slot_info *info;
	char buffer[SLOT_NAME_SIZE];
	int result;

	info = kmalloc (sizeof (struct hotplug_slot_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	make_slot_name (&buffer[0], SLOT_NAME_SIZE, slot);
	info->power_status = get_slot_enabled(ctrl, slot);
	info->attention_status = cpq_get_attention_status(ctrl, slot);
	info->latch_status = cpq_get_latch_status(ctrl, slot);
	info->adapter_status = get_presence_status(ctrl, slot);
	result = pci_hp_change_slot_info(buffer, info);
	kfree (info);
	return result;
*/
}


/*
 * slot_remove - Removes a node from the linked list of slots.
 * @old_slot: slot to remove
 *
 * Returns 0 if successful, !0 otherwise.
 */
static int slot_remove(struct pci_func * old_slot)
{
	struct pci_func *next;

	dbg("%s", __FUNCTION__);
	if (old_slot == NULL)
		return(1);

	next = amdshpc_slot_list[old_slot->bus];

	if (next == NULL) {
		return(1);
	}

	if (next == old_slot) {
		amdshpc_slot_list[old_slot->bus] = old_slot->next;
		amdshpc_destroy_board_resources(old_slot);
		kfree(old_slot);
		return(0);
	}

	while ((next->next != old_slot) && (next->next != NULL)) {
		next = next->next;
	}

	if (next->next == old_slot) {
		next->next = old_slot->next;
		amdshpc_destroy_board_resources(old_slot);
		kfree(old_slot);
		return(0);
	} else
		return(2);
}

// DJZ: I don't think is_bridge will work as is.
//FIXME
static int is_bridge(struct pci_func * func)
{
	dbg("%s", __FUNCTION__);
	// Check the header type
	if (((func->config_space[0x03] >> 16) & 0xFF) == 0x01)
		return 1;
	else
		return 0;
}


/**
 * bridge_slot_remove - Removes a node from the linked list of slots.
 * @bridge: bridge to remove
 *
 * Returns 0 if successful, !0 otherwise.
 */
static int bridge_slot_remove(struct pci_func *bridge)
{
	u8 subordinateBus, secondaryBus;
	u8 tempBus;
	struct pci_func *next;

	dbg("%s", __FUNCTION__);
	if (bridge == NULL)
		return(1);

	secondaryBus = (bridge->config_space[0x06] >> 8) & 0xFF;
	subordinateBus = (bridge->config_space[0x06] >> 16) & 0xFF;

	for (tempBus = secondaryBus; tempBus <= subordinateBus; tempBus++) {
		next = amdshpc_slot_list[tempBus];

		while (!slot_remove(next)) {
			next = amdshpc_slot_list[tempBus];
		}
	}

	next = amdshpc_slot_list[bridge->bus];

	if (next == NULL) {
		return(1);
	}

	if (next == bridge) {
		amdshpc_slot_list[bridge->bus] = bridge->next;
		kfree(bridge);
		return(0);
	}

	while ((next->next != bridge) && (next->next != NULL)) {
		next = next->next;
	}

	if (next->next == bridge) {
		next->next = bridge->next;
		kfree(bridge);
		return(0);
	} else
		return(2);
}


