/*
 * Serverworks AGPGART routines.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

static int agp_try_unsupported __initdata = 0;

struct serverworks_page_map {
	unsigned long *real;
	unsigned long *remapped;
};

static struct _serverworks_private {
	struct pci_dev *svrwrks_dev;	/* device one */
	volatile u8 *registers;
	struct serverworks_page_map **gatt_pages;
	int num_tables;
	struct serverworks_page_map scratch_dir;

	int gart_addr_ofs;
	int mm_addr_ofs;
} serverworks_private;

static int serverworks_create_page_map(struct serverworks_page_map *page_map)
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

static void serverworks_free_page_map(struct serverworks_page_map *page_map)
{
	iounmap(page_map->remapped);
	ClearPageReserved(virt_to_page(page_map->real));
	free_page((unsigned long) page_map->real);
}

static void serverworks_free_gatt_pages(void)
{
	int i;
	struct serverworks_page_map **tables;
	struct serverworks_page_map *entry;

	tables = serverworks_private.gatt_pages;
	for(i = 0; i < serverworks_private.num_tables; i++) {
		entry = tables[i];
		if (entry != NULL) {
			if (entry->real != NULL) {
				serverworks_free_page_map(entry);
			}
			kfree(entry);
		}
	}
	kfree(tables);
}

static int serverworks_create_gatt_pages(int nr_tables)
{
	struct serverworks_page_map **tables;
	struct serverworks_page_map *entry;
	int retval = 0;
	int i;

	tables = kmalloc((nr_tables + 1) * sizeof(struct serverworks_page_map *), 
			 GFP_KERNEL);
	if (tables == NULL) {
		return -ENOMEM;
	}
	memset(tables, 0, sizeof(struct serverworks_page_map *) * (nr_tables + 1));
	for (i = 0; i < nr_tables; i++) {
		entry = kmalloc(sizeof(struct serverworks_page_map), GFP_KERNEL);
		if (entry == NULL) {
			retval = -ENOMEM;
			break;
		}
		memset(entry, 0, sizeof(struct serverworks_page_map));
		tables[i] = entry;
		retval = serverworks_create_page_map(entry);
		if (retval != 0) break;
	}
	serverworks_private.num_tables = nr_tables;
	serverworks_private.gatt_pages = tables;

	if (retval != 0) serverworks_free_gatt_pages();

	return retval;
}

#define SVRWRKS_GET_GATT(addr) (serverworks_private.gatt_pages[\
	GET_PAGE_DIR_IDX(addr)]->remapped)

#ifndef GET_PAGE_DIR_OFF
#define GET_PAGE_DIR_OFF(addr) (addr >> 22)
#endif

#ifndef GET_PAGE_DIR_IDX
#define GET_PAGE_DIR_IDX(addr) (GET_PAGE_DIR_OFF(addr) - \
	GET_PAGE_DIR_OFF(agp_bridge->gart_bus_addr))
#endif

#ifndef GET_GATT_OFF
#define GET_GATT_OFF(addr) ((addr & 0x003ff000) >> 12)
#endif

static int serverworks_create_gatt_table(void)
{
	struct aper_size_info_lvl2 *value;
	struct serverworks_page_map page_dir;
	int retval;
	u32 temp;
	int i;

	value = A_SIZE_LVL2(agp_bridge->current_size);
	retval = serverworks_create_page_map(&page_dir);
	if (retval != 0) {
		return retval;
	}
	retval = serverworks_create_page_map(&serverworks_private.scratch_dir);
	if (retval != 0) {
		serverworks_free_page_map(&page_dir);
		return retval;
	}
	/* Create a fake scratch directory */
	for(i = 0; i < 1024; i++) {
		serverworks_private.scratch_dir.remapped[i] = (unsigned long) agp_bridge->scratch_page;
		page_dir.remapped[i] =
			virt_to_phys(serverworks_private.scratch_dir.real);
		page_dir.remapped[i] |= 0x00000001;
	}

	retval = serverworks_create_gatt_pages(value->num_entries / 1024);
	if (retval != 0) {
		serverworks_free_page_map(&page_dir);
		serverworks_free_page_map(&serverworks_private.scratch_dir);
		return retval;
	}

	agp_bridge->gatt_table_real = (u32 *)page_dir.real;
	agp_bridge->gatt_table = (u32 *)page_dir.remapped;
	agp_bridge->gatt_bus_addr = virt_to_phys(page_dir.real);

	/* Get the address for the gart region.
	 * This is a bus address even on the alpha, b/c its
	 * used to program the agp master not the cpu
	 */

	pci_read_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,&temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* Calculate the agp offset */	

	for(i = 0; i < value->num_entries / 1024; i++) {
		page_dir.remapped[i] =
			virt_to_phys(serverworks_private.gatt_pages[i]->real);
		page_dir.remapped[i] |= 0x00000001;
	}

	return 0;
}

