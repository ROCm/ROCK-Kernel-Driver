/*
 * SiS AGPGART routines. 
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

static int sis_fetch_size(void)
{
	u8 temp_size;
	int i;
	struct aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge->dev, SIS_APSIZE, &temp_size);
	values = A_SIZE_8(agp_bridge->driver->aperture_sizes);
	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if ((temp_size == values[i].size_value) ||
		    ((temp_size & ~(0x03)) ==
		     (values[i].size_value & ~(0x03)))) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);

			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static void sis_tlbflush(struct agp_memory *mem)
{
	pci_write_config_byte(agp_bridge->dev, SIS_TLBFLUSH, 0x02);
}

static int sis_configure(void)
{
	u32 temp;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);
	pci_write_config_byte(agp_bridge->dev, SIS_TLBCNTRL, 0x05);
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	pci_write_config_dword(agp_bridge->dev, SIS_ATTBASE,
			       agp_bridge->gatt_bus_addr);
	pci_write_config_byte(agp_bridge->dev, SIS_APSIZE,
			      current_size->size_value);
	return 0;
}

static void sis_cleanup(void)
{
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, SIS_APSIZE,
			      (previous_size->size_value & ~(0x03)));
}

static struct aper_size_info_8 sis_generic_sizes[7] =
{
	{256, 65536, 6, 99},
	{128, 32768, 5, 83},
	{64, 16384, 4, 67},
	{32, 8192, 3, 51},
	{16, 4096, 2, 35},
	{8, 2048, 1, 19},
	{4, 1024, 0, 3}
};

struct agp_bridge_driver sis_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes 	= sis_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= sis_configure,
	.fetch_size		= sis_fetch_size,
	.cleanup		= sis_cleanup,
	.tlb_flush		= sis_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= NULL,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
};

static struct agp_device_ids sis_agp_device_ids[] __devinitdata =
{
	{
		.device_id	= PCI_DEVICE_ID_SI_530,
		.chipset_name	= "530",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_540,
		.chipset_name	= "540",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_550,
		.chipset_name	= "550",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_620,
		.chipset_name	= "620",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_630,
		.chipset_name	= "630",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_645,
		.chipset_name	= "645",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_646,
		.chipset_name	= "646",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_648,
		.chipset_name	= "648",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_650,
		.chipset_name	= "650",
	},
	{
		.device_id  = PCI_DEVICE_ID_SI_651,
		.chipset_name   = "651",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_655,
		.chipset_name	= "655",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_661,
		.chipset_name	= "661",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_730,
		.chipset_name	= "730",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_735,
		.chipset_name	= "735",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_740,
		.chipset_name	= "740",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_741,
		.chipset_name	= "741",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_745,
		.chipset_name	= "745",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_746,
		.chipset_name	= "746",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_760,
		.chipset_name	= "760",
	},
	{ }, /* dummy final entry, always present */
};

static int __devinit agp_sis_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = sis_agp_device_ids;
	struct agp_bridge_data *bridge;
	u8 cap_ptr;
	int j;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	/* probe for known chipsets */
	for (j = 0; devs[j].chipset_name; j++) {
		if (pdev->device == devs[j].device_id) {
			printk(KERN_INFO PFX "Detected SiS %s chipset\n",
					devs[j].chipset_name);
			goto found;
		}
	}

	printk(KERN_ERR PFX "Unsupported SiS chipset (device id: %04x)\n",
		    pdev->device);
	return -ENODEV;

found:
	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->driver = &sis_driver;
	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(pdev,
			bridge->capndx+PCI_AGP_STATUS,
			&bridge->mode);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_sis_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_sis_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_SI,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_sis_pci_table);

static struct pci_driver agp_sis_pci_driver = {
	.name		= "agpgart-sis",
	.id_table	= agp_sis_pci_table,
	.probe		= agp_sis_probe,
	.remove		= agp_sis_remove,
};

static int __init agp_sis_init(void)
{
	return pci_module_init(&agp_sis_pci_driver);
}

static void __exit agp_sis_cleanup(void)
{
	pci_unregister_driver(&agp_sis_pci_driver);
}

module_init(agp_sis_init);
module_exit(agp_sis_cleanup);

MODULE_LICENSE("GPL and additional rights");
