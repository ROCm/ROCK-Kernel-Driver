/*
 * AGPGART module version 0.99
 * Copyright (C) 1999 Jeff Hartmann
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
#include "agp.h"

MODULE_AUTHOR("Jeff Hartmann <jhartmann@precisioninsight.com>");
MODULE_PARM(agp_try_unsupported, "1i");
MODULE_LICENSE("GPL and additional rights");
EXPORT_SYMBOL(agp_free_memory);
EXPORT_SYMBOL(agp_allocate_memory);
EXPORT_SYMBOL(agp_copy_info);
EXPORT_SYMBOL(agp_bind_memory);
EXPORT_SYMBOL(agp_unbind_memory);
EXPORT_SYMBOL(agp_enable);
EXPORT_SYMBOL(agp_backend_acquire);
EXPORT_SYMBOL(agp_backend_release);

struct agp_bridge_data agp_bridge = { type: NOT_SUPPORTED };
static int agp_try_unsupported __initdata = 0;

int agp_memory_reserved;
__u32 *agp_gatt_table; 

int agp_backend_acquire(void)
{
	if (agp_bridge.type == NOT_SUPPORTED)
		return -EINVAL;

	atomic_inc(&agp_bridge.agp_in_use);

	if (atomic_read(&agp_bridge.agp_in_use) != 1) {
		atomic_dec(&agp_bridge.agp_in_use);
		return -EBUSY;
	}
	MOD_INC_USE_COUNT;
	return 0;
}

void agp_backend_release(void)
{
	if (agp_bridge.type == NOT_SUPPORTED)
		return;

	atomic_dec(&agp_bridge.agp_in_use);
	MOD_DEC_USE_COUNT;
}

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
		clear_bit(key, agp_bridge.key_list);
}

static int agp_get_key(void)
{
	int bit;

	bit = find_first_zero_bit(agp_bridge.key_list, MAXKEY);
	if (bit < MAXKEY) {
		set_bit(bit, agp_bridge.key_list);
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
	int i;

	if ((agp_bridge.type == NOT_SUPPORTED) || (curr == NULL))
		return;

	if (curr->is_bound == TRUE)
		agp_unbind_memory(curr);

	if (curr->type != 0) {
		agp_bridge.free_by_type(curr);
		return;
	}
	if (curr->page_count != 0) {
		for (i = 0; i < curr->page_count; i++) {
			curr->memory[i] &= ~(0x00000fff);
			agp_bridge.agp_destroy_page(phys_to_virt(curr->memory[i]));
		}
	}
	agp_free_key(curr->key);
	vfree(curr->memory);
	kfree(curr);
	MOD_DEC_USE_COUNT;
}

#define ENTRIES_PER_PAGE		(PAGE_SIZE / sizeof(unsigned long))

agp_memory *agp_allocate_memory(size_t page_count, u32 type)
{
	int scratch_pages;
	agp_memory *new;
	int i;

	if (agp_bridge.type == NOT_SUPPORTED)
		return NULL;

	if ((atomic_read(&agp_bridge.current_memory_agp) + page_count) >
	    agp_bridge.max_memory_agp) {
		return NULL;
	}

	if (type != 0) {
		new = agp_bridge.alloc_by_type(page_count, type);
		return new;
	}
      	/* We always increase the module count, since free auto-decrements
	 * it
	 */

	MOD_INC_USE_COUNT;

	scratch_pages = (page_count + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;

	new = agp_create_memory(scratch_pages);

	if (new == NULL) {
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	for (i = 0; i < page_count; i++) {
		void *addr = agp_bridge.agp_alloc_page();

		if (addr == NULL) {
			/* Free this structure */
			agp_free_memory(new);
			return NULL;
		}
		new->memory[i] = agp_bridge.mask_memory(virt_to_phys(addr), type);
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

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
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

	return current_size;
}

/* Routine to copy over information structure */

int agp_copy_info(agp_kern_info * info)
{
	unsigned long page_mask = 0;
	int i;

	memset(info, 0, sizeof(agp_kern_info));
	if (agp_bridge.type == NOT_SUPPORTED) {
		info->chipset = agp_bridge.type;
		return -EIO;
	}
	info->version.major = agp_bridge.version->major;
	info->version.minor = agp_bridge.version->minor;
	info->device = agp_bridge.dev;
	info->chipset = agp_bridge.type;
	info->mode = agp_bridge.mode;
	info->aper_base = agp_bridge.gart_bus_addr;
	info->aper_size = agp_return_size();
	info->max_memory = agp_bridge.max_memory_agp;
	info->current_memory = atomic_read(&agp_bridge.current_memory_agp);
	info->cant_use_aperture = agp_bridge.cant_use_aperture;

	for(i = 0; i < agp_bridge.num_of_masks; i++)
		page_mask |= agp_bridge.mask_memory(page_mask, i);

	info->page_mask = ~page_mask;
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

	if ((agp_bridge.type == NOT_SUPPORTED) ||
	    (curr == NULL) || (curr->is_bound == TRUE)) {
		return -EINVAL;
	}
	if (curr->is_flushed == FALSE) {
		CACHE_FLUSH();
		curr->is_flushed = TRUE;
	}
	ret_val = agp_bridge.insert_memory(curr, pg_start, curr->type);

	if (ret_val != 0)
		return ret_val;

	curr->is_bound = TRUE;
	curr->pg_start = pg_start;
	return 0;
}

int agp_unbind_memory(agp_memory * curr)
{
	int ret_val;

	if ((agp_bridge.type == NOT_SUPPORTED) || (curr == NULL))
		return -EINVAL;

	if (curr->is_bound != TRUE)
		return -EINVAL;

	ret_val = agp_bridge.remove_memory(curr, curr->pg_start, curr->type);

	if (ret_val != 0)
		return ret_val;

	curr->is_bound = FALSE;
	curr->pg_start = 0;
	return 0;
}

/* End - Routines for handling swapping of agp_memory into the GATT */

/* 
 * Driver routines - start
 * Currently this module supports the following chipsets:
 * i810, i815, 440lx, 440bx, 440gx, i830, i840, i845, i850, i860, via vp3,
 * via mvp3, via kx133, via kt133, amd irongate, amd 761, amd 762, ALi M1541,
 * and generic support for the SiS chipsets.
 */

/* Generic Agp routines - Start */

void agp_generic_agp_enable(u32 mode)
{
	struct pci_dev *device = NULL;
	u32 command, scratch; 
	u8 cap_ptr;

	pci_read_config_dword(agp_bridge.dev, agp_bridge.capndx + 4, &command);

	/*
	 * PASS1: go throu all devices that claim to be
	 *        AGP devices and collect their data.
	 */


	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00) {
			/*
			 * Ok, here we have a AGP device. Disable impossible 
			 * settings, and adjust the readqueue to the minimum.
			 */

			pci_read_config_dword(device, cap_ptr + 4, &scratch);

			/* adjust RQ depth */
			command = ((command & ~0xff000000) |
			     min_t(u32, (mode & 0xff000000),
				 min_t(u32, (command & 0xff000000),
				     (scratch & 0xff000000))));

			/* disable SBA if it's not supported */
			if (!((command & 0x00000200) &&
			      (scratch & 0x00000200) &&
			      (mode & 0x00000200)))
				command &= ~0x00000200;

			/* disable FW if it's not supported */
			if (!((command & 0x00000010) &&
			      (scratch & 0x00000010) &&
			      (mode & 0x00000010)))
				command &= ~0x00000010;

			if (!((command & 4) &&
			      (scratch & 4) &&
			      (mode & 4)))
				command &= ~0x00000004;

			if (!((command & 2) &&
			      (scratch & 2) &&
			      (mode & 2)))
				command &= ~0x00000002;

			if (!((command & 1) &&
			      (scratch & 1) &&
			      (mode & 1)))
				command &= ~0x00000001;
		}
	}
	/*
	 * PASS2: Figure out the 4X/2X/1X setting and enable the
	 *        target (our motherboard chipset).
	 */

	if (command & 4)
		command &= ~3;	/* 4X */

	if (command & 2)
		command &= ~5;	/* 2X */

	if (command & 1)
		command &= ~6;	/* 1X */

	command |= 0x00000100;

	pci_write_config_dword(agp_bridge.dev,
			       agp_bridge.capndx + 8,
			       command);

	/*
	 * PASS3: Go throu all AGP devices and update the
	 *        command registers.
	 */

	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00)
			pci_write_config_dword(device, cap_ptr + 8, command);
	}
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
	if (agp_bridge.size_type == LVL2_APER_SIZE) {
		return -EINVAL;
	}

	table = NULL;
	i = agp_bridge.aperture_size_idx;
	temp = agp_bridge.current_size;
	size = page_order = num_entries = 0;

	if (agp_bridge.size_type != FIXED_APER_SIZE) {
		do {
			switch (agp_bridge.size_type) {
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
				switch (agp_bridge.size_type) {
				case U8_APER_SIZE:
					agp_bridge.current_size = A_IDX8();
					break;
				case U16_APER_SIZE:
					agp_bridge.current_size = A_IDX16();
					break;
				case U32_APER_SIZE:
					agp_bridge.current_size = A_IDX32();
					break;
					/* This case will never really 
					 * happen. 
					 */
				case FIXED_APER_SIZE:
				case LVL2_APER_SIZE:
				default:
					agp_bridge.current_size =
					    agp_bridge.current_size;
					break;
				}
				temp = agp_bridge.current_size;	
			} else {
				agp_bridge.aperture_size_idx = i;
			}
		} while ((table == NULL) &&
			 (i < agp_bridge.num_aperture_sizes));
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

	agp_bridge.gatt_table_real = (unsigned long *) table;
	agp_gatt_table = (void *)table; 
	CACHE_FLUSH();
	agp_bridge.gatt_table = ioremap_nocache(virt_to_phys(table),
					(PAGE_SIZE * (1 << page_order)));
	CACHE_FLUSH();

	if (agp_bridge.gatt_table == NULL) {
		for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
			ClearPageReserved(page);

		free_pages((unsigned long) table, page_order);

		return -ENOMEM;
	}
	agp_bridge.gatt_bus_addr = virt_to_phys(agp_bridge.gatt_table_real);

	for (i = 0; i < num_entries; i++)
		agp_bridge.gatt_table[i] = (unsigned long) agp_bridge.scratch_page;

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

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
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

	iounmap(agp_bridge.gatt_table);
	table = (char *) agp_bridge.gatt_table_real;
	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
		ClearPageReserved(page);

	free_pages((unsigned long) agp_bridge.gatt_table_real, page_order);
	return 0;
}

