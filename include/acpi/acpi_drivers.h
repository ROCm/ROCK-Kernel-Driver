/*
 *  acpi_drivers.h  ($Revision: 31 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef __ACPI_DRIVERS_H__
#define __ACPI_DRIVERS_H__

#include <linux/acpi.h>
#include <acpi/acpi_bus.h>


#define ACPI_MAX_STRING			80

#define ACPI_BUS_COMPONENT		0x00010000
#define ACPI_SYSTEM_COMPONENT		0x02000000

/* _HID definitions */

#define ACPI_POWER_HID			"ACPI_PWR"
#define ACPI_PROCESSOR_HID		"ACPI_CPU"
#define ACPI_SYSTEM_HID			"ACPI_SYS"
#define ACPI_THERMAL_HID		"ACPI_THM"
#define ACPI_BUTTON_HID_POWERF		"ACPI_FPB"
#define ACPI_BUTTON_HID_SLEEPF		"ACPI_FSB"


/* --------------------------------------------------------------------------
                                       PCI
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_PCI

#define ACPI_PCI_COMPONENT		0x00400000

/* ACPI PCI Interrupt Link (pci_link.c) */

int acpi_irq_penalty_init (void);
int acpi_pci_link_get_irq (acpi_handle handle, int index, int* edge_level, int* active_high_low);

/* ACPI PCI Interrupt Routing (pci_irq.c) */

int acpi_pci_irq_add_prt (acpi_handle handle, int segment, int bus);

/* ACPI PCI Device Binding (pci_bind.c) */

struct pci_bus;

int acpi_pci_bind (struct acpi_device *device);
int acpi_pci_bind_root (struct acpi_device *device, struct acpi_pci_id *id, struct pci_bus *bus);

/* Arch-defined function to add a bus to the system */

struct pci_bus *pci_acpi_scan_root(struct acpi_device *device, int domain, int bus);

#endif /*CONFIG_ACPI_PCI*/


/* --------------------------------------------------------------------------
                                  Power Resource
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_POWER

int acpi_power_get_inferred_state (struct acpi_device *device);
int acpi_power_transition (struct acpi_device *device, int state);
#endif


/* --------------------------------------------------------------------------
                                  Embedded Controller
   -------------------------------------------------------------------------- */
#ifdef CONFIG_ACPI_EC
int acpi_ec_ecdt_probe (void);
#endif

/* --------------------------------------------------------------------------
                                    Processor
   -------------------------------------------------------------------------- */

#define ACPI_PROCESSOR_LIMIT_NONE	0x00
#define ACPI_PROCESSOR_LIMIT_INCREMENT	0x01
#define ACPI_PROCESSOR_LIMIT_DECREMENT	0x02

int acpi_processor_set_thermal_limit(acpi_handle handle, int type);


/* --------------------------------------------------------------------------
                                Debug Support
   -------------------------------------------------------------------------- */

#define ACPI_DEBUG_RESTORE	0
#define ACPI_DEBUG_LOW		1
#define ACPI_DEBUG_MEDIUM	2
#define ACPI_DEBUG_HIGH		3
#define ACPI_DEBUG_DRIVERS	4

extern u32 acpi_dbg_level;
extern u32 acpi_dbg_layer;

static inline void
acpi_set_debug (
	u32			flag)
{
	static u32		layer_save;
	static u32		level_save;

	switch (flag) {
	case ACPI_DEBUG_RESTORE:
		acpi_dbg_layer = layer_save;
		acpi_dbg_level = level_save;
		break;
	case ACPI_DEBUG_LOW:
	case ACPI_DEBUG_MEDIUM:
	case ACPI_DEBUG_HIGH:
	case ACPI_DEBUG_DRIVERS:
		layer_save = acpi_dbg_layer;
		level_save = acpi_dbg_level;
		break;
	}

	switch (flag) {
	case ACPI_DEBUG_LOW:
		acpi_dbg_layer = ACPI_COMPONENT_DEFAULT | ACPI_ALL_DRIVERS;
		acpi_dbg_level = ACPI_DEBUG_DEFAULT;
		break;
	case ACPI_DEBUG_MEDIUM:
		acpi_dbg_layer = ACPI_COMPONENT_DEFAULT | ACPI_ALL_DRIVERS;
		acpi_dbg_level = ACPI_LV_FUNCTIONS | ACPI_LV_ALL_EXCEPTIONS;
		break;
	case ACPI_DEBUG_HIGH:
		acpi_dbg_layer = 0xFFFFFFFF;
		acpi_dbg_level = 0xFFFFFFFF;
		break;
	case ACPI_DEBUG_DRIVERS:
		acpi_dbg_layer = ACPI_ALL_DRIVERS;
		acpi_dbg_level = 0xFFFFFFFF;
		break;
	}
}


#endif /*__ACPI_DRIVERS_H__*/
