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

static struct aper_size_info_fixed intel_i810_sizes[] =
{
	{64, 16384, 4},
     /* The 32M mode still requires a 64k gatt */
	{32, 8192, 4}
};

#define AGP_DCACHE_MEMORY 1
#define AGP_PHYS_MEMORY   2

static struct gatt_mask intel_i810_masks[] =
{
	{mask: I810_PTE_VALID, type: 0},
	{mask: (I810_PTE_VALID | I810_PTE_LOCAL), type: AGP_DCACHE_MEMORY},
	{mask: I810_PTE_VALID, type: 0}
};

static struct _intel_i810_private {
	struct pci_dev *i810_dev;	/* device one */
	volatile u8 *registers;
	int num_dcache_entries;
} intel_i810_private;

static int intel_i810_fetch_size(void)
{
	u32 smram_miscc;
	struct aper_size_info_fixed *values;

	pci_read_config_dword(agp_bridge.dev, I810_SMRAM_MISCC, &smram_miscc);
	values = A_SIZE_FIX(agp_bridge.aperture_sizes);

	if ((smram_miscc & I810_GMS) == I810_GMS_DISABLE) {
		printk(KERN_WARNING PFX "i810 is disabled\n");
		return 0;
	}
	if ((smram_miscc & I810_GFX_MEM_WIN_SIZE) == I810_GFX_MEM_WIN_32M) {
		agp_bridge.previous_size =
		    agp_bridge.current_size = (void *) (values + 1);
		agp_bridge.aperture_size_idx = 1;
		return values[1].size;
	} else {
		agp_bridge.previous_size =
		    agp_bridge.current_size = (void *) (values);
		agp_bridge.aperture_size_idx = 0;
		return values[0].size;
	}

	return 0;
}

static int intel_i810_configure(void)
{
	struct aper_size_info_fixed *current_size;
	u32 temp;
	int i;

	current_size = A_SIZE_FIX(agp_bridge.current_size);

	pci_read_config_dword(intel_i810_private.i810_dev, I810_MMADDR, &temp);
	temp &= 0xfff80000;

	intel_i810_private.registers =
	    (volatile u8 *) ioremap(temp, 128 * 4096);

	if ((INREG32(intel_i810_private.registers, I810_DRAM_CTL)
	     & I810_DRAM_ROW_0) == I810_DRAM_ROW_0_SDRAM) {
		/* This will need to be dynamically assigned */
		printk(KERN_INFO PFX "detected 4MB dedicated video ram.\n");
		intel_i810_private.num_dcache_entries = 1024;
	}
	pci_read_config_dword(intel_i810_private.i810_dev, I810_GMADDR, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	OUTREG32(intel_i810_private.registers, I810_PGETBL_CTL,
		 agp_bridge.gatt_bus_addr | I810_PGETBL_ENABLED);
	CACHE_FLUSH();

	if (agp_bridge.needs_scratch_page == TRUE) {
		for (i = 0; i < current_size->num_entries; i++) {
			OUTREG32(intel_i810_private.registers,
				 I810_PTE_BASE + (i * 4),
				 agp_bridge.scratch_page);
		}
	}
	return 0;
}

static void intel_i810_cleanup(void)
{
	OUTREG32(intel_i810_private.registers, I810_PGETBL_CTL, 0);
	iounmap((void *) intel_i810_private.registers);
}

static void intel_i810_tlbflush(agp_memory * mem)
{
	return;
}

static void intel_i810_agp_enable(u32 mode)
{
	return;
}

static int intel_i810_insert_entries(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i, j, num_entries;
	void *temp;

	temp = agp_bridge.current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}
	for (j = pg_start; j < (pg_start + mem->page_count); j++) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[j])) {
			return -EBUSY;
		}
	}

	if (type != 0 || mem->type != 0) {
		if ((type == AGP_DCACHE_MEMORY) &&
		    (mem->type == AGP_DCACHE_MEMORY)) {
			/* special insert */
			CACHE_FLUSH();
			for (i = pg_start;
			     i < (pg_start + mem->page_count); i++) {
				OUTREG32(intel_i810_private.registers,
					 I810_PTE_BASE + (i * 4),
					 (i * 4096) | I810_PTE_LOCAL |
					 I810_PTE_VALID);
			}
			CACHE_FLUSH();
			agp_bridge.tlb_flush(mem);
			return 0;
		}
	        if((type == AGP_PHYS_MEMORY) &&
		   (mem->type == AGP_PHYS_MEMORY)) {
		   goto insert;
		}
		return -EINVAL;
	}

