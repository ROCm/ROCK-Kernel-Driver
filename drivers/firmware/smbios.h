/*
 * linux/drivers/firmware/smbios.c
 *  Copyright (C) 2002, 2003, 2004 Dell Inc.
 *  by Michael Brown <Michael_E_Brown@dell.com>
 *  vim:noet:ts=8:sw=8:filetype=c:textwidth=80:
 *
 * BIOS SMBIOS Table access 
 * conformant to DMTF SMBIOS definition
 *   at http://www.dmtf.org/standards/smbios
 *
 * This code takes information provided by SMBIOS tables
 * and presents it in sysfs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_SMBIOS_H
#define _LINUX_SMBIOS_H

#include <linux/types.h>

struct smbios_table_entry_point {
	u8 anchor[4];
	u8 checksum;
	u8 eps_length;
	u8 major_ver;
	u8 minor_ver;
	u16 max_struct_size;
	u8 revision;
	u8 formatted_area[5];
	u8 dmi_anchor[5];
	u8 intermediate_checksum;
	u16 table_length;
	u32 table_address;
	u16 table_num_structs;
	u8 smbios_bcd_revision;
} __attribute__ ((packed));

struct smbios_structure_header {
	u8 type;
	u8 length;
	u16 handle;
} __attribute__ ((packed));

#endif				/* _LINUX_SMBIOS_H */
