/*
 * AGPGART driver.
 * Copyright (C) 2002 Dave Jones.
 * Copyright (C) 1999 Jeff Hartmann.
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * TODO: 
 * - Allocate more than order 0 pages to avoid too much linear map splitting.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/miscdevice.h>
#include <linux/pm.h>
#include <linux/agp_backend.h>
#include <linux/vmalloc.h>
#include "agp.h"

__u32 *agp_gatt_table; 
int agp_memory_reserved;

/* 
 * Generic routines for handling agp_memory structures -
 * They use the basic page allocation routines to do the
 * brunt of the work.
 */

void agp_free_key(int key)
{

	if (key < 0)
		return;

	if (key < MAXKEY)
		clear_bit(key, agp_bridge->key_list);
}

static int agp_get_key(void)
{
	int bit;

	bit = find_first_zero_bit(agp_bridge->key_list, MAXKEY);
	if (bit < MAXKEY) {
		set_bit(bit, agp_bridge->key_list);
		return bit;
	}
	return -1;
}

agp_memory *agp_create_memory(int scratch_pages)
{
	agp_memory *new;

	new = kmalloc(sizeof(agp_memory), GFP_KERNEL);

	if (new == NULL)
		return NULL;

	memset(new, 0, sizeof(agp_memory));
	new->key = agp_get_key();

	if (new->key < 0) {
		kfree(new);
		return NULL;
	}
	new->memory = vmalloc(PAGE_SIZE * scratch_pages);

	if (new->memory == NULL) {
		agp_free_key(new->key);
		kfree(new);
		return NULL;
	}
	new->num_scratch_pages = scratch_pages;
	return new;
}

void agp_free_memory(agp_memory * curr)
{
	size_t i;

	if ((agp_bridge->type == NOT_SUPPORTED) || (curr == NULL))
		return;

	if (curr->is_bound == TRUE)
		agp_unbind_memory(curr);

	if (curr->type != 0) {
		agp_bridge->free_by_type(curr);
		return;
	}
	if (curr->page_count != 0) {
		for (i = 0; i < curr->page_count; i++) {
			agp_bridge->agp_destroy_page(phys_to_virt(curr->memory[i]));
		}
	}
	agp_free_key(curr->key);
	vfree(curr->memory);
	kfree(curr);
}

#define ENTRIES_PER_PAGE		(PAGE_SIZE / sizeof(unsigned long))