insert:
   	CACHE_FLUSH();
	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		OUTREG32(intel_i810_private.registers,
			 I810_PTE_BASE + (j * 4), mem->memory[i]);
	}
	CACHE_FLUSH();

	agp_bridge.tlb_flush(mem);
	return 0;
}

static int intel_i810_remove_entries(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i;

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		OUTREG32(intel_i810_private.registers,
			 I810_PTE_BASE + (i * 4),
			 agp_bridge.scratch_page);
	}

	CACHE_FLUSH();
	agp_bridge.tlb_flush(mem);
	return 0;
}

static agp_memory *intel_i810_alloc_by_type(size_t pg_count, int type)
{
	agp_memory *new;

	if (type == AGP_DCACHE_MEMORY) {
		if (pg_count != intel_i810_private.num_dcache_entries) {
			return NULL;
		}
		new = agp_create_memory(1);

		if (new == NULL) {
			return NULL;
		}
		new->type = AGP_DCACHE_MEMORY;
		new->page_count = pg_count;
		new->num_scratch_pages = 0;
		vfree(new->memory);
	   	MOD_INC_USE_COUNT;
		return new;
	}
	if(type == AGP_PHYS_MEMORY) {
		void *addr;
		/* The I810 requires a physical address to program
		 * it's mouse pointer into hardware.  However the
		 * Xserver still writes to it through the agp
		 * aperture
		 */
	   	if (pg_count != 1) {
		   	return NULL;
		}
	   	new = agp_create_memory(1);

		if (new == NULL) {
			return NULL;
		}
	   	MOD_INC_USE_COUNT;
		addr = agp_bridge.agp_alloc_page();

		if (addr == NULL) {
			/* Free this structure */
			agp_free_memory(new);
			return NULL;
		}
		new->memory[0] = agp_bridge.mask_memory(virt_to_phys(addr), type);
		new->page_count = 1;
	   	new->num_scratch_pages = 1;
	   	new->type = AGP_PHYS_MEMORY;
	        new->physical = virt_to_phys((void *) new->memory[0]);
	   	return new;
	}
   
	return NULL;
}

static void intel_i810_free_by_type(agp_memory * curr)
{
	agp_free_key(curr->key);
   	if(curr->type == AGP_PHYS_MEMORY) {
	   	agp_bridge.agp_destroy_page(
				 phys_to_virt(curr->memory[0]));
		vfree(curr->memory);
	}
	kfree(curr);
   	MOD_DEC_USE_COUNT;
}

static unsigned long intel_i810_mask_memory(unsigned long addr, int type)
{
	/* Type checking must be done elsewhere */
	return addr | agp_bridge.masks[type].mask;
}

int __init intel_i810_setup(struct pci_dev *i810_dev)
{
	intel_i810_private.i810_dev = i810_dev;

	agp_bridge.masks = intel_i810_masks;
	agp_bridge.num_of_masks = 2;
	agp_bridge.aperture_sizes = (void *) intel_i810_sizes;
	agp_bridge.size_type = FIXED_APER_SIZE;
	agp_bridge.num_aperture_sizes = 2;
	agp_bridge.dev_private_data = (void *) &intel_i810_private;
	agp_bridge.needs_scratch_page = TRUE;
	agp_bridge.configure = intel_i810_configure;
	agp_bridge.fetch_size = intel_i810_fetch_size;
	agp_bridge.cleanup = intel_i810_cleanup;
	agp_bridge.tlb_flush = intel_i810_tlbflush;
	agp_bridge.mask_memory = intel_i810_mask_memory;
	agp_bridge.agp_enable = intel_i810_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = intel_i810_insert_entries;
	agp_bridge.remove_memory = intel_i810_remove_entries;
	agp_bridge.alloc_by_type = intel_i810_alloc_by_type;
	agp_bridge.free_by_type = intel_i810_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
}

static struct aper_size_info_fixed intel_i830_sizes[] =
{
	{128, 32768, 5},
	/* The 64M mode still requires a 128k gatt */
	{64, 16384, 5}
};

static struct _intel_i830_private {
	struct pci_dev *i830_dev;   /* device one */
	volatile u8 *registers;
	int gtt_entries;
} intel_i830_private;

