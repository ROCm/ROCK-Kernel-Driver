/*
 * PCI HotPlug Core Functions
 *
 * Copyright (c) 1995,2001 Compaq Computer Corporation
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (c) 2001 IBM Corp.
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
 * Send feedback to <greg@kroah.com>
 *
 */
#ifndef _PCI_HOTPLUG_H
#define _PCI_HOTPLUG_H


struct hotplug_slot;
struct hotplug_slot_core;

/**
 * struct hotplug_slot_ops -the callbacks that the hotplug pci core can use
 * @owner: The module owner of this structure
 * @enable_slot: Called when the user wants to enable a specific pci slot
 * @disable_slot: Called when the user wants to disable a specific pci slot
 * @set_attention_status: Called to set the specific slot's attention LED to
 * the specified value
 * @hardware_test: Called to run a specified hardware test on the specified
 * slot.
 * @get_power_status: Called to get the current power status of a slot.
 * 	If this field is NULL, the value passed in the struct hotplug_slot_info
 * 	will be used when this value is requested by a user.
 * @get_attention_status: Called to get the current attention status of a slot.
 *	If this field is NULL, the value passed in the struct hotplug_slot_info
 *	will be used when this value is requested by a user.
 * @get_latch_status: Called to get the current latch status of a slot.
 *	If this field is NULL, the value passed in the struct hotplug_slot_info
 *	will be used when this value is requested by a user.
 * @get_adapter_present: Called to get see if an adapter is present in the slot or not.
 *	If this field is NULL, the value passed in the struct hotplug_slot_info
 *	will be used when this value is requested by a user.
 *
 * The table of function pointers that is passed to the hotplug pci core by a
 * hotplug pci driver.  These functions are called by the hotplug pci core when
 * the user wants to do something to a specific slot (query it for information,
 * set an LED, enable / disable power, etc.)
 */
struct hotplug_slot_ops {
	struct module *owner;
	int (*enable_slot)		(struct hotplug_slot *slot);
	int (*disable_slot)		(struct hotplug_slot *slot);
	int (*set_attention_status)	(struct hotplug_slot *slot, u8 value);
	int (*hardware_test)		(struct hotplug_slot *slot, u32 value);
	int (*get_power_status)		(struct hotplug_slot *slot, u8 *value);
	int (*get_attention_status)	(struct hotplug_slot *slot, u8 *value);
	int (*get_latch_status)		(struct hotplug_slot *slot, u8 *value);
	int (*get_adapter_status)	(struct hotplug_slot *slot, u8 *value);
};

/**
 * struct hotplug_slot_info - used to notify the hotplug pci core of the state of the slot
 * @power: if power is enabled or not (1/0)
 * @attention_status: if the attention light is enabled or not (1/0)
 * @latch_status: if the latch (if any) is open or closed (1/0)
 * @adapter_present: if there is a pci board present in the slot or not (1/0)
 *
 * Used to notify the hotplug pci core of the status of a specific slot.
 */
struct hotplug_slot_info {
	u8	power_status;
	u8	attention_status;
	u8	latch_status;
	u8	adapter_status;
};

/**
 * struct hotplug_slot - used to register a physical slot with the hotplug pci core
 * @name: the name of the slot being registered.  This string must
 * be unique amoung slots registered on this system.
 * @ops: pointer to the &struct hotplug_slot_ops to be used for this slot
 * @info: pointer to the &struct hotplug_slot_info for the inital values for
 * this slot.
 * @private: used by the hotplug pci controller driver to store whatever it
 * needs.
 */
struct hotplug_slot {
	char				*name;
	struct hotplug_slot_ops		*ops;
	struct hotplug_slot_info	*info;
	void				*private;

	/* Variables below this are for use only by the hotplug pci core. */
	struct list_head		slot_list;
	struct hotplug_slot_core	*core_priv;
};

extern int pci_hp_register		(struct hotplug_slot *slot);
extern int pci_hp_deregister		(struct hotplug_slot *slot);
extern int pci_hp_change_slot_info	(const char *name,
					 struct hotplug_slot_info *info);

struct pci_dev_wrapped {
	struct pci_dev	*dev;
	void		*data;
};

struct pci_bus_wrapped {
	struct pci_bus	*bus;
	void		*data;
};

struct pci_visit {
	int (* pre_visit_pci_bus)	(struct pci_bus_wrapped *,
					 struct pci_dev_wrapped *);
	int (* post_visit_pci_bus)	(struct pci_bus_wrapped *,
					 struct pci_dev_wrapped *);

	int (* pre_visit_pci_dev)	(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
	int (* visit_pci_dev)		(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
	int (* post_visit_pci_dev)	(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
};

extern int pci_visit_dev	(struct pci_visit *fn,
				 struct pci_dev_wrapped *wrapped_dev,
				 struct pci_bus_wrapped *wrapped_parent);

extern int pci_read_config_byte_nodev	(struct pci_ops *ops, u8 bus, u8 device,
					 u8 function, int where, u8 *val);
extern int pci_read_config_word_nodev	(struct pci_ops *ops, u8 bus, u8 device,
					 u8 function, int where, u16 *val);
extern int pci_read_config_dword_nodev	(struct pci_ops *ops, u8 bus, u8 device,
					 u8 function, int where, u32 *val);

extern int pci_write_config_byte_nodev	(struct pci_ops *ops, u8 bus, u8 device,
					 u8 function, int where, u8 val);
extern int pci_write_config_word_nodev	(struct pci_ops *ops, u8 bus, u8 device,
					 u8 function, int where, u16 val);
extern int pci_write_config_dword_nodev	(struct pci_ops *ops, u8 bus, u8 device,
					 u8 function, int where, u32 val);


#endif