int agp_generic_insert_memory(agp_memory * mem, off_t pg_start, int type)
{
	int i, j, num_entries;
	void *temp;

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
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

	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;

	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[j])) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++)
		agp_bridge.gatt_table[j] = mem->memory[i];

	agp_bridge.tlb_flush(mem);
	return 0;
}

int agp_generic_remove_memory(agp_memory * mem, off_t pg_start, int type)
{
	int i;

	if (type != 0 || mem->type != 0) {
		/* The generic routines know nothing of memory types */
		return -EINVAL;
	}
	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		agp_bridge.gatt_table[i] =
		    (unsigned long) agp_bridge.scratch_page;
	}

	agp_bridge.tlb_flush(mem);
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
	atomic_inc(&agp_bridge.current_memory_agp);
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
	atomic_dec(&agp_bridge.current_memory_agp);
}

/* End Basic Page Allocation Routines */

void agp_enable(u32 mode)
{
	if (agp_bridge.type == NOT_SUPPORTED)
		return;
	agp_bridge.agp_enable(mode);
}

/* End - Generic Agp routines */


/* per-chipset initialization data.
 * note -- all chipsets for a single vendor MUST be grouped together
 */
static struct {
	unsigned short device_id; /* first, to make table easier to read */
	unsigned short vendor_id;
	enum chipset_type chipset;
	const char *vendor_name;
	const char *chipset_name;
	int (*chipset_setup) (struct pci_dev *pdev);
} agp_bridge_info[] __initdata = {

#ifdef CONFIG_AGP_ALI
	{
		.device_id	= PCI_DEVICE_ID_AL_M1541,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1541,
		.vendor_name	= "Ali",
		.chipset_name	= "M1541",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1621,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1621,
		.vendor_name	= "Ali",
		.chipset_name	= "M1621",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1631,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1631,
		.vendor_name	= "Ali",
		.chipset_name	= "M1631",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1632,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1632,
		.vendor_name	= "Ali",
		.chipset_name	= "M1632",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1641,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1641,
		.vendor_name	= "Ali",
		.chipset_name	= "M1641",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1644,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1644,
		.vendor_name	= "Ali",
		.chipset_name	= "M1644",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1647,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1647,
		.vendor_name	= "Ali",
		.chipset_name	= "M1647",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1651,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1651,
		.vendor_name	= "Ali",
		.chipset_name	= "M1651",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1671,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_M1671,
		.vendor_name	= "Ali",
		.chipset_name	= "M1671",
		.chipset_setup	= ali_generic_setup,
	},
	{
		.device_id	= 0,
		.vendor_id	= PCI_VENDOR_ID_AL,
		.chipset	= ALI_GENERIC,
		.vendor_name	= "Ali",
		.chipset_name	= "Generic",
		.chipset_setup	= ali_generic_setup,
	},
#endif /* CONFIG_AGP_ALI */

#ifdef CONFIG_AGP_AMD_8151
	{ 
		.device_id	= PCI_DEVICE_ID_AMD_8151_0,
		.vendor_id  = PCI_VENDOR_ID_AMD,
		.chipset    = AMD_8151,
		.vendor_name = "AMD",
		.chipset_name = "8151",
		.chipset_setup = amd_8151_setup 
	},
#endif /* CONFIG_AGP_AMD */

#ifdef CONFIG_AGP_AMD
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_7006,
		.vendor_id	= PCI_VENDOR_ID_AMD,
		.chipset	= AMD_IRONGATE,
		.vendor_name	= "AMD",
		.chipset_name	= "Irongate",
		.chipset_setup	= amd_irongate_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_700E,
		.vendor_id	= PCI_VENDOR_ID_AMD,
		.chipset	= AMD_761,
		.vendor_name	= "AMD",
		.chipset_name	= "761",
		.chipset_setup	= amd_irongate_setup,
	},
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_700C,
		.vendor_id	= PCI_VENDOR_ID_AMD,
		.chipset	= AMD_762,
		.vendor_name	= "AMD",
		.chipset_name	= "760MP",
		.chipset_setup	= amd_irongate_setup,
	},
	{
		.device_id	= 0,
		.vendor_id	= PCI_VENDOR_ID_AMD,
		.chipset	= AMD_GENERIC,
		.vendor_name	= "AMD",
		.chipset_name	= "Generic",
		.chipset_setup	= amd_irongate_setup,
	},
