#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/hw_irq.h>
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
	struct pci_dev *dev = NULL;

	if (pcibios_scanned)
		return 0;

	if (acpi_noirq)
		return 0;

	printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
	acpi_irq_penalty_init();
	pcibios_scanned++;
	pcibios_enable_irq = acpi_pci_irq_enable;

	/*
	 * PCI IRQ routing is set up by pci_enable_device(), but we
	 * also do it here in case there are still broken drivers that
	 * don't use pci_enable_device().
	 */
	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL)
		acpi_pci_irq_enable(dev);

#ifdef CONFIG_X86_IO_APIC
	if (acpi_ioapic)
		print_IO_APIC();
#endif

	return 0;
}
subsys_initcall(pci_acpi_init);
