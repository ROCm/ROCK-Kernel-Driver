/*
 * VIA AGPGART routines. 
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
	{.mask = 0x00000000, .type = 0}
};

static int __init via_generic_setup (struct pci_dev *pdev)
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
}

/*
 * The KT400 does magick to put the AGP bridge compliant with the same
 * standards version as the graphics card. If we haven't fallen into
 * 2.0 compatability mode, we abort, as this gets picked up by
 * via-agp3.o
 */

static int __init via_kt400_setup(struct pci_dev *pdev)
{
	u8 reg;
	pci_read_config_byte(pdev, VIA_AGPSEL, &reg);
	/* Check AGP 2.0 compatability mode. */
	if ((reg & (1<<1))==1) {
		via_generic_setup(pdev);
		return 0;
	}
	return -ENODEV;
}

static struct agp_device_ids via_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C597_0,
		.chipset	= VIA_VP3,
		.chipset_name	= "VP3",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C598_0,
		.chipset	= VIA_MVP3,
		.chipset_name	= "MVP3",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8501_0,
		.chipset	= VIA_MVP4,
		.chipset_name	= "MVP4",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C691,
		.chipset	= VIA_APOLLO_PRO,
		.chipset_name	= "Apollo Pro",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8371_0,
		.chipset	= VIA_APOLLO_KX133,
		.chipset_name	= "Apollo Pro KX133",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8633_0,
		.chipset	= VIA_APOLLO_PRO_266,
		.chipset_name	= "Apollo Pro 266",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8363_0,
		.chipset	= VIA_APOLLO_KT133,
		.chipset_name	= "Apollo Pro KT133",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8367_0,
		.chipset	= VIA_APOLLO_KT133,
		.chipset_name	= "Apollo Pro KT266",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8653_0,
		.chipset	= VIA_APOLLO_PRO,
		.chipset_name	= "Apollo Pro 266T",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8377_0,
		.chipset	= VIA_APOLLO_KT400,
		.chipset_name	= "Apollo Pro KT400",
		.chipset_setup	= via_kt400_setup,
	},
	{
		/* VIA ProSavage PM133 (Apollo Pro133A chipset with S3 Savage4) */
		.device_id	= PCI_DEVICE_ID_VIA_82C694X_0,
		.chipset	= VIA_VT8605,
		.chipset_name	= "Apollo ProSavage PM133"
	},
	{ }, /* dummy final entry, always present */
};


/* scan table above for supported devices */
static int __init agp_lookup_host_bridge (struct pci_dev *pdev)
{
	int j=0;
	struct agp_device_ids *devs;
	
	devs = via_agp_device_ids;

	while (devs[j].chipset_name != NULL) {
		if (pdev->device == devs[j].device_id) {
			printk (KERN_INFO PFX "Detected VIA %s chipset\n", devs[j].chipset_name);
			agp_bridge.type = devs[j].chipset;

			if (devs[j].chipset_setup != NULL)
				return devs[j].chipset_setup(pdev);
			else
				return via_generic_setup(pdev);
		}
		j++;
	}

	/* try init anyway, if user requests it */
	if (agp_try_unsupported) {
		printk(KERN_WARNING PFX "Trying generic VIA routines"
		       " for device id: %04x\n", pdev->device);
		agp_bridge.type = VIA_GENERIC;
		return via_generic_setup(pdev);
	}

	printk(KERN_ERR PFX "Unsupported VIA chipset (device id: %04x),"
		" you might want to try agp_try_unsupported=1.\n", pdev->device);
	return -ENODEV;
}


static int __init agp_via_probe (struct pci_dev *dev, const struct pci_device_id *ent)
{
	u8 cap_ptr = 0;

	cap_ptr = pci_find_capability(dev, PCI_CAP_ID_AGP);
	if (cap_ptr == 0)
		return -ENODEV;

	/* probe for known chipsets */
	if (agp_lookup_host_bridge (dev) != -ENODEV) {
		agp_bridge.dev = dev;
		agp_bridge.capndx = cap_ptr;
		/* Fill in the mode register */
		pci_read_config_dword(agp_bridge.dev, agp_bridge.capndx+4, &agp_bridge.mode);
		agp_register_driver(dev);
		return 0;
	}
	return -ENODEV;	
}

static struct pci_device_id agp_via_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_ANY_ID,
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
MODULE_LICENSE("GPL and additional rights");
