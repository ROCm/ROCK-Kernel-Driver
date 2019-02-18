#ifndef AMDKCL_ACPI_H
#define AMDKCL_ACPI_H

/**
 * interface change in mainline kernel 3.13
 * but only affect RHEL6 without backport
 */

#include <linux/acpi.h>

#ifndef ACPI_HANDLE
#define ACPI_HANDLE(dev) DEVICE_ACPI_HANDLE(dev)
#endif

#endif /* AMDKCL_ACPI_H */
