/*
 * arch/ia64/kernel/acpi-ext.c
 *
 * Copyright (C) 2003 Hewlett-Packard
 * Copyright (C) Alex Williamson
 *
 * Vendor specific extensions to ACPI.  These are used by both
 * HP and NEC.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/efi.h>

#include <asm/acpi-ext.h>

/*
 * Note: Strictly speaking, this is only needed for HP and NEC machines.
 *	 However, NEC machines identify themselves as DIG-compliant, so there is
 *	 no easy way to #ifdef this out.
 */
acpi_status
hp_acpi_csr_space (acpi_handle obj, u64 *csr_base, u64 *csr_length)
{
	int i, offset = 0;
	acpi_status status;
	struct acpi_buffer buf;
	struct acpi_resource_vendor *res;
	struct acpi_hp_vendor_long *hp_res;
	efi_guid_t vendor_guid;

	*csr_base = 0;
	*csr_length = 0;

	status = acpi_get_crs(obj, &buf);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to get _CRS data on object\n");
		return status;
	}

	res = (struct acpi_resource_vendor *)acpi_get_crs_type(&buf, &offset, ACPI_RSTYPE_VENDOR);
	if (!res) {
		printk(KERN_ERR PREFIX "Failed to find config space for device\n");
		acpi_dispose_crs(&buf);
		return AE_NOT_FOUND;
	}

	hp_res = (struct acpi_hp_vendor_long *)(res->reserved);

	if (res->length != HP_CCSR_LENGTH || hp_res->guid_id != HP_CCSR_TYPE) {
		printk(KERN_ERR PREFIX "Unknown Vendor data\n");
		acpi_dispose_crs(&buf);
		return AE_TYPE; /* Revisit error? */
	}

	memcpy(&vendor_guid, hp_res->guid, sizeof(efi_guid_t));
	if (efi_guidcmp(vendor_guid, HP_CCSR_GUID) != 0) {
		printk(KERN_ERR PREFIX "Vendor GUID does not match\n");
		acpi_dispose_crs(&buf);
		return AE_TYPE; /* Revisit error? */
	}

	for (i = 0 ; i < 8 ; i++) {
		*csr_base |= ((u64)(hp_res->csr_base[i]) << (i * 8));
		*csr_length |= ((u64)(hp_res->csr_length[i]) << (i * 8));
	}

	acpi_dispose_crs(&buf);
	return AE_OK;
}

EXPORT_SYMBOL(hp_acpi_csr_space);
