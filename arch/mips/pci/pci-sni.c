/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SNI specific PCI support for RM200/RM300.
 *
 * Copyright (C) 1997 - 2000 Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/sni.h>

#define mkaddr(bus, devfn, where)                                                   \
do {                                                                         \
	if (bus->number == 0)                                         \
		return -1;                                                   \
	*(volatile u32 *)PCIMT_CONFIG_ADDRESS =                              \
		 ((bus->number    & 0xff) << 0x10) |                    \
	         ((devfn & 0xff) << 0x08) |                             \
	         (where  & 0xfc);                                            \
} while(0)

#if 0
/* To do:  Bring this uptodate ...  */
static void pcimt_pcibios_fixup(void)
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		/*
		 * TODO: Take care of RM300 revision D boards for where the
		 * network slot became an ordinary PCI slot.
		 */
		if (dev->devfn == PCI_DEVFN(1, 0)) {
			/* Evil hack ...  */
			set_cp0_config(CONF_CM_CMASK,
				       CONF_CM_CACHABLE_NO_WA);
			dev->irq = PCIMT_IRQ_SCSI;
			continue;
		}
		if (dev->devfn == PCI_DEVFN(2, 0)) {
			dev->irq = PCIMT_IRQ_ETHERNET;
			continue;
		}

		switch (dev->irq) {
		case 1...4:
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
static int pcimt_read(struct pci_bus *bus, unsigned int devfn, int where,
		      int size, u32 * val)
{
	u32 res;

	switch (size) {
	case 1:
		mkaddr(bus, devfn, where);
		res = *(volatile u32 *) PCIMT_CONFIG_DATA;
		res = (le32_to_cpu(res) >> ((where & 3) << 3)) & 0xff;
		*val = (u8) res;
		break;
	case 2:
		if (where & 1)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		mkaddr(bus, devfn, where);
		res = *(volatile u32 *) PCIMT_CONFIG_DATA;
		res = (le32_to_cpu(res) >> ((where & 3) << 3)) & 0xffff;
		*val = (u16) res;
		break;
	case 4:
		if (where & 3)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		mkaddr(bus, devfn, where);
		res = *(volatile u32 *) PCIMT_CONFIG_DATA;
		res = le32_to_cpu(res);
		*val = res;
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int pcimt_write(struct pci_bus *bus, unsigned int devfn, int where,
		       int size, u32 val)
{
	switch (size) {
	case 1:
		mkaddr(bus, devfn, where);
		*(volatile u8 *) (PCIMT_CONFIG_DATA + (where & 3)) =
		    (u8) le32_to_cpu(val);
		break;
	case 2:
		if (where & 1)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		mkaddr(bus, devfn, where);
		*(volatile u16 *) (PCIMT_CONFIG_DATA + (where & 3)) =
		    (u16) le32_to_cpu(val);
		break;
	case 4:
		if (where & 3)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		mkaddr(bus, devfn, where);
		*(volatile u32 *) PCIMT_CONFIG_DATA = le32_to_cpu(val);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops sni_pci_ops = {
	.read = pcimt_read,
	.write = pcimt_write,
};

void __devinit pcibios_fixup_bus(struct pci_bus *b)
{
}

static int __init pcibios_init(void)
{
	struct pci_ops *ops = &sni_pci_ops;

	pci_scan_bus(0, ops, NULL);

	return 0;
}

subsys_initcall(pcibios_init);

int __init pcibios_enable_device(struct pci_dev *dev, int mask)
{
	/* Not needed, since we enable all devices at startup.  */
	return 0;
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
}

unsigned __init int pcibios_assign_all_busses(void)
{
	return 0;
}

char *__init pcibios_setup(char *str)
{
	/* Nothing to do for now.  */

	return str;
}

struct pci_fixup pcibios_fixups[] = {
	{0}
};
