/******************************************************************************
 *
 * Name: acinterp.h - Interpreter subcomponent prototypes and defines
 *       $Revision: 116 $
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

#ifndef __ACINTERP_H__
#define __ACINTERP_H__


#define WALK_OPERANDS       &(walk_state->operands [walk_state->num_operands -1])


/* Interpreter constants */

#define AML_END_OF_BLOCK            -1
#define PUSH_PKG_LENGTH             1
#define DO_NOT_PUSH_PKG_LENGTH      0


#define STACK_TOP                   0
#define STACK_BOTTOM                (u32) -1

/* Constants for global "When_to_parse_methods" */

#define METHOD_PARSE_AT_INIT        0x0
#define METHOD_PARSE_JUST_IN_TIME   0x1
#define METHOD_DELETE_AT_COMPLETION 0x2


acpi_status
acpi_ex_resolve_operands (
	u16                     opcode,
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state);


/*
 * amxface - External interpreter interfaces
 */

acpi_status
acpi_ex_load_table (
	acpi_table_type         table_id);

acpi_status
acpi_ex_execute_method (
	acpi_namespace_node     *method_node,
	acpi_operand_object     **params,
	acpi_operand_object     **return_obj_desc);


/*
 * amconvrt - object conversion
 */

acpi_status
acpi_ex_convert_to_integer (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **result_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_convert_to_buffer (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **result_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_convert_to_string (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **result_desc,
	u32                     base,
	u32                     max_length,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_convert_to_target_type (
	acpi_object_type8       destination_type,
	acpi_operand_object     **obj_desc,
	acpi_walk_state         *walk_state);


/*
 * amfield - ACPI AML (p-code) execution - field manipulation
 */

acpi_status
acpi_ex_extract_from_field (
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

acpi_status
acpi_ex_insert_into_field (
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

acpi_status
acpi_ex_setup_field (
	acpi_operand_object     *obj_desc,
	u32                     field_byte_offset);

acpi_status
acpi_ex_read_field_datum (
	acpi_operand_object     *obj_desc,
	u32                     field_byte_offset,
	u32                     *value);

acpi_status
acpi_ex_common_access_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);


acpi_status
acpi_ex_access_index_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

acpi_status
acpi_ex_access_bank_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

acpi_status
acpi_ex_access_region_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);


acpi_status
acpi_ex_access_buffer_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

acpi_status
acpi_ex_read_data_from_field (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **ret_buffer_desc);

acpi_status
acpi_ex_write_data_to_field (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *obj_desc);

/*
 * ammisc - ACPI AML (p-code) execution - specific opcodes
 */

acpi_status
acpi_ex_opcode_3A_0T_0R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_opcode_3A_1T_1R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_opcode_6A_0T_1R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_get_object_reference (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **return_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_do_concatenate (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     *obj_desc2,
	acpi_operand_object     **actual_return_desc,
	acpi_walk_state         *walk_state);

u8
acpi_ex_do_logical_op (
	u16                     opcode,
	acpi_integer            operand0,
	acpi_integer            operand1);

acpi_integer
acpi_ex_do_math_op (
	u16                     opcode,
	acpi_integer            operand0,
	acpi_integer            operand1);

acpi_status
acpi_ex_load_op (
	acpi_operand_object     *rgn_desc,
	acpi_operand_object     *ddb_handle);

acpi_status
acpi_ex_unload_table (
	acpi_operand_object     *ddb_handle);

acpi_status
acpi_ex_create_mutex (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_create_processor (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_create_power_resource (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_create_region (
	u8                      *aml_start,
	u32                     aml_length,
	u8                      region_space,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_create_table_region (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_create_event (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_create_alias (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_create_method (
	u8                      *aml_start,
	u32                     aml_length,
	acpi_walk_state         *walk_state);


/*
 * ammutex - mutex support
 */

acpi_status
acpi_ex_acquire_mutex (
	acpi_operand_object     *time_desc,
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_release_mutex (
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_release_all_mutexes (
	acpi_operand_object     *mutex_list);

void
acpi_ex_unlink_mutex (
	acpi_operand_object     *obj_desc);


/*
 * amprep - ACPI AML (p-code) execution - prep utilities
 */

acpi_status
acpi_ex_prep_common_field_object (
	acpi_operand_object     *obj_desc,
	u8                      field_flags,
	u32                     field_position,
	u32                     field_length);

acpi_status
acpi_ex_prep_region_field_value (
	acpi_namespace_node     *node,
	acpi_handle             region,
	u8                      field_flags,
	u32                     field_position,
	u32                     field_length);

acpi_status
acpi_ex_prep_bank_field_value (
	acpi_namespace_node     *node,
	acpi_namespace_node     *region_node,
	acpi_namespace_node     *bank_register_node,
	u32                     bank_val,
	u8                      field_flags,
	u32                     field_position,
	u32                     field_length);

acpi_status
acpi_ex_prep_index_field_value (
	acpi_namespace_node     *node,
	acpi_namespace_node     *index_reg,
	acpi_namespace_node     *data_reg,
	u8                      field_flags,
	u32                     field_position,
	u32                     field_length);

acpi_status
acpi_ex_prep_field_value (
	ACPI_CREATE_FIELD_INFO  *info);

/*
 * amsystem - Interface to OS services
 */

acpi_status
acpi_ex_system_do_notify_op (
	acpi_operand_object     *value,
	acpi_operand_object     *obj_desc);

void
acpi_ex_system_do_suspend(
	u32                     time);

void
acpi_ex_system_do_stall (
	u32                     time);

acpi_status
acpi_ex_system_acquire_mutex(
	acpi_operand_object     *time,
	acpi_operand_object     *obj_desc);

acpi_status
acpi_ex_system_release_mutex(
	acpi_operand_object     *obj_desc);

acpi_status
acpi_ex_system_signal_event(
	acpi_operand_object     *obj_desc);

acpi_status
acpi_ex_system_wait_event(
	acpi_operand_object     *time,
	acpi_operand_object     *obj_desc);

acpi_status
acpi_ex_system_reset_event(
	acpi_operand_object     *obj_desc);

acpi_status
acpi_ex_system_wait_semaphore (
	acpi_handle             semaphore,
	u32                     timeout);


/*
 * ammonadic - ACPI AML (p-code) execution, monadic operators
 */

acpi_status
acpi_ex_opcode_1A_0T_0R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_opcode_1A_0T_1R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_opcode_1A_1T_1R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_opcode_1A_1T_0R (
	acpi_walk_state         *walk_state);

/*
 * amdyadic - ACPI AML (p-code) execution, dyadic operators
 */

acpi_status
acpi_ex_opcode_2A_0T_0R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_opcode_2A_0T_1R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_opcode_2A_1T_1R (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_opcode_2A_2T_1R (
	acpi_walk_state         *walk_state);


/*
 * amresolv  - Object resolution and get value functions
 */

acpi_status
acpi_ex_resolve_to_value (
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_resolve_node_to_value (
	acpi_namespace_node     **stack_ptr,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_resolve_object_to_value (
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_get_buffer_field_value (
	acpi_operand_object     *field_desc,
	acpi_operand_object     *result_desc);


/*
 * amdump - Scanner debug output routines
 */

void
acpi_ex_show_hex_value (
	u32                     byte_count,
	u8                      *aml_start,
	u32                     lead_space);


acpi_status
acpi_ex_dump_operand (
	acpi_operand_object     *entry_desc);

void
acpi_ex_dump_operands (
	acpi_operand_object     **operands,
	operating_mode          interpreter_mode,
	NATIVE_CHAR             *ident,
	u32                     num_levels,
	NATIVE_CHAR             *note,
	NATIVE_CHAR             *module_name,
	u32                     line_number);

void
acpi_ex_dump_object_descriptor (
	acpi_operand_object     *object,
	u32                     flags);


void
acpi_ex_dump_node (
	acpi_namespace_node     *node,
	u32                     flags);


/*
 * amnames - interpreter/scanner name load/execute
 */

NATIVE_CHAR *
acpi_ex_allocate_name_string (
	u32                     prefix_count,
	u32                     num_name_segs);

u32
acpi_ex_good_char (
	u32                     character);

acpi_status
acpi_ex_name_segment (
	u8                      **in_aml_address,
	NATIVE_CHAR             *name_string);

acpi_status
acpi_ex_get_name_string (
	acpi_object_type8       data_type,
	u8                      *in_aml_address,
	NATIVE_CHAR             **out_name_string,
	u32                     *out_name_length);

acpi_status
acpi_ex_do_name (
	acpi_object_type        data_type,
	operating_mode          load_exec_mode);


/*
 * amstore - Object store support
 */

acpi_status
acpi_ex_store (
	acpi_operand_object     *val_desc,
	acpi_operand_object     *dest_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_store_object_to_index (
	acpi_operand_object     *val_desc,
	acpi_operand_object     *dest_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_store_object_to_node (
	acpi_operand_object     *source_desc,
	acpi_namespace_node     *node,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_store_object_to_object (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *dest_desc,
	acpi_walk_state         *walk_state);


/*
 *
 */

acpi_status
acpi_ex_resolve_object (
	acpi_operand_object     **source_desc_ptr,
	acpi_object_type8       target_type,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_store_object (
	acpi_operand_object     *source_desc,
	acpi_object_type8       target_type,
	acpi_operand_object     **target_desc_ptr,
	acpi_walk_state         *walk_state);


/*
 * amcopy - object copy
 */

acpi_status
acpi_ex_copy_buffer_to_buffer (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *target_desc);

acpi_status
acpi_ex_copy_string_to_string (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *target_desc);

acpi_status
acpi_ex_copy_integer_to_index_field (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *target_desc);

acpi_status
acpi_ex_copy_integer_to_bank_field (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *target_desc);

acpi_status
acpi_ex_copy_data_to_named_field (
	acpi_operand_object     *source_desc,
	acpi_namespace_node     *node);

acpi_status
acpi_ex_copy_integer_to_buffer_field (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *target_desc);

/*
 * amutils - interpreter/scanner utilities
 */

acpi_status
acpi_ex_enter_interpreter (
	void);

void
acpi_ex_exit_interpreter (
	void);

void
acpi_ex_truncate_for32bit_table (
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state);

u8
acpi_ex_validate_object_type (
	acpi_object_type        type);

u8
acpi_ex_acquire_global_lock (
	u32                     rule);

acpi_status
acpi_ex_release_global_lock (
	u8                      locked);

u32
acpi_ex_digits_needed (
	acpi_integer            value,
	u32                     base);

acpi_status
acpi_ex_eisa_id_to_string (
	u32                     numeric_id,
	NATIVE_CHAR             *out_string);

acpi_status
acpi_ex_unsigned_integer_to_string (
	acpi_integer            value,
	NATIVE_CHAR             *out_string);


/*
 * amregion - default Op_region handlers
 */

acpi_status
acpi_ex_system_memory_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_system_io_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_pci_config_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_cmos_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_pci_bar_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_embedded_controller_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_sm_bus_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);


#endif /* __INTERP_H__ */
