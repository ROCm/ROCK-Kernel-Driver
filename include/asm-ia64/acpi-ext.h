/*
 * ia64/platform/hp/common/hp_acpi.h
 *
 * Copyright (C) 2003 Hewlett-Packard
 * Copyright (C) Alex Williamson
 *
 * Vendor specific extensions to ACPI.  The HP-specific extensiosn are also used by NEC.
 */
#ifndef _ASM_IA64_ACPI_EXT_H
#define _ASM_IA64_ACPI_EXT_H

#include <linux/types.h>

#define HP_CCSR_LENGTH	0x21
#define HP_CCSR_TYPE	0x2
#define HP_CCSR_GUID	EFI_GUID(0x69e9adf9, 0x924f, 0xab5f, \
				 0xf6, 0x4a, 0x24, 0xd2, 0x01, 0x37, 0x0e, 0xad)

struct acpi_hp_vendor_long {
	u8      guid_id;
	u8      guid[16];
	u8      csr_base[8];
	u8      csr_length[8];
};

extern acpi_status hp_acpi_csr_space (acpi_handle, u64 *base, u64 *length);
extern acpi_status acpi_get_crs (acpi_handle, struct acpi_buffer *);
extern struct acpi_resource *acpi_get_crs_next (struct acpi_buffer *, int *);
extern union acpi_resource_data *acpi_get_crs_type (struct acpi_buffer *, int *, int);
extern void acpi_dispose_crs (struct acpi_buffer *);

#endif /* _ASM_IA64_ACPI_EXT_H */