#endif /* CONFIG_AGP_AMD */
#ifdef CONFIG_AGP_INTEL
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82443LX_0,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_LX,
		.vendor_name	= "Intel",
		.chipset_name	= "440LX",
		.chipset_setup	= intel_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82443BX_0,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_BX,
		.vendor_name	= "Intel",
		.chipset_name	= "440BX",
		.chipset_setup	= intel_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82443GX_0,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_GX,
		.vendor_name	= "Intel",
		.chipset_name	= "440GX",
		.chipset_setup	= intel_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82815_MC,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I815,
		.vendor_name	= "Intel",
		.chipset_name	= "i815",
		.chipset_setup	= intel_815_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82820_HB,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I820,
		.vendor_name	= "Intel",
		.chipset_name	= "i820",
		.chipset_setup	= intel_820_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82820_UP_HB,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I820,
		.vendor_name	= "Intel",
		.chipset_name	= "i820",
		.chipset_setup	= intel_820_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82830_HB,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I830_M,
		.vendor_name	= "Intel",
		.chipset_name	= "i830M",
		.chipset_setup	= intel_830mp_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82845G_HB,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I845_G,
		.vendor_name	= "Intel",
		.chipset_name	= "i845G",
		.chipset_setup	= intel_830mp_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82840_HB,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I840,
		.vendor_name	= "Intel",
		.chipset_name	= "i840",
		.chipset_setup	= intel_840_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82845_HB,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I845,
		.vendor_name	= "Intel",
		.chipset_name	= "i845",
		.chipset_setup	= intel_845_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82850_HB,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I850,
		.vendor_name	= "Intel",
		.chipset_name	= "i850",
		.chipset_setup	= intel_850_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82860_HB,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_I860,
		.vendor_name	= "Intel",
		.chipset_name	= "i860",
		.chipset_setup	= intel_860_setup
	},
	{
		.device_id	= 0,
		.vendor_id	= PCI_VENDOR_ID_INTEL,
		.chipset	= INTEL_GENERIC,
		.vendor_name	= "Intel",
		.chipset_name	= "Generic",
		.chipset_setup	= intel_generic_setup
	},

