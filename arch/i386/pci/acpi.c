#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include "pci.h"

struct pci_bus * __devinit pci_acpi_scan_root(struct acpi_device *device, int domain, int busnum)
{
	if (domain != 0) {
		printk(KERN_WARNING "PCI: Multiple domains not supported\n");
		return NULL;
	}

	return pcibios_scan_root(busnum);
}

static int __init pci_acpi_init(void)
{
	if (pcibios_scanned)
		return 0;

	if (!(pci_probe & PCI_NO_ACPI_ROUTING)) {
		if (!acpi_pci_irq_init()) {
			printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
			printk(KERN_INFO "PCI: if you experience problems, try using option 'pci=noacpi' or even 'acpi=off'\n");
			pcibios_scanned++;
			pcibios_enable_irq = acpi_pci_irq_enable;
		} else
			printk(KERN_WARNING "PCI: Invalid ACPI-PCI IRQ routing table\n");

	}

	return 0;
}

/*
 * pci_disable_acpi()
 * act like pci=noacpi seen on command line
 * called by DMI blacklist code
 */
__init void pci_disable_acpi(void)
{
        pci_probe |= PCI_NO_ACPI_ROUTING;
}

subsys_initcall(pci_acpi_init);
