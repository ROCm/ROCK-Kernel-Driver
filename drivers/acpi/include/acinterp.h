/******************************************************************************
 *
 * Name: acinterp.h - Interpreter subcomponent prototypes and defines
 *       $Revision: 139 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
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


#define ACPI_WALK_OPERANDS       (&(walk_state->operands [walk_state->num_operands -1]))


acpi_status
acpi_ex_resolve_operands (
	u16                     opcode,
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_check_object_type (
	acpi_object_type        type_needed,
	acpi_object_type        this_type,
	void                    *object);

/*
 * exxface - External interpreter interfaces
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
 * exconvrt - object conversion
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
	acpi_object_type        destination_type,
	acpi_operand_object     *source_desc,
	acpi_operand_object     **result_desc,
	acpi_walk_state         *walk_state);

u32
acpi_ex_convert_to_ascii (
	acpi_integer            integer,
	u32                     base,
	u8                      *string);

/*
 * exfield - ACPI AML (p-code) execution - field manipulation
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
acpi_ex_setup_region (
	acpi_operand_object     *obj_desc,
	u32                     field_datum_byte_offset);

acpi_status
acpi_ex_access_region (
	acpi_operand_object     *obj_desc,
	u32                     field_datum_byte_offset,
	acpi_integer            *value,
	u32                     read_write);

u8
acpi_ex_register_overflow (
	acpi_operand_object     *obj_desc,
	acpi_integer            value);

acpi_status
acpi_ex_field_datum_io (
	acpi_operand_object     *obj_desc,
	u32                     field_datum_byte_offset,
	acpi_integer            *value,
	u32                     read_write);

acpi_status
acpi_ex_write_with_update_rule (
	acpi_operand_object     *obj_desc,
	acpi_integer            mask,
	acpi_integer            field_value,
	u32                     field_datum_byte_offset);

void
acpi_ex_get_buffer_datum(
	acpi_integer            *datum,
	void                    *buffer,
	u32                     byte_granularity,
	u32                     offset);

void
acpi_ex_set_buffer_datum (
	acpi_integer            merged_datum,
	void                    *buffer,
	u32                     byte_granularity,
	u32                     offset);

acpi_status
acpi_ex_read_data_from_field (
	acpi_walk_state         *walk_state,
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **ret_buffer_desc);

acpi_status
acpi_ex_write_data_to_field (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *obj_desc);

/*
 * exmisc - ACPI AML (p-code) execution - specific opcodes
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

u8
acpi_ex_do_match (
	u32                     match_op,
	acpi_integer            package_value,
	acpi_integer            match_value);

acpi_status
acpi_ex_get_object_reference (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **return_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_resolve_multiple (
	acpi_walk_state         *walk_state,
	acpi_operand_object     *operand,
	acpi_object_type        *return_type,
	acpi_operand_object     **return_desc);

acpi_status
acpi_ex_concat_template (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     *obj_desc2,
	acpi_operand_object     **actual_return_desc,
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
 * exconfig - dynamic table load/unload
 */

acpi_status
acpi_ex_add_table (
	acpi_table_header       *table,
	acpi_namespace_node     *parent_node,
	acpi_operand_object     **ddb_handle);

acpi_status
acpi_ex_load_op (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     *target,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_load_table_op (
	acpi_walk_state         *walk_state,
	acpi_operand_object     **return_desc);

acpi_status
acpi_ex_unload_table (
	acpi_operand_object     *ddb_handle);


/*
 * exmutex - mutex support
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

void
acpi_ex_release_all_mutexes (
	ACPI_THREAD_STATE       *thread);

void
acpi_ex_unlink_mutex (
	acpi_operand_object     *obj_desc);

void
acpi_ex_link_mutex (
	acpi_operand_object     *obj_desc,
	ACPI_THREAD_STATE       *thread);

/*
 * exprep - ACPI AML (p-code) execution - prep utilities
 */

acpi_status
acpi_ex_prep_common_field_object (
	acpi_operand_object     *obj_desc,
	u8                      field_flags,
	u8                      field_attribute,
	u32                     field_bit_position,
	u32                     field_bit_length);

acpi_status
acpi_ex_prep_field_value (
	ACPI_CREATE_FIELD_INFO  *info);

/*
 * exsystem - Interface to OS services
 */

acpi_status
acpi_ex_system_do_notify_op (
	acpi_operand_object     *value,
	acpi_operand_object     *obj_desc);

acpi_status
acpi_ex_system_do_suspend(
	u32                     time);

acpi_status
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
 * exmonadic - ACPI AML (p-code) execution, monadic operators
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
 * exdyadic - ACPI AML (p-code) execution, dyadic operators
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
 * exresolv  - Object resolution and get value functions
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


/*
 * exdump - Scanner debug output routines
 */

void
acpi_ex_dump_operand (
	acpi_operand_object     *entry_desc);

void
acpi_ex_dump_operands (
	acpi_operand_object     **operands,
	acpi_interpreter_mode   interpreter_mode,
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

void
acpi_ex_out_string (
	char                    *title,
	char                    *value);

void
acpi_ex_out_pointer (
	char                    *title,
	void                    *value);

void
acpi_ex_out_integer (
	char                    *title,
	u32                     value);

void
acpi_ex_out_address (
	char                    *title,
	ACPI_PHYSICAL_ADDRESS   value);


/*
 * exnames - interpreter/scanner name load/execute
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
	acpi_object_type        data_type,
	u8                      *in_aml_address,
	NATIVE_CHAR             **out_name_string,
	u32                     *out_name_length);

acpi_status
acpi_ex_do_name (
	acpi_object_type        data_type,
	acpi_interpreter_mode   load_exec_mode);


/*
 * exstore - Object store support
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


/*
 * exstoren
 */

acpi_status
acpi_ex_resolve_object (
	acpi_operand_object     **source_desc_ptr,
	acpi_object_type        target_type,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ex_store_object_to_object (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *dest_desc,
	acpi_operand_object     **new_desc,
	acpi_walk_state         *walk_state);


/*
 * excopy - object copy
 */

acpi_status
acpi_ex_store_buffer_to_buffer (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *target_desc);

acpi_status
acpi_ex_store_string_to_string (
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
 * exutils - interpreter/scanner utilities
 */

acpi_status
acpi_ex_enter_interpreter (
	void);

void
acpi_ex_exit_interpreter (
	void);

void
acpi_ex_truncate_for32bit_table (
	acpi_operand_object     *obj_desc);

u8
acpi_ex_validate_object_type (
	acpi_object_type        type);

u8
acpi_ex_acquire_global_lock (
	u32                     rule);

void
acpi_ex_release_global_lock (
	u8                      locked);

u32
acpi_ex_digits_needed (
	acpi_integer            value,
	u32                     base);

void
acpi_ex_eisa_id_to_string (
	u32                     numeric_id,
	NATIVE_CHAR             *out_string);

void
acpi_ex_unsigned_integer_to_string (
	acpi_integer            value,
	NATIVE_CHAR             *out_string);


/*
 * exregion - default Op_region handlers
 */

acpi_status
acpi_ex_system_memory_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_system_io_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_pci_config_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_cmos_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_pci_bar_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_embedded_controller_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context);

acpi_status
acpi_ex_sm_bus_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context);


acpi_status
acpi_ex_data_table_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context);

#endif /* __INTERP_H__ */
