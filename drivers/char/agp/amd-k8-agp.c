/* 
 * Copyright 2001-2003 SuSE Labs.
 * Distributed under the GNU public license, v2.
 * 
 * This is a GART driver for the AMD Opteron/Athlon64 on-CPU northbridge.
 * It also includes support for the AMD 8151 AGP bridge,
 * although it doesn't actually do much, as all the real
 * work is done in the northbridge(s).
 */

/*
 * On x86-64 the AGP driver needs to be initialized early by the IOMMU 
 * code.  When you use this driver as a template for a new K8 AGP bridge
 * driver don't forget to change arch/x86_64/kernel/pci-gart.c too -AK.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

/* Will need to be increased if hammer ever goes >8-way. */
#define MAX_HAMMER_GARTS   8

static int nr_garts;
static struct pci_dev * hammers[MAX_HAMMER_GARTS];

static int gart_iterator;
#define for_each_nb() for(gart_iterator=0;gart_iterator<nr_garts;gart_iterator++)

static int x86_64_insert_memory(agp_memory * mem, off_t pg_start, int type)
{
	int i, j, num_entries;
	long tmp;
	u32 pte;
	u64 addr;

	num_entries = agp_num_entries();

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	/* Make sure we can fit the range in the gatt table. */
	/* FIXME: could wrap */
	if (((unsigned long)pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;

	/* gatt table should be empty. */
	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge->gatt_table[j]))
			return -EBUSY;
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = agp_bridge->mask_memory(mem->memory[i], mem->type);

		tmp = addr;
		BUG_ON(tmp & 0xffffff0000000ffc);
		pte = (tmp & 0x000000ff00000000) >> 28;
		pte |=(tmp & 0x00000000fffff000);
		pte |= 1<<1|1<<0;

		agp_bridge->gatt_table[j] = pte;
	}
	agp_bridge->tlb_flush(mem);
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

	dev = hammers[0];
	if (dev==NULL)
		return 0;

	pci_read_config_dword(dev, AMD_X86_64_GARTAPERTURECTL, &temp);
	temp = (temp & 0xe);
	values = A_SIZE_32(x86_64_aperture_sizes);

	for (i = 0; i < agp_bridge->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);

			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}
	return 0;
}


static void flush_x86_64_tlb(struct pci_dev *dev)
{
	u32 tmp;

	pci_read_config_dword (dev, AMD_X86_64_GARTCACHECTL, &tmp);
	tmp |= 1<<0;
	pci_write_config_dword (dev, AMD_X86_64_GARTCACHECTL, tmp);
}


static void amd_x86_64_tlbflush(agp_memory * temp)
{
	for_each_nb() {
		flush_x86_64_tlb (hammers[gart_iterator]);
	}
}


/*
 * In a multiprocessor x86-64 system, this function gets
 * called once for each CPU.
 */
static u64 amd_x86_64_configure (struct pci_dev *hammer, u64 gatt_table)
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
	unsigned long gatt_bus = virt_to_phys(agp_bridge->gatt_table_real);

	/* Configure AGP regs in each x86-64 host bridge. */
	for_each_nb() {
		agp_bridge->gart_bus_addr =
				amd_x86_64_configure(hammers[gart_iterator],gatt_bus);
	}
	return 0;
}


static void amd_8151_cleanup(void)
{
	u32 tmp;

	for_each_nb() {
		/* disable gart translation */
		pci_read_config_dword (hammers[gart_iterator], AMD_X86_64_GARTAPERTURECTL, &tmp);
		tmp &= ~(AMD_X86_64_GARTEN);
		pci_write_config_dword (hammers[gart_iterator], AMD_X86_64_GARTAPERTURECTL, tmp);
	}
}


static unsigned long amd_8151_mask_memory(unsigned long addr, int type)
{
	return addr | agp_bridge->masks[0].mask;
}


static struct gatt_mask amd_8151_masks[] =
{
	{.mask = 0x00000001, .type = 0}
};


