#include <linux/pci.h>
#include <linux/module.h>

static struct pci_bus *
pci_do_find_bus(struct pci_bus* bus, unsigned char busnr)
{
	struct pci_bus* child;
	struct list_head *tmp;

	if(bus->number == busnr)
		return bus;

	list_for_each(tmp, &bus->children) {
		child = pci_do_find_bus(pci_bus_b(tmp), busnr);
		if(child)
			return child;
	}
	return NULL;
}

/**
 * pci_find_bus - locate PCI bus from a given bus number
 * @busnr: number of desired PCI bus
 *
 * Given a PCI bus number, the desired PCI bus is located in system
 * global list of PCI buses.  If the bus is found, a pointer to its
 * data structure is returned.  If no bus is found, %NULL is returned.
 */
struct pci_bus *
pci_find_bus(unsigned char busnr)
{
	struct pci_bus* bus;
	struct pci_bus* tmp_bus;

	pci_for_each_bus(bus) {
		tmp_bus = pci_do_find_bus(bus, busnr);
		if(tmp_bus)
			return tmp_bus;
	}
	return NULL;
}

/**
 * pci_find_slot - locate PCI device from a given PCI slot
 * @bus: number of PCI bus on which desired PCI device resides
 * @devfn: encodes number of PCI slot in which the desired PCI 
 * device resides and the logical device number within that slot 
 * in case of multi-function devices.
 *
 * Given a PCI bus and slot/function number, the desired PCI device 
 * is located in system global list of PCI devices.  If the device
 * is found, a pointer to its data structure is returned.  If no 
 * device is found, %NULL is returned.
 */
struct pci_dev *
pci_find_slot(unsigned int bus, unsigned int devfn)
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		if (dev->bus->number == bus && dev->devfn == devfn)
			return dev;
	}
	return NULL;
}

/**
 * pci_find_subsys - begin or continue searching for a PCI device by vendor/subvendor/device/subdevice id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @ss_vendor: PCI subsystem vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @ss_device: PCI subsystem device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is
 * found with a matching @vendor, @device, @ss_vendor and @ss_device, a pointer to its
 * device structure is returned.  Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL to the @from argument.
 * Otherwise if @from is not %NULL, searches continue from next device on the global list.
 */
struct pci_dev *
pci_find_subsys(unsigned int vendor, unsigned int device,
		unsigned int ss_vendor, unsigned int ss_device,
		const struct pci_dev *from)
{
	struct list_head *n = from ? from->global_list.next : pci_devices.next;

	while (n != &pci_devices) {
		struct pci_dev *dev = pci_dev_g(n);
		if ((vendor == PCI_ANY_ID || dev->vendor == vendor) &&
		    (device == PCI_ANY_ID || dev->device == device) &&
		    (ss_vendor == PCI_ANY_ID || dev->subsystem_vendor == ss_vendor) &&
		    (ss_device == PCI_ANY_ID || dev->subsystem_device == ss_device))
			return dev;
		n = n->next;
	}
	return NULL;
}


/**
 * pci_find_device - begin or continue searching for a PCI device by vendor/device id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is
 * found with a matching @vendor and @device, a pointer to its device structure is
 * returned.  Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL to the @from argument.
 * Otherwise if @from is not %NULL, searches continue from next device on the global list.
 */
struct pci_dev *
pci_find_device(unsigned int vendor, unsigned int device, const struct pci_dev *from)
{
	return pci_find_subsys(vendor, device, PCI_ANY_ID, PCI_ANY_ID, from);
}


/**
 * pci_find_device_reverse - begin or continue searching for a PCI device by vendor/device id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices in the reverse order of pci_find_device().
 * If a PCI device is found with a matching @vendor and @device, a pointer to
 * its device structure is returned.  Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL to the @from argument.
 * Otherwise if @from is not %NULL, searches continue from previous device on the global list.
 */
struct pci_dev *
pci_find_device_reverse(unsigned int vendor, unsigned int device, const struct pci_dev *from)
{
	struct list_head *n = from ? from->global_list.prev : pci_devices.prev;

	while (n != &pci_devices) {
		struct pci_dev *dev = pci_dev_g(n);
		if ((vendor == PCI_ANY_ID || dev->vendor == vendor) &&
		    (device == PCI_ANY_ID || dev->device == device))
			return dev;
		n = n->prev;
	}
	return NULL;
}


/**
 * pci_find_class - begin or continue searching for a PCI device by class
 * @class: search for a PCI device with this class designation
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is
 * found with a matching @class, a pointer to its device structure is
 * returned.  Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL to the @from argument.
 * Otherwise if @from is not %NULL, searches continue from next device
 * on the global list.
 */
struct pci_dev *
pci_find_class(unsigned int class, const struct pci_dev *from)
{
	struct list_head *n = from ? from->global_list.next : pci_devices.next;

	while (n != &pci_devices) {
		struct pci_dev *dev = pci_dev_g(n);
		if (dev->class == class)
			return dev;
		n = n->next;
	}
	return NULL;
}

/**
 * pci_present - determine if there are any pci devices on this system
 *
 * Returns 0 if no pci devices are present, 1 if pci devices are present.
 */
int pci_present(void)
{
	return !list_empty(&pci_devices);
}

EXPORT_SYMBOL(pci_find_bus);
EXPORT_SYMBOL(pci_find_class);
EXPORT_SYMBOL(pci_find_device);
EXPORT_SYMBOL(pci_find_device_reverse);
EXPORT_SYMBOL(pci_find_slot);
EXPORT_SYMBOL(pci_find_subsys);
EXPORT_SYMBOL(pci_present);
