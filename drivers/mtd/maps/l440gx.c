/*
 * $Id: l440gx.c,v 1.7 2001/10/02 15:05:14 dwmw2 Exp $
 *
 * BIOS Flash chip on Intel 440GX board.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/config.h>


#define WINDOW_ADDR 0xfff00000
#define WINDOW_SIZE 0x00100000
#define BUSWIDTH 1

#define IOBASE 0xc00
#define TRIBUF_PORT (IOBASE+0x37)
#define VPP_PORT (IOBASE+0x28)

static struct mtd_info *mymtd;

__u8 l440gx_read8(struct map_info *map, unsigned long ofs)
{
	return __raw_readb(map->map_priv_1 + ofs);
}

__u16 l440gx_read16(struct map_info *map, unsigned long ofs)
{
	return __raw_readw(map->map_priv_1 + ofs);
}

__u32 l440gx_read32(struct map_info *map, unsigned long ofs)
{
	return __raw_readl(map->map_priv_1 + ofs);
}

void l440gx_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy_fromio(to, map->map_priv_1 + from, len);
}

void l440gx_write8(struct map_info *map, __u8 d, unsigned long adr)
{
	__raw_writeb(d, map->map_priv_1 + adr);
	mb();
}

void l440gx_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	__raw_writew(d, map->map_priv_1 + adr);
	mb();
}

void l440gx_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	__raw_writel(d, map->map_priv_1 + adr);
	mb();
}

void l440gx_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	memcpy_toio(map->map_priv_1 + to, from, len);
}

void l440gx_set_vpp(struct map_info *map, int vpp)
{
	unsigned long l;

	l = inl(VPP_PORT);
	l = vpp?(l | 1):(l & ~1);
	outl(l, VPP_PORT);
}

struct map_info l440gx_map = {
	name: "L440GX BIOS",
	size: WINDOW_SIZE,
	buswidth: BUSWIDTH,
	read8: l440gx_read8,
	read16: l440gx_read16,
	read32: l440gx_read32,
	copy_from: l440gx_copy_from,
	write8: l440gx_write8,
	write16: l440gx_write16,
	write32: l440gx_write32,
	copy_to: l440gx_copy_to,
	set_vpp: l440gx_set_vpp
};

static int __init init_l440gx(void)
{
	struct pci_dev *dev;
	unsigned char b;
	__u16 w;

	dev = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_0,
			      NULL);

	if (!dev) {
		printk(KERN_NOTICE "L440GX flash mapping: failed to find PIIX4 ISA bridge, cannot continue\n");
		return -ENODEV;
	}


	l440gx_map.map_priv_1 = (unsigned long)ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!l440gx_map.map_priv_1) {
		printk("Failed to ioremap L440GX flash region\n");
		return -ENOMEM;
	}

	/* Set XBCS# */
	pci_read_config_word(dev, 0x4e, &w);
	w |= 0x4;
        pci_write_config_word(dev, 0x4e, w);

	/* Enable the gate on the WE line */
	b = inb(TRIBUF_PORT);
	b |= 1;
	outb(b, TRIBUF_PORT);
	
       	printk(KERN_NOTICE "Enabled WE line to L440GX BIOS flash chip.\n");

	mymtd = do_map_probe("jedec", &l440gx_map);
	if (!mymtd) {
		printk(KERN_NOTICE "JEDEC probe on BIOS chip failed. Using ROM\n");
		mymtd = do_map_probe("map_rom", &l440gx_map);
	}
	if (mymtd) {
		mymtd->module = THIS_MODULE;

		add_mtd_device(mymtd);
		return 0;
	}

	iounmap((void *)l440gx_map.map_priv_1);
	return -ENXIO;
}

static void __exit cleanup_l440gx(void)
{
	del_mtd_device(mymtd);
	map_destroy(mymtd);
	
	iounmap((void *)l440gx_map.map_priv_1);
}

module_init(init_l440gx);
module_exit(cleanup_l440gx);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("MTD map driver for BIOS chips on Intel L440GX motherboards");