#endif /* CONFIG_AGP_INTEL */

#ifdef CONFIG_AGP_SIS
	{
		.device_id	= PCI_DEVICE_ID_SI_740,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "740",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_650,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "650",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_645,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "645",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_735,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "735",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_745,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "745",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_730,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "730",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_630,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "630",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_540,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "540",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_620,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "620",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_530,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "530",
		.chipset_setup	= sis_generic_setup
	},
        {
		.device_id	= PCI_DEVICE_ID_SI_550,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "550",
		.chipset_setup	= sis_generic_setup
	},
	{
		.device_id	= 0,
		.vendor_id	= PCI_VENDOR_ID_SI,
		.chipset	= SIS_GENERIC,
		.vendor_name	= "SiS",
		.chipset_name	= "Generic",
		.chipset_setup	= sis_generic_setup
	},
#endif /* CONFIG_AGP_SIS */

#ifdef CONFIG_AGP_VIA
	{
		.device_id	= PCI_DEVICE_ID_VIA_8501_0,
		.vendor_id	= PCI_VENDOR_ID_VIA,
		.chipset	= VIA_MVP4,
		.vendor_name	= "Via",
		.chipset_name	= "MVP4",
		.chipset_setup	= via_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C597_0,
		.vendor_id	= PCI_VENDOR_ID_VIA,
		.chipset	= VIA_VP3,
		.vendor_name	= "Via",
		.chipset_name	= "VP3",
		.chipset_setup	= via_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C598_0,
		.vendor_id	= PCI_VENDOR_ID_VIA,
		.chipset	= VIA_MVP3,
		.vendor_name	= "Via",
		.chipset_name	= "MVP3",
		.chipset_setup	= via_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C691,
		.vendor_id	= PCI_VENDOR_ID_VIA,
		.chipset	= VIA_APOLLO_PRO,
		.vendor_name	= "Via",
		.chipset_name	= "Apollo Pro",
		.chipset_setup	= via_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8371_0,
		.vendor_id	= PCI_VENDOR_ID_VIA,
		.chipset	= VIA_APOLLO_KX133,
		.vendor_name	= "Via",
		.chipset_name	= "Apollo Pro KX133",
		.chipset_setup	= via_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8363_0,
		.vendor_id	= PCI_VENDOR_ID_VIA,
		.chipset	= VIA_APOLLO_KT133,
		.vendor_name	= "Via",
		.chipset_name	= "Apollo Pro KT133",
		.chipset_setup	= via_generic_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8367_0,
		.vendor_id	= PCI_VENDOR_ID_VIA,
		.chipset	= VIA_APOLLO_KT133,
		.vendor_name	= "Via",
		.chipset_name	= "Apollo Pro KT266",
		.chipset_setup	= via_generic_setup
	},
	{
		.device_id	= 0,
		.vendor_id	= PCI_VENDOR_ID_VIA,
		.chipset	= VIA_GENERIC,
		.vendor_name	= "Via",
		.chipset_name	= "Generic",
		.chipset_setup	= via_generic_setup
	},
