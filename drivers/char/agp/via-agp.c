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

	values = A_SIZE_8(agp_bridge->driver->aperture_sizes);
	pci_read_config_byte(agp_bridge->dev, VIA_APSIZE, &temp);
	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
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
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);
	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, VIA_APSIZE,
			      current_size->size_value);
	/* address to map too */
	pci_read_config_dword(agp_bridge->dev, VIA_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* GART control register */
	pci_write_config_dword(agp_bridge->dev, VIA_GARTCTRL, 0x0000000f);

	/* attbase - aperture GATT base */
	pci_write_config_dword(agp_bridge->dev, VIA_ATTBASE,
			    (agp_bridge->gatt_bus_addr & 0xfffff000) | 3);
	return 0;
}


static void via_cleanup(void)
{
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, VIA_APSIZE,
			      previous_size->size_value);
	/* Do not disable by writing 0 to VIA_ATTBASE, it screws things up
	 * during reinitialization.
	 */
}


static void via_tlbflush(agp_memory * mem)
{
	pci_write_config_dword(agp_bridge->dev, VIA_GARTCTRL, 0x0000008f);
	pci_write_config_dword(agp_bridge->dev, VIA_GARTCTRL, 0x0000000f);
}


static unsigned long via_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge->driver->masks[0].mask;
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


static int via_fetch_size_agp3(void)
{
	int i;
	u16 temp;
	struct aper_size_info_16 *values;

	values = A_SIZE_16(agp_bridge->driver->aperture_sizes);
	pci_read_config_word(agp_bridge->dev, VIA_AGP3_APSIZE, &temp);
	temp &= 0xfff;

	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size =
				agp_bridge->current_size = (void *) (values + i);
			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}
	return 0;
}


static int via_configure_agp3(void)
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


static void via_cleanup_agp3(void)
{
	struct aper_size_info_16 *previous_size;

	previous_size = A_SIZE_16(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, VIA_APSIZE, previous_size->size_value);
}


static void via_tlbflush_agp3(agp_memory * mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, temp & ~(1<<7));
	pci_write_config_dword(agp_bridge->dev, VIA_AGP3_GARTCTRL, temp);
}


static struct aper_size_info_16 via_generic_agp3_sizes[11] =
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

struct agp_bridge_driver via_agp3_driver = {
	.owner			= THIS_MODULE,
	.masks			= via_generic_masks,
	.aperture_sizes		= via_generic_agp3_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 10,
	.configure		= via_configure_agp3,
	.fetch_size		= via_fetch_size_agp3,
	.cleanup		= via_cleanup_agp3,
	.tlb_flush		= via_tlbflush_agp3,
	.mask_memory		= via_mask_memory,
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
	.suspend		= agp_generic_suspend,
	.resume			= agp_generic_resume,
};

struct agp_bridge_driver via_driver = {
	.owner			= THIS_MODULE,
	.masks			= via_generic_masks,
	.aperture_sizes		= via_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= via_configure,
	.fetch_size		= via_fetch_size,
	.cleanup		= via_cleanup,
	.tlb_flush		= via_tlbflush,
	.mask_memory		= via_mask_memory,
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
	.suspend		= agp_generic_suspend,
	.resume			= agp_generic_resume,
};

