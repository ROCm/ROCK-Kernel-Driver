#ifndef AMDKCL_ACPI_H
#define AMDKCL_ACPI_H

#if (!defined(HAVE_ACPI_HANDLE))

#include <acpi/acpi_bus.h>

#define ACPI_HANDLE(dev) DEVICE_ACPI_HANDLE(dev)

#endif

#endif /* AMDKCL_ACPI_H */
