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
#include <asm/pci-bridge.h>
#include "../pci.h"		/* for pci_add_new_bus */

#include "rpaphp.h"

struct pci_dev *rpaphp_find_pci_dev(struct device_node *dn)
{
	struct pci_dev *retval_dev = NULL, *dev = NULL;
	char bus_id[BUS_ID_SIZE];

	sprintf(bus_id, "%04x:%02x:%02x.%d",dn->phb->global_number,
		dn->busno, PCI_SLOT(dn->devfn), PCI_FUNC(dn->devfn));

	dbg("Enter rpaphp_find_pci_dev() full_name=%s bus_id=%s\n", 
		dn->full_name, bus_id);
	
	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
               if (!strcmp(pci_name(dev), bus_id)) {
			retval_dev = dev;
			dbg("rpaphp_find_pci_dev(): found dev=%p\n\n", dev);
			break;
		}
	}
	return retval_dev;

}

EXPORT_SYMBOL_GPL(rpaphp_find_pci_dev);

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

EXPORT_SYMBOL_GPL(rpaphp_claim_resource);

static struct pci_dev *rpaphp_find_bridge_pdev(struct slot *slot)
{
	return rpaphp_find_pci_dev(slot->dn);
}

static struct pci_dev *rpaphp_find_adapter_pdev(struct slot *slot)
{
	return rpaphp_find_pci_dev(slot->dn->child);
}

static int rpaphp_get_sensor_state(struct slot *slot, int *state)
{
	int rc;
	int setlevel;

	rc = rtas_get_sensor(DR_ENTITY_SENSE, slot->index, state);

	if (rc) {
		if (rc == NEED_POWER || rc == PWR_ONLY) {
			dbg("%s: slot must be power up to get sensor-state\n",
			    __FUNCTION__);

			/* some slots have to be powered up 
			 * before get-sensor will succeed.
			 */
			rc = rtas_set_power_level(slot->power_domain, POWER_ON,
						  &setlevel);
			if (rc) {
				dbg("%s: power on slot[%s] failed rc=%d.\n",
				    __FUNCTION__, slot->name, rc);
			} else {
				rc = rtas_get_sensor(DR_ENTITY_SENSE,
						     slot->index, state);
			}
		} else if (rc == ERR_SENSE_USE)
			info("%s: slot is unusable\n", __FUNCTION__);
		else
			err("%s failed to get sensor state\n", __FUNCTION__);
	}
	return rc;
}

/*
 * get_pci_adapter_status - get  the status of a slot
 * 
 * 0-- slot is empty
 * 1-- adapter is configured
 * 2-- adapter is not configured
 * 3-- not valid
 */
int rpaphp_get_pci_adapter_status(struct slot *slot, int is_init, u8 * value)
{
	int state, rc;
	*value = NOT_VALID;

	rc = rpaphp_get_sensor_state(slot, &state);
	if (rc)
		goto exit;
	if (state == PRESENT) {
		if (!is_init)
			/* at run-time slot->state can be changed by */
			/* config/unconfig adapter                        */
			*value = slot->state;
		else {
			if (!slot->dn->child)
				dbg("%s: %s is not valid OFDT node\n",
				    __FUNCTION__, slot->dn->full_name);
			else if (rpaphp_find_pci_dev(slot->dn->child))
				*value = CONFIGURED;
			else {
				dbg("%s: can't find pdev of adapter in slot[%s]\n", __FUNCTION__, slot->name);
				*value = NOT_CONFIGURED;
			}
		}
	} else if (state == EMPTY) {
		dbg("slot is empty\n");
		*value = state;
	}

      exit:
	return rc;
}

