/*
 * Interface for Dynamic Logical Partitioning of I/O Slots
 *
 * John Rose <johnrose@austin.ibm.com>
 * October 2003
 *
 * Copyright (C) 2003 IBM.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/pci-bridge.h>
#include "../pci.h"
#include "rpaphp.h"
#include "rpadlpar.h"

#define MODULE_VERSION "1.0"
#define MODULE_NAME "rpadlpar_io"

static inline int is_hotplug_capable(struct device_node *dn)
{
	unsigned char *ptr = get_property(dn, "ibm,fw-pci-hot-plug-ctrl", NULL);

	return (int) (ptr != NULL);
}

static char *get_node_drc_name(struct device_node *dn)
{
	char *ptr = NULL;
	int *drc_names;

	if ((drc_names = (int *) get_property(dn, "ibm,drc-names", NULL)))
		ptr = (char *) &drc_names[1];

	return ptr;
}

static struct device_node *find_php_slot_node(char *drc_name)
{
	struct device_node *np = NULL;
	char *name;

	while ((np = of_find_node_by_type(np, "pci")))
		if (is_hotplug_capable(np)) {
			name = get_node_drc_name(np);
			if (name && (!strcmp(drc_name, name)))
				break;
		}

	return np;
}

static inline struct hotplug_slot *find_php_slot(char *drc_name)
{
	struct kobject *k;

	if (!(k = kset_find_obj(&pci_hotplug_slots_subsys.kset, drc_name)))
		return NULL;

	return to_hotplug_slot(k);
}

static struct slot *find_slot(char *drc_name)
{
	struct hotplug_slot *php_slot;

	if (!(php_slot = find_php_slot(drc_name)))
		return NULL;

	return (struct slot *) php_slot->private;
}

static void rpadlpar_claim_one_bus(struct pci_bus *b)
{
	struct list_head *ld;
	struct pci_bus *child_bus;

	for (ld = b->devices.next; ld != &b->devices; ld = ld->next) {
		struct pci_dev *dev = pci_dev_b(ld);
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;
			rpaphp_claim_resource(dev, i);
		}
	}

	list_for_each_entry(child_bus, &b->children, node)
		rpadlpar_claim_one_bus(child_bus);
}

static int pci_add_secondary_bus(struct device_node *dn,
		struct pci_dev *bridge_dev)
{
	struct pci_controller *hose = dn->phb;
	struct pci_bus *child;
	u8 sec_busno;

	/* Get busno of downstream bus */
	pci_read_config_byte(bridge_dev, PCI_SECONDARY_BUS, &sec_busno);

	/* Allocate and add to children of bridge_dev->bus */
	child = pci_add_new_bus(bridge_dev->bus, bridge_dev, sec_busno);
	if (!child) {
		printk(KERN_ERR "%s: could not add secondary bus\n", __FUNCTION__);
		return 1;
	}

	sprintf(child->name, "PCI Bus #%02x", child->number);

	/* Fixup subordinate bridge bases and resources */
	pcibios_fixup_bus(child);

	/* Claim new bus resources */
	rpadlpar_claim_one_bus(bridge_dev->bus);

	if (hose->last_busno < child->number)
	    	hose->last_busno = child->number;

	dn->bussubno = child->number;

	/* ioremap() for child bus */
	if (remap_bus_range(child)) {
		printk(KERN_ERR "%s: could not ioremap() child bus\n",
				__FUNCTION__);
		return 1;
	}

	return 0;
}

static struct pci_dev *dlpar_pci_add_bus(struct device_node *dn)
{
	struct pci_controller *hose = dn->phb;
	struct pci_dev *dev = NULL;

	/* Scan phb bus for devices, adding new ones to bus->devices */
	if (!pci_scan_slot(hose->bus, dn->devfn)) {
		printk(KERN_ERR "%s: found no devices on bus\n", __FUNCTION__);
		return NULL;
	}

	/* Add new devices to global lists.  Register in proc, sysfs. */
	pci_bus_add_devices(hose->bus);

	/* Confirm new bridge dev was created */
	if (!(dev = rpaphp_find_pci_dev(dn))) {
		printk(KERN_ERR "%s: failed to add pci device\n", __FUNCTION__);
		return NULL;
	}

