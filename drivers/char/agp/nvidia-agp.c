/*
 * Nvidia AGPGART routines.
 * Based upon a 2.4 agpgart diff by the folks from NVIDIA, and hacked up
 * to work in 2.5 by Dave Jones <davej@codemonkey.org.uk>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <linux/gfp.h>
#include <linux/page-flags.h>
#include <linux/mm.h>
#include "agp.h"

static int agp_try_unsupported __initdata = 0;

static struct _nvidia_private {
	struct pci_dev *dev_1;
	struct pci_dev *dev_2;
	struct pci_dev *dev_3;
	volatile u32 *aperture;
	int num_active_entries;
	off_t pg_offset;
} nvidia_private;


static int nvidia_fetch_size(void)
{
	int i;
	u8 size_value;
	struct aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge->dev, NVIDIA_0_APSIZE, &size_value);
	size_value &= 0x0f;
	values = A_SIZE_8(agp_bridge->aperture_sizes);

	for (i = 0; i < agp_bridge->num_aperture_sizes; i++) {
		if (size_value == values[i].size_value) {
			agp_bridge->previous_size =
				agp_bridge->current_size = (void *) (values + i);
			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}


static int nvidia_configure(void)
{
	int i, num_dirs;
	u32 apbase, aplimit;
	struct aper_size_info_8 *current_size;
	u32 temp;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, NVIDIA_0_APSIZE,
		current_size->size_value);

    /* address to map to */
	pci_read_config_dword(agp_bridge->dev, NVIDIA_0_APBASE, &apbase);
	apbase &= PCI_BASE_ADDRESS_MEM_MASK;
	agp_bridge->gart_bus_addr = apbase;
	aplimit = apbase + (current_size->size * 1024 * 1024) - 1;
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_APBASE, apbase);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_APLIMIT, aplimit);
	pci_write_config_dword(nvidia_private.dev_3, NVIDIA_3_APBASE, apbase);
	pci_write_config_dword(nvidia_private.dev_3, NVIDIA_3_APLIMIT, aplimit);

	/* directory size is 64k */
	num_dirs = current_size->size / 64;
	nvidia_private.num_active_entries = current_size->num_entries;
	nvidia_private.pg_offset = 0;
	if (num_dirs == 0) {
		num_dirs = 1;
		nvidia_private.num_active_entries /= (64 / current_size->size);
		nvidia_private.pg_offset = (apbase & (64 * 1024 * 1024 - 1) &
			~(current_size->size * 1024 * 1024 - 1)) / PAGE_SIZE;
	}

	/* attbase */
	for(i = 0; i < 8; i++) {
		pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_ATTBASE(i),
			(agp_bridge->gatt_bus_addr + (i % num_dirs) * 64 * 1024) | 1);
	}

	/* gtlb control */
	pci_read_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, &temp);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, temp | 0x11);

	/* gart control */
	pci_read_config_dword(agp_bridge->dev, NVIDIA_0_APSIZE, &temp);
	pci_write_config_dword(agp_bridge->dev, NVIDIA_0_APSIZE, temp | 0x100);

	/* map aperture */
	nvidia_private.aperture =
		(volatile u32 *) ioremap(apbase, 33 * PAGE_SIZE);

	return 0;
}

static void nvidia_cleanup(void)
{
	struct aper_size_info_8 *previous_size;
	u32 temp;

	/* gart control */
	pci_read_config_dword(agp_bridge->dev, NVIDIA_0_APSIZE, &temp);
	pci_write_config_dword(agp_bridge->dev, NVIDIA_0_APSIZE, temp & ~(0x100));

	/* gtlb control */
	pci_read_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, &temp);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, temp & ~(0x11));

	/* unmap aperture */
	iounmap((void *) nvidia_private.aperture);

	/* restore previous aperture size */
	previous_size = A_SIZE_8(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, NVIDIA_0_APSIZE,
		previous_size->size_value);
}


static unsigned long nvidia_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */
	return addr | agp_bridge->masks[0].mask;
}

#if 0
extern int agp_memory_reserved;

static int nvidia_insert_memory(agp_memory * mem, off_t pg_start, int type)
{
	int i, j;
	
	if ((type != 0) || (mem->type != 0))
		return -EINVAL;
	
	if ((pg_start + mem->page_count) >
		(nvidia_private.num_active_entries - agp_memory_reserved/PAGE_SIZE))
		return -EINVAL;
	
	for(j = pg_start; j < (pg_start + mem->page_count); j++) {
		if (!PGE_EMPTY(agp_bridge, agp_bridge->gatt_table[nvidia_private.pg_offset + j]))
			return -EBUSY;
	}

	if (mem->is_flushed == FALSE) {
		global_cache_flush();
		mem->is_flushed = TRUE;
	}
	for (i = 0, j = pg_start; i < mem->page_count; i++, j++)
		agp_bridge->gatt_table[nvidia_private.pg_offset + j] = mem->memory[i];

	agp_bridge->tlb_flush(mem);
	return 0;
}

static int nvidia_remove_memory(agp_memory * mem, off_t pg_start, int type)
{
	int i;

	if ((type != 0) || (mem->type != 0))
		return -EINVAL;
	
	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		agp_bridge->gatt_table[nvidia_private.pg_offset + i] =
		    (unsigned long) agp_bridge->scratch_page;
	}

	agp_bridge->tlb_flush(mem);
	return 0;
}
#endif


