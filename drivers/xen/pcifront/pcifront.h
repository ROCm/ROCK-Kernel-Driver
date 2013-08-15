/*
 * PCI Frontend - Common data structures & function declarations
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#ifndef __XEN_PCIFRONT_H__
#define __XEN_PCIFRONT_H__

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <xen/xenbus.h>
#include <xen/interface/io/pciif.h>
#include <linux/interrupt.h>
#include <xen/pcifront.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

struct pci_bus_entry {
	struct list_head list;
	struct pci_bus *bus;
};

#define _PDEVB_op_active		(0)
#define PDEVB_op_active 		(1 << (_PDEVB_op_active))

struct pcifront_device {
	struct xenbus_device *xdev;
	struct list_head root_buses;
	spinlock_t dev_lock;

	int evtchn;
	int gnt_ref;
	int irq;

	/* Lock this when doing any operations in sh_info */
	spinlock_t sh_info_lock;
	struct xen_pci_sharedinfo *sh_info;
	struct work_struct op_work;
	unsigned long flags;

};

int pcifront_connect(struct pcifront_device *pdev);
void pcifront_disconnect(struct pcifront_device *pdev);

int pcifront_scan_root(struct pcifront_device *pdev,
		       unsigned int domain, unsigned int bus);
int pcifront_rescan_root(struct pcifront_device *pdev,
			 unsigned int domain, unsigned int bus);
void pcifront_free_roots(struct pcifront_device *pdev);

void pcifront_do_aer(struct work_struct *data);

irqreturn_t pcifront_handler_aer(int irq, void *dev);

#ifndef __ia64__

#define pcifront_sd pci_sysdata

static inline struct pcifront_device *
pcifront_get_pdev(struct pcifront_sd *sd)
{
	return sd->pdev;
}

static inline void pcifront_setup_root_resources(struct pci_bus *bus,
						 struct pcifront_sd *sd)
{
}

#else /* __ia64__ */

#define pcifront_sd pci_controller

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

#endif	/* __XEN_PCIFRONT_H__ */
