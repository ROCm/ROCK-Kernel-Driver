/******************************************************************************
 *
 * Name: acresrc.h - Resource Manager function prototypes
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2003, R. Byron Moore
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

#ifndef __ACRESRC_H__
#define __ACRESRC_H__


/*
 *  Function prototypes called from Acpi* APIs
 */

acpi_status
acpi_rs_get_prt_method_data (
	acpi_handle                     handle,
	struct acpi_buffer              *ret_buffer);


acpi_status
acpi_rs_get_crs_method_data (
	acpi_handle                     handle,
	struct acpi_buffer              *ret_buffer);

acpi_status
acpi_rs_get_prs_method_data (
	acpi_handle                     handle,
	struct acpi_buffer              *ret_buffer);

acpi_status
acpi_rs_set_srs_method_data (
	acpi_handle                     handle,
	struct acpi_buffer              *ret_buffer);

acpi_status
acpi_rs_create_resource_list (
	union acpi_operand_object       *byte_stream_buffer,
	struct acpi_buffer              *output_buffer);

acpi_status
acpi_rs_create_byte_stream (
	struct acpi_resource            *linked_list_buffer,
	struct acpi_buffer              *output_buffer);

acpi_status
acpi_rs_create_pci_routing_table (
	union acpi_operand_object       *package_object,
	struct acpi_buffer              *output_buffer);


/*
 * Function prototypes called from acpi_rs_create*
 */
void
acpi_rs_dump_irq (
	union acpi_resource_data        *data);

void
acpi_rs_dump_address16 (
	union acpi_resource_data        *data);

void
acpi_rs_dump_address32 (
	union acpi_resource_data        *data);

void
acpi_rs_dump_address64 (
	union acpi_resource_data        *data);

void
acpi_rs_dump_dma (
	union acpi_resource_data        *data);

void
acpi_rs_dump_io (
	union acpi_resource_data        *data);

void
acpi_rs_dump_extended_irq (
	union acpi_resource_data        *data);

void
acpi_rs_dump_fixed_io (
	union acpi_resource_data        *data);

void
acpi_rs_dump_fixed_memory32 (
	union acpi_resource_data        *data);

void
acpi_rs_dump_memory24 (
	union acpi_resource_data        *data);

void
acpi_rs_dump_memory32 (
	union acpi_resource_data        *data);

void
acpi_rs_dump_start_depend_fns (
	union acpi_resource_data        *data);

void
acpi_rs_dump_vendor_specific (
	union acpi_resource_data        *data);

void
acpi_rs_dump_resource_list (
	struct acpi_resource            *resource);

void
acpi_rs_dump_irq_list (
	u8                              *route_table);

acpi_status
acpi_rs_get_byte_stream_start (
	u8                              *byte_stream_buffer,
	u8                              **byte_stream_start,
	u32                             *size);

acpi_status
acpi_rs_get_list_length (
	u8                              *byte_stream_buffer,
	u32                             byte_stream_buffer_length,
	acpi_size                       *size_needed);

acpi_status
acpi_rs_get_byte_stream_length (
	struct acpi_resource            *linked_list_buffer,
	acpi_size                       *size_needed);

acpi_status
acpi_rs_get_pci_routing_table_length (
	union acpi_operand_object       *package_object,
	acpi_size                       *buffer_size_needed);

acpi_status
acpi_rs_byte_stream_to_list (
	u8                              *byte_stream_buffer,
	u32                             byte_stream_buffer_length,
	u8                              *output_buffer);

acpi_status
acpi_rs_list_to_byte_stream (
	struct acpi_resource            *linked_list,
	acpi_size                       byte_stream_size_needed,
	u8                              *output_buffer);

acpi_status
acpi_rs_io_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_fixed_io_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_io_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_fixed_io_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_irq_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_irq_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_dma_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_dma_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_address16_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_address16_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_address32_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_address32_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_address64_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_address64_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_start_depend_fns_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_end_depend_fns_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_start_depend_fns_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_end_depend_fns_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_memory24_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_memory24_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_memory32_range_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_fixed_memory32_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_memory32_range_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_fixed_memory32_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_extended_irq_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_extended_irq_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_end_tag_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_end_tag_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

acpi_status
acpi_rs_vendor_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size);

acpi_status
acpi_rs_vendor_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed);

u8
acpi_rs_get_resource_type (
	u8                              resource_start_byte);

#endif  /* __ACRESRC_H__ */