static int serverworks_free_gatt_table(void)
{
	struct serverworks_page_map page_dir;
   
	page_dir.real = (unsigned long *)agp_bridge->gatt_table_real;
	page_dir.remapped = (unsigned long *)agp_bridge->gatt_table;

	serverworks_free_gatt_pages();
	serverworks_free_page_map(&page_dir);
	serverworks_free_page_map(&serverworks_private.scratch_dir);
	return 0;
}

static int serverworks_fetch_size(void)
{
	int i;
	u32 temp;
	u32 temp2;
	struct aper_size_info_lvl2 *values;

	values = A_SIZE_LVL2(agp_bridge->aperture_sizes);
	pci_read_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,&temp);
	pci_write_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,
					SVWRKS_SIZE_MASK);
	pci_read_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,&temp2);
	pci_write_config_dword(agp_bridge->dev,serverworks_private.gart_addr_ofs,temp);
	temp2 &= SVWRKS_SIZE_MASK;

	for (i = 0; i < agp_bridge->num_aperture_sizes; i++) {
		if (temp2 == values[i].size_value) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);

			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int serverworks_configure(void)
{
	struct aper_size_info_lvl2 *current_size;
	u32 temp;
	u8 enable_reg;
	u16 cap_reg;

	current_size = A_SIZE_LVL2(agp_bridge->current_size);

	/* Get the memory mapped registers */
	pci_read_config_dword(agp_bridge->dev, serverworks_private.mm_addr_ofs, &temp);
	temp = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	serverworks_private.registers = (volatile u8 *) ioremap(temp, 4096);

	OUTREG8(serverworks_private.registers, SVWRKS_GART_CACHE, 0x0a);

	OUTREG32(serverworks_private.registers, SVWRKS_GATTBASE, 
		 agp_bridge->gatt_bus_addr);

	cap_reg = INREG16(serverworks_private.registers, SVWRKS_COMMAND);
	cap_reg &= ~0x0007;
	cap_reg |= 0x4;
	OUTREG16(serverworks_private.registers, SVWRKS_COMMAND, cap_reg);

	pci_read_config_byte(serverworks_private.svrwrks_dev,
			     SVWRKS_AGP_ENABLE, &enable_reg);
	enable_reg |= 0x1; /* Agp Enable bit */
	pci_write_config_byte(serverworks_private.svrwrks_dev,
			      SVWRKS_AGP_ENABLE, enable_reg);
	agp_bridge->tlb_flush(NULL);

	agp_bridge->capndx = pci_find_capability(serverworks_private.svrwrks_dev, PCI_CAP_ID_AGP);

	/* Fill in the mode register */
	pci_read_config_dword(serverworks_private.svrwrks_dev,
			      agp_bridge->capndx+PCI_AGP_STATUS, &agp_bridge->mode);

	pci_read_config_byte(agp_bridge->dev, SVWRKS_CACHING, &enable_reg);
	enable_reg &= ~0x3;
	pci_write_config_byte(agp_bridge->dev, SVWRKS_CACHING, enable_reg);

	pci_read_config_byte(agp_bridge->dev, SVWRKS_FEATURE, &enable_reg);
	enable_reg |= (1<<6);
	pci_write_config_byte(agp_bridge->dev,SVWRKS_FEATURE, enable_reg);

	return 0;
}

static void serverworks_cleanup(void)
{
	iounmap((void *) serverworks_private.registers);
}

