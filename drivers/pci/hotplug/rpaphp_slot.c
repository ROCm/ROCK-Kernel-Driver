/*
 * RPA Virtual I/O device functions 
 * Copyright (C) 2004 Linda Xie <lxie@us.ibm.com>
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/pci.h>
#include "rpaphp.h"

static ssize_t location_read_file (struct hotplug_slot *php_slot, char *buf)
{
        char *value;
        int retval = -ENOENT;
	struct slot *slot = (struct slot *)php_slot->private;

	if (!slot)
		return retval;

        value = slot->location;
        retval = sprintf (buf, "%s\n", value);
        return retval;
}

static struct hotplug_slot_attribute hotplug_slot_attr_location = {
	.attr = {.name = "phy_location", .mode = S_IFREG | S_IRUGO},
	.show = location_read_file,
};

static void rpaphp_sysfs_add_attr_location (struct hotplug_slot *slot)
{
	sysfs_create_file(&slot->kobj, &hotplug_slot_attr_location.attr);
}

void rpaphp_sysfs_remove_attr_location (struct hotplug_slot *slot)
{
	sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_location.attr);
}

/* free up the memory user by a slot */
static void rpaphp_release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot? (struct slot *) hotplug_slot->private:NULL;

	if (slot == NULL)
		return;

	dealloc_slot_struct(slot);
}

void dealloc_slot_struct(struct slot *slot)
{
	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot->name);
	kfree(slot->hotplug_slot);
	kfree(slot);
	return;
}

struct slot *alloc_slot_struct(struct device_node *dn, int drc_index, char *drc_name,
		  int power_domain)
{
	struct slot *slot;
	
	dbg("Enter alloc_slot_struct(): dn->full_name=%s drc_index=0x%x drc_name=%s\n",
		dn->full_name, drc_index, drc_name);

	slot = kmalloc(sizeof (struct slot), GFP_KERNEL);
	if (!slot)
		return (NULL);
	memset(slot, 0, sizeof (struct slot));
	slot->hotplug_slot = kmalloc(sizeof (struct hotplug_slot), GFP_KERNEL);
	if (!slot->hotplug_slot) {
		kfree(slot);
		return (NULL);
	}
	memset(slot->hotplug_slot, 0, sizeof (struct hotplug_slot));
	slot->hotplug_slot->info = kmalloc(sizeof (struct hotplug_slot_info),
					   GFP_KERNEL);
	if (!slot->hotplug_slot->info) {
		kfree(slot->hotplug_slot);
		kfree(slot);
		return (NULL);
	}
	memset(slot->hotplug_slot->info, 0, sizeof (struct hotplug_slot_info));
	slot->hotplug_slot->name = kmalloc(BUS_ID_SIZE + 1, GFP_KERNEL);
	if (!slot->hotplug_slot->name) {
		kfree(slot->hotplug_slot->info);
		kfree(slot->hotplug_slot);
		kfree(slot);
		return (NULL);
	}
	slot->location = kmalloc(strlen(drc_name) + 1, GFP_KERNEL);
	if (!slot->location) {
		kfree(slot->hotplug_slot->info);
		kfree(slot->hotplug_slot->name);
		kfree(slot->hotplug_slot);
		kfree(slot);
		return (NULL);
	}
	slot->name = slot->hotplug_slot->name;
	slot->dn = dn;
	slot->index = drc_index;
	strcpy(slot->location, drc_name);
	slot->power_domain = power_domain;
	slot->magic = SLOT_MAGIC;
	slot->hotplug_slot->private = slot;
	slot->hotplug_slot->ops = &rpaphp_hotplug_slot_ops;
	slot->hotplug_slot->release = &rpaphp_release_slot;
	dbg("Exit alloc_slot_struct(): slot->dn->full_name=%s drc_index=0x%x drc_name=%s\n",
		slot->dn->full_name, slot->index, slot->name);
	return (slot);
}

int register_slot(struct slot *slot)
{
	int retval;
	char *vio_uni_addr = NULL;

	dbg("%s registering slot:path[%s] index[%x], name[%s] pdomain[%x] type[%d]\n", __FUNCTION__, slot->dn->full_name, slot->index, slot->name, slot->power_domain, slot->type);

	retval = pci_hp_register(slot->hotplug_slot);
	if (retval) {
		err("pci_hp_register failed with error %d\n", retval);
		rpaphp_release_slot(slot->hotplug_slot);
		return (retval);
	}
	
	/* create "phy_locatoin" file */
	rpaphp_sysfs_add_attr_location(slot->hotplug_slot);	

	/* add slot to our internal list */
	dbg("%s adding slot[%s] to rpaphp_slot_list\n",
	    __FUNCTION__, slot->name);

	list_add(&slot->rpaphp_slot_list, &rpaphp_slot_head);

	if (vio_uni_addr)
		info("Slot [%s](vio_uni_addr=%s) registered\n",
		     slot->name, vio_uni_addr);
	else
		info("Slot [%s](bus_id=%s) registered\n",
		     slot->name, pci_name(slot->bridge));
	num_slots++;
	return (0);
}

int rpaphp_get_power_status(struct slot *slot, u8 * value)
{
	int rc;

	rc = rtas_get_power_level(slot->power_domain, (int *) value);
	if (rc)
		err("failed to get power-level for slot(%s), rc=0x%x\n",
		    slot->name, rc);

	return rc;
}

int rpaphp_set_attention_status(struct slot *slot, u8 status)
{
	int rc;

	/* status: LED_OFF or LED_ON */
	rc = rtas_set_indicator(DR_INDICATOR, slot->index, status);
	if (rc)
		err("slot(%s) set attention-status(%d) failed! rc=0x%x\n",
		    slot->name, status, rc);

	return rc;
}
