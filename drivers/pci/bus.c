/*
 *	drivers/pci/bus.c
 *
 * From setup-res.c, by:
 *	Dave Rusling (david.rusling@reo.mts.dec.com)
 *	David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *	Ivan Kokshaysky (ink@jurassic.park.msu.ru)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>

/**
 * pci_bus_alloc_resource - allocate a resource from a parent bus
 * @bus: PCI bus
 * @res: resource to allocate
 * @size: size of resource to allocate
 * @align: alignment of resource to allocate
 * @min: minimum /proc/iomem address to allocate
 * @type_mask: IORESOURCE_* type flags
 * @alignf: resource alignment function
 * @alignf_data: data argument for resource alignment function
 *
 * Given the PCI bus a device resides on, the size, minimum address,
 * alignment and type, try to find an acceptable resource allocation
 * for a specific device resource.
 */
int
pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
	unsigned long size, unsigned long align, unsigned long min,
	unsigned int type_mask,
	void (*alignf)(void *, struct resource *,
			unsigned long, unsigned long),
	void *alignf_data)
{
	int i, ret = -ENOMEM;

	type_mask |= IORESOURCE_IO | IORESOURCE_MEM;

	for (i = 0; i < PCI_BUS_NUM_RESOURCES; i++) {
		struct resource *r = bus->resource[i];
		if (!r)
			continue;

		/* type_mask must match */
		if ((res->flags ^ r->flags) & type_mask)
			continue;

		/* We cannot allocate a non-prefetching resource
		   from a pre-fetching area */
		if ((r->flags & IORESOURCE_PREFETCH) &&
		    !(res->flags & IORESOURCE_PREFETCH))
			continue;

		/* Ok, try it out.. */
		ret = allocate_resource(r, res, size, min, -1, align,
					alignf, alignf_data);
		if (ret == 0)
			break;
	}
	return ret;
}

void pci_enable_bridges(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->subordinate) {
			pci_enable_device(dev);
			pci_set_master(dev);
			pci_enable_bridges(dev->subordinate);
		}
	}
}

EXPORT_SYMBOL(pci_bus_alloc_resource);
EXPORT_SYMBOL(pci_enable_bridges);
