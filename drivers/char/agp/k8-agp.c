/* 
 * Copyright 2001,2002 SuSE Labs
 * Distributed under the GNU public license, v2.
 * 
 * This is a GART driver for the AMD K8 northbridge and the AMD 8151 
 * AGP bridge. The main work is done in the northbridge. The configuration
 * is only mirrored in the 8151 for compatibility (could be likely
 * removed now).
 */ 

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

extern int agp_memory_reserved;
extern __u32 *agp_gatt_table; 

static u_int64_t pci_read64 (struct pci_dev *dev, int reg)
{
	union {
		u64 full;
		struct {
			u32 high;
			u32 low;
		} split;
	} tmp;
	pci_read_config_dword(dev, reg, &tmp.split.high);
	pci_read_config_dword(dev, reg+4, &tmp.split.low);
	return tmp.full;
}

static void pci_write64 (struct pci_dev *dev, int reg, u64 value)
{
	union {
		u64 full;
		struct {
			u32 high;
			u32 low;
		} split;
	} tmp;
	tmp.full = value;
	pci_write_config_dword(dev, reg, tmp.split.high);
	pci_write_config_dword(dev, reg+4, tmp.split.low);
}


static int x86_64_insert_memory(agp_memory * mem, off_t pg_start, int type)
{
	int i, j, num_entries;
	void *temp;
	long tmp;
	u32 pte;
	u64 addr;

	temp = agp_bridge.current_size;

	num_entries = A_SIZE_32(temp)->num_entries;

	num_entries -= agp_memory_reserved>>PAGE_SHIFT;

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	/* Make sure we can fit the range in the gatt table. */
	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;

	/* gatt table should be empty. */
	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[j]))
			return -EBUSY;
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = mem->memory[i];

		tmp = addr;
		BUG_ON(tmp & 0xffffff0000000ffc);
		pte = (tmp & 0x000000ff00000000) >> 28;
		pte |=(tmp & 0x00000000fffff000);
		pte |= 1<<1|1<<0;

		agp_bridge.gatt_table[j] = pte;
	}
	agp_bridge.tlb_flush(mem);
	return 0;
}

/*
 * This hack alters the order element according
 * to the size of a long. It sucks. I totally disown this, even
 * though it does appear to work for the most part.
 */
static struct aper_size_info_32 x86_64_aperture_sizes[7] =
{
	{32,   8192,   3+(sizeof(long)/8), 0 },
	{64,   16384,  4+(sizeof(long)/8), 1<<1 },
	{128,  32768,  5+(sizeof(long)/8), 1<<2 },
	{256,  65536,  6+(sizeof(long)/8), 1<<1 | 1<<2 },
	{512,  131072, 7+(sizeof(long)/8), 1<<3 },
	{1024, 262144, 8+(sizeof(long)/8), 1<<1 | 1<<3},
	{2048, 524288, 9+(sizeof(long)/8), 1<<2 | 1<<3}
};


/*
 * Get the current Aperture size from the x86-64.
 * Note, that there may be multiple x86-64's, but we just return
 * the value from the first one we find. The set_size functions
 * keep the rest coherent anyway. Or at least should do.
 */
static int amd_x86_64_fetch_size(void)
{
	struct pci_dev *dev;
	int i;
	u32 temp;
	struct aper_size_info_32 *values;

	pci_for_each_dev(dev) {
		if (dev->bus->number==0 &&
			PCI_FUNC(dev->devfn)==3 &&
			PCI_SLOT(dev->devfn)>=24 && PCI_SLOT(dev->devfn)<=31) {

			pci_read_config_dword(dev, AMD_X86_64_GARTAPERTURECTL, &temp);
			temp = (temp & 0xe);
			values = A_SIZE_32(x86_64_aperture_sizes);

			for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
				if (temp == values[i].size_value) {
					agp_bridge.previous_size =
					    agp_bridge.current_size = (void *) (values + i);

					agp_bridge.aperture_size_idx = i;
					return values[i].size;
				}
			}
		}
	}
	/* erk, couldn't find an x86-64 ? */
	return 0;
}


static void inline flush_x86_64_tlb(struct pci_dev *dev)
{
	u32 tmp;

	pci_read_config_dword (dev, AMD_X86_64_GARTCACHECTL, &tmp);
	tmp |= 1<<0;
	pci_write_config_dword (dev, AMD_X86_64_GARTCACHECTL, tmp);
}


void amd_x86_64_tlbflush(agp_memory * temp)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		if (dev->bus->number==0 && PCI_FUNC(dev->devfn)==3 &&
		    PCI_SLOT(dev->devfn) >=24 && PCI_SLOT(dev->devfn) <=31) {
			flush_x86_64_tlb (dev);
		}
	}
}


/*
 * In a multiprocessor x86-64 system, this function gets
 * called once for each CPU.
 */
