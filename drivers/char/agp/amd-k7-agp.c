/*
 * AMD K7 AGPGART routines.
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

struct amd_page_map {
	unsigned long *real;
	unsigned long *remapped;
};

static struct _amd_irongate_private {
	volatile u8 *registers;
	struct amd_page_map **gatt_pages;
	int num_tables;
} amd_irongate_private;

static int amd_create_page_map(struct amd_page_map *page_map)
{
	int i;

	page_map->real = (unsigned long *) __get_free_page(GFP_KERNEL);
	if (page_map->real == NULL) {
		return -ENOMEM;
	}
	SetPageReserved(virt_to_page(page_map->real));
	CACHE_FLUSH();
	page_map->remapped = ioremap_nocache(virt_to_phys(page_map->real), 
					    PAGE_SIZE);
	if (page_map->remapped == NULL) {
		ClearPageReserved(virt_to_page(page_map->real));
		free_page((unsigned long) page_map->real);
		page_map->real = NULL;
		return -ENOMEM;
	}
	CACHE_FLUSH();

	for(i = 0; i < PAGE_SIZE / sizeof(unsigned long); i++) {
		page_map->remapped[i] = agp_bridge->scratch_page;
	}

	return 0;
}

static void amd_free_page_map(struct amd_page_map *page_map)
{
	iounmap(page_map->remapped);
	ClearPageReserved(virt_to_page(page_map->real));
	free_page((unsigned long) page_map->real);
}

static void amd_free_gatt_pages(void)
{
	int i;
	struct amd_page_map **tables;
	struct amd_page_map *entry;

	tables = amd_irongate_private.gatt_pages;
	for(i = 0; i < amd_irongate_private.num_tables; i++) {
		entry = tables[i];
		if (entry != NULL) {
			if (entry->real != NULL) {
				amd_free_page_map(entry);
			}
			kfree(entry);
		}
	}
	kfree(tables);
}

static int amd_create_gatt_pages(int nr_tables)
{
	struct amd_page_map **tables;
	struct amd_page_map *entry;
	int retval = 0;
	int i;

	tables = kmalloc((nr_tables + 1) * sizeof(struct amd_page_map *), 
			 GFP_KERNEL);
	if (tables == NULL) {
		return -ENOMEM;
	}
	memset(tables, 0, sizeof(struct amd_page_map *) * (nr_tables + 1));
	for (i = 0; i < nr_tables; i++) {
		entry = kmalloc(sizeof(struct amd_page_map), GFP_KERNEL);
		if (entry == NULL) {
			retval = -ENOMEM;
			break;
		}
		memset(entry, 0, sizeof(struct amd_page_map));
		tables[i] = entry;
		retval = amd_create_page_map(entry);
		if (retval != 0) break;
	}
	amd_irongate_private.num_tables = nr_tables;
	amd_irongate_private.gatt_pages = tables;

	if (retval != 0) amd_free_gatt_pages();

	return retval;
}

/* Since we don't need contigious memory we just try
 * to get the gatt table once
 */

#define GET_PAGE_DIR_OFF(addr) (addr >> 22)
#define GET_PAGE_DIR_IDX(addr) (GET_PAGE_DIR_OFF(addr) - \
	GET_PAGE_DIR_OFF(agp_bridge->gart_bus_addr))
#define GET_GATT_OFF(addr) ((addr & 0x003ff000) >> 12) 
#define GET_GATT(addr) (amd_irongate_private.gatt_pages[\
	GET_PAGE_DIR_IDX(addr)]->remapped)

static int amd_create_gatt_table(void)
{
	struct aper_size_info_lvl2 *value;
	struct amd_page_map page_dir;
	unsigned long addr;
	int retval;
	u32 temp;
	int i;

	value = A_SIZE_LVL2(agp_bridge->current_size);
	retval = amd_create_page_map(&page_dir);
	if (retval != 0) {
		return retval;
	}

	retval = amd_create_gatt_pages(value->num_entries / 1024);
	if (retval != 0) {
		amd_free_page_map(&page_dir);
		return retval;
	}

	agp_bridge->gatt_table_real = (u32 *)page_dir.real;
	agp_bridge->gatt_table = (u32 *)page_dir.remapped;
	agp_bridge->gatt_bus_addr = virt_to_phys(page_dir.real);

	/* Get the address for the gart region.
	 * This is a bus address even on the alpha, b/c its
	 * used to program the agp master not the cpu
	 */

	pci_read_config_dword(agp_bridge->dev, AMD_APBASE, &temp);
	addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	agp_bridge->gart_bus_addr = addr;

	/* Calculate the agp offset */
	for(i = 0; i < value->num_entries / 1024; i++, addr += 0x00400000) {
		page_dir.remapped[GET_PAGE_DIR_OFF(addr)] =
			virt_to_phys(amd_irongate_private.gatt_pages[i]->real);
		page_dir.remapped[GET_PAGE_DIR_OFF(addr)] |= 0x00000001;
	}

	return 0;
}