#endif /* CONFIG_AGP_VIA */

#ifdef CONFIG_AGP_HP_ZX1
	{
		.device_id	= PCI_DEVICE_ID_HP_ZX1_LBA,
		.vendor_id	= PCI_VENDOR_ID_HP,
		.chipset	= HP_ZX1,
		.vendor_name	= "HP",
		.chipset_name	= "ZX1",
		.chipset_setup	= hp_zx1_setup
	},
#endif

	{ }, /* dummy final entry, always present */
};


/* scan table above for supported devices */
static int __init agp_lookup_host_bridge (struct pci_dev *pdev)
{
	int i;
	
	for (i = 0; i < ARRAY_SIZE (agp_bridge_info); i++)
		if (pdev->vendor == agp_bridge_info[i].vendor_id)
			break;

	if (i >= ARRAY_SIZE (agp_bridge_info)) {
		printk (KERN_DEBUG PFX "unsupported bridge\n");
		return -ENODEV;
	}

	while ((i < ARRAY_SIZE (agp_bridge_info)) &&
	       (agp_bridge_info[i].vendor_id == pdev->vendor)) {
		if (pdev->device == agp_bridge_info[i].device_id) {
#ifdef CONFIG_AGP_ALI
			if (pdev->device == PCI_DEVICE_ID_AL_M1621) {
				u8 hidden_1621_id;

				pci_read_config_byte(pdev, 0xFB, &hidden_1621_id);
				switch (hidden_1621_id) {
				case 0x31:
					agp_bridge_info[i].chipset_name="M1631";
					break;
				case 0x32:
					agp_bridge_info[i].chipset_name="M1632";
					break;
				case 0x41:
					agp_bridge_info[i].chipset_name="M1641";
					break;
				case 0x43:
					break;
				case 0x47:
					agp_bridge_info[i].chipset_name="M1647";
					break;
				case 0x51:
					agp_bridge_info[i].chipset_name="M1651";
					break;
				default:
					break;
				}
			}
#endif

			printk (KERN_INFO PFX "Detected %s %s chipset\n",
				agp_bridge_info[i].vendor_name,
				agp_bridge_info[i].chipset_name);
			agp_bridge.type = agp_bridge_info[i].chipset;
			return agp_bridge_info[i].chipset_setup (pdev);
		}
		
		i++;
	}

	i--; /* point to vendor generic entry (device_id == 0) */

	/* try init anyway, if user requests it AND
	 * there is a 'generic' bridge entry for this vendor */
	if (agp_try_unsupported && agp_bridge_info[i].device_id == 0) {
		printk(KERN_WARNING PFX "Trying generic %s routines"
		       " for device id: %04x\n",
		       agp_bridge_info[i].vendor_name, pdev->device);
		agp_bridge.type = agp_bridge_info[i].chipset;
		return agp_bridge_info[i].chipset_setup (pdev);
	}

	printk(KERN_ERR PFX "Unsupported %s chipset (device id: %04x),"
	       " you might want to try agp_try_unsupported=1.\n",
	       agp_bridge_info[i].vendor_name, pdev->device);
	return -ENODEV;
}


