#ifndef AMDKCL_PCI_H
#define AMDKCL_PCI_H

#include <linux/pci.h>

#ifdef BUILD_AS_DKMS
int pci_enable_atomic_ops_to_root(struct pci_dev *dev);
#endif

#endif /* AMDKCL_PCI_H */
