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

static int ali_fetch_size(void)
{
	int i;
	u32 temp;
	struct aper_size_info_32 *values;

	pci_read_config_dword(agp_bridge.dev, ALI_ATTBASE, &temp);
	temp &= ~(0xfffffff0);
	values = A_SIZE_32(agp_bridge.aperture_sizes);

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

static void ali_tlbflush(agp_memory * mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
// clear tag
	pci_write_config_dword(agp_bridge.dev, ALI_TAGCTRL,
			((temp & 0xfffffff0) | 0x00000001|0x00000002));
}

static void ali_cleanup(void)
{
	struct aper_size_info_32 *previous_size;
	u32 temp;

	previous_size = A_SIZE_32(agp_bridge.previous_size);

	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
// clear tag
	pci_write_config_dword(agp_bridge.dev, ALI_TAGCTRL,
			((temp & 0xffffff00) | 0x00000001|0x00000002));

	pci_read_config_dword(agp_bridge.dev,  ALI_ATTBASE, &temp);
	pci_write_config_dword(agp_bridge.dev, ALI_ATTBASE,
			((temp & 0x00000ff0) | previous_size->size_value));
}

static int ali_configure(void)
{
	u32 temp;
	struct aper_size_info_32 *current_size;

	current_size = A_SIZE_32(agp_bridge.current_size);

	/* aperture size and gatt addr */
	pci_read_config_dword(agp_bridge.dev, ALI_ATTBASE, &temp);
	temp = (((temp & 0x00000ff0) | (agp_bridge.gatt_bus_addr & 0xfffff000))
			| (current_size->size_value & 0xf));
	pci_write_config_dword(agp_bridge.dev, ALI_ATTBASE, temp);

	/* tlb control */

	/*
	 *	Question: Jeff, ALi's patch deletes this:
	 *
	 *	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
	 *	pci_write_config_dword(agp_bridge.dev, ALI_TLBCTRL,
	 *			       ((temp & 0xffffff00) | 0x00000010));
	 *
	 *	and replaces it with the following, which seems to duplicate the
	 *	next couple of lines below it. I suspect this was an oversight,
	 *	but you might want to check up on this?
	 */
	
	pci_read_config_dword(agp_bridge.dev, ALI_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, ALI_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

#if 0
	if (agp_bridge.type == ALI_M1541) {
		u32 nlvm_addr = 0;

		switch (current_size->size_value) {
			case 0:  break;
			case 1:  nlvm_addr = 0x100000;break;
			case 2:  nlvm_addr = 0x200000;break;
			case 3:  nlvm_addr = 0x400000;break;
			case 4:  nlvm_addr = 0x800000;break;
			case 6:  nlvm_addr = 0x1000000;break;
			case 7:  nlvm_addr = 0x2000000;break;
			case 8:  nlvm_addr = 0x4000000;break;
			case 9:  nlvm_addr = 0x8000000;break;
			case 10: nlvm_addr = 0x10000000;break;
			default: break;
		}
		nlvm_addr--;
		nlvm_addr&=0xfff00000;

		nlvm_addr+= agp_bridge.gart_bus_addr;
		nlvm_addr|=(agp_bridge.gart_bus_addr>>12);
		printk(KERN_INFO PFX "nlvm top &base = %8x\n",nlvm_addr);
	}
#endif

	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
	temp &= 0xffffff7f;		//enable TLB
	pci_write_config_dword(agp_bridge.dev, ALI_TLBCTRL, temp);

	return 0;
}

static unsigned long ali_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static void ali_cache_flush(void)
{
	global_cache_flush();

	if (agp_bridge.type == ALI_M1541) {
		int i, page_count;
		u32 temp;

		page_count = 1 << A_SIZE_32(agp_bridge.current_size)->page_order;
		for (i = 0; i < PAGE_SIZE * page_count; i += PAGE_SIZE) {
			pci_read_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL, &temp);
			pci_write_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL,
					(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
					  (agp_bridge.gatt_bus_addr + i)) |
					    ALI_CACHE_FLUSH_EN));
		}
	}
}

static void *ali_alloc_page(void)
{
	void *adr = agp_generic_alloc_page();
	u32 temp;

	if (adr == 0)
		return 0;

	if (agp_bridge.type == ALI_M1541) {
		pci_read_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL, &temp);
		pci_write_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL,
				(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
				  virt_to_phys(adr)) |
				    ALI_CACHE_FLUSH_EN ));
	}
	return adr;
}

static void ali_destroy_page(void * addr)
{
	u32 temp;

	if (addr == NULL)
		return;

	global_cache_flush();

	if (agp_bridge.type == ALI_M1541) {
		pci_read_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL, &temp);
		pci_write_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL,
				(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
				  virt_to_phys(addr)) |
				    ALI_CACHE_FLUSH_EN));
	}

	agp_generic_destroy_page(addr);
}

/* Setup function */
static struct gatt_mask ali_generic_masks[] =
{
	{mask: 0x00000000, type: 0}
};

static struct aper_size_info_32 ali_generic_sizes[7] =
{
	{256, 65536, 6, 10},
	{128, 32768, 5, 9},
	{64, 16384, 4, 8},
	{32, 8192, 3, 7},
	{16, 4096, 2, 6},
	{8, 2048, 1, 4},
	{4, 1024, 0, 3}
};

int __init ali_generic_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = ali_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) ali_generic_sizes;
	agp_bridge.size_type = U32_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = ali_configure;
	agp_bridge.fetch_size = ali_fetch_size;
	agp_bridge.cleanup = ali_cleanup;
	agp_bridge.tlb_flush = ali_tlbflush;
	agp_bridge.mask_memory = ali_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = ali_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = ali_alloc_page;
	agp_bridge.agp_destroy_page = ali_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