/* Supported Device Scanning routine */

static int __init agp_find_supported_device(struct pci_dev *dev)
{
	u8 cap_ptr = 0x00;

	agp_bridge.dev = dev;

	/* Need to test for I810 here */
#ifdef CONFIG_AGP_I810
	if (dev->vendor == PCI_VENDOR_ID_INTEL) {
		struct pci_dev *i810_dev;

		switch (dev->device) {
		case PCI_DEVICE_ID_INTEL_82810_MC1:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					       PCI_DEVICE_ID_INTEL_82810_IG1,
						   NULL);
			if (i810_dev == NULL) {
				printk(KERN_ERR PFX "Detected an Intel i810,"
				       " but could not find the secondary"
				       " device.\n");
				return -ENODEV;
			}
			printk(KERN_INFO PFX "Detected an Intel "
			       "i810 Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i810_setup (i810_dev);

		case PCI_DEVICE_ID_INTEL_82810_MC3:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					 PCI_DEVICE_ID_INTEL_82810_IG3,
						   NULL);
			if (i810_dev == NULL) {
				printk(KERN_ERR PFX "Detected an Intel i810 "
				       "DC100, but could not find the "
				       "secondary device.\n");
				return -ENODEV;
			}
			printk(KERN_INFO PFX "Detected an Intel i810 "
			       "DC100 Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i810_setup(i810_dev);

		case PCI_DEVICE_ID_INTEL_82810E_MC:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					     PCI_DEVICE_ID_INTEL_82810E_IG,
						   NULL);
			if (i810_dev == NULL) {
				printk(KERN_ERR PFX "Detected an Intel i810 E"
				    ", but could not find the secondary "
				       "device.\n");
				return -ENODEV;
			}
			printk(KERN_INFO PFX "Detected an Intel i810 E "
			       "Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i810_setup(i810_dev);

		 case PCI_DEVICE_ID_INTEL_82815_MC:
		   /* The i815 can operate either as an i810 style
		    * integrated device, or as an AGP4X motherboard.
		    *
		    * This only addresses the first mode:
		    */
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
						PCI_DEVICE_ID_INTEL_82815_CGC,
						   NULL);
			if (i810_dev == NULL) {
				printk(KERN_ERR PFX "agpgart: Detected an "
					"Intel i815, but could not find the"
					" secondary device. Assuming a "
					"non-integrated video card.\n");
				break;
			}
			printk(KERN_INFO PFX "agpgart: Detected an Intel i815 "
				"Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i810_setup(i810_dev);

		case PCI_DEVICE_ID_INTEL_82845G_HB:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_82845G_IG, NULL);
			if(i810_dev && PCI_FUNC(i810_dev->devfn) != 0) {
				i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_82845G_IG, i810_dev);
			}

			if (i810_dev == NULL) {
				/* 
				 * We probably have a I845MP chipset
				 * with an external graphics
				 * card. It will be initialized later 
				 */
				agp_bridge.type = INTEL_I845_G;
				break;
			}
			printk(KERN_INFO PFX "Detected an Intel "
				   "845G Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i830_setup(i810_dev);
		   
		case PCI_DEVICE_ID_INTEL_82830_HB:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
						   PCI_DEVICE_ID_INTEL_82830_CGC,
						   NULL);
			if(i810_dev && PCI_FUNC(i810_dev->devfn) != 0) {
				i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
							   PCI_DEVICE_ID_INTEL_82830_CGC,
							   i810_dev);
			}

			if (i810_dev == NULL) {
				/* Intel 830MP with external graphic card */
				/* It will be initialized later */
				agp_bridge.type = INTEL_I830_M;
				break;
			}
			printk(KERN_INFO PFX "Detected an Intel "
				   "830M Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i830_setup(i810_dev);
		default:
			break;
		}
	}
#endif /* CONFIG_AGP_I810 */

