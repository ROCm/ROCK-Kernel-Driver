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

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"


static int intel_fetch_size(void)
{
	int i;
	u16 temp;
	struct aper_size_info_16 *values;

	pci_read_config_word(agp_bridge.dev, INTEL_APSIZE, &temp);
	values = A_SIZE_16(agp_bridge.aperture_sizes);

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

static int intel_8xx_fetch_size(void)
{
	int i;
	u8 temp;
	struct aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge.dev, INTEL_APSIZE, &temp);

	/* Intel 815 chipsets have a _weird_ APSIZE register with only
	 * one non-reserved bit, so mask the others out ... */
	if (agp_bridge.type == INTEL_I815)
		temp &= (1 << 3);

	values = A_SIZE_8(agp_bridge.aperture_sizes);

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


static void intel_tlbflush(agp_memory * mem)
{
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2200);
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2280);
}


static void intel_8xx_tlbflush(agp_memory * mem)
{
  u32 temp;
  pci_read_config_dword(agp_bridge.dev, INTEL_AGPCTRL, &temp);
  pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, temp & ~(1 << 7));
  pci_read_config_dword(agp_bridge.dev, INTEL_AGPCTRL, &temp);
  pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, temp | (1 << 7));
}


static void intel_cleanup(void)
{
	u16 temp;
	struct aper_size_info_16 *previous_size;

	previous_size = A_SIZE_16(agp_bridge.previous_size);
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG, temp & ~(1 << 9));
	pci_write_config_word(agp_bridge.dev, INTEL_APSIZE,
			      previous_size->size_value);
}


static void intel_8xx_cleanup(void)
{
	u16 temp;
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG, temp & ~(1 << 9));
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      previous_size->size_value);
}


static int intel_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_16 *current_size;

	current_size = A_SIZE_16(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_word(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2280);

	/* paccfg/nbxcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG,
			      (temp2 & ~(1 << 10)) | (1 << 9));
	/* clear any possible error conditions */
	pci_write_config_byte(agp_bridge.dev, INTEL_ERRSTS + 1, 7);
	return 0;
}

static int intel_815_configure(void)
{
	u32 temp, addr;
	u8 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	/* the Intel 815 chipset spec. says that bits 29-31 in the
	* ATTBASE register are reserved -> try not to write them */
	if (agp_bridge.gatt_bus_addr &  INTEL_815_ATTBASE_MASK)
		panic("gatt bus addr too high");
	pci_read_config_dword(agp_bridge.dev, INTEL_ATTBASE, &addr);
	addr &= INTEL_815_ATTBASE_MASK;
	addr |= agp_bridge.gatt_bus_addr;
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE, addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* apcont */
	pci_read_config_byte(agp_bridge.dev, INTEL_815_APCONT, &temp2);
	pci_write_config_byte(agp_bridge.dev, INTEL_815_APCONT, temp2 | (1 << 1));

	/* clear any possible error conditions */
	/* Oddness : this chipset seems to have no ERRSTS register ! */
	return 0;
}

static void intel_820_tlbflush(agp_memory * mem)
{
  return;
}

static void intel_820_cleanup(void)
{
	u8 temp;
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_read_config_byte(agp_bridge.dev, INTEL_I820_RDCR, &temp);
	pci_write_config_byte(agp_bridge.dev, INTEL_I820_RDCR, 
			      temp & ~(1 << 1));
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      previous_size->size_value);
}


static int intel_820_configure(void)
{
	u32 temp;
 	u8 temp2; 
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr); 

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* global enable aperture access */
	/* This flag is not accessed through MCHCFG register as in */
	/* i850 chipset. */
	pci_read_config_byte(agp_bridge.dev, INTEL_I820_RDCR, &temp2);
	pci_write_config_byte(agp_bridge.dev, INTEL_I820_RDCR, 
			      temp2 | (1 << 1));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I820_ERRSTS, 0x001c); 
	return 0;
}

