/*
 * legacy.c - traditional, old school PCI bus probing
 */
#include <linux/init.h>
#include <linux/pci.h>
#include "pci.h"

/*
 * Discover remaining PCI buses in case there are peer host bridges.
 * We use the number of last PCI bus provided by the PCI BIOS.
 */
void __devinit pcibios_fixup_peer_bridges(void)
{
	int n;
	struct pci_bus *bus;
	struct pci_dev *dev;
	u16 l;

	if (pcibios_last_bus <= 0 || pcibios_last_bus >= 0xff)
		return;
	DBG("PCI: Peer bridge fixup\n");

	bus = kmalloc(sizeof(*bus), GFP_ATOMIC);
	dev = kmalloc(sizeof(*dev), GFP_ATOMIC);
	if (!bus || !dev) {
		printk(KERN_ERR "Out of memory in %s\n", __FUNCTION__);
		goto exit;
	}

	for (n=0; n <= pcibios_last_bus; n++) {
		if (pci_bus_exists(&pci_root_buses, n))
			continue;
		bus->number = n;
		bus->ops = pci_root_ops;
		dev->bus = bus;
		for (dev->devfn=0; dev->devfn<256; dev->devfn += 8)
			if (!pci_read_config_word(dev, PCI_VENDOR_ID, &l) &&
			    l != 0x0000 && l != 0xffff) {
				DBG("Found device at %02x:%02x [%04x]\n", n, dev->devfn, l);
				printk(KERN_INFO "PCI: Discovered peer bus %02x\n", n);
				pci_scan_bus(n, pci_root_ops, NULL);
				break;
			}
	}
exit:
	kfree(dev);
	kfree(bus);
}

static int __init pci_legacy_init(void)
{
	if (!pci_root_ops) {
		printk("PCI: System does not support PCI\n");
		return 0;
	}

	if (pcibios_scanned++)
		return 0;

	printk("PCI: Probing PCI hardware\n");
	pci_root_bus = pcibios_scan_root(0);

	pcibios_fixup_peer_bridges();

	return 0;
}

subsys_initcall(pci_legacy_init);
