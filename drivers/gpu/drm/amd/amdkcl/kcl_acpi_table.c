/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/acpi.h>
#include <kcl/kcl_acpi_table.h>

#ifndef HAVE_ACPI_PUT_TABLE
amdkcl_dummy_symbol(acpi_put_table, void, return,
				  struct acpi_table_header *table)
#endif