static int intel_840_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr); 

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* mcgcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_I840_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_I840_MCHCFG,
			      temp2 | (1 << 9));
	/* clear any possible error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I840_ERRSTS, 0xc000); 
	return 0;
}

static int intel_845_configure(void)
{
	u32 temp;
	u8 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr); 

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* agpm */
	pci_read_config_byte(agp_bridge.dev, INTEL_I845_AGPM, &temp2);
	pci_write_config_byte(agp_bridge.dev, INTEL_I845_AGPM,
			      temp2 | (1 << 1));
	/* clear any possible error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I845_ERRSTS, 0x001c); 
	return 0;
}

static int intel_850_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr); 

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* mcgcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_I850_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_I850_MCHCFG,
			      temp2 | (1 << 9));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I850_ERRSTS, 0x001c); 
	return 0;
}

static int intel_860_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000);

	/* mcgcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_I860_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_I860_MCHCFG,
			      temp2 | (1 << 9));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I860_ERRSTS, 0xf700);
	return 0;
}

static int intel_830mp_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000);

	/* gmch */
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG,
			      temp2 | (1 << 9));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I830_ERRSTS, 0x1c);
	return 0;
}

static unsigned long intel_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static void intel_resume(void)
{
	intel_configure();
}

/* Setup function */
static struct gatt_mask intel_generic_masks[] =
{
	{mask: 0x00000017, type: 0}
};

static struct aper_size_info_8 intel_815_sizes[2] =
{
	{64, 16384, 4, 0},
	{32, 8192, 3, 8},
};
	
static struct aper_size_info_8 intel_8xx_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 32},
	{64, 16384, 4, 48},
	{32, 8192, 3, 56},
	{16, 4096, 2, 60},
	{8, 2048, 1, 62},
	{4, 1024, 0, 63}
};

static struct aper_size_info_16 intel_generic_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 32},
	{64, 16384, 4, 48},
	{32, 8192, 3, 56},
	{16, 4096, 2, 60},
	{8, 2048, 1, 62},
	{4, 1024, 0, 63}
};

static struct aper_size_info_8 intel_830mp_sizes[4] = 
{
  {256, 65536, 6, 0},
  {128, 32768, 5, 32},
  {64, 16384, 4, 48},
  {32, 8192, 3, 56}
};

int __init intel_generic_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) intel_generic_sizes;
	agp_bridge.size_type = U16_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_configure;
	agp_bridge.fetch_size = intel_fetch_size;
	agp_bridge.cleanup = intel_cleanup;
	agp_bridge.tlb_flush = intel_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
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
	agp_bridge.resume = intel_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

int __init intel_815_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) intel_815_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 2;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_815_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
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
}


int __init intel_820_setup (struct pci_dev *pdev)
{
       agp_bridge.masks = intel_generic_masks;
       agp_bridge.num_of_masks = 1;
       agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
       agp_bridge.size_type = U8_APER_SIZE;
       agp_bridge.num_aperture_sizes = 7;
       agp_bridge.dev_private_data = NULL;
       agp_bridge.needs_scratch_page = FALSE;
       agp_bridge.configure = intel_820_configure;
       agp_bridge.fetch_size = intel_8xx_fetch_size;
       agp_bridge.cleanup = intel_820_cleanup;
       agp_bridge.tlb_flush = intel_820_tlbflush;
       agp_bridge.mask_memory = intel_mask_memory;
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

int __init intel_830mp_setup (struct pci_dev *pdev)
{
       agp_bridge.masks = intel_generic_masks;
       agp_bridge.num_of_masks = 1;
       agp_bridge.aperture_sizes = (void *) intel_830mp_sizes;
       agp_bridge.size_type = U8_APER_SIZE;
       agp_bridge.num_aperture_sizes = 4;
       agp_bridge.dev_private_data = NULL;
       agp_bridge.needs_scratch_page = FALSE;
       agp_bridge.configure = intel_830mp_configure;
       agp_bridge.fetch_size = intel_8xx_fetch_size;
       agp_bridge.cleanup = intel_8xx_cleanup;
       agp_bridge.tlb_flush = intel_8xx_tlbflush;
       agp_bridge.mask_memory = intel_mask_memory;
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

int __init intel_840_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_840_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
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

int __init intel_845_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_845_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
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

int __init intel_850_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_850_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
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

int __init intel_860_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_860_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
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

