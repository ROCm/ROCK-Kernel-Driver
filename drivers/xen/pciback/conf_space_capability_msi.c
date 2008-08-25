/*
 * PCI Backend -- Configuration overlay for MSI capability
 */
#include <linux/pci.h>
#include "conf_space.h"
#include "conf_space_capability.h"
#include <xen/interface/io/pciif.h>
#include "pciback.h"

int pciback_enable_msi(struct pciback_device *pdev,
		struct pci_dev *dev, struct xen_pci_op *op)
{
	int otherend = pdev->xdev->otherend_id;
	int status;

	status = pci_enable_msi(dev);

	if (status) {
		printk("error enable msi for guest %x status %x\n", otherend, status);
		op->value = 0;
		return XEN_PCI_ERR_op_failed;
	}

	op->value = dev->irq;
	return 0;
}

int pciback_disable_msi(struct pciback_device *pdev,
		struct pci_dev *dev, struct xen_pci_op *op)
{
	pci_disable_msi(dev);

	op->value = dev->irq;
	return 0;
}

int pciback_enable_msix(struct pciback_device *pdev,
		struct pci_dev *dev, struct xen_pci_op *op)
{
	int result;

	if (op->value > SH_INFO_MAX_VEC)
		return -EINVAL;
	else {
		struct msix_entry entries[op->value];
		int i;

		for (i = 0; i < op->value; i++) {
			entries[i].entry = op->msix_entries[i].entry;
			entries[i].vector = op->msix_entries[i].vector;
		}

		result = pci_enable_msix(dev, entries, op->value);
		op->value = result;
	}

	return result;
}

int pciback_disable_msix(struct pciback_device *pdev,
		struct pci_dev *dev, struct xen_pci_op *op)
{

	pci_disable_msix(dev);

	op->value = dev->irq;
	return 0;
}

