#ifndef AMDKCL_ACPI_H
#define AMDKCL_ACPI_H

/**
 * interface change in mainline kernel 3.13
 * but only affect RHEL6 without backport
 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32)

#include <acpi/acpi_bus.h>

#define ACPI_HANDLE(dev) DEVICE_ACPI_HANDLE(dev)

#endif

#endif /* AMDKCL_ACPI_H */
