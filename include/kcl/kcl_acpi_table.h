/* SPDX-License-Identifier: GPL-2.0 */
#ifndef KCL_KCL_ACPI_TABLE_H
#define KCL_KCL_ACPI_TABLE_H

#include <linux/acpi.h>

#ifndef HAVE_ACPI_PUT_TABLE
void acpi_put_table(struct acpi_table_header *table);
#endif

#endif
