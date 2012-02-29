/*
 * PCI Frontend - arch-dependendent declarations
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#ifndef __XEN_ASM_PCIFRONT_H__
#define __XEN_ASM_PCIFRONT_H__

#include <linux/spinlock.h>

#ifdef __KERNEL__

#ifndef __ia64__

#include <asm/pci.h>

struct pcifront_device;
struct pci_bus;
#define pcifront_sd pci_sysdata

static inline struct pcifront_device *
pcifront_get_pdev(struct pcifront_sd *sd)
{
	return sd->pdev;
}

static inline void pcifront_init_sd(struct pcifront_sd *sd,
				    unsigned int domain, unsigned int bus,
				    struct pcifront_device *pdev)
{
	sd->domain = domain;
	sd->pdev = pdev;
}

static inline void pcifront_setup_root_resources(struct pci_bus *bus,
						 struct pcifront_sd *sd)
{
}

#else /* __ia64__ */

#include <linux/acpi.h>
#include <asm/pci.h>
#define pcifront_sd pci_controller

extern void xen_add_resource(struct pci_controller *, unsigned int,
			     unsigned int, struct acpi_resource *);
extern void xen_pcibios_setup_root_windows(struct pci_bus *,
					   struct pci_controller *);

static inline struct pcifront_device *
pcifront_get_pdev(struct pcifront_sd *sd)
{
	return (struct pcifront_device *)sd->platform_data;
}

static inline void pcifront_setup_root_resources(struct pci_bus *bus,
						 struct pcifront_sd *sd)
{
	xen_pcibios_setup_root_windows(bus, sd);
}

#endif /* __ia64__ */

extern struct rw_semaphore pci_bus_sem;

#endif /* __KERNEL__ */

#endif /* __XEN_ASM_PCIFRONT_H__ */