u64 amd_x86_64_configure (struct pci_dev *hammer, u64 gatt_table)
{
	u64 aperturebase;
	u32 tmp;
	u64 addr, aper_base;

	/* Address to map to */
	pci_read_config_dword (hammer, AMD_X86_64_GARTAPERTUREBASE, &tmp);
	aperturebase = tmp << 25;
	aper_base = (aperturebase & PCI_BASE_ADDRESS_MEM_MASK);

	/* address of the mappings table */
	addr = (u64) gatt_table;
	addr >>= 12;
	tmp = (u32) addr<<4;
	tmp &= ~0xf;
	pci_write_config_dword (hammer, AMD_X86_64_GARTTABLEBASE, tmp);

	/* Enable GART translation for this hammer. */
	pci_read_config_dword(hammer, AMD_X86_64_GARTAPERTURECTL, &tmp);
	tmp &= 0x3f;
	tmp |= 1<<0;
	pci_write_config_dword(hammer, AMD_X86_64_GARTAPERTURECTL, tmp);

	/* keep CPU's coherent. */
	flush_x86_64_tlb (hammer);
	
	return aper_base;
}


static struct aper_size_info_32 amd_8151_sizes[7] =
{
	{2048, 524288, 9, 0x00000000 },	/* 0 0 0 0 0 0 */
	{1024, 262144, 8, 0x00000400 },	/* 1 0 0 0 0 0 */
	{512,  131072, 7, 0x00000600 },	/* 1 1 0 0 0 0 */
	{256,  65536,  6, 0x00000700 },	/* 1 1 1 0 0 0 */
	{128,  32768,  5, 0x00000720 },	/* 1 1 1 1 0 0 */
	{64,   16384,  4, 0x00000730 },	/* 1 1 1 1 1 0 */
	{32,   8192,   3, 0x00000738 } 	/* 1 1 1 1 1 1 */
};

static int amd_8151_configure(void)
{
	struct pci_dev *dev, *hammer=NULL;
	int current_size;
	int tmp, tmp2, i;
	u64 aperbar;
	unsigned long gatt_bus = virt_to_phys(agp_bridge.gatt_table_real);

	/* Configure AGP regs in each x86-64 host bridge. */
	pci_for_each_dev(dev) {
		if (dev->bus->number==0 &&
			PCI_FUNC(dev->devfn)==3 &&
			PCI_SLOT(dev->devfn)>=24 && PCI_SLOT(dev->devfn)<=31) {
			agp_bridge.gart_bus_addr = amd_x86_64_configure(dev,gatt_bus);
			hammer = dev;

			/*
			 * TODO: Cache pci_dev's of x86-64's in private struct to save us
			 * having to scan the pci list each time.
			 */
		}
	}

	if (hammer == NULL) {
		return -ENODEV;
	}

	/* Shadow x86-64 registers into 8151 registers. */

	dev = agp_bridge.dev;
	if (!dev) 
		return -ENODEV;

	current_size = amd_x86_64_fetch_size();

	pci_read_config_dword(dev, AMD_8151_APERTURESIZE, &tmp);
	tmp &= ~(0xfff);

	/* translate x86-64 size bits to 8151 size bits*/
	for (i=0 ; i<7; i++) {
		if (amd_8151_sizes[i].size == current_size)
			tmp |= (amd_8151_sizes[i].size_value) << 3;
	}
	pci_write_config_dword(dev, AMD_8151_APERTURESIZE, tmp);

	pci_read_config_dword (hammer, AMD_X86_64_GARTAPERTUREBASE, &tmp);
	aperbar = pci_read64 (dev, AMD_8151_VMAPERTURE);
	aperbar |= (tmp & 0x7fff) <<25;
	aperbar &= 0x000000ffffffffff;
	aperbar |= 1<<2;	/* This address is a 64bit ptr FIXME: Make conditional in 32bit mode */
	pci_write64 (dev, AMD_8151_VMAPERTURE, aperbar);

	pci_read_config_dword(dev, AMD_8151_AGP_CTL , &tmp);
	tmp &= ~(AMD_8151_GTLBEN | AMD_8151_APEREN);
	
	pci_read_config_dword(hammer, AMD_X86_64_GARTAPERTURECTL, &tmp2);
	if (tmp2 & AMD_X86_64_GARTEN)
		tmp |= AMD_8151_APEREN;
	// FIXME: bit 7 of AMD_8151_AGP_CTL (GTLBEN) must be copied if set.
	// But where is it set ?
	pci_write_config_dword(dev, AMD_8151_AGP_CTL, tmp);

	return 0;
}


static void amd_8151_cleanup(void)
{
	struct pci_dev *dev;
	u32 tmp;

	pci_for_each_dev(dev) {
		/* disable gart translation */
		if (dev->bus->number==0 && PCI_FUNC(dev->devfn)==3 &&
		    (PCI_SLOT(dev->devfn) >=24) && (PCI_SLOT(dev->devfn) <=31)) {

			pci_read_config_dword (dev, AMD_X86_64_GARTAPERTURECTL, &tmp);
			tmp &= ~(AMD_X86_64_GARTEN);
			pci_write_config_dword (dev, AMD_X86_64_GARTAPERTURECTL, tmp);
		}

		/* Now shadow the disable in the 8151 */
		if (dev->vendor == PCI_VENDOR_ID_AMD &&
			dev->device == PCI_DEVICE_ID_AMD_8151_0) {

			pci_read_config_dword (dev, AMD_8151_AGP_CTL, &tmp);
			tmp &= ~(AMD_8151_APEREN);	
			pci_write_config_dword (dev, AMD_8151_AGP_CTL, tmp);
		}
	}
}



