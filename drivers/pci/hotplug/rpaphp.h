/*
 * PCI Hot Plug Controller Driver for RPA-compliant PPC64 platform.
 *
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
 * Send feedback to <lxie@us.ibm.com>,
 *
 */

#ifndef _PPC64PHP_H
#define _PPC64PHP_H
#include "pci_hotplug.h"

#define DR_INDICATOR 9002
#define DR_ENTITY_SENSE 9003

#define POWER_ON	100
#define POWER_OFF	0

#define LED_OFF		0 
#define LED_ON		1	/* continuous on */ 
#define LED_ID		2	/* slow blinking */
#define LED_ACTION	3	/* fast blinking */

#define SLOT_NAME_SIZE 12

/* Error status from rtas_get-sensor */
#define NEED_POWER    -9000     /* slot must be power up and unisolated to get state */
#define PWR_ONLY      -9001     /* slot must be powerd up to get state, leave isolated */
#define ERR_SENSE_USE -9002     /* No DR operation will succeed, slot is unusable  */

/* Sensor values from rtas_get-sensor */
#define EMPTY	0       /* No card in slot */
#define PRESENT	1       /* Card in slot */

#define MY_NAME "rpaphp"

#define dbg(format, arg...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG "%s: " format,	\
				MY_NAME , ## arg); 		\
	} while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format, MY_NAME , ## arg)

#define SLOT_MAGIC	0x67267322

/* slot states */

#define	NOT_VALID	3
#define	NOT_CONFIGURED	2
#define	CONFIGURED	1
#define	EMPTY		0

/*
 * struct slot - slot information for each *physical* slot
 */
struct slot {
	u32	magic;
	int     state;
	u32     index;
	u32     type;
	u32     power_domain;
	char    *name;
	struct	device_node *dn;/* slot's device_node in OFDT		*/
				/* dn has phb info			*/
	struct	pci_dev	*bridge;/* slot's pci_dev in pci_devices	*/

	struct	pci_dev	*dev;	/* pci_dev of device in this slot 	*/
				/* it will be used for unconfig		*/ 
				/* NULL if slot is empty		*/

	struct  hotplug_slot    *hotplug_slot;
	struct list_head	rpaphp_slot_list;
};

extern struct pci_dev *rpaphp_find_pci_dev(struct device_node *dn);
extern int rpaphp_add_slot(char *slot_name);
extern int rpaphp_remove_slot(struct slot *slot);
extern int rpaphp_claim_resource(struct pci_dev *dev, int resource);

#endif /* _PPC64PHP_H */