static void intel_i830_init_gtt_entries(void)
{
	u16 gmch_ctrl;
	int gtt_entries;
	u8 rdct;
	static const int ddt[4] = { 0, 16, 32, 64 };

	pci_read_config_word(agp_bridge.dev,I830_GMCH_CTRL,&gmch_ctrl);

	switch (gmch_ctrl & I830_GMCH_GMS_MASK) {
	case I830_GMCH_GMS_STOLEN_512:
		gtt_entries = KB(512) - KB(132);
		printk(KERN_INFO PFX "detected %dK stolen memory.\n",gtt_entries / KB(1));
		break;
	case I830_GMCH_GMS_STOLEN_1024:
		gtt_entries = MB(1) - KB(132);
		printk(KERN_INFO PFX "detected %dK stolen memory.\n",gtt_entries / KB(1));
		break;
	case I830_GMCH_GMS_STOLEN_8192:
		gtt_entries = MB(8) - KB(132);
		printk(KERN_INFO PFX "detected %dK stolen memory.\n",gtt_entries / KB(1));
		break;
	case I830_GMCH_GMS_LOCAL:
		rdct = INREG8(intel_i830_private.registers,I830_RDRAM_CHANNEL_TYPE);
		gtt_entries = (I830_RDRAM_ND(rdct) + 1) * MB(ddt[I830_RDRAM_DDT(rdct)]);
		printk(KERN_INFO PFX "detected %dK local memory.\n",gtt_entries / KB(1));
		break;
	default:
		printk(KERN_INFO PFX "no video memory detected.\n");
		gtt_entries = 0;
		break;
	}

	gtt_entries /= KB(4);

	intel_i830_private.gtt_entries = gtt_entries;
}

/* The intel i830 automatically initializes the agp aperture during POST.
 * Use the memory already set aside for in the GTT.
 */
static int intel_i830_create_gatt_table(void)
{
	int page_order;
	struct aper_size_info_fixed *size;
	int num_entries;
	u32 temp;

	size = agp_bridge.current_size;
	page_order = size->page_order;
	num_entries = size->num_entries;
	agp_bridge.gatt_table_real = 0;

	pci_read_config_dword(intel_i830_private.i830_dev,I810_MMADDR,&temp);
	temp &= 0xfff80000;

	intel_i830_private.registers = (volatile u8 *) ioremap(temp,128 * 4096);
	if (!intel_i830_private.registers) return (-ENOMEM);

	temp = INREG32(intel_i830_private.registers,I810_PGETBL_CTL) & 0xfffff000;
	CACHE_FLUSH();

	/* we have to call this as early as possible after the MMIO base address is known */
	intel_i830_init_gtt_entries();

	agp_bridge.gatt_table = NULL;

	agp_bridge.gatt_bus_addr = temp;

	return(0);
}

/* Return the gatt table to a sane state. Use the top of stolen
 * memory for the GTT.
 */
static int intel_i830_free_gatt_table(void)
{
	return(0);
}

static int intel_i830_fetch_size(void)
{
	u16 gmch_ctrl;
	struct aper_size_info_fixed *values;

	pci_read_config_word(agp_bridge.dev,I830_GMCH_CTRL,&gmch_ctrl);
	values = A_SIZE_FIX(agp_bridge.aperture_sizes);

	if ((gmch_ctrl & I830_GMCH_MEM_MASK) == I830_GMCH_MEM_128M) {
		agp_bridge.previous_size = agp_bridge.current_size = (void *) values;
		agp_bridge.aperture_size_idx = 0;
		return(values[0].size);
	} else {
		agp_bridge.previous_size = agp_bridge.current_size = (void *) values;
		agp_bridge.aperture_size_idx = 1;
		return(values[1].size);
	}

	return(0);
}