agp_memory *agp_allocate_memory(size_t page_count, u32 type)
{
	int scratch_pages;
	agp_memory *new;
	size_t i;

	if (agp_bridge->type == NOT_SUPPORTED)
		return NULL;

	if ((atomic_read(&agp_bridge->current_memory_agp) + page_count) > agp_bridge->max_memory_agp)
		return NULL;

	if (type != 0) {
		new = agp_bridge->alloc_by_type(page_count, type);
		return new;
	}

	scratch_pages = (page_count + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;

	new = agp_create_memory(scratch_pages);

	if (new == NULL)
		return NULL;

	for (i = 0; i < page_count; i++) {
		void *addr = agp_bridge->agp_alloc_page();

		if (addr == NULL) {
			agp_free_memory(new);
			return NULL;
		}
		new->memory[i] = virt_to_phys(addr);
		new->page_count++;
	}

	flush_agp_mappings();

	return new;
}

/* End - Generic routines for handling agp_memory structures */

static int agp_return_size(void)
{
	int current_size;
	void *temp;

	temp = agp_bridge->current_size;

	switch (agp_bridge->size_type) {
	case U8_APER_SIZE:
		current_size = A_SIZE_8(temp)->size;
		break;
	case U16_APER_SIZE:
		current_size = A_SIZE_16(temp)->size;
		break;
	case U32_APER_SIZE:
		current_size = A_SIZE_32(temp)->size;
		break;
	case LVL2_APER_SIZE:
		current_size = A_SIZE_LVL2(temp)->size;
		break;
	case FIXED_APER_SIZE:
		current_size = A_SIZE_FIX(temp)->size;
		break;
	default:
		current_size = 0;
		break;
	}

	current_size -= (agp_memory_reserved / (1024*1024));
	if (current_size <0)
		current_size = 0;
	return current_size;
}

int agp_num_entries(void)
{
	int num_entries;
	void *temp;

	temp = agp_bridge->current_size;

	switch (agp_bridge->size_type) {
	case U8_APER_SIZE:
		num_entries = A_SIZE_8(temp)->num_entries;
		break;
	case U16_APER_SIZE:
		num_entries = A_SIZE_16(temp)->num_entries;
		break;
	case U32_APER_SIZE:
		num_entries = A_SIZE_32(temp)->num_entries;
		break;
	case LVL2_APER_SIZE:
		num_entries = A_SIZE_LVL2(temp)->num_entries;
		break;
	case FIXED_APER_SIZE:
		num_entries = A_SIZE_FIX(temp)->num_entries;
		break;
	default:
		num_entries = 0;
		break;
	}

	num_entries -= agp_memory_reserved>>PAGE_SHIFT;
	if (num_entries<0)
		num_entries = 0;
	return num_entries;
}

/* Routine to copy over information structure */

int agp_copy_info(agp_kern_info * info)
{
	memset(info, 0, sizeof(agp_kern_info));
	if (agp_bridge->type == NOT_SUPPORTED) {
		info->chipset = agp_bridge->type;
		return -EIO;
	}
	info->version.major = agp_bridge->version->major;
	info->version.minor = agp_bridge->version->minor;
	info->device = agp_bridge->dev;
	info->chipset = agp_bridge->type;
	info->mode = agp_bridge->mode;
	info->aper_base = agp_bridge->gart_bus_addr;
	info->aper_size = agp_return_size();
	info->max_memory = agp_bridge->max_memory_agp;
	info->current_memory = atomic_read(&agp_bridge->current_memory_agp);
	info->cant_use_aperture = agp_bridge->cant_use_aperture;
	info->vm_ops = agp_bridge->vm_ops;
	info->page_mask = ~0UL;
	return 0;
}

/* End - Routine to copy over information structure */

/*
 * Routines for handling swapping of agp_memory into the GATT -
 * These routines take agp_memory and insert them into the GATT.
 * They call device specific routines to actually write to the GATT.
 */

int agp_bind_memory(agp_memory * curr, off_t pg_start)
{
	int ret_val;

	if ((agp_bridge->type == NOT_SUPPORTED) ||
	    (curr == NULL) || (curr->is_bound == TRUE)) {
		return -EINVAL;
	}
	if (curr->is_flushed == FALSE) {
		CACHE_FLUSH();
		curr->is_flushed = TRUE;
	}
	ret_val = agp_bridge->insert_memory(curr, pg_start, curr->type);

	if (ret_val != 0)
		return ret_val;

	curr->is_bound = TRUE;
	curr->pg_start = pg_start;
	return 0;
}

int agp_unbind_memory(agp_memory * curr)
{
	int ret_val;

	if ((agp_bridge->type == NOT_SUPPORTED) || (curr == NULL))
		return -EINVAL;

	if (curr->is_bound != TRUE)
		return -EINVAL;

	ret_val = agp_bridge->remove_memory(curr, curr->pg_start, curr->type);

	if (ret_val != 0)
		return ret_val;

	curr->is_bound = FALSE;
	curr->pg_start = 0;
	return 0;
}

/* End - Routines for handling swapping of agp_memory into the GATT */


/* Generic Agp routines - Start */

u32 agp_collect_device_status(u32 mode, u32 command)
{
	struct pci_dev *device;
	u8 agp;
	u32 scratch; 

	pci_for_each_dev(device) {
		agp = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (!agp)
			continue;

		/*
		 * Ok, here we have a AGP device. Disable impossible 
		 * settings, and adjust the readqueue to the minimum.
		 */
		pci_read_config_dword(device, agp + PCI_AGP_STATUS, &scratch);

		/* adjust RQ depth */
		command = ((command & ~AGPSTAT_RQ_DEPTH) |
		     min_t(u32, (mode & AGPSTAT_RQ_DEPTH),
			 min_t(u32, (command & AGPSTAT_RQ_DEPTH),
			     (scratch & AGPSTAT_RQ_DEPTH))));

		/* disable SBA if it's not supported */
		if (!((command & AGPSTAT_SBA) && (scratch & AGPSTAT_SBA) && (mode & AGPSTAT_SBA)))
			command &= ~AGPSTAT_SBA;

		/* disable FW if it's not supported */
		if (!((command & AGPSTAT_FW) && (scratch & AGPSTAT_FW) && (mode & AGPSTAT_FW)))
			command &= ~AGPSTAT_FW;

		/* Set speed */
		if (!((command & AGPSTAT2_4X) && (scratch & AGPSTAT2_4X) && (mode & AGPSTAT2_4X)))
			command &= ~AGPSTAT2_4X;

		if (!((command & AGPSTAT2_2X) && (scratch & AGPSTAT2_2X) && (mode & AGPSTAT2_2X)))
			command &= ~AGPSTAT2_2X;

		if (!((command & AGPSTAT2_1X) && (scratch & AGPSTAT2_1X) && (mode & AGPSTAT2_1X)))
			command &= ~AGPSTAT2_1X;
	}

	/* Now we know what mode it should be, clear out the unwanted bits. */
	if (command & AGPSTAT2_4X)
		command &= ~(AGPSTAT2_1X | AGPSTAT2_2X);	/* 4X */

	if (command & AGPSTAT2_2X)
		command &= ~(AGPSTAT2_1X | AGPSTAT2_4X);	/* 2X */

	if (command & AGPSTAT2_1X)
		command &= ~(AGPSTAT2_2X | AGPSTAT2_4X);	/* 1Xf */

	return command;
}

void agp_device_command(u32 command, int agp_v3)
{
	struct pci_dev *device;
	int mode;

	mode = command & 0x7;
	if (agp_v3)
		mode *= 4;

	pci_for_each_dev(device) {
		u8 agp = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (!agp)
			continue;

		printk(KERN_INFO PFX "Putting AGP V%d device at %s into %dx mode\n",
				agp_v3 ? 3 : 2, device->slot_name, mode);
		pci_write_config_dword(device, agp + PCI_AGP_COMMAND, command);
	}
}

void agp_generic_enable(u32 mode)
{
	u32 command, ncapid, major, minor;

	pci_read_config_dword(agp_bridge->dev, agp_bridge->capndx, &ncapid);
	major = (ncapid >> 20) & 0xf;
	minor = (ncapid >> 16) & 0xf;
	printk(KERN_INFO PFX "Found an AGP %d.%d compliant device.\n",major, minor);

	if(major >= 3) {
		u32 agp_3_0;

		pci_read_config_dword(agp_bridge->dev, agp_bridge->capndx + 0x4, &agp_3_0);
		/* Check to see if we are operating in 3.0 mode */
		if((agp_3_0 >> 3) & 0x1) {
			agp_3_0_node_enable(mode, minor);
			return;
		} else {
			printk (KERN_INFO PFX "not in AGP 3.0 mode, falling back to 2.x\n");
		}
	}

	/* AGP v<3 */
	pci_read_config_dword(agp_bridge->dev,
		      agp_bridge->capndx + PCI_AGP_STATUS, &command);

	command = agp_collect_device_status(mode, command);
	command |= 0x100;

	pci_write_config_dword(agp_bridge->dev,
		       agp_bridge->capndx + PCI_AGP_COMMAND, command);
	agp_device_command(command, 0);
}

int agp_generic_create_gatt_table(void)
{
	char *table;
	char *table_end;
	int size;
	int page_order;
	int num_entries;
	int i;
	void *temp;
	struct page *page;

	/* The generic routines can't handle 2 level gatt's */
	if (agp_bridge->size_type == LVL2_APER_SIZE)
		return -EINVAL;

	table = NULL;
	i = agp_bridge->aperture_size_idx;
	temp = agp_bridge->current_size;
	size = page_order = num_entries = 0;

	if (agp_bridge->size_type != FIXED_APER_SIZE) {
		do {
			switch (agp_bridge->size_type) {
			case U8_APER_SIZE:
				size = A_SIZE_8(temp)->size;
				page_order =
				    A_SIZE_8(temp)->page_order;
				num_entries =
				    A_SIZE_8(temp)->num_entries;
				break;
			case U16_APER_SIZE:
				size = A_SIZE_16(temp)->size;
				page_order = A_SIZE_16(temp)->page_order;
				num_entries = A_SIZE_16(temp)->num_entries;
				break;
			case U32_APER_SIZE:
				size = A_SIZE_32(temp)->size;
				page_order = A_SIZE_32(temp)->page_order;
				num_entries = A_SIZE_32(temp)->num_entries;
				break;
				/* This case will never really happen. */
			case FIXED_APER_SIZE:
			case LVL2_APER_SIZE:
			default:
				size = page_order = num_entries = 0;
				break;
			}

			table = (char *) __get_free_pages(GFP_KERNEL,
							  page_order);

			if (table == NULL) {
				i++;
				switch (agp_bridge->size_type) {
				case U8_APER_SIZE:
					agp_bridge->current_size = A_IDX8();
					break;
				case U16_APER_SIZE:
					agp_bridge->current_size = A_IDX16();
					break;
				case U32_APER_SIZE:
					agp_bridge->current_size = A_IDX32();
					break;
					/* This case will never really 
					 * happen. 
					 */
				case FIXED_APER_SIZE:
				case LVL2_APER_SIZE:
				default:
					agp_bridge->current_size =
					    agp_bridge->current_size;
					break;
				}
				temp = agp_bridge->current_size;	
			} else {
				agp_bridge->aperture_size_idx = i;
			}
		} while ((table == NULL) && (i < agp_bridge->num_aperture_sizes));
	} else {
		size = ((struct aper_size_info_fixed *) temp)->size;
		page_order = ((struct aper_size_info_fixed *) temp)->page_order;
		num_entries = ((struct aper_size_info_fixed *) temp)->num_entries;
		table = (char *) __get_free_pages(GFP_KERNEL, page_order);
	}

	if (table == NULL)
		return -ENOMEM;

	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
		SetPageReserved(page);

	agp_bridge->gatt_table_real = (u32 *) table;
	agp_gatt_table = (void *)table; 
	CACHE_FLUSH();
	agp_bridge->gatt_table = ioremap_nocache(virt_to_phys(table),
					(PAGE_SIZE * (1 << page_order)));
	CACHE_FLUSH();

	if (agp_bridge->gatt_table == NULL) {
		for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
			ClearPageReserved(page);

		free_pages((unsigned long) table, page_order);

		return -ENOMEM;
	}
	agp_bridge->gatt_bus_addr = virt_to_phys(agp_bridge->gatt_table_real);

	/* AK: bogus, should encode addresses > 4GB */
	for (i = 0; i < num_entries; i++)
		agp_bridge->gatt_table[i] = (unsigned long) agp_bridge->scratch_page;

	return 0;
}

int agp_generic_suspend(void)
{
	return 0;
}

void agp_generic_resume(void)
{
	return;
}

int agp_generic_free_gatt_table(void)
{
	int page_order;
	char *table, *table_end;
	void *temp;
	struct page *page;

	temp = agp_bridge->current_size;

	switch (agp_bridge->size_type) {
	case U8_APER_SIZE:
		page_order = A_SIZE_8(temp)->page_order;
		break;
	case U16_APER_SIZE:
		page_order = A_SIZE_16(temp)->page_order;
		break;
	case U32_APER_SIZE:
		page_order = A_SIZE_32(temp)->page_order;
		break;
	case FIXED_APER_SIZE:
		page_order = A_SIZE_FIX(temp)->page_order;
		break;
	case LVL2_APER_SIZE:
		/* The generic routines can't deal with 2 level gatt's */
		return -EINVAL;
		break;
	default:
		page_order = 0;
		break;
	}

	/* Do not worry about freeing memory, because if this is
	 * called, then all agp memory is deallocated and removed
	 * from the table.
	 */

	iounmap(agp_bridge->gatt_table);
	table = (char *) agp_bridge->gatt_table_real;
	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
		ClearPageReserved(page);

	free_pages((unsigned long) agp_bridge->gatt_table_real, page_order);
	return 0;
}

int agp_generic_insert_memory(agp_memory * mem, off_t pg_start, int type)
{
	int num_entries;
	size_t i;
	off_t j;
	void *temp;

	temp = agp_bridge->current_size;

	switch (agp_bridge->size_type) {
	case U8_APER_SIZE:
		num_entries = A_SIZE_8(temp)->num_entries;
		break;
	case U16_APER_SIZE:
		num_entries = A_SIZE_16(temp)->num_entries;
		break;
	case U32_APER_SIZE:
		num_entries = A_SIZE_32(temp)->num_entries;
		break;
	case FIXED_APER_SIZE:
		num_entries = A_SIZE_FIX(temp)->num_entries;
		break;
	case LVL2_APER_SIZE:
		/* The generic routines can't deal with 2 level gatt's */
		return -EINVAL;
		break;
	default:
		num_entries = 0;
		break;
	}

	num_entries -= agp_memory_reserved/PAGE_SIZE;
	if (num_entries < 0) num_entries = 0;

	if (type != 0 || mem->type != 0) {
		/* The generic routines know nothing of memory types */
		return -EINVAL;
	}

	/* AK: could wrap */
	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;

	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge->gatt_table[j])) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++)
		agp_bridge->gatt_table[j] =
				agp_bridge->mask_memory(mem->memory[i], mem->type);

	agp_bridge->tlb_flush(mem);
	return 0;
}

