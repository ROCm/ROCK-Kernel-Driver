/*
 * amd76xrom.c
 *
 * Normal mappings of chips in physical memory
 * $Id: amd76xrom.c,v 1.8 2003/05/28 15:44:28 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>


struct amd76xrom_map_info {
	struct map_info map;
	struct mtd_info *mtd;
	unsigned long window_addr;
	u32 window_start, window_size;
	struct pci_dev *pdev;
};


static struct amd76xrom_map_info amd76xrom_map = {
	.map = {
		.name = "AMD76X rom",
		.size = 0,
		.buswidth = 1,
	},
	.mtd = 0,
	.window_addr = 0,
};

static int __devinit amd76xrom_init_one (struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct rom_window {
		u32 start;
		u32 size;
		u8 segen_bits;
	};
	static struct rom_window rom_window[] = {
		{ 0xffb00000, 5*1024*1024, (1<<7) | (1<<6), },
		{ 0xffc00000, 4*1024*1024, (1<<7), },
		{ 0xffff0000, 64*1024,     0 },
		{ 0         , 0,           0 },
	};
	static const u32 rom_probe_sizes[] = { 
		5*1024*1024, 4*1024*1024, 2*1024*1024, 1024*1024, 512*1024, 
		256*1024, 128*1024, 64*1024, 0};
	static char *rom_probe_types[] = { "cfi_probe", "jedec_probe", 0 };
	u8 byte;
	struct amd76xrom_map_info *info = &amd76xrom_map;
	struct rom_window *window;
	int i;
	u32 rom_size;

	window = &rom_window[0];

	/* disabled because it fights with BIOS reserved regions */
#define REQUEST_MEM_REGION 0
#if REQUEST_MEM_REGION
	while(window->size) {
		if (request_mem_region(window->start, window->size, "amd76xrom")) {
			break;
		}
		window++;
	}
	if (!window->size) {
		printk(KERN_ERR "amd76xrom: cannot reserve rom window\n");
		goto err_out_none;
	}
#endif /* REQUEST_MEM_REGION */

	/* Enable the selected rom window */
	pci_read_config_byte(pdev, 0x43, &byte);
	pci_write_config_byte(pdev, 0x43, byte | window->segen_bits);

	/* Enable writes through the rom window */
	pci_read_config_byte(pdev, 0x40, &byte);
	pci_write_config_byte(pdev, 0x40, byte | 1);

	/* FIXME handle registers 0x80 - 0x8C the bios region locks */

	printk(KERN_NOTICE "amd76xrom window : %x at %x\n", 
		window->size, window->start);
	/* For write accesses caches are useless */
	info->window_addr = (unsigned long)ioremap_nocache(window->start, window->size);

	if (!info->window_addr) {
		printk(KERN_ERR "Failed to ioremap\n");
		goto err_out_free_mmio_region;
	}
	info->mtd = 0;
	for(i = 0; (rom_size = rom_probe_sizes[i]); i++) {
		char **chip_type;
		if (rom_size > window->size) {
			continue;
		}
		info->map.phys = window->start + window->size - rom_size;
		info->map.virt = 
			info->window_addr + window->size - rom_size;
		info->map.size = rom_size;
		simple_map_init(&info->map);
		chip_type = rom_probe_types;
		for(; !info->mtd && *chip_type; chip_type++) {
			info->mtd = do_map_probe(*chip_type, &amd76xrom_map.map);
		}
		if (info->mtd) {
			break;
		}
	}
	if (!info->mtd) {
		goto err_out_iounmap;
	}
	printk(KERN_NOTICE "amd76xrom chip at offset: 0x%x\n",
		window->size - rom_size);
		
	info->mtd->owner = THIS_MODULE;
	add_mtd_device(info->mtd);
	info->window_start = window->start;
	info->window_size = window->size;
	return 0;

err_out_iounmap:
	iounmap((void *)(info->window_addr));
err_out_free_mmio_region:
#if REQUEST_MEM_REGION
	release_mem_region(window->start, window->size);
err_out_none:
#endif /* REQUEST_MEM_REGION */
	return -ENODEV;
}


static void __devexit amd76xrom_remove_one (struct pci_dev *pdev)
{
	struct amd76xrom_map_info *info = &amd76xrom_map;
	u8 byte;

	del_mtd_device(info->mtd);
	map_destroy(info->mtd);
	info->mtd = 0;
	info->map.virt = 0;

	iounmap((void *)(info->window_addr));
	info->window_addr = 0;

	/* Disable writes through the rom window */
	pci_read_config_byte(pdev, 0x40, &byte);
	pci_write_config_byte(pdev, 0x40, byte & ~1);

#if REQUEST_MEM_REGION
	release_mem_region(info->window_start, info->window_size);
#endif /* REQUEST_MEM_REGION */
}

static struct pci_device_id amd76xrom_pci_tbl[] = {
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7410,  
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7440,  
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_AMD, 0x7468 }, /* amd8111 support */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, amd76xrom_pci_tbl);

#if 0
static struct pci_driver amd76xrom_driver = {
	.name =		"amd76xrom",
	.id_table =	amd76xrom_pci_tbl,
	.probe =	amd76xrom_init_one,
	.remove =	amd76xrom_remove_one,
};
#endif

int __init init_amd76xrom(void)
{
	struct pci_dev *pdev;
	struct pci_device_id *id;
	pdev = 0;
	for(id = amd76xrom_pci_tbl; id->vendor; id++) {
		pdev = pci_find_device(id->vendor, id->device, 0);
		if (pdev) {
			break;
		}
	}
	if (pdev) {
		amd76xrom_map.pdev = pdev;
		return amd76xrom_init_one(pdev, &amd76xrom_pci_tbl[0]);
	}
	return -ENXIO;
#if 0
	return pci_module_init(&amd76xrom_driver);
#endif
}

static void __exit cleanup_amd76xrom(void)
{
	amd76xrom_remove_one(amd76xrom_map.pdev);
}

module_init(init_amd76xrom);
module_exit(cleanup_amd76xrom);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Biederman <ebiederman@lnxi.com>");
MODULE_DESCRIPTION("MTD map driver for BIOS chips on the AMD76X southbridge");

