/*
 * amd76xrom.c
 *
 * Normal mappings of chips in physical memory
 * $Id: amd76xrom.c,v 1.12 2004/07/14 14:44:31 thayne Exp $
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


#define xstr(s) str(s)
#define str(s) #s
#define MOD_NAME xstr(KBUILD_BASENAME)

#define MTD_DEV_NAME_LENGTH 16

struct amd76xrom_map_info {
	struct map_info map;
	struct mtd_info *mtd;
	unsigned long window_addr;
	u32 window_start, window_size;
	struct pci_dev *pdev;
	struct resource window_rsrc;
	struct resource rom_rsrc;
	char mtd_name[MTD_DEV_NAME_LENGTH];
};


static struct amd76xrom_map_info amd76xrom_map = {
	.map = {
		.name = MOD_NAME,
		.size = 0,
		.bankwidth = 1,
	}
	/* remaining fields of structure are initialized to 0 */
};


static void amd76xrom_cleanup(struct amd76xrom_map_info *info)
{
	u8 byte;

	/* Disable writes through the rom window */
	pci_read_config_byte(info->pdev, 0x40, &byte);
	pci_write_config_byte(info->pdev, 0x40, byte & ~1);

	if (info->mtd) {
		del_mtd_device(info->mtd);
		map_destroy(info->mtd);
		info->mtd = NULL;
		info->map.virt = 0;
	}
	if (info->rom_rsrc.parent)
		release_resource(&info->rom_rsrc);
	if (info->window_rsrc.parent)
		release_resource(&info->window_rsrc);

	if (info->window_addr) {
		iounmap((void *)(info->window_addr));
		info->window_addr = 0;
	}
}


static int __devinit amd76xrom_init_one (struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct rom_window {
		u32 start;
		u32 size;
		u8 segen_bits;
	};
	static struct rom_window rom_window[] = {
		/*
		 * Need the 5MiB window for chips that have block lock/unlock
		 * registers located below 4MiB window.
		 */
		{ 0xffb00000, 5*1024*1024, (1<<7) | (1<<6), },
		{ 0xffc00000, 4*1024*1024, (1<<7), },
		{ 0xffff0000, 64*1024,     0 },
		{ 0         , 0,           0 },
	};
	static const u32 rom_probe_sizes[] = { 
		5*1024*1024, 4*1024*1024, 2*1024*1024, 1024*1024, 512*1024, 
		256*1024, 128*1024, 64*1024, 0};
	static char *rom_probe_types[] = { "cfi_probe", "jedec_probe", NULL };
	u8 byte;
	struct amd76xrom_map_info *info = &amd76xrom_map;
	struct rom_window *window;
	int i;
	u32 rom_size;

	info->pdev = pdev;
	window = &rom_window[0];

	while (window->size) {
		/*
		 * Try to reserve the window mem region.  If this fails then
		 * it is likely due to a fragment of the window being
		 * "reseved" by the BIOS.  In the case that the
		 * request_mem_region() fails then once the rom size is
		 * discovered we will try to reserve the unreserved fragment.
		 */
		info->window_rsrc.name = MOD_NAME;
		info->window_rsrc.start = window->start;
		info->window_rsrc.end = window->start + window->size - 1;
		info->window_rsrc.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		if (request_resource(&iomem_resource, &info->window_rsrc)) {
			info->window_rsrc.parent = NULL;
			printk(KERN_ERR MOD_NAME
			       " %s(): Unable to register resource"
			       " 0x%.08lx-0x%.08lx - kernel bug?\n",
			       __func__,
			       info->window_rsrc.start, info->window_rsrc.end);
		}

		/* Enable the selected rom window */
		pci_read_config_byte(pdev, 0x43, &byte);
		pci_write_config_byte(pdev, 0x43, byte | window->segen_bits);

		/* Enable writes through the rom window */
		pci_read_config_byte(pdev, 0x40, &byte);
		pci_write_config_byte(pdev, 0x40, byte | 1);

		/* FIXME handle registers 0x80 - 0x8C the bios region locks */

		printk(KERN_NOTICE MOD_NAME " window : %x at %x\n", 
		       window->size, window->start);
		/* For write accesses caches are useless */
		info->window_addr =
			(unsigned long)ioremap_nocache(window->start,
						       window->size);

		if (!info->window_addr) {
			printk(KERN_ERR "Failed to ioremap\n");
			continue;
		}

		info->mtd = NULL;

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
			if (info->mtd) goto found_mtd;
		}
		iounmap((void *)(info->window_addr));
		info->window_addr = 0;

		/* Disable writes through the rom window */
		pci_read_config_byte(pdev, 0x40, &byte);
		pci_write_config_byte(pdev, 0x40, byte & ~1);

		window++;
	}
	goto failed;

 found_mtd:
	printk(KERN_NOTICE MOD_NAME " chip at offset: 0x%x\n",
		window->size - rom_size);

	info->mtd->owner = THIS_MODULE;

	if (!info->window_rsrc.parent) {
		/* failed to reserve entire window - try fragments */
		info->window_rsrc.name = MOD_NAME;
		info->window_rsrc.start = window->start;
		info->window_rsrc.end = window->start + window->size - rom_size - 1;
		info->window_rsrc.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		if (request_resource(&iomem_resource, &info->window_rsrc)) {
			printk(KERN_ERR MOD_NAME
			       ": cannot reserve window resource fragment\n");
#if 0
			/*
			 * The BIOS e820 usually reserves this so it isn't
			 * usually an error.
			 */
			goto failed;
#endif
		}
	}

	add_mtd_device(info->mtd);
	info->window_start = window->start;
	info->window_size = window->size;

	if (info->window_rsrc.parent) {
		/*
		 * Registering the MTD device in iomem may not be possible
		 * if there is a BIOS "reserved" and BUSY range.  If this
		 * fails then continue anyway.
		 */
		snprintf(info->mtd_name, MTD_DEV_NAME_LENGTH,
			 "mtd%d", info->mtd->index);

		info->rom_rsrc.name = info->mtd_name;
		info->rom_rsrc.start = window->start + window->size - rom_size;
		info->rom_rsrc.end = window->start + window->size - 1;
		info->rom_rsrc.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		if (request_resource(&info->window_rsrc, &info->rom_rsrc)) {
			printk(KERN_ERR MOD_NAME
			       ": cannot reserve MTD resource\n");
			info->rom_rsrc.parent = NULL;
		}
	}

	return 0;

 failed:
	amd76xrom_cleanup(info);
	return -ENODEV;
}


static void __devexit amd76xrom_remove_one (struct pci_dev *pdev)
{
	struct amd76xrom_map_info *info = &amd76xrom_map;

	amd76xrom_cleanup(info);
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
	.name =		MOD_NAME,
	.id_table =	amd76xrom_pci_tbl,
	.probe =	amd76xrom_init_one,
	.remove =	amd76xrom_remove_one,
};
#endif

int __init init_amd76xrom(void)
{
	struct pci_dev *pdev;
	struct pci_device_id *id;
	pdev = NULL;
	for(id = amd76xrom_pci_tbl; id->vendor; id++) {
		pdev = pci_find_device(id->vendor, id->device, NULL);
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