#ifdef CONFIG_AGP_SWORKS
	/* Everything is on func 1 here so we are hardcoding function one */
	if (dev->vendor == PCI_VENDOR_ID_SERVERWORKS) {
		struct pci_dev *bridge_dev;

		bridge_dev = pci_find_slot ((unsigned int)dev->bus->number, 
					    PCI_DEVFN(0, 1));
		if(bridge_dev == NULL) {
			printk(KERN_INFO PFX "agpgart: Detected a Serverworks "
			       "Chipset, but could not find the secondary "
			       "device.\n");
			return -ENODEV;
		}

		switch (dev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_HE:
			agp_bridge.type = SVWRKS_HE;
			return serverworks_setup(bridge_dev);

		case PCI_DEVICE_ID_SERVERWORKS_LE:
		case 0x0007:
			agp_bridge.type = SVWRKS_LE;
			return serverworks_setup(bridge_dev);

		default:
			if(agp_try_unsupported) {
				agp_bridge.type = SVWRKS_GENERIC;
				return serverworks_setup(bridge_dev);
			}
			break;
		}
	}

#endif /* CONFIG_AGP_SWORKS */

#ifdef CONFIG_AGP_HP_ZX1
	if (dev->vendor == PCI_VENDOR_ID_HP) {
		/* ZX1 LBAs can be either PCI or AGP bridges */
		if (pci_find_capability(dev, PCI_CAP_ID_AGP)) {
			printk(KERN_INFO PFX "Detected HP ZX1 AGP "
			       "chipset at %s\n", dev->slot_name);
			agp_bridge.type = HP_ZX1;
			agp_bridge.dev = dev;
			return hp_zx1_setup(dev);
		}
		return -ENODEV;
	}
#endif /* CONFIG_AGP_HP_ZX1 */

	/* find capndx */
	cap_ptr = pci_find_capability(dev, PCI_CAP_ID_AGP);
	if (cap_ptr == 0x00)
		return -ENODEV;
	agp_bridge.capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(agp_bridge.dev,
			      agp_bridge.capndx + 4,
			      &agp_bridge.mode);

	/* probe for known chipsets */
	return agp_lookup_host_bridge (dev);
}

struct agp_max_table {
	int mem;
	int agp;
};

static struct agp_max_table maxes_table[9] __initdata =
{
	{0, 0},
	{32, 4},
	{64, 28},
	{128, 96},
	{256, 204},
	{512, 440},
	{1024, 942},
	{2048, 1920},
	{4096, 3932}
};

static int __init agp_find_max (void)
{
	long memory, index, result;

	memory = virt_to_phys(high_memory) >> 20;
	index = 1;

	while ((memory > maxes_table[index].mem) &&
	       (index < 8)) {
		index++;
	}

	result = maxes_table[index - 1].agp +
	   ( (memory - maxes_table[index - 1].mem)  *
	     (maxes_table[index].agp - maxes_table[index - 1].agp)) /
	   (maxes_table[index].mem - maxes_table[index - 1].mem);

	printk(KERN_INFO PFX "Maximum main memory to use "
	       "for agp memory: %ldM\n", result);
	result = result << (20 - PAGE_SHIFT);
        return result;
}

#define AGPGART_VERSION_MAJOR 0
#define AGPGART_VERSION_MINOR 99

static struct agp_version agp_current_version =
{
	.major	= AGPGART_VERSION_MAJOR,
	.minor	= AGPGART_VERSION_MINOR,
};

