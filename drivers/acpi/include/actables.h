/******************************************************************************
 *
 * Name: actables.h - ACPI table management
 *       $Revision: 32 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 R. Byron Moore
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

#ifndef __ACTABLES_H__
#define __ACTABLES_H__


/* Used in Acpi_tb_map_acpi_table for size parameter if table header is to be used */

#define SIZE_IN_HEADER          0


acpi_status
acpi_tb_handle_to_object (
	u16                     table_id,
	acpi_table_desc         **table_desc);

/*
 * tbconvrt - Table conversion routines
 */

acpi_status
acpi_tb_convert_to_xsdt (
	acpi_table_desc         *table_info,
	u32                     *number_of_tables);

acpi_status
acpi_tb_convert_table_fadt (
	void);

acpi_status
acpi_tb_build_common_facs (
	acpi_table_desc         *table_info);

u32
acpi_tb_get_table_count (
	RSDP_DESCRIPTOR         *RSDP,
	acpi_table_header       *RSDT);

/*
 * tbget - Table "get" routines
 */

acpi_status
acpi_tb_get_table_ptr (
	acpi_table_type         table_type,
	u32                     instance,
	acpi_table_header       **table_ptr_loc);

acpi_status
acpi_tb_get_table (
	ACPI_PHYSICAL_ADDRESS   physical_address,
	acpi_table_header       *buffer_ptr,
	acpi_table_desc         *table_info);

acpi_status
acpi_tb_verify_rsdp (
	ACPI_PHYSICAL_ADDRESS   RSDP_physical_address);

acpi_status
acpi_tb_get_table_facs (
	acpi_table_header       *buffer_ptr,
	acpi_table_desc         *table_info);

ACPI_PHYSICAL_ADDRESS
acpi_tb_get_rsdt_address (
	void);

acpi_status
acpi_tb_validate_rsdt (
	acpi_table_header       *table_ptr);

acpi_status
acpi_tb_get_table_pointer (
	ACPI_PHYSICAL_ADDRESS   physical_address,
	u32                     flags,
	u32                     *size,
	acpi_table_header       **table_ptr);

/*
 * tbgetall - Get all firmware ACPI tables
 */

acpi_status
acpi_tb_get_all_tables (
	u32                     number_of_tables,
	acpi_table_header       *buffer_ptr);


/*
 * tbinstall - Table installation
 */

acpi_status
acpi_tb_install_table (
	acpi_table_header       *table_ptr,
	acpi_table_desc         *table_info);

acpi_status
acpi_tb_recognize_table (
	acpi_table_header       *table_ptr,
	acpi_table_desc         *table_info);

acpi_status
acpi_tb_init_table_descriptor (
	acpi_table_type         table_type,
	acpi_table_desc         *table_info);


/*
 * tbremove - Table removal and deletion
 */

void
acpi_tb_delete_acpi_tables (
	void);

void
acpi_tb_delete_acpi_table (
	acpi_table_type         type);

void
acpi_tb_delete_single_table (
	acpi_table_desc         *table_desc);

acpi_table_desc *
acpi_tb_uninstall_table (
	acpi_table_desc         *table_desc);

void
acpi_tb_free_acpi_tables_of_type (
	acpi_table_desc         *table_info);


/*
 * tbrsd - RSDP, RSDT utilities
 */

acpi_status
acpi_tb_get_table_rsdt (
	u32                     *number_of_tables);

u8 *
acpi_tb_scan_memory_for_rsdp (
	u8                      *start_address,
	u32                     length);

acpi_status
acpi_tb_find_rsdp (
	acpi_table_desc         *table_info,
	u32                     flags);


/*
 * tbutils - common table utilities
 */

acpi_status
acpi_tb_map_acpi_table (
	ACPI_PHYSICAL_ADDRESS   physical_address,
	u32                     *size,
	acpi_table_header       **logical_address);

acpi_status
acpi_tb_verify_table_checksum (
	acpi_table_header       *table_header);

u8
acpi_tb_checksum (
	void                    *buffer,
	u32                     length);

acpi_status
acpi_tb_validate_table_header (
	acpi_table_header       *table_header);


#endif /* __ACTABLES_H__ */