int agp_generic_remove_memory(agp_memory * mem, off_t pg_start, int type)
{
	size_t i;

	if (type != 0 || mem->type != 0) {
		/* The generic routines know nothing of memory types */
		return -EINVAL;
	}

	/* AK: bogus, should encode addresses > 4GB */
	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		agp_bridge->gatt_table[i] =
		    (unsigned long) agp_bridge->scratch_page;
	}

	agp_bridge->tlb_flush(mem);
	return 0;
}

agp_memory *agp_generic_alloc_by_type(size_t page_count, int type)
{
	return NULL;
}

void agp_generic_free_by_type(agp_memory * curr)
{
	if (curr->memory != NULL)
		vfree(curr->memory);

	agp_free_key(curr->key);
	kfree(curr);
}

/* 
 * Basic Page Allocation Routines -
 * These routines handle page allocation
 * and by default they reserve the allocated 
 * memory.  They also handle incrementing the
 * current_memory_agp value, Which is checked
 * against a maximum value.
 */

void *agp_generic_alloc_page(void)
{
	struct page * page;
	
	page = alloc_page(GFP_KERNEL);
	if (page == NULL)
		return 0;

	map_page_into_agp(page);

	get_page(page);
	SetPageLocked(page);
	atomic_inc(&agp_bridge->current_memory_agp);
	return page_address(page);
}

