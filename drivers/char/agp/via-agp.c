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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"


static int via_fetch_size(void)
{
	int i;
	u8 temp;
	struct aper_size_info_8 *values;

	values = A_SIZE_8(agp_bridge.aperture_sizes);
	pci_read_config_byte(agp_bridge.dev, VIA_APSIZE, &temp);
	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int via_configure(void)
{
	u32 temp;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);
	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, VIA_APSIZE,
			      current_size->size_value);
	/* address to map too */
	pci_read_config_dword(agp_bridge.dev, VIA_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* GART control register */
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000000f);

	/* attbase - aperture GATT base */
	pci_write_config_dword(agp_bridge.dev, VIA_ATTBASE,
			    (agp_bridge.gatt_bus_addr & 0xfffff000) | 3);
	return 0;
}

static void via_cleanup(void)
{
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_write_config_byte(agp_bridge.dev, VIA_APSIZE,
			      previous_size->size_value);
	/* Do not disable by writing 0 to VIA_ATTBASE, it screws things up
	 * during reinitialization.
	 */
}

static void via_tlbflush(agp_memory * mem)
{
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000008f);
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000000f);
}

static unsigned long via_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static struct aper_size_info_8 via_generic_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 128},
	{64, 16384, 4, 192},
	{32, 8192, 3, 224},
	{16, 4096, 2, 240},
	{8, 2048, 1, 248},
	{4, 1024, 0, 252}
};

static struct gatt_mask via_generic_masks[] =
{
	{mask: 0x00000000, type: 0}
};

int __init via_generic_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = via_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) via_generic_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = via_configure;
	agp_bridge.fetch_size = via_fetch_size;
	agp_bridge.cleanup = via_cleanup;
	agp_bridge.tlb_flush = via_tlbflush;
	agp_bridge.mask_memory = via_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}