static int amd_free_gatt_table(void)
{
	struct amd_page_map page_dir;
   
	page_dir.real = (unsigned long *)agp_bridge->gatt_table_real;
	page_dir.remapped = (unsigned long *)agp_bridge->gatt_table;

	amd_free_gatt_pages();
	amd_free_page_map(&page_dir);
	return 0;
}

static int amd_irongate_fetch_size(void)
{
	int i;
	u32 temp;
	struct aper_size_info_lvl2 *values;

	pci_read_config_dword(agp_bridge->dev, AMD_APSIZE, &temp);
	temp = (temp & 0x0000000e);
	values = A_SIZE_LVL2(agp_bridge->aperture_sizes);
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

static int amd_irongate_configure(void)
{
	struct aper_size_info_lvl2 *current_size;
	u32 temp;
	u16 enable_reg;

	current_size = A_SIZE_LVL2(agp_bridge->current_size);

	/* Get the memory mapped registers */
	pci_read_config_dword(agp_bridge->dev, AMD_MMBASE, &temp);
	temp = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	amd_irongate_private.registers = (volatile u8 *) ioremap(temp, 4096);

	/* Write out the address of the gatt table */
	OUTREG32(amd_irongate_private.registers, AMD_ATTBASE,
		 agp_bridge->gatt_bus_addr);

	/* Write the Sync register */
	pci_write_config_byte(agp_bridge->dev, AMD_MODECNTL, 0x80);
   
   	/* Set indexing mode */
   	pci_write_config_byte(agp_bridge->dev, AMD_MODECNTL2, 0x00);

	/* Write the enable register */
	enable_reg = INREG16(amd_irongate_private.registers, AMD_GARTENABLE);
	enable_reg = (enable_reg | 0x0004);
	OUTREG16(amd_irongate_private.registers, AMD_GARTENABLE, enable_reg);

	/* Write out the size register */
	pci_read_config_dword(agp_bridge->dev, AMD_APSIZE, &temp);
	temp = (((temp & ~(0x0000000e)) | current_size->size_value)
		| 0x00000001);
	pci_write_config_dword(agp_bridge->dev, AMD_APSIZE, temp);

	/* Flush the tlb */
	OUTREG32(amd_irongate_private.registers, AMD_TLBFLUSH, 0x00000001);

	return 0;
}

static void amd_irongate_cleanup(void)
{
	struct aper_size_info_lvl2 *previous_size;
	u32 temp;
	u16 enable_reg;

	previous_size = A_SIZE_LVL2(agp_bridge->previous_size);

	enable_reg = INREG16(amd_irongate_private.registers, AMD_GARTENABLE);
	enable_reg = (enable_reg & ~(0x0004));
	OUTREG16(amd_irongate_private.registers, AMD_GARTENABLE, enable_reg);

	/* Write back the previous size and disable gart translation */
	pci_read_config_dword(agp_bridge->dev, AMD_APSIZE, &temp);
	temp = ((temp & ~(0x0000000f)) | previous_size->size_value);
	pci_write_config_dword(agp_bridge->dev, AMD_APSIZE, temp);
	iounmap((void *) amd_irongate_private.registers);
}

/*
 * This routine could be implemented by taking the addresses
 * written to the GATT, and flushing them individually.  However
 * currently it just flushes the whole table.  Which is probably
 * more efficent, since agp_memory blocks can be a large number of
 * entries.
 */

static void amd_irongate_tlbflush(agp_memory * temp)
{
	OUTREG32(amd_irongate_private.registers, AMD_TLBFLUSH, 0x00000001);
}

static unsigned long amd_irongate_mask_memory(unsigned long addr, int type)
{
	/* Only type 0 is supported by the irongate */

	return addr | agp_bridge->masks[0].mask;
}

static int amd_insert_memory(agp_memory * mem,
			     off_t pg_start, int type)
{
	int i, j, num_entries;
	unsigned long *cur_gatt;
	unsigned long addr;

	num_entries = A_SIZE_LVL2(agp_bridge->current_size)->num_entries;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}
	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}

	j = pg_start;
	while (j < (pg_start + mem->page_count)) {
		addr = (j * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		if (!PGE_EMPTY(cur_gatt[GET_GATT_OFF(addr)])) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = (j * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] =
			agp_bridge->mask_memory(mem->memory[i], mem->type);
	}
	agp_bridge->tlb_flush(mem);
	return 0;
}

static int amd_remove_memory(agp_memory * mem, off_t pg_start,
			     int type)
{
	int i;
	unsigned long *cur_gatt;
	unsigned long addr;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}
	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		addr = (i * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] = 
			(unsigned long) agp_bridge->scratch_page;
	}

	agp_bridge->tlb_flush(mem);
	return 0;
}

static struct aper_size_info_lvl2 amd_irongate_sizes[7] =
{
	{2048, 524288, 0x0000000c},
	{1024, 262144, 0x0000000a},
	{512, 131072, 0x00000008},
	{256, 65536, 0x00000006},
	{128, 32768, 0x00000004},
	{64, 16384, 0x00000002},
	{32, 8192, 0x00000000}
};