static void nvidia_tlbflush(agp_memory * mem)
{
	int i;
	unsigned long end;
	u32 wbc_reg, wbc_mask, temp;

	/* flush chipset */
	switch (agp_bridge->type) {
	case NVIDIA_NFORCE:
		wbc_mask = 0x00010000;
		break;
	case NVIDIA_NFORCE2:
		wbc_mask = 0x80000000;
		break;
	default:
		wbc_mask = 0;
		break;
	}

	if (wbc_mask) {
		pci_read_config_dword(nvidia_private.dev_1, NVIDIA_1_WBC, &wbc_reg);
		wbc_reg |= wbc_mask;
		pci_write_config_dword(nvidia_private.dev_1, NVIDIA_1_WBC, wbc_reg);

		end = jiffies + 3*HZ;
		do {
			pci_read_config_dword(nvidia_private.dev_1, NVIDIA_1_WBC, &wbc_reg);
			if ((signed)(end - jiffies) <= 0)
				printk(KERN_ERR "TLB flush took more than 3 seconds.\n");
		} while (wbc_reg & wbc_mask);
	}

	/* flush TLB entries */
	for(i = 0; i < 32 + 1; i++)
		temp = nvidia_private.aperture[i * PAGE_SIZE / sizeof(u32)];
	for(i = 0; i < 32 + 1; i++)
		temp = nvidia_private.aperture[i * PAGE_SIZE / sizeof(u32)];
}


static struct aper_size_info_8 nvidia_generic_sizes[5] =
{
	{512, 131072, 7, 0},
	{256, 65536, 6, 8},
	{128, 32768, 5, 12},
	{64, 16384, 4, 14},
	/* The 32M mode still requires a 64k gatt */
	{32, 16384, 4, 15}
};


static struct gatt_mask nvidia_generic_masks[] =
{
	{0x00000001, 0}
};


struct agp_bridge_data nvidia_bridge = {
	.masks			= nvidia_generic_masks,
	.aperture_sizes		= (void *) nvidia_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 5,
	.dev_private_data	= (void *) &nvidia_private,
	.needs_scratch_page	= FALSE,
	.configure		= nvidia_configure,
	.fetch_size		= nvidia_fetch_size,
	.cleanup		= nvidia_cleanup,
	.tlb_flush		= nvidia_tlbflush,
	.mask_memory		= nvidia_mask_memory,
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
	.cant_use_aperture	= 0,
};

struct agp_device_ids nvidia_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_NVIDIA_NFORCE,
		.chipset	= NVIDIA_NFORCE,
		.chipset_name	= "nForce",
	},
	{
		.device_id	= PCI_DEVICE_ID_NVIDIA_NFORCE2,
		.chipset	= NVIDIA_NFORCE2,
		.chipset_name	= "nForce2",
	},
	{ }, /* dummy final entry, always present */
};


static struct agp_driver nvidia_agp_driver = {
	.owner = THIS_MODULE,
};


/* Supported Device Scanning routine */
static int __init agp_nvidia_probe(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = nvidia_agp_device_ids;
	u8 cap_ptr;
	int j;

	nvidia_private.dev_1 = pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(0, 1));
	nvidia_private.dev_2 = pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(0, 2));
	nvidia_private.dev_3 = pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(30, 0));
	
	if((nvidia_private.dev_1 == NULL) ||
		(nvidia_private.dev_2 == NULL) ||
		(nvidia_private.dev_3 == NULL)) {
		printk(KERN_INFO PFX "agpgart: Detected an NVIDIA "
			"nForce/nForce2 chipset, but could not find "
			"the secondary devices.\n");
		return -ENODEV;
	}

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	for (j = 0; devs[j].chipset_name; j++) {
		if (pdev->device == devs[j].device_id) {
			printk (KERN_INFO PFX "Detected NVIDIA %s chipset\n", devs[j].chipset_name);
			nvidia_bridge.type = devs[j].chipset;
			goto found;
		}
	}

	if (!agp_try_unsupported) {
		printk(KERN_ERR PFX "Unsupported NVIDIA chipset (device id: %04x),"
		    " you might want to try agp_try_unsupported=1.\n", pdev->device);
		return -ENODEV;
	}

	printk(KERN_WARNING PFX "Trying generic NVIDIA routines"
	       " for device id: %04x\n", pdev->device);
	nvidia_bridge.type = NVIDIA_GENERIC;

found:
	nvidia_bridge.dev = pdev;
	nvidia_bridge.capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(pdev,	nvidia_bridge.capndx+PCI_AGP_STATUS, &nvidia_bridge.mode);

	memcpy(agp_bridge, &nvidia_bridge, sizeof(struct agp_bridge_data));

	nvidia_agp_driver.dev = pdev;
	agp_register_driver(&nvidia_agp_driver);
	return 0;
}


static struct pci_device_id agp_nvidia_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_NVIDIA,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_nvidia_pci_table);

static struct __initdata pci_driver agp_nvidia_pci_driver = {
	.name		= "agpgart-nvidia",
	.id_table	= agp_nvidia_pci_table,
	.probe		= agp_nvidia_probe,
};

static int __init agp_nvidia_init(void)
{
	return pci_module_init(&agp_nvidia_pci_driver);
}

static void __exit agp_nvidia_cleanup(void)
{
	agp_unregister_driver(&nvidia_agp_driver);
	pci_unregister_driver(&agp_nvidia_pci_driver);
}

module_init(agp_nvidia_init);
module_exit(agp_nvidia_cleanup);

MODULE_PARM(agp_try_unsupported, "1i");
MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("NVIDIA Corporation");

