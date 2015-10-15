/*
 * PCI Frontend Stub - puts some "dummy" functions in to the Linux x86 PCI core
 *                     to support the Xen PCI Frontend's operation
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <asm/acpi.h>
#include <asm/pci_x86.h>
#include <xen/evtchn.h>

static int pcifront_enable_irq(struct pci_dev *dev)
{
	u8 irq;
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
	if (!alloc_irq_and_cfg_at(irq, numa_node_id()))
		return -ENOMEM;
	evtchn_register_pirq(irq, irq);
	dev->irq = irq;

	return 0;
}

static int __init pcifront_x86_stub_init(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	/* Only install our method if we haven't found real hardware already */
	if (raw_pci_ops)
		return 0;

	pr_info("PCI: setting up Xen PCI frontend stub\n");

	/* Copied from arch/i386/pci/common.c */
	if (c->x86_clflush_size > 0) {
		pci_dfl_cache_line_size = c->x86_clflush_size >> 2;
		printk(KERN_DEBUG "PCI: pci_cache_line_size set to %d bytes\n",
			pci_dfl_cache_line_size << 2);
	} else {
 		pci_dfl_cache_line_size = 32 >> 2;
		printk(KERN_DEBUG "PCI: Unknown cacheline size. Setting to 32 bytes\n");
	}

	/* On x86, we need to disable the normal IRQ routing table and
	 * just ask the backend
	 */
	pcibios_enable_irq = pcifront_enable_irq;
	pcibios_disable_irq = NULL;

#ifdef CONFIG_ACPI
	/* Keep ACPI out of the picture */
	acpi_noirq = 1;
#endif

	return 0;
}

arch_initcall(pcifront_x86_stub_init);