static struct gatt_mask amd_irongate_masks[] =
{
	{.mask = 0x00000001, .type = 0}
};

static int __init amd_irongate_setup (struct pci_dev *pdev)
{
	agp_bridge->masks = amd_irongate_masks;
	agp_bridge->aperture_sizes = (void *) amd_irongate_sizes;
	agp_bridge->size_type = LVL2_APER_SIZE;
	agp_bridge->num_aperture_sizes = 7;
	agp_bridge->dev_private_data = (void *) &amd_irongate_private;
	agp_bridge->needs_scratch_page = FALSE;
	agp_bridge->configure = amd_irongate_configure;
	agp_bridge->fetch_size = amd_irongate_fetch_size;
	agp_bridge->cleanup = amd_irongate_cleanup;
	agp_bridge->tlb_flush = amd_irongate_tlbflush;
	agp_bridge->mask_memory = amd_irongate_mask_memory;
	agp_bridge->agp_enable = agp_generic_enable;
	agp_bridge->cache_flush = global_cache_flush;
	agp_bridge->create_gatt_table = amd_create_gatt_table;
	agp_bridge->free_gatt_table = amd_free_gatt_table;
	agp_bridge->insert_memory = amd_insert_memory;
	agp_bridge->remove_memory = amd_remove_memory;
	agp_bridge->alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge->free_by_type = agp_generic_free_by_type;
	agp_bridge->agp_alloc_page = agp_generic_alloc_page;
	agp_bridge->agp_destroy_page = agp_generic_destroy_page;
	agp_bridge->suspend = agp_generic_suspend;
	agp_bridge->resume = agp_generic_resume;
	agp_bridge->cant_use_aperture = 0;
	return 0;
}

struct agp_device_ids amd_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_7006,
		.chipset	= AMD_IRONGATE,
		.chipset_name	= "Irongate",
	},
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_700E,
		.chipset	= AMD_761,
		.chipset_name	= "761",
	},
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_700C,
		.chipset	= AMD_762,
		.chipset_name	= "760MP",
	},
	{ }, /* dummy final entry, always present */
};


/* scan table above for supported devices */
static int __init agp_lookup_host_bridge (struct pci_dev *pdev)
{
	int j=0;
	struct agp_device_ids *devs;
	
	devs = amd_agp_device_ids;

	while (devs[j].chipset_name != NULL) {
		if (pdev->device == devs[j].device_id) {
			printk (KERN_INFO PFX "Detected AMD %s chipset\n", devs[j].chipset_name);
			agp_bridge->type = devs[j].chipset;

			if (devs[j].chipset_setup != NULL)
				return devs[j].chipset_setup(pdev);
			else
				return amd_irongate_setup(pdev);
		}
		j++;
	}

	/* try init anyway, if user requests it */
	if (agp_try_unsupported) {
		printk(KERN_WARNING PFX "Trying generic AMD routines"
		       " for device id: %04x\n", pdev->device);
		agp_bridge->type = AMD_GENERIC;
		return amd_irongate_setup(pdev);
	}

	printk(KERN_ERR PFX "Unsupported AMD chipset (device id: %04x),"
		" you might want to try agp_try_unsupported=1.\n", pdev->device);
	return -ENODEV;
}


static struct agp_driver amd_k7_agp_driver = {
	.owner = THIS_MODULE,
};

/* Supported Device Scanning routine */

static int __init agp_amdk7_probe (struct pci_dev *dev, const struct pci_device_id *ent)
{
	u8 cap_ptr = 0;

	cap_ptr = pci_find_capability(dev, PCI_CAP_ID_AGP);
	if (cap_ptr == 0)
		return -ENODEV;

	if (agp_lookup_host_bridge(dev) != -ENODEV) {
		agp_bridge->dev = dev;
		agp_bridge->capndx = cap_ptr;
		/* Fill in the mode register */
		pci_read_config_dword(agp_bridge->dev, agp_bridge->capndx+PCI_AGP_STATUS, &agp_bridge->mode);
		amd_k7_agp_driver.dev = dev;
		agp_register_driver(&amd_k7_agp_driver);
		return 0;
	}
	return -ENODEV;
}

static struct pci_device_id agp_amdk7_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AMD,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_amdk7_pci_table);

static struct __initdata pci_driver agp_amdk7_pci_driver = {
	.name		= "agpgart-amdk7",
	.id_table	= agp_amdk7_pci_table,
	.probe		= agp_amdk7_probe,
};

static int __init agp_amdk7_init(void)
{
	int ret_val;

	ret_val = pci_module_init(&agp_amdk7_pci_driver);
	if (ret_val)
		agp_bridge->type = NOT_SUPPORTED;

	return ret_val;
}

static void __exit agp_amdk7_cleanup(void)
{
	agp_unregister_driver(&amd_k7_agp_driver);
	pci_unregister_driver(&agp_amdk7_pci_driver);
}

module_init(agp_amdk7_init);
module_exit(agp_amdk7_cleanup);

MODULE_PARM(agp_try_unsupported, "1i");
MODULE_LICENSE("GPL and additional rights");

