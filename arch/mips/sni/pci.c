/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SNI specific PCI support for RM200/RM300.
 *
 * Copyright (C) 1997 - 2000 Ralf Baechle
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/sni.h>

#ifdef CONFIG_PCI

#define mkaddr(dev, where)                                                   \
do {                                                                         \
	if ((dev)->bus->number == 0)                                         \
		return -1;                                                   \
	*(volatile u32 *)PCIMT_CONFIG_ADDRESS =                              \
		 ((dev->bus->number    & 0xff) << 0x10) |                    \
	         ((dev->devfn & 0xff) << 0x08) |                             \
	         (where  & 0xfc);                                            \
} while(0)

#if 0
/* To do:  Bring this uptodate ...  */
static void pcimt_pcibios_fixup (void)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		/*
		 * TODO: Take care of RM300 revision D boards for where the
		 * network slot became an ordinary PCI slot.
		 */
		if (dev->devfn == PCI_DEVFN(1, 0)) {
			/* Evil hack ...  */
			set_cp0_config(CONF_CM_CMASK, CONF_CM_CACHABLE_NO_WA);
			dev->irq = PCIMT_IRQ_SCSI;
			continue;
		}
		if (dev->devfn == PCI_DEVFN(2, 0)) {
			dev->irq = PCIMT_IRQ_ETHERNET;
			continue;
		}

		switch(dev->irq) {
		case 1 ... 4:
			dev->irq += PCIMT_IRQ_INTA - 1;
			break;
		case 0:
			break;
		default:
			printk("PCI device on bus %d, dev %d, function %d "
			       "impossible interrupt configured.\n",
			       dev->bus->number, PCI_SLOT(dev->devfn),
			       PCI_SLOT(dev->devfn));
		}
	}
}
#endif

/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int pcimt_read_config_byte (struct pci_dev *dev,
                                   int where, unsigned char *val)
{
	u32 res;

	mkaddr(dev, where);
	res = *(volatile u32 *)PCIMT_CONFIG_DATA;
	res = (le32_to_cpu(res) >> ((where & 3) << 3)) & 0xff;
	*val = res;

	return PCIBIOS_SUCCESSFUL;
}

static int pcimt_read_config_word (struct pci_dev *dev,
                                   int where, unsigned short *val)
{
	u32 res;

	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	mkaddr(dev, where);
	res = *(volatile u32 *)PCIMT_CONFIG_DATA;
	res = (le32_to_cpu(res) >> ((where & 3) << 3)) & 0xffff;
	*val = res;

	return PCIBIOS_SUCCESSFUL;
}

static int pcimt_read_config_dword (struct pci_dev *dev,
                                    int where, unsigned int *val)
{
	u32 res;

		if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	mkaddr(dev, where);
	res = *(volatile u32 *)PCIMT_CONFIG_DATA;
	res = le32_to_cpu(res);
	*val = res;

	return PCIBIOS_SUCCESSFUL;
}

static int pcimt_write_config_byte (struct pci_dev *dev,
                                    int where, unsigned char val)
{
	mkaddr(dev, where);
	*(volatile u8 *)(PCIMT_CONFIG_DATA + (where & 3)) = val;

	return PCIBIOS_SUCCESSFUL;
}

static int pcimt_write_config_word (struct pci_dev *dev,
                                    int where, unsigned short val)
{
	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	mkaddr(dev, where);
	*(volatile u16 *)(PCIMT_CONFIG_DATA + (where & 3)) = le16_to_cpu(val);

	return PCIBIOS_SUCCESSFUL;
}

static int pcimt_write_config_dword (struct pci_dev *dev,
                                     int where, unsigned int val)
{
	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	mkaddr(dev, where);
	*(volatile u32 *)PCIMT_CONFIG_DATA = le32_to_cpu(val);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops sni_pci_ops = {
	pcimt_read_config_byte,
	pcimt_read_config_word,
	pcimt_read_config_dword,
	pcimt_write_config_byte,
	pcimt_write_config_word,
	pcimt_write_config_dword
};

void __init
pcibios_fixup_bus(struct pci_bus *b)
{
}

void
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			struct resource *res, int resource)
{
	u32 new, check;
	int reg;

	new = res->start | (res->flags & PCI_REGION_FLAG_MASK);
	if (resource < 6) {
		reg = PCI_BASE_ADDRESS_0 + 4*resource;
	} else if (resource == PCI_ROM_RESOURCE) {
		res->flags |= PCI_ROM_ADDRESS_ENABLE;
		new |= PCI_ROM_ADDRESS_ENABLE;
		reg = dev->rom_base_reg;
	} else {
		/* Somebody might have asked allocation of a non-standard resource */
		return;
	}
	
	pci_write_config_dword(dev, reg, new);
	pci_read_config_dword(dev, reg, &check);
	if ((new ^ check) & ((new & PCI_BASE_ADDRESS_SPACE_IO) ? PCI_BASE_ADDRESS_IO_MASK : PCI_BASE_ADDRESS_MEM_MASK)) {
		printk(KERN_ERR "PCI: Error while updating region "
		       "%s/%d (%08x != %08x)\n", dev->slot_name, resource,
		       new, check);
	}
}

void __init pcibios_init(void)
{
	struct pci_ops *ops = &sni_pci_ops;

	pci_scan_bus(0, ops, NULL);
}

int __init pcibios_enable_device(struct pci_dev *dev)
{
	/* Not needed, since we enable all devices at startup.  */
	return 0;
}

void __init
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
}

unsigned __init int pcibios_assign_all_busses(void)
{
	return 0;
}

char * __init
pcibios_setup(char *str)
{
	/* Nothing to do for now.  */

	return str;
}

struct pci_fixup pcibios_fixups[] = {
	{ 0 }
};

#endif /* CONFIG_PCI */
