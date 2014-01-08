/*
 * PCI Frontend - arch-dependendent declarations
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#ifndef __XEN_ASM_PCIFRONT_H__
#define __XEN_ASM_PCIFRONT_H__

#ifdef __KERNEL__

#include <linux/pci.h>

int pci_frontend_enable_msi(struct pci_dev *, unsigned int nvec);
void pci_frontend_disable_msi(struct pci_dev *);
int pci_frontend_enable_msix(struct pci_dev *, struct msix_entry *, int nvec);
void pci_frontend_disable_msix(struct pci_dev *);

#ifdef __ia64__

#include <linux/acpi.h>

extern void xen_add_resource(struct pci_controller *, unsigned int,
			     unsigned int, struct acpi_resource *);
extern void xen_pcibios_setup_root_windows(struct pci_bus *,
					   struct pci_controller *);

#endif /* __ia64__ */

#endif /* __KERNEL__ */

#endif /* __XEN_ASM_PCIFRONT_H__ */
