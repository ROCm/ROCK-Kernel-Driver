/*
 *  table.c - ACPI tables, chipset, and errata handling
 *
 *  Copyright (C) 2000 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("table")

FADT_DESCRIPTOR acpi_fadt;

/*
 * Fetch the fadt information
 */
static int
acpi_fetch_fadt(void)
{
	ACPI_BUFFER buffer;

	memset(&acpi_fadt, 0, sizeof(acpi_fadt));
	buffer.pointer = &acpi_fadt;
	buffer.length = sizeof(acpi_fadt);
	if (!ACPI_SUCCESS(acpi_get_table(ACPI_TABLE_FADT, 1, &buffer))) {
		printk(KERN_ERR "ACPI: missing fadt\n");
		return -ENODEV;
	}

	if (acpi_fadt.plvl2_lat
	    && acpi_fadt.plvl2_lat <= ACPI_MAX_P_LVL2_LAT) {
		acpi_c2_exit_latency
			= ACPI_MICROSEC_TO_TMR_TICKS(acpi_fadt.plvl2_lat);
		acpi_c2_enter_latency
			= ACPI_MICROSEC_TO_TMR_TICKS(ACPI_TMR_HZ / 1000);
	}
	if (acpi_fadt.plvl3_lat
	    && acpi_fadt.plvl3_lat <= ACPI_MAX_P_LVL3_LAT) {
		acpi_c3_exit_latency
			= ACPI_MICROSEC_TO_TMR_TICKS(acpi_fadt.plvl3_lat);
		acpi_c3_enter_latency
			= ACPI_MICROSEC_TO_TMR_TICKS(acpi_fadt.plvl3_lat * 5);
	}

	return 0;
}

/*
 * Find and load ACPI tables
 */
int
acpi_find_and_load_tables(u64 rsdp)
{
	if (ACPI_SUCCESS(acpi_load_tables(rsdp)))
	{
		printk(KERN_INFO "ACPI: System description tables loaded\n");
	}
	else {
		printk(KERN_INFO "ACPI: System description table load failed\n");
		acpi_terminate();
		return -1;
	}

	if (acpi_fetch_fadt()) {
		acpi_terminate();
		return -1;
	}

	return 0;
}