static int intel_i830_configure(void)
{
	struct aper_size_info_fixed *current_size;
	u32 temp;
	u16 gmch_ctrl;
	int i;

	current_size = A_SIZE_FIX(agp_bridge.current_size);

	pci_read_config_dword(intel_i830_private.i830_dev,I810_GMADDR,&temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	pci_read_config_word(agp_bridge.dev,I830_GMCH_CTRL,&gmch_ctrl);
	gmch_ctrl |= I830_GMCH_ENABLED;
	pci_write_config_word(agp_bridge.dev,I830_GMCH_CTRL,gmch_ctrl);

	OUTREG32(intel_i830_private.registers,I810_PGETBL_CTL,agp_bridge.gatt_bus_addr | I810_PGETBL_ENABLED);
	CACHE_FLUSH();

	if (agp_bridge.needs_scratch_page == TRUE)
		for (i = intel_i830_private.gtt_entries; i < current_size->num_entries; i++)
			OUTREG32(intel_i830_private.registers,I810_PTE_BASE + (i * 4),agp_bridge.scratch_page);

	return (0);
}

static void intel_i830_cleanup(void)
{
	iounmap((void *) intel_i830_private.registers);
}

static int intel_i830_insert_entries(agp_memory *mem,off_t pg_start,int type)
{
	int i,j,num_entries;
	void *temp;

	temp = agp_bridge.current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if (pg_start < intel_i830_private.gtt_entries) {
		printk (KERN_DEBUG "pg_start == 0x%.8lx,intel_i830_private.gtt_entries == 0x%.8x\n",
				pg_start,intel_i830_private.gtt_entries);

		printk ("Trying to insert into local/stolen memory\n");
		return (-EINVAL);
	}

	if ((pg_start + mem->page_count) > num_entries)
		return (-EINVAL);

	/* The i830 can't check the GTT for entries since its read only,
	 * depend on the caller to make the correct offset decisions.
	 */

	if ((type != 0 && type != AGP_PHYS_MEMORY) ||
		(mem->type != 0 && mem->type != AGP_PHYS_MEMORY))
		return (-EINVAL);

	CACHE_FLUSH();

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++)
		OUTREG32(intel_i830_private.registers,I810_PTE_BASE + (j * 4),mem->memory[i]);

	CACHE_FLUSH();

	agp_bridge.tlb_flush(mem);

	return(0);
}

static int intel_i830_remove_entries(agp_memory *mem,off_t pg_start,int type)
{
	int i;

	CACHE_FLUSH ();

	if (pg_start < intel_i830_private.gtt_entries) {
		printk ("Trying to disable local/stolen memory\n");
		return (-EINVAL);
	}

	for (i = pg_start; i < (mem->page_count + pg_start); i++)
		OUTREG32(intel_i830_private.registers,I810_PTE_BASE + (i * 4),agp_bridge.scratch_page);

	CACHE_FLUSH();

	agp_bridge.tlb_flush(mem);

	return (0);
}

static agp_memory *intel_i830_alloc_by_type(size_t pg_count,int type)
{
	agp_memory *nw;

	/* always return NULL for now */
	if (type == AGP_DCACHE_MEMORY) return(NULL);

	if (type == AGP_PHYS_MEMORY) {
		void *addr;

		/* The i830 requires a physical address to program
		 * it's mouse pointer into hardware. However the
		 * Xserver still writes to it through the agp
		 * aperture
		 */

		if (pg_count != 1) return(NULL);

		nw = agp_create_memory(1);

		if (nw == NULL) return(NULL);

		MOD_INC_USE_COUNT;
		addr = agp_bridge.agp_alloc_page();
		if (addr == NULL) {
			/* free this structure */
			agp_free_memory(nw);
			return(NULL);
		}

		nw->memory[0] = agp_bridge.mask_memory(virt_to_phys(addr),type);
		nw->page_count = 1;
		nw->num_scratch_pages = 1;
		nw->type = AGP_PHYS_MEMORY;
		nw->physical = virt_to_phys(addr);
		return(nw);
	}

	return(NULL);
}

int __init intel_i830_setup(struct pci_dev *i830_dev)
{
	intel_i830_private.i830_dev = i830_dev;

	agp_bridge.masks = intel_i810_masks;
	agp_bridge.num_of_masks = 3;
	agp_bridge.aperture_sizes = (void *) intel_i830_sizes;
	agp_bridge.size_type = FIXED_APER_SIZE;
	agp_bridge.num_aperture_sizes = 2;

	agp_bridge.dev_private_data = (void *) &intel_i830_private;
	agp_bridge.needs_scratch_page = TRUE;

	agp_bridge.configure = intel_i830_configure;
	agp_bridge.fetch_size = intel_i830_fetch_size;
	agp_bridge.cleanup = intel_i830_cleanup;
	agp_bridge.tlb_flush = intel_i810_tlbflush;
	agp_bridge.mask_memory = intel_i810_mask_memory;
	agp_bridge.agp_enable = intel_i810_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;

	agp_bridge.create_gatt_table = intel_i830_create_gatt_table;
	agp_bridge.free_gatt_table = intel_i830_free_gatt_table;

	agp_bridge.insert_memory = intel_i830_insert_entries;
	agp_bridge.remove_memory = intel_i830_remove_entries;
	agp_bridge.alloc_by_type = intel_i830_alloc_by_type;
	agp_bridge.free_by_type = intel_i810_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;

	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return(0);
}

