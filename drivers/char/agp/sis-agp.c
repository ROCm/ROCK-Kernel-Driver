/*
 * SiS AGPGART routines. 
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

static int agp_try_unsupported __initdata = 0;

static int sis_fetch_size(void)
{
	u8 temp_size;
	int i;
	struct aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge->dev, SIS_APSIZE, &temp_size);
	values = A_SIZE_8(agp_bridge->aperture_sizes);
	for (i = 0; i < agp_bridge->num_aperture_sizes; i++) {
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

static void sis_tlbflush(agp_memory * mem)
{
	pci_write_config_byte(agp_bridge->dev, SIS_TLBFLUSH, 0x02);
}

static int sis_configure(void)
{
	u32 temp;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);
	pci_write_config_byte(agp_bridge->dev, SIS_TLBCNTRL, 0x05);
	pci_read_config_dword(agp_bridge->dev, SIS_APBASE, &temp);
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

static unsigned long sis_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge->masks[0].mask;
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

static struct gatt_mask sis_generic_masks[] =
{
	{.mask = 0x00000000, .type = 0}
};

static int __init sis_generic_setup (struct pci_dev *pdev)
{
	agp_bridge->masks = sis_generic_masks;
	agp_bridge->aperture_sizes = (void *) sis_generic_sizes;
	agp_bridge->size_type = U8_APER_SIZE;
	agp_bridge->num_aperture_sizes = 7;
	agp_bridge->dev_private_data = NULL;
	agp_bridge->needs_scratch_page = FALSE;
	agp_bridge->configure = sis_configure;
	agp_bridge->fetch_size = sis_fetch_size;
	agp_bridge->cleanup = sis_cleanup;
	agp_bridge->tlb_flush = sis_tlbflush;
	agp_bridge->mask_memory = sis_mask_memory;
	agp_bridge->agp_enable = agp_generic_enable;
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

	return 0;
}

struct agp_device_ids sis_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_SI_740,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "740",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_650,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "650",
	},
	{
		.device_id  = PCI_DEVICE_ID_SI_651,
		.chipset    = SIS_GENERIC,
		.chipset_name   = "651",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_645,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "645",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_646,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "646",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_735,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "735",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_745,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "745",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_730,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "730",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_630,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "630",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_540,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "540",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_620,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "620",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_530,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "530",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_550,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "550",
	},
	{ }, /* dummy final entry, always present */
};

/* scan table above for supported devices */
static int __init agp_lookup_host_bridge (struct pci_dev *pdev)
{
	int j=0;
	struct agp_device_ids *devs;
	
	devs = sis_agp_device_ids;

	while (devs[j].chipset_name != NULL) {
		if (pdev->device == devs[j].device_id) {
			printk (KERN_INFO PFX "Detected SiS %s chipset\n",
				devs[j].chipset_name);
			agp_bridge->type = devs[j].chipset;

			if (devs[j].chipset_setup != NULL)
				return devs[j].chipset_setup(pdev);
			else
				return sis_generic_setup(pdev);
		}
		j++;
	}

	/* try init anyway, if user requests it */
	if (agp_try_unsupported) {
		printk(KERN_WARNING PFX "Trying generic SiS routines"
		       " for device id: %04x\n", pdev->device);
		agp_bridge->type = SIS_GENERIC;
		return sis_generic_setup(pdev);
	}

	printk(KERN_ERR PFX "Unsupported SiS chipset (device id: %04x),"
		" you might want to try agp_try_unsupported=1.\n", pdev->device);
	return -ENODEV;
}

static struct agp_driver sis_agp_driver = {
	.owner = THIS_MODULE,
};

static int __init agp_sis_probe (struct pci_dev *dev, const struct pci_device_id *ent)
{
	u8 cap_ptr = 0;

	cap_ptr = pci_find_capability(dev, PCI_CAP_ID_AGP);
	if (cap_ptr == 0)
		return -ENODEV;

	/* probe for known chipsets */
	if (agp_lookup_host_bridge(dev) != -ENODEV) {
		agp_bridge->dev = dev;
		agp_bridge->capndx = cap_ptr;
		/* Fill in the mode register */
		pci_read_config_dword(agp_bridge->dev, agp_bridge->capndx+PCI_AGP_STATUS, &agp_bridge->mode);
		sis_agp_driver.dev = dev;
		agp_register_driver(&sis_agp_driver);
		return 0;
	}
	return -ENODEV;
}

static struct pci_device_id agp_sis_pci_table[] __initdata = {
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

static struct __initdata pci_driver agp_sis_pci_driver = {
	.name		= "agpgart-sis",
	.id_table	= agp_sis_pci_table,
	.probe		= agp_sis_probe,
};

static int __init agp_sis_init(void)
{
	int ret_val;

	ret_val = pci_module_init(&agp_sis_pci_driver);
	if (ret_val)
		agp_bridge->type = NOT_SUPPORTED;

	return ret_val;
}

static void __exit agp_sis_cleanup(void)
{
	agp_unregister_driver(&sis_agp_driver);
	pci_unregister_driver(&agp_sis_pci_driver);
}

module_init(agp_sis_init);
module_exit(agp_sis_cleanup);

MODULE_PARM(agp_try_unsupported, "1i");
MODULE_LICENSE("GPL and additional rights");