/* Must be called before pci_bus_add_devices */
static void rpaphp_fixup_new_pci_devices(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/*
		 * Skip already-present devices (which are on the
		 * global device list.)
		 */
		if (list_empty(&dev->global_list)) {
			int i;

			pcibios_fixup_device_resources(dev, bus);
			pci_read_irq_line(dev);
			for (i = 0; i < PCI_NUM_RESOURCES; i++) {
				struct resource *r = &dev->resource[i];
				if (r->parent || !r->start || !r->flags)
					continue;
				rpaphp_claim_resource(dev, i);
			}
		}
	}
}

static void rpaphp_pci_config_device(struct pci_bus *pci_bus, struct device_node *dn)
{
	int num;

	num = pci_scan_slot(pci_bus, PCI_DEVFN(PCI_SLOT(dn->devfn), 0));
	if (num) {
		rpaphp_fixup_new_pci_devices(pci_bus);
		pci_bus_add_devices(pci_bus);
	}
	return;
}

static int rpaphp_pci_config_bridge(struct pci_dev *dev, struct device_node *dn);

/*****************************************************************************
 rpaphp_pci_config_dn() will recursively configure all devices under the 
 given slot->dn and return the dn's pci_dev.
 *****************************************************************************/
static struct pci_dev *rpaphp_pci_config_dn(struct device_node *dn, struct pci_bus *bus)
{
	struct device_node *local;
	struct pci_dev *dev;

	for (local = dn->child; local; local = local->sibling) {
		rpaphp_pci_config_device(bus, local);
		dev = rpaphp_find_pci_dev(local);
		if (!rpaphp_pci_config_bridge(dev, local))
			return NULL;
	}

	return dev;
}

static int rpaphp_pci_config_bridge(struct pci_dev *dev, struct device_node *dn)
{
	if (dev && dn->child) {	/* dn is a PCI bridge node */
		struct pci_bus *child;
		u8 sec_busno;

		/* get busno of downstream bus */
		pci_read_config_byte(dev, PCI_SECONDARY_BUS, &sec_busno);

		/* add to children of PCI bridge dev->bus */
		child = pci_add_new_bus(dev->bus, dev, sec_busno);
		if (!child) {
			err("%s: could not add second bus\n", __FUNCTION__);
			return 0;
		}
		sprintf(child->name, "PCI Bus #%02x", child->number);
		/* Fixup subordinate bridge bases and resureces */
		pcibios_fixup_bus(child);

		/* may need do more stuff here */
		rpaphp_pci_config_dn(dn, dev->subordinate);
	}
	return 1;
}

static struct pci_dev *rpaphp_config_pci_adapter(struct slot *slot)
{
	struct pci_bus *pci_bus;
	struct pci_dev *dev = NULL;

	dbg("Entry %s: slot[%s]\n", __FUNCTION__, slot->name);

	if (slot->bridge) {

		pci_bus = slot->bridge->subordinate;
		if (!pci_bus) {
			err("%s: can't find bus structure\n", __FUNCTION__);
			goto exit;
		}

		dev = rpaphp_pci_config_dn(slot->dn, pci_bus);
		eeh_add_device(dev);
	} else {
		/* slot is not enabled */
		err("slot doesn't have pci_dev structure\n");
		dev = NULL;
		goto exit;
	}

      exit:
	dbg("Exit %s: pci_dev %s\n", __FUNCTION__, dev ? "found" : "not found");
	return dev;
}

int rpaphp_unconfig_pci_adapter(struct slot *slot)
{
	int retval = 0;

	dbg("Entry %s: slot[%s]\n", __FUNCTION__, slot->name);
	if (!slot->dev.pci_dev) {
		info("%s: no card in slot[%s]\n", __FUNCTION__, slot->name);

		retval = -EINVAL;
		goto exit;
	}
	/* remove the device from the pci core */
	eeh_remove_device(slot->dev.pci_dev);
	pci_remove_bus_device(slot->dev.pci_dev);

	slot->state = NOT_CONFIGURED;
	info("%s: adapter in slot[%s] unconfigured.\n", __FUNCTION__,
	     slot->name);
exit:
	dbg("Exit %s, rc=0x%x\n", __FUNCTION__, retval);
	return retval;
}