/*
 * This routine could be implemented by taking the addresses
 * written to the GATT, and flushing them individually.  However
 * currently it just flushes the whole table.  Which is probably
 * more efficent, since agp_memory blocks can be a large number of
 * entries.
 */

static void serverworks_tlbflush(agp_memory * temp)
{
	unsigned long end;

	OUTREG8(serverworks_private.registers, SVWRKS_POSTFLUSH, 0x01);
	end = jiffies + 3*HZ;
	while(INREG8(serverworks_private.registers, 
		     SVWRKS_POSTFLUSH) == 0x01) {
		if((signed)(end - jiffies) <= 0) {
			printk(KERN_ERR "Posted write buffer flush took more"
			       "then 3 seconds\n");
		}
	}
	OUTREG32(serverworks_private.registers, SVWRKS_DIRFLUSH, 0x00000001);
	end = jiffies + 3*HZ;
	while(INREG32(serverworks_private.registers, 
		     SVWRKS_DIRFLUSH) == 0x00000001) {
		if((signed)(end - jiffies) <= 0) {
			printk(KERN_ERR "TLB flush took more"
			       "then 3 seconds\n");
		}
	}
}

static unsigned long serverworks_mask_memory(unsigned long addr, int type)
{
	/* Only type 0 is supported by the serverworks chipsets */

	return addr | agp_bridge->masks[0].mask;
}

static int serverworks_insert_memory(agp_memory * mem,
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
		cur_gatt = SVRWRKS_GET_GATT(addr);
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
		cur_gatt = SVRWRKS_GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] =
			agp_bridge->mask_memory(mem->memory[i], mem->type);
	}
	agp_bridge->tlb_flush(mem);
	return 0;
}

static int serverworks_remove_memory(agp_memory * mem, off_t pg_start,
			     int type)
{
	int i;
	unsigned long *cur_gatt;
	unsigned long addr;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	CACHE_FLUSH();
	agp_bridge->tlb_flush(mem);

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		addr = (i * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = SVRWRKS_GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] = 
			(unsigned long) agp_bridge->scratch_page;
	}

	agp_bridge->tlb_flush(mem);
	return 0;
}

static struct gatt_mask serverworks_masks[] =
{
	{.mask = 0x00000001, .type = 0}
};

static struct aper_size_info_lvl2 serverworks_sizes[7] =
{
	{2048, 524288, 0x80000000},
	{1024, 262144, 0xc0000000},
	{512, 131072, 0xe0000000},
	{256, 65536, 0xf0000000},
	{128, 32768, 0xf8000000},
	{64, 16384, 0xfc000000},
	{32, 8192, 0xfe000000}
};

static void serverworks_agp_enable(u32 mode)
{
	u32 command;

	pci_read_config_dword(serverworks_private.svrwrks_dev,
			      agp_bridge->capndx + PCI_AGP_STATUS,
			      &command);

	command = agp_collect_device_status(mode, command);

	command &= ~0x10;	/* disable FW */
	command &= ~0x08;

	command |= 0x100;

	pci_write_config_dword(serverworks_private.svrwrks_dev,
			       agp_bridge->capndx + PCI_AGP_COMMAND,
			       command);

	agp_device_command(command, 0);
}