	if (dev->hdr_type != PCI_HEADER_TYPE_BRIDGE)  {
		printk(KERN_ERR "%s: unexpected header type %d\n",
				__FUNCTION__, dev->hdr_type);
		return NULL;
	}

	if (pci_add_secondary_bus(dn, dev))
		return NULL;

	return dev;
}

static int dlpar_pci_remove_bus(struct pci_dev *bridge_dev)
{
	struct pci_bus *secondary_bus;

	if (!bridge_dev) {
		printk(KERN_ERR "%s: %s() unexpected null device\n", 
				MODULE_NAME, __FUNCTION__);
		return 1;
	}

	secondary_bus = bridge_dev->subordinate;

	if (unmap_bus_range(secondary_bus)) {
		printk(KERN_ERR "%s: failed to unmap bus range\n", 
				__FUNCTION__);
		return 1;
	}

	pci_remove_bus_device(bridge_dev);

	return 0;
}

/**
 * dlpar_add_slot - DLPAR add an I/O Slot
 * @drc_name: drc-name of newly added slot
 *
 * Make the hotplug module and the kernel aware
 * of a newly added I/O Slot.
 * Return Codes -
 * 0			Success
 * -ENODEV		Not a valid drc_name
 * -EINVAL		Slot already added
 * -EIO			Internal PCI Error
 */
int dlpar_add_slot(char *drc_name)
{
	struct device_node *dn = find_php_slot_node(drc_name);
	struct pci_dev *dev;

	if (!dn)
		return -ENODEV;

	/* Check for existing hotplug slot */
	if (find_slot(drc_name))
		return -EINVAL;

	/* Add pci bus */
	dev = dlpar_pci_add_bus(dn);
	if (!dev) {
		printk(KERN_ERR "%s: unable to add bus %s\n", __FUNCTION__,
				drc_name);
		return -EIO;
	}

	/* Add hotplug slot for new bus */
	if (rpaphp_add_slot(drc_name)) {
		printk(KERN_ERR "%s: unable to add hotplug slot %s\n",
				__FUNCTION__, drc_name);
		return -EIO;
	}

	return 0;
}

/**
 * dlpar_remove_slot - DLPAR remove an I/O Slot
 * @drc_name: drc-name of newly added slot
 *
 * Remove the kernel and hotplug representations
 * of an I/O Slot.
 * Return Codes:
 * 0			Success
 * -ENODEV		Not a valid drc_name
 * -EINVAL		Slot already removed
 * -EIO			Internal PCI Error
 */
int dlpar_remove_slot(char *drc_name)
{
	struct device_node *dn = find_php_slot_node(drc_name);
	struct slot *slot;
	struct pci_dev *bridge_dev;

	if (!dn)
		return -ENODEV;

	if (!(slot = find_slot(drc_name)))
		return -EINVAL;

	bridge_dev = slot->bridge;
	if (!bridge_dev) {
		printk(KERN_ERR "%s: %s(): unexpected null bridge device\n",
				MODULE_NAME, __FUNCTION__);
		return -EIO;
	}

	/* Remove hotplug slot */
	if (rpaphp_remove_slot(slot)) {
		printk(KERN_ERR "%s: %s(): unable to remove hotplug slot %s\n",
				MODULE_NAME, __FUNCTION__, drc_name);
		return -EIO;
	}

	/* Remove pci bus */
	if (dlpar_pci_remove_bus(bridge_dev)) {
		printk(KERN_ERR "%s: %s() unable to remove pci bus %s\n",
				MODULE_NAME, __FUNCTION__, drc_name);
		return -EIO;
	}

	return 0;
}

static inline int is_dlpar_capable(void)
{
	int rc = rtas_token("ibm,configure-connector");

	return (int) (rc != RTAS_UNKNOWN_SERVICE);
}

int __init rpadlpar_io_init(void)
{
	int rc;

	if (!is_dlpar_capable()) {
		printk(KERN_WARNING "partition not DLPAR capable, exiting %s\n",
				MODULE_NAME);
		return -EPERM;
	}

	if ((rc = dlpar_sysfs_init()))
		return rc;

	return 0;
}

void rpadlpar_io_exit(void)
{
	dlpar_sysfs_exit();
	return;
}

module_init(rpadlpar_io_init);
module_exit(rpadlpar_io_exit);
MODULE_LICENSE("GPL");