void agp_generic_destroy_page(void *addr)
{
	struct page *page;

	if (addr == NULL)
		return;

	page = virt_to_page(addr);
	unmap_page_from_agp(page);
	put_page(page);
	unlock_page(page);
	free_page((unsigned long)addr);
	atomic_dec(&agp_bridge->current_memory_agp);
}

/* End Basic Page Allocation Routines */

void agp_enable(u32 mode)
{
	if (agp_bridge->type == NOT_SUPPORTED)
		return;
	agp_bridge->agp_enable(mode);
}

EXPORT_SYMBOL(agp_free_memory);
EXPORT_SYMBOL(agp_allocate_memory);
EXPORT_SYMBOL(agp_copy_info);
EXPORT_SYMBOL(agp_create_memory);
EXPORT_SYMBOL(agp_bind_memory);
EXPORT_SYMBOL(agp_unbind_memory);
EXPORT_SYMBOL(agp_free_key);
EXPORT_SYMBOL(agp_enable);
EXPORT_SYMBOL(agp_bridge);

EXPORT_SYMBOL(agp_generic_alloc_page);
EXPORT_SYMBOL(agp_generic_destroy_page);
EXPORT_SYMBOL(agp_generic_suspend);
EXPORT_SYMBOL(agp_generic_resume);
EXPORT_SYMBOL(agp_generic_enable);
EXPORT_SYMBOL(agp_generic_create_gatt_table);
EXPORT_SYMBOL(agp_generic_free_gatt_table);
EXPORT_SYMBOL(agp_generic_insert_memory);
EXPORT_SYMBOL(agp_generic_remove_memory);
EXPORT_SYMBOL(agp_generic_alloc_by_type);
EXPORT_SYMBOL(agp_generic_free_by_type);
EXPORT_SYMBOL(global_cache_flush);

EXPORT_SYMBOL(agp_device_command);
EXPORT_SYMBOL(agp_collect_device_status);

EXPORT_SYMBOL_GPL(agp_num_entries);

