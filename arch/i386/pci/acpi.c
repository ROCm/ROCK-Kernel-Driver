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

	if (!acpi_noirq) {
		if (!acpi_pci_irq_init()) {
			printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
			pcibios_scanned++;
			pcibios_enable_irq = acpi_pci_irq_enable;
		} else
			printk(KERN_WARNING "PCI: Invalid ACPI-PCI IRQ routing table\n");

	}

	return 0;
}
subsys_initcall(pci_acpi_init);