static int __init serverworks_setup (struct pci_dev *pdev)
{
	u32 temp;
	u32 temp2;

	serverworks_private.svrwrks_dev = pdev;

	agp_bridge->masks = serverworks_masks;
	agp_bridge->aperture_sizes = (void *) serverworks_sizes;
	agp_bridge->size_type = LVL2_APER_SIZE;
	agp_bridge->num_aperture_sizes = 7;
	agp_bridge->dev_private_data = (void *) &serverworks_private;
	agp_bridge->needs_scratch_page = TRUE;
	agp_bridge->configure = serverworks_configure;
	agp_bridge->fetch_size = serverworks_fetch_size;
	agp_bridge->cleanup = serverworks_cleanup;
	agp_bridge->tlb_flush = serverworks_tlbflush;
	agp_bridge->mask_memory = serverworks_mask_memory;
	agp_bridge->agp_enable = serverworks_agp_enable;
	agp_bridge->cache_flush = global_cache_flush;
	agp_bridge->create_gatt_table = serverworks_create_gatt_table;
	agp_bridge->free_gatt_table = serverworks_free_gatt_table;
	agp_bridge->insert_memory = serverworks_insert_memory;
	agp_bridge->remove_memory = serverworks_remove_memory;
	agp_bridge->alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge->free_by_type = agp_generic_free_by_type;
	agp_bridge->agp_alloc_page = agp_generic_alloc_page;
	agp_bridge->agp_destroy_page = agp_generic_destroy_page;
	agp_bridge->suspend = agp_generic_suspend;
	agp_bridge->resume = agp_generic_resume;
	agp_bridge->cant_use_aperture = 0;

	pci_read_config_dword(agp_bridge->dev,
			      SVWRKS_APSIZE,
			      &temp);

	serverworks_private.gart_addr_ofs = 0x10;

	if(temp & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		pci_read_config_dword(agp_bridge->dev,
				      SVWRKS_APSIZE + 4,
				      &temp2);
		if(temp2 != 0) {
			printk("Detected 64 bit aperture address, but top "
			       "bits are not zero.  Disabling agp\n");
			return -ENODEV;
		}
		serverworks_private.mm_addr_ofs = 0x18;
	} else {
		serverworks_private.mm_addr_ofs = 0x14;
	}

	pci_read_config_dword(agp_bridge->dev,
			      serverworks_private.mm_addr_ofs,
			      &temp);
	if(temp & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		pci_read_config_dword(agp_bridge->dev,
				      serverworks_private.mm_addr_ofs + 4,
				      &temp2);
		if(temp2 != 0) {
			printk("Detected 64 bit MMIO address, but top "
			       "bits are not zero.  Disabling agp\n");
			return -ENODEV;
		}
	}

	return 0;
}


static int __init agp_find_supported_device(struct pci_dev *dev)
{
	struct pci_dev *bridge_dev;

	/* Everything is on func 1 here so we are hardcoding function one */
	bridge_dev = pci_find_slot ((unsigned int)dev->bus->number, PCI_DEVFN(0, 1));
	if(bridge_dev == NULL) {
		printk(KERN_INFO PFX "agpgart: Detected a Serverworks "
		       "Chipset, but could not find the secondary "
		       "device.\n");
		return -ENODEV;
	}

	agp_bridge->dev = dev;

	switch (dev->device) {
	case PCI_DEVICE_ID_SERVERWORKS_HE:
	case PCI_DEVICE_ID_SERVERWORKS_LE:
	case 0x0007:
		agp_bridge->type = SVWRKS_GENERIC;
		return serverworks_setup(bridge_dev);

	default:
		if(agp_try_unsupported) {
			agp_bridge->type = SVWRKS_GENERIC;
			return serverworks_setup(bridge_dev);
		}
		break;
	}
	return -ENODEV;
}

static struct agp_driver serverworks_agp_driver = {
	.owner = THIS_MODULE,
};

static int __init agp_serverworks_probe (struct pci_dev *dev, const struct pci_device_id *ent)
{
	if (agp_find_supported_device(dev) == 0) {
		serverworks_agp_driver.dev = dev;
		agp_register_driver(&serverworks_agp_driver);
		return 0;
	}
	return -ENODEV;	
}

static struct pci_device_id agp_serverworks_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_SERVERWORKS,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_serverworks_pci_table);

static struct __initdata pci_driver agp_serverworks_pci_driver = {
	.name		= "agpgart-serverworks",
	.id_table	= agp_serverworks_pci_table,
	.probe		= agp_serverworks_probe,
};

static int __init agp_serverworks_init(void)
{
	int ret_val;

	ret_val = pci_module_init(&agp_serverworks_pci_driver);
	if (ret_val)
		agp_bridge->type = NOT_SUPPORTED;

	return ret_val;
}

static void __exit agp_serverworks_cleanup(void)
{
	agp_unregister_driver(&serverworks_agp_driver);
	pci_unregister_driver(&agp_serverworks_pci_driver);
}

module_init(agp_serverworks_init);
module_exit(agp_serverworks_cleanup);

MODULE_PARM(agp_try_unsupported, "1i");
MODULE_LICENSE("GPL and additional rights");