static int __init agp_backend_initialize(struct pci_dev *dev)
{
	int size_value, rc, got_gatt=0, got_keylist=0;

	memset(&agp_bridge, 0, sizeof(struct agp_bridge_data));
	agp_bridge.type = NOT_SUPPORTED;
	agp_bridge.max_memory_agp = agp_find_max();
	agp_bridge.version = &agp_current_version;

	rc = agp_find_supported_device(dev);
	if (rc) {
		/* not KERN_ERR because error msg should have already printed */
		printk(KERN_DEBUG PFX "no supported devices found.\n");
		return rc;
	}

	if (agp_bridge.needs_scratch_page == TRUE) {
		void *addr;
		addr = agp_bridge.agp_alloc_page();

		if (addr == NULL) {
			printk(KERN_ERR PFX "unable to get memory for "
			       "scratch page.\n");
			return -ENOMEM;
		}
		agp_bridge.scratch_page = virt_to_phys(addr);
		agp_bridge.scratch_page =
		    agp_bridge.mask_memory(agp_bridge.scratch_page, 0);
	}

	size_value = agp_bridge.fetch_size();

	if (size_value == 0) {
		printk(KERN_ERR PFX "unable to determine aperture size.\n");
		rc = -EINVAL;
		goto err_out;
	}
	if (agp_bridge.create_gatt_table()) {
		printk(KERN_ERR PFX "unable to get memory for graphics "
		       "translation table.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_gatt = 1;
	
	agp_bridge.key_list = vmalloc(PAGE_SIZE * 4);
	if (agp_bridge.key_list == NULL) {
		printk(KERN_ERR PFX "error allocating memory for key lists.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_keylist = 1;
	
	/* FIXME vmalloc'd memory not guaranteed contiguous */
	memset(agp_bridge.key_list, 0, PAGE_SIZE * 4);

	if (agp_bridge.configure()) {
		printk(KERN_ERR PFX "error configuring host chipset.\n");
		rc = -EINVAL;
		goto err_out;
	}

	printk(KERN_INFO PFX "AGP aperture is %dM @ 0x%lx\n",
	       size_value, agp_bridge.gart_bus_addr);

	return 0;

err_out:
	if (agp_bridge.needs_scratch_page == TRUE) {
		agp_bridge.scratch_page &= ~(0x00000fff);
		agp_bridge.agp_destroy_page(phys_to_virt(agp_bridge.scratch_page));
	}
	if (got_gatt)
		agp_bridge.free_gatt_table();
	if (got_keylist)
		vfree(agp_bridge.key_list);
	return rc;
}


/* cannot be __exit b/c as it could be called from __init code */
static void agp_backend_cleanup(void)
{
	agp_bridge.cleanup();
	agp_bridge.free_gatt_table();
	vfree(agp_bridge.key_list);

	if (agp_bridge.needs_scratch_page == TRUE) {
		agp_bridge.scratch_page &= ~(0x00000fff);
		agp_bridge.agp_destroy_page(phys_to_virt(agp_bridge.scratch_page));
	}
}

static int agp_power(struct pm_dev *dev, pm_request_t rq, void *data)
{
	switch(rq)
	{
		case PM_SUSPEND:
			return agp_bridge.suspend();
		case PM_RESUME:
			agp_bridge.resume();
			return 0;
	}		
	return 0;
}

extern int agp_frontend_initialize(void);
extern void agp_frontend_cleanup(void);

static const drm_agp_t drm_agp = {
	&agp_free_memory,
	&agp_allocate_memory,
	&agp_bind_memory,
	&agp_unbind_memory,
	&agp_enable,
	&agp_backend_acquire,
	&agp_backend_release,
	&agp_copy_info
};

static int agp_probe (struct pci_dev *dev, const struct pci_device_id *ent)
{
	int ret_val;

	if (agp_bridge.type != NOT_SUPPORTED) {
		printk (KERN_DEBUG PFX "Oops, don't init more than one agpgart device.\n");
		return -ENODEV;
	}

	ret_val = agp_backend_initialize(dev);
	if (ret_val) {
		agp_bridge.type = NOT_SUPPORTED;
		return ret_val;
	}
	ret_val = agp_frontend_initialize();
	if (ret_val) {
		agp_bridge.type = NOT_SUPPORTED;
		agp_backend_cleanup();
		return ret_val;
	}

	inter_module_register("drm_agp", THIS_MODULE, &drm_agp);
	
	pm_register(PM_PCI_DEV, PM_PCI_ID(agp_bridge.dev), agp_power);
	return 0;
}

static struct pci_device_id agp_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_ANY_ID,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_pci_table);

static struct pci_driver agp_pci_driver = {
	.name		= "agpgart",
	.id_table	= agp_pci_table,
	.probe		= agp_probe,
};

int __init agp_init(void)
{
	int ret_val;

	printk(KERN_INFO "Linux agpgart interface v%d.%d (c) Jeff Hartmann\n",
	       AGPGART_VERSION_MAJOR, AGPGART_VERSION_MINOR);

	ret_val = pci_module_init(&agp_pci_driver);
	if (ret_val) {
		agp_bridge.type = NOT_SUPPORTED;
		return ret_val;
	}
	return 0;
}

static void __exit agp_cleanup(void)
{
	pci_unregister_driver(&agp_pci_driver);
	if (agp_bridge.type != NOT_SUPPORTED) {
		pm_unregister_all(agp_power);
		agp_frontend_cleanup();
		agp_backend_cleanup();
		inter_module_unregister("drm_agp");
	}
}

#ifndef CONFIG_GART_IOMMU
module_init(agp_init);
module_exit(agp_cleanup);
#endif