static struct agp_device_ids via_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C597_0,
		.chipset_name	= "VP3",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_82C598_0,
		.chipset_name	= "MVP3",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_8501_0,
		.chipset_name	= "MVP4",
	},

	/* VT8601 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8601_0,
		.chipset_name	= "PLE133 ProMedia",
	},

	/* VT82C693A / VT28C694T */
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C691,
		.chipset_name	= "Apollo Pro 133",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_8371_0,
		.chipset_name	= "Apollo Pro KX133",
	},

	/* VT8633 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8633_0,
		.chipset_name	= "Apollo Pro 266",
	},

	/* VT8361 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8361,
		.chipset_name	= "Apollo KLE133",
	},

	/* VT8365 / VT8362 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8363_0,
		.chipset_name	= "Apollo Pro KT133/KM133/TwisterK",
	},

	/* VT8753A */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8753_0,
		.chipset_name	= "P4X266",
	},

	/* VT8366 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8367_0,
		.chipset_name	= "Apollo Pro KT266/KT333",
	},

	/* VT8633 (for CuMine/ Celeron) */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8653_0,
		.chipset_name	= "Apollo Pro 266T",
	},

	/* KM266 / PM266 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_KM266,
		.chipset_name	= "KM266/PM266",
	},

	/* CLE266 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_CLE266,
		.chipset_name	= "CLE266",
	},

	{
		.device_id	= PCI_DEVICE_ID_VIA_8377_0,
		.chipset_name	= "Apollo Pro KT400",
	},

	/* VT8604 / VT8605 / VT8603 / TwisterT
	 * (Apollo Pro133A chipset with S3 Savage4) */
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C694X_0,
		.chipset_name	= "Apollo ProSavage PM133/PL133/PN133/Twister"
	},

	/* VT8752*/
	{
		.device_id	= PCI_DEVICE_ID_VIA_8752,
		.chipset_name	= "ProSavage DDR P4M266",
	},

	/* KN266/PN266 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_KN266,
		.chipset_name	= "KN266/PN266",
	},

	/* VT8754 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_8754,
		.chipset_name	= "Apollo P4X333/P4X400"
	},

	/* P4N333 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_P4N333,
		.chipset_name	= "P4N333",
	},

	/* P4X600 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_P4X600,
		.chipset_name	= "P4X600",
	},

	/* KM400 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_KM400,
		.chipset_name	= "KM400",
	},

	/* P4M400 */
	{
		.device_id	= PCI_DEVICE_ID_VIA_P4M400,
		.chipset_name	= "P4M400",
	},

	{ }, /* dummy final entry, always present */
};

static int __init agp_via_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = via_agp_device_ids;
	struct agp_bridge_data *bridge;
	int j = 0;
	u8 cap_ptr, reg;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	/* probe for known chipsets */
	for (j = 0; devs[j].chipset_name; j++) {
		if (pdev->device == devs[j].device_id) {
			printk (KERN_INFO PFX "Detected VIA %s chipset\n",
					devs[j].chipset_name);
			goto found;
		}
	}

	if (agp_try_unsupported) {
		printk(KERN_ERR PFX 
		    "Unsupported VIA chipset (device id: %04x),"
		    " you might want to try agp_try_unsupported=1.\n",
		    pdev->device);
		return -ENODEV;
	}

	printk(KERN_WARNING PFX "Trying generic VIA routines"
	       " for device id: %04x\n", pdev->device);

found:
	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_8367_0:
		/*
		 * Garg, there are KT400s with KT266 IDs.
		 */
		/* Is there a KT400 subsystem ? */
		if (pdev->subsystem_device != PCI_DEVICE_ID_VIA_8377_0)
			break;
		
		printk(KERN_INFO PFX "Found KT400 in disguise as a KT266.\n");
		/*FALLTHROUGH*/
	case PCI_DEVICE_ID_VIA_8377_0:
		/*
		 * The KT400 does magick to put the AGP bridge compliant
		 * with the same standards version as the graphics card.
		 */
		pci_read_config_byte(pdev, VIA_AGPSEL, &reg);
		/* Check AGP 2.0 compatibility mode. */
		if ((reg & (1<<1))==0) {
			bridge->driver = &via_agp3_driver;
			break;
		}
		/*FALLTHROUGH*/
	default:
		bridge->driver = &via_driver;
		break;
	}


	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(pdev,
			bridge->capndx+PCI_AGP_STATUS, &bridge->mode);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_via_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
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
	.remove		= agp_via_remove,
};


static int __init agp_via_init(void)
{
	return pci_module_init(&agp_via_pci_driver);
}

static void __exit agp_via_cleanup(void)
{
	pci_unregister_driver(&agp_via_pci_driver);
}

module_init(agp_via_init);
module_exit(agp_via_cleanup);

MODULE_PARM(agp_try_unsupported, "1i");
MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