static int setup_pci_hotplug_slot_info(struct slot *slot)
{
	dbg("%s Initilize the PCI slot's hotplug->info structure ...\n",
	    __FUNCTION__);
	rpaphp_get_power_status(slot, &slot->hotplug_slot->info->power_status);
	rpaphp_get_pci_adapter_status(slot, 1,
				      &slot->hotplug_slot->info->
				      adapter_status);
	if (slot->hotplug_slot->info->adapter_status == NOT_VALID) {
		dbg("%s: NOT_VALID: skip dn->full_name=%s\n",
		    __FUNCTION__, slot->dn->full_name);
		return (-1);
	}
	return (0);
}

static int setup_pci_slot(struct slot *slot)
{
	slot->bridge = rpaphp_find_bridge_pdev(slot);
	if (!slot->bridge) {	/* slot being added doesn't have pci_dev yet */
		dbg("%s: no pci_dev for bridge dn %s\n", __FUNCTION__, slot->name);
		dealloc_slot_struct(slot);
		return 1;
	}

	/* find slot's pci_dev if it's not empty */
	if (slot->hotplug_slot->info->adapter_status == EMPTY) {
		slot->state = EMPTY;	/* slot is empty */
		slot->dev.pci_dev = NULL;
	} else {
		/* slot is occupied */
		if (!(slot->dn->child)) {
			/* non-empty slot has to have child */
			err("%s: slot[%s]'s device_node doesn't have child for adapter\n", __FUNCTION__, slot->name);
			dealloc_slot_struct(slot);
			return 1;
		}
		slot->dev.pci_dev = rpaphp_find_adapter_pdev(slot);
		if (slot->dev.pci_dev) {
			slot->state = CONFIGURED;
		
		} else {
			/* DLPAR add as opposed to 
			 * boot time */
			slot->state = NOT_CONFIGURED;
		}
	}
	return 0;
}

int register_pci_slot(struct slot *slot)
{
	int rc = 1;

	slot->dev_type = PCI_DEV;
	if (setup_pci_hotplug_slot_info(slot))
		goto exit_rc;
	if (setup_pci_slot(slot))
		goto exit_rc;
	rc = register_slot(slot);
      exit_rc:
	if (rc)
		dealloc_slot_struct(slot);
	return rc;
}

int rpaphp_enable_pci_slot(struct slot *slot)
{
	int retval = 0, state;

	retval = rpaphp_get_sensor_state(slot, &state);
	if (retval)
		goto exit;
	dbg("%s: sensor state[%d]\n", __FUNCTION__, state);
	/* if slot is not empty, enable the adapter */
	if (state == PRESENT) {
		dbg("%s : slot[%s] is occupid.\n", __FUNCTION__, slot->name);
		if ((slot->dev.pci_dev =
		     rpaphp_config_pci_adapter(slot)) != NULL) {
			slot->state = CONFIGURED;
			dbg("%s: PCI adapter %s in slot[%s] has been configured\n", 
				__FUNCTION__, pci_name(slot->dev.pci_dev), slot->name);
		} else {
			slot->state = NOT_CONFIGURED;
			dbg("%s: no pci_dev struct for adapter in slot[%s]\n",
			    __FUNCTION__, slot->name);
		}
	} else if (state == EMPTY) {
		dbg("%s : slot[%s] is empty\n", __FUNCTION__, slot->name);
		slot->state = EMPTY;
	} else {
		err("%s: slot[%s] is in invalid state\n", __FUNCTION__,
		    slot->name);
		slot->state = NOT_VALID;
		retval = -EINVAL;
	}
      exit:
	if (slot->state != NOT_VALID)
		rpaphp_set_attention_status(slot, LED_ON);
	else
		rpaphp_set_attention_status(slot, LED_ID);
	dbg("%s - Exit: rc[%d]\n", __FUNCTION__, retval);
	return retval;
}
