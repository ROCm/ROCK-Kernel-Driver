/*
 *	drivers/pci/setup-res.c
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *
 * Support routines for initializing a PCI subsystem.
 */

/* fixed for multiple pci buses, 1999 Andrea Arcangeli <andrea@suse.de> */

/*
 * Nov 2000, Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 *	     Resource sorting
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>
#include <linux/slab.h>


#define DEBUG_CONFIG 0
#if DEBUG_CONFIG
# define DBGC(args)     printk args
#else
# define DBGC(args)
#endif


int __init
pci_claim_resource(struct pci_dev *dev, int resource)
{
	struct resource *res = &dev->resource[resource];
	struct resource *root = pci_find_parent_resource(dev, res);
	char *dtype = resource < PCI_BRIDGE_RESOURCES ? "device" : "bridge";
	int err;

	err = -EINVAL;
	if (root != NULL)
		err = request_resource(root, res);

	if (err) {
		printk(KERN_ERR "PCI: %s region %d of %s %s [%lx:%lx]\n",
		       root ? "Address space collision on" :
			      "No parent found for",
		       resource, dtype, dev->slot_name, res->start, res->end);
	}

	return err;
}

/*
 * Given the PCI bus a device resides on, try to
 * find an acceptable resource allocation for a
 * specific device resource..
 */
static int pci_assign_bus_resource(const struct pci_bus *bus,
	struct pci_dev *dev,
	struct resource *res,
	unsigned long size,
	unsigned long min,
	unsigned int type_mask,
	int resno)
{
	unsigned long align;
	int i;

	type_mask |= IORESOURCE_IO | IORESOURCE_MEM;
	for (i = 0 ; i < PCI_BUS_NUM_RESOURCES; i++) {
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

		/* The bridge resources are special, as their
		   size != alignment. Sizing routines return
		   required alignment in the "start" field. */
		align = (resno < PCI_BRIDGE_RESOURCES) ? size : res->start;

		/* Ok, try it out.. */
		if (allocate_resource(r, res, size, min, -1, align,
				      pcibios_align_resource, dev) < 0)
			continue;

		/* Update PCI config space.  */
		pcibios_update_resource(dev, r, res, resno);
		return 0;
	}
	return -EBUSY;
}

int 
pci_assign_resource(struct pci_dev *dev, int i)
{
	const struct pci_bus *bus = dev->bus;
	struct resource *res = dev->resource + i;
	unsigned long size, min;

	size = res->end - res->start + 1;
	min = (res->flags & IORESOURCE_IO) ? PCIBIOS_MIN_IO : PCIBIOS_MIN_MEM;

	/* First, try exact prefetching match.. */
	if (pci_assign_bus_resource(bus, dev, res, size, min, IORESOURCE_PREFETCH, i) < 0) {
		/*
		 * That failed.
		 *
		 * But a prefetching area can handle a non-prefetching
		 * window (it will just not perform as well).
		 */
		if (!(res->flags & IORESOURCE_PREFETCH) || pci_assign_bus_resource(bus, dev, res, size, min, 0, i) < 0) {
			printk(KERN_ERR "PCI: Failed to allocate resource %d(%lx-%lx) for %s\n",
			       i, res->start, res->end, dev->slot_name);
			return -EBUSY;
		}
	}

	DBGC((KERN_ERR "  got res[%lx:%lx] for resource %d of %s\n", res->start,
						res->end, i, dev->dev.name));

	return 0;
}

/* Sort resources by alignment */
void __devinit
pdev_sort_resources(struct pci_dev *dev, struct resource_list *head)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *r;
		struct resource_list *list, *tmp;
		unsigned long r_align;

		r = &dev->resource[i];
		r_align = r->end - r->start;
		
		if (!(r->flags) || r->parent)
			continue;
		if (!r_align) {
			printk(KERN_WARNING "PCI: Ignore bogus resource %d "
					    "[%lx:%lx] of %s\n",
					    i, r->start, r->end, dev->dev.name);
			continue;
		}
		r_align = (i < PCI_BRIDGE_RESOURCES) ? r_align + 1 : r->start;
		for (list = head; ; list = list->next) {
			unsigned long align = 0;
			struct resource_list *ln = list->next;
			int idx;

			if (ln) {
				idx = ln->res - &ln->dev->resource[0];
				align = (idx < PCI_BRIDGE_RESOURCES) ?
					ln->res->end - ln->res->start + 1 :
					ln->res->start;
			}
			if (r_align > align) {
				tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
				if (!tmp)
					panic("pdev_sort_resources(): "
					      "kmalloc() failed!\n");
				tmp->next = ln;
				tmp->res = r;
				tmp->dev = dev;
				list->next = tmp;
				break;
			}
		}
	}
}
