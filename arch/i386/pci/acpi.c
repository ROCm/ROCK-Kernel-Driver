#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include "pci.h"

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

subsys_initcall(pci_acpi_init);
