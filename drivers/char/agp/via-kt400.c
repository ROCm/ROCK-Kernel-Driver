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

static int agp_try_unsupported __initdata = 0;

static int via_fetch_size(void)
{
	return 0;
}

static int via_configure(void)
{
	return 0;
}

static void via_cleanup(void)
{
}

static void via_tlbflush(agp_memory * mem)
{
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
	{.mask = 0x00000000, .type = 0}
};


static void __init via_kt400_enable(u32 mode)
{
	if ((agp_generic_agp_3_0_enable(mode))==FALSE)
		printk (KERN_INFO PFX "agp_generic_agp_3_0_enable() failed\n");
}


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
	if ((reg & (1<<1))==1)
		return -ENODEV;

	printk (KERN_INFO PFX "Detected VIA KT400 AGP3 chipset\n");

	agp_bridge.dev = dev;
	agp_bridge.type = VIA_APOLLO_KT400_3;
	agp_bridge.capndx = cap_ptr;
	agp_bridge.masks = via_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) via_generic_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.agp_enable = via_kt400_enable;
	agp_bridge.configure = via_configure;
	agp_bridge.fetch_size = via_fetch_size;
	agp_bridge.cleanup = via_cleanup;
	agp_bridge.tlb_flush = via_tlbflush;
	agp_bridge.mask_memory = via_mask_memory;
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

	/* Fill in the mode register */
	pci_read_config_dword(agp_bridge.dev, agp_bridge.capndx+4, &agp_bridge.mode);

	agp_register_driver(dev);
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
		agp_bridge.type = NOT_SUPPORTED;

	return ret_val;
}

static void __exit agp_via_cleanup(void)
{
	agp_unregister_driver();
	pci_unregister_driver(&agp_via_pci_driver);
}

module_init(agp_via_init);
module_exit(agp_via_cleanup);

MODULE_PARM(agp_try_unsupported, "1i");
MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_LICENSE("GPL and additional rights");

