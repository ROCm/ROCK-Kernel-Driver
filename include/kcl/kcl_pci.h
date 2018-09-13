#ifndef AMDKCL_PCI_H
#define AMDKCL_PCI_H

#include <linux/pci.h>
#include <linux/version.h>

#ifdef BUILD_AS_DKMS

#define PCI_EXP_DEVCAP2_ATOMIC_ROUTE	0x00000040 /* Atomic Op routing */
#define PCI_EXP_DEVCAP2_ATOMIC_COMP32	0x00000080 /* 32b AtomicOp completion */
#define PCI_EXP_DEVCAP2_ATOMIC_COMP64	0x00000100 /* 64b AtomicOp completion*/
#define PCI_EXP_DEVCAP2_ATOMIC_COMP128	0x00000200 /* 128b AtomicOp completion*/
#define PCI_EXP_DEVCTL2_ATOMIC_REQ	0x0040	/* Set Atomic requests */
#define PCI_EXP_DEVCTL2_ATOMIC_BLOCK	0x0040	/* Block AtomicOp on egress */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
int pci_enable_atomic_ops_to_root(struct pci_dev *dev, u32 comp_caps);
#endif

void _kcl_pci_configure_extended_tags(struct pci_dev *dev);

#endif

static inline void kcl_pci_configure_extended_tags(struct pci_dev *dev)
{
#if defined(BUILD_AS_DKMS) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	_kcl_pci_configure_extended_tags(dev);
#endif
}

#endif /* AMDKCL_PCI_H */
