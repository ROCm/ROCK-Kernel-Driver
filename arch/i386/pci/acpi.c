#include <linux/pci.h>
#include <linux/acpi.h>
#include "pci.h"

extern void eisa_set_level_irq(int irq);

static int acpi_lookup_irq (
	struct pci_dev		*dev,
	int			assign)
{
	int			result = 0;
	int			irq = 0;
	u8			pin;

	/* TBD: Select IRQ from possible to improve routing performance. */

	/* Find IRQ pin */
	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	if (!pin) {
		DBG(" -> no interrupt pin\n");
		return 0;
	}
	pin = pin - 1;

	result = acpi_prt_get_irq(dev, pin, &irq);
	if (!irq)
		result = -ENODEV;
	if (0 != result) {
		printk(KERN_WARNING "PCI: No IRQ known for interrupt pin %c of device %s\n",
		       'A'+pin, dev->slot_name);
		return result;
	}

	/* only check for the IRQ */
	if (!assign) {
		printk(KERN_INFO "PCI: Found IRQ %d for device %s\n", 
		       irq, dev->slot_name);
		return 1;
	}

	dev->irq = irq;

	/* also assign an IRQ */
	if (irq && (dev->class >> 8) != PCI_CLASS_DISPLAY_VGA) {
		result = acpi_prt_set_irq(dev, pin, irq);
		if (0 != result) {
			printk(KERN_WARNING "PCI: Could not assign IRQ %d to device %s\n", irq, dev->slot_name);
			return result;
		}

		eisa_set_level_irq(irq);

		printk(KERN_INFO "PCI: Assigned IRQ %d for device %s\n", irq, dev->slot_name);
	}

	return 1;
}

static int __init pci_acpi_init(void)
{
	if (!(pci_probe & PCI_NO_ACPI_ROUTING)) {
		if (acpi_prts.count) {
			printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
			printk(KERN_INFO "PCI: if you experience problems, try using option 'pci=noacpi'\n");
			pci_use_acpi_routing = 1;
			pci_lookup_irq = acpi_lookup_irq;
		} else
			printk(KERN_WARNING "PCI: Invalid ACPI-PCI IRQ routing table\n");

	}
	return 0;
}

arch_initcall(pci_acpi_init);
