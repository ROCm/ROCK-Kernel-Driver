/*
 *  power.c - Overall power driver. Also handles AC adapter device.
 *
 *  Copyright (C) 2000 Andrew Grover
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
#include <linux/proc_fs.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("power")

int acpi_cmbatt_init(void);
int acpi_cmbatt_terminate(void);

/* ACPI-specific defines */
#define ACPI_AC_ADAPTER_HID	"ACPI0003"

static int ac_count = 0;
static ACPI_HANDLE ac_handle = 0;

/*
 * We found a device with the correct HID
 */
static ACPI_STATUS
acpi_found_ac_adapter(ACPI_HANDLE handle, u32 level, void *ctx, void **value)
{
	ACPI_DEVICE_INFO	info;

	if (ac_count > 0) {
		printk(KERN_ERR "AC Adapter: more than one!\n");
		return (AE_OK);
	}

	if (!ACPI_SUCCESS(acpi_get_object_info(handle, &info))) {
		printk(KERN_ERR "AC Adapter: Could not get AC Adapter object info\n");
		return (AE_OK);
	}

	if (!(info.valid & ACPI_VALID_STA)) {
		printk(KERN_ERR "AC Adapter: Battery _STA invalid\n");
		return AE_OK;
	}

	printk(KERN_INFO "AC Adapter: found\n");

	ac_handle = handle;

	ac_count++;

	return AE_OK;
}

static int
proc_read_ac_adapter_status(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	ACPI_OBJECT obj;
	ACPI_BUFFER buf;

	char *p = page;
	int len;

	buf.length = sizeof(obj);
	buf.pointer = &obj;
	if (!ACPI_SUCCESS(acpi_evaluate_object(ac_handle, "_PSR", NULL, &buf))
		|| obj.type != ACPI_TYPE_NUMBER) {
		p += sprintf(p, "Could not read AC status\n");
		goto end;
	}

	if (obj.number.value)
		p += sprintf(p, "on-line\n");
	else
		p += sprintf(p, "off-line\n");

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}

int
acpi_power_init(void)
{
	acpi_get_devices(ACPI_AC_ADAPTER_HID, 
			acpi_found_ac_adapter,
			NULL,
			NULL);

	if (!proc_mkdir("power", NULL))
		return 0;

	if (ac_handle) {
		create_proc_read_entry("power/ac", 0, NULL,
			proc_read_ac_adapter_status, NULL);
	}

	acpi_cmbatt_init();

	return 0;
}

int
acpi_power_terminate(void)
{
	acpi_cmbatt_terminate();

	if (ac_handle) {
		remove_proc_entry("power/ac", NULL);
	}

	remove_proc_entry("power", NULL);

	return 0;
}
