/*
 * PCI Hot Plug Controller Driver for RPA-compliant PPC64 platform.
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <lxie@us.ibm.com>
 *
 */
#include <linux/pci.h>
#include <asm/pci-bridge.h>	/* for pci_controller */
#include "rpaphp.h"


struct pci_dev *rpaphp_find_pci_dev(struct device_node *dn)
{
	struct pci_dev		*retval_dev = NULL, *dev = NULL;

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		if(!dev->bus)
			continue;

		if (dev->devfn != dn->devfn)
			continue;

		if (dn->phb->global_number == pci_domain_nr(dev->bus) &&
		    dn->busno == dev->bus->number) {
			retval_dev = dev;
			break;
		}
	}

	return retval_dev;

}

int rpaphp_claim_resource(struct pci_dev *dev, int resource)
{
	struct resource *res = &dev->resource[resource];
	struct resource *root = pci_find_parent_resource(dev, res);
	char *dtype = resource < PCI_BRIDGE_RESOURCES ? "device" : "bridge";
	int err;

	err = -EINVAL;
	if (root != NULL) {
		err = request_resource(root, res);
	}

	if (err) {
		err("PCI: %s region %d of %s %s [%lx:%lx]\n",
			root ? "Address space collision on" :
			"No parent found for",
			resource, dtype, pci_name(dev), res->start, res->end);
	}

	return err;
}

EXPORT_SYMBOL_GPL(rpaphp_find_pci_dev);
EXPORT_SYMBOL_GPL(rpaphp_claim_resource);