static int __init amd_8151_setup (struct pci_dev *pdev)
{
	struct pci_dev *dev;
	int i=0;

	agp_bridge->masks = amd_8151_masks;
	agp_bridge->aperture_sizes = (void *) amd_8151_sizes;
	agp_bridge->size_type = U32_APER_SIZE;
	agp_bridge->num_aperture_sizes = 7;
	agp_bridge->dev_private_data = NULL;
	agp_bridge->needs_scratch_page = FALSE;
	agp_bridge->configure = amd_8151_configure;
	agp_bridge->fetch_size = amd_x86_64_fetch_size;
	agp_bridge->cleanup = amd_8151_cleanup;
	agp_bridge->tlb_flush = amd_x86_64_tlbflush;
	agp_bridge->mask_memory = amd_8151_mask_memory;
	agp_bridge->agp_enable = agp_generic_enable;
	agp_bridge->cache_flush = global_cache_flush;
	agp_bridge->create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge->free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge->insert_memory = x86_64_insert_memory;
	agp_bridge->remove_memory = agp_generic_remove_memory;
	agp_bridge->alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge->free_by_type = agp_generic_free_by_type;
	agp_bridge->agp_alloc_page = agp_generic_alloc_page;
	agp_bridge->agp_destroy_page = agp_generic_destroy_page;
	agp_bridge->suspend = agp_generic_suspend;
	agp_bridge->resume = agp_generic_resume;
	agp_bridge->cant_use_aperture = 0;


	/* cache pci_devs of northbridges. */
	pci_for_each_dev(dev) {
		if (dev->bus->number==0 && PCI_FUNC(dev->devfn)==3 &&
			(PCI_SLOT(dev->devfn) >=24) && (PCI_SLOT(dev->devfn) <=31)) {

			hammers[i++] = dev;
			nr_garts = i;
			if (i==MAX_HAMMER_GARTS)
				return 0;
		}
	}

	return 0;
}

static struct agp_driver amd_k8_agp_driver = {
	.owner = THIS_MODULE,
};

static int __init agp_amdk8_probe (struct pci_dev *dev, const struct pci_device_id *ent)
{
	u8 cap_ptr = 0;

	cap_ptr = pci_find_capability(dev, PCI_CAP_ID_AGP);
	if (cap_ptr == 0)
		return -ENODEV;

	printk (KERN_INFO PFX "Detected Opteron/Athlon64 on-CPU GART\n");

	agp_bridge->dev = dev;
	agp_bridge->capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(agp_bridge->dev, agp_bridge->capndx+PCI_AGP_STATUS, &agp_bridge->mode);
	amd_8151_setup(dev);
	amd_k8_agp_driver.dev = dev;
	agp_register_driver(&amd_k8_agp_driver);
	return 0;
}

static struct pci_device_id agp_amdk8_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AMD,
	.device		= PCI_DEVICE_ID_AMD_8151_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_amdk8_pci_table);

static struct __initdata pci_driver agp_amdk8_pci_driver = {
	.name		= "agpgart-amd-k8",
	.id_table	= agp_amdk8_pci_table,
	.probe		= agp_amdk8_probe,
};

/* Not static due to IOMMU code calling it early. */
int __init agp_amdk8_init(void)
{
	int ret_val;

	ret_val = pci_module_init(&agp_amdk8_pci_driver);
	if (ret_val)
		agp_bridge->type = NOT_SUPPORTED;

	agp_bridge->type = AMD_8151;

	return ret_val;
}

static void __exit agp_amdk8_cleanup(void)
{
	agp_unregister_driver(&amd_k8_agp_driver);
	pci_unregister_driver(&agp_amdk8_pci_driver);
}

/* On x86-64 the PCI driver needs to initialize this driver early
   for the IOMMU, so it has to be called via a backdoor. */
#ifndef CONFIG_GART_IOMMU
module_init(agp_amdk8_init);
module_exit(agp_amdk8_cleanup);
#endif

MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_LICENSE("GPL and additional rights");