static unsigned long amd_8151_mask_memory(unsigned long addr, int type)
{
	return addr | agp_bridge.masks[0].mask;
}


static struct gatt_mask amd_8151_masks[] =
{
	{0x00000001, 0}
};


/*
 * Try to configure an AGP v3 capable setup.
 * If we fail (typically because we don't have an AGP v3
 * card in the system) we fall back to the generic AGP v2
 * routines.
 */
static void agp_x86_64_agp_enable(u32 mode)
{
	struct pci_dev *device = NULL;
	u32 command, scratch; 
	u8 cap_ptr;
	u8 agp_v3;
	u8 v3_devs=0;

	/* FIXME: If 'mode' is x1/x2/x4 should we call the AGPv2 routines directly ?
	 * Messy, as some AGPv3 cards can only do x4 as a minimum.
	 */

	/* PASS1: Count # of devs capable of AGPv3 mode. */
	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00) {
			pci_read_config_dword(device, cap_ptr, &scratch);
			scratch &= (1<<20|1<<21|1<<22|1<<23);
			scratch = scratch>>20;
			/* AGP v3 capable ? */
			if (scratch>=3) {
				v3_devs++;
				printk (KERN_INFO "AGP: Found AGPv3 capable device at %d:%d:%d\n",
					device->bus->number, PCI_FUNC(device->devfn), PCI_SLOT(device->devfn));
			} else {
				printk (KERN_INFO "AGP: Meh. version %x AGP device found.\n", scratch);
			}
		}
	}
	/* If not enough, go to AGP v2 setup */
	if (v3_devs<2) {
		printk (KERN_INFO "AGP: Only %d devices found, not enough, trying AGPv2\n", v3_devs);
		return agp_generic_agp_enable(mode);
	} else {
		printk (KERN_INFO "AGP: Enough AGPv3 devices found, setting up...\n");
	}


	pci_read_config_dword(agp_bridge.dev, agp_bridge.capndx + 4, &command);

	/*
	 * PASS2: go through all devices that claim to be
	 *        AGP devices and collect their data.
	 */

	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00) {
			/*
			 * Ok, here we have a AGP device. Disable impossible 
			 * settings, and adjust the readqueue to the minimum.
			 */

			printk (KERN_INFO "AGP: Setting up AGPv3 capable device at %d:%d:%d\n",
					device->bus->number, PCI_FUNC(device->devfn), PCI_SLOT(device->devfn));
			pci_read_config_dword(device, cap_ptr + 4, &scratch);
			agp_v3 = (scratch & (1<<3) ) >>3;

			/* adjust RQ depth */
			command =
			    ((command & ~0xff000000) |
			     min_t(u32, (mode & 0xff000000),
				 min_t(u32, (command & 0xff000000),
				     (scratch & 0xff000000))));

			/* disable SBA if it's not supported */
			if (!((command & 0x200) && (scratch & 0x200) && (mode & 0x200)))
				command &= ~0x200;

			/* disable FW if it's not supported */
			if (!((command & 0x10) && (scratch & 0x10) && (mode & 0x10)))
				command &= ~0x10;

			if (!((command & 2) && (scratch & 2) && (mode & 2))) {
				command &= ~2;		/* 8x */
				printk (KERN_INFO "AGP: Putting device into 8x mode\n");
			}

			if (!((command & 1) && (scratch & 1) && (mode & 1))) {
				command &= ~1;		/* 4x */
				printk (KERN_INFO "AGP: Putting device into 4x mode\n");
			}
		}
	}
	/*
	 * PASS3: Figure out the 8X/4X setting and enable the
	 *        target (our motherboard chipset).
	 */

	if (command & 2)
		command &= ~5;	/* 8X */

	if (command & 1)
		command &= ~6;	/* 4X */

	command |= 0x100;

	pci_write_config_dword(agp_bridge.dev, agp_bridge.capndx + 8, command);

	/*
	 * PASS4: Go through all AGP devices and update the
	 *        command registers.
	 */

	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00)
			pci_write_config_dword(device, cap_ptr + 8, command);
	}
}


int __init amd_8151_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = amd_8151_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) amd_8151_sizes;
	agp_bridge.size_type = U32_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = amd_8151_configure;
	agp_bridge.fetch_size = amd_x86_64_fetch_size;
	agp_bridge.cleanup = amd_8151_cleanup;
	agp_bridge.tlb_flush = amd_x86_64_tlbflush;
	agp_bridge.mask_memory = amd_8151_mask_memory;
	agp_bridge.agp_enable = agp_x86_64_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = x86_64_insert_memory;
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
