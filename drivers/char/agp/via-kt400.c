/*
 * VIA KT400 AGPGART routines. 
 *
 * The KT400 does magick to put the AGP bridge compliant with the same
 * standards version as the graphics card. If we haven't fallen into
 * 2.0 compatability mode, we run this code. Otherwise, we run the
 * code in via-agp.c
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

static int via_fetch_size(void)
{
	int i;
	u16 temp;
	struct aper_size_info_16 *values;

	values = A_SIZE_16(agp_bridge->aperture_sizes);
	pci_read_config_word(agp_bridge->dev, VIA_AGP3_APSIZE, &temp);
	temp &= 0xfff;

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

static int via_configure(void)
{
	u32 temp;
	struct aper_size_info_16 *current_size;
    
	current_size = A_SIZE_16(agp_bridge->current_size);

	/* address to map too */
	pci_read_config_dword(agp_bridge->dev, VIA_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture GATT base */
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_ATTBASE,
		agp_bridge->gatt_bus_addr & 0xfffff000);
	return 0;
}

static void via_cleanup(void)
{
	struct aper_size_info_16 *previous_size;

	previous_size = A_SIZE_16(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, VIA_APSIZE, previous_size->size_value);
}

static void via_tlbflush(agp_memory * mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, temp & ~(1<<7));
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, temp);
}

static unsigned long via_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge->masks[0].mask;
}

static struct aper_size_info_16 via_generic_sizes[11] =
{
	{ 4,     1024,  0, 1<<11|1<<10|1<<9|1<<8|1<<5|1<<4|1<<3|1<<2|1<<1|1<<0 },
	{ 8,     2048,  1, 1<<11|1<<10|1<<9|1<<8|1<<5|1<<4|1<<3|1<<2|1<<1},
	{ 16,    4096,  2, 1<<11|1<<10|1<<9|1<<8|1<<5|1<<4|1<<3|1<<2},
	{ 32,    8192,  3, 1<<11|1<<10|1<<9|1<<8|1<<5|1<<4|1<<3},
	{ 64,   16384,  4, 1<<11|1<<10|1<<9|1<<8|1<<5|1<<4},
	{ 128,  32768,  5, 1<<11|1<<10|1<<9|1<<8|1<<5},
	{ 256,  65536,  6, 1<<11|1<<10|1<<9|1<<8},
	{ 512,  131072, 7, 1<<11|1<<10|1<<9},
	{ 1024, 262144, 8, 1<<11|1<<10},
	{ 2048, 524288, 9, 1<<11}	/* 2GB <- Max supported */
};

static struct gatt_mask via_generic_masks[] =
{
	{.mask = 0x00000000, .type = 0}
};


static void via_kt400_enable(u32 mode)
{
	if ((agp_generic_agp_3_0_enable(mode))==FALSE)
		printk (KERN_INFO PFX "agp_generic_agp_3_0_enable() failed\n");
}

static struct agp_driver via_kt400_agp_driver = {
	.owner = THIS_MODULE,
};

static int __init agp_via_probe (struct pci_dev *dev, const struct pci_device_id *ent)
{
	u8 reg;
	u8 cap_ptr = 0;

	cap_ptr = pci_find_capability(dev, PCI_CAP_ID_AGP);
	if (cap_ptr == 0)
		return -ENODEV;

	pci_read_config_byte(dev, VIA_AGPSEL, &reg);
	/* Check if we are in AGP 2.0 compatability mode, if so it
	 * will be picked up by via-agp.o */
	if ((reg & (1<<1))!=0)
		return -ENODEV;

	printk (KERN_INFO PFX "Detected VIA KT400 AGP3 chipset\n");

	agp_bridge->dev = dev;
	agp_bridge->type = VIA_APOLLO_KT400_3;
	agp_bridge->capndx = cap_ptr;
	agp_bridge->masks = via_generic_masks;
	agp_bridge->aperture_sizes = (void *) via_generic_sizes;
	agp_bridge->size_type = U8_APER_SIZE;
	agp_bridge->num_aperture_sizes = 7;
	agp_bridge->dev_private_data = NULL;
	agp_bridge->needs_scratch_page = FALSE;
	agp_bridge->agp_enable = via_kt400_enable;
	agp_bridge->configure = via_configure;
	agp_bridge->fetch_size = via_fetch_size;
	agp_bridge->cleanup = via_cleanup;
	agp_bridge->tlb_flush = via_tlbflush;
	agp_bridge->mask_memory = via_mask_memory;
	agp_bridge->cache_flush = global_cache_flush;
	agp_bridge->create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge->free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge->insert_memory = agp_generic_insert_memory;
	agp_bridge->remove_memory = agp_generic_remove_memory;
	agp_bridge->alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge->free_by_type = agp_generic_free_by_type;
	agp_bridge->agp_alloc_page = agp_generic_alloc_page;
	agp_bridge->agp_destroy_page = agp_generic_destroy_page;
	agp_bridge->suspend = agp_generic_suspend;
	agp_bridge->resume = agp_generic_resume;
	agp_bridge->cant_use_aperture = 0;

	/* Fill in the mode register */
	pci_read_config_dword(agp_bridge->dev, agp_bridge->capndx+PCI_AGP_STATUS, &agp_bridge->mode);

	via_kt400_agp_driver.dev = dev;
	agp_register_driver(&via_kt400_agp_driver);
	return 0;
}

static struct pci_device_id agp_via_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_DEVICE_ID_VIA_8377_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_DEVICE_ID_VIA_8367_0,
	.subvendor	= PCI_VENDOR_ID_VIA,
	.subdevice	= PCI_DEVICE_ID_VIA_8377_0,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_via_pci_table);

static struct __initdata pci_driver agp_via_pci_driver = {
	.name		= "agpgart-via",
	.id_table	= agp_via_pci_table,
	.probe		= agp_via_probe,
};

static int __init agp_via_init(void)
{
	int ret_val;

	ret_val = pci_module_init(&agp_via_pci_driver);
	if (ret_val)
		agp_bridge->type = NOT_SUPPORTED;

	return ret_val;
}

static void __exit agp_via_cleanup(void)
{
	agp_unregister_driver(&via_kt400_agp_driver);
	pci_unregister_driver(&agp_via_pci_driver);
}

module_init(agp_via_init);
module_exit(agp_via_cleanup);

MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_LICENSE("GPL and additional rights");

