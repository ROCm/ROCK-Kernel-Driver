/******************************************************************************
 *
 * Name: acinterp.h - Interpreter subcomponent prototypes and defines
 *       $Revision: 102 $
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


ACPI_STATUS
acpi_ex_resolve_operands (
	u16                     opcode,
	ACPI_OPERAND_OBJECT     **stack_ptr,
	ACPI_WALK_STATE         *walk_state);


/*
 * amxface - External interpreter interfaces
 */

ACPI_STATUS
acpi_ex_load_table (
	ACPI_TABLE_TYPE         table_id);

ACPI_STATUS
acpi_ex_execute_method (
	ACPI_NAMESPACE_NODE     *method_node,
	ACPI_OPERAND_OBJECT     **params,
	ACPI_OPERAND_OBJECT     **return_obj_desc);


/*
 * amconvrt - object conversion
 */

ACPI_STATUS
acpi_ex_convert_to_integer (
	ACPI_OPERAND_OBJECT     **obj_desc,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_convert_to_buffer (
	ACPI_OPERAND_OBJECT     **obj_desc,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_convert_to_string (
	ACPI_OPERAND_OBJECT     **obj_desc,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_convert_to_target_type (
	ACPI_OBJECT_TYPE8       destination_type,
	ACPI_OPERAND_OBJECT     **obj_desc,
	ACPI_WALK_STATE         *walk_state);


/*
 * amfield - ACPI AML (p-code) execution - field manipulation
 */

ACPI_STATUS
acpi_ex_extract_from_field (
	ACPI_OPERAND_OBJECT     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

ACPI_STATUS
acpi_ex_insert_into_field (
	ACPI_OPERAND_OBJECT     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

ACPI_STATUS
acpi_ex_setup_field (
	ACPI_OPERAND_OBJECT     *obj_desc,
	u32                     field_byte_offset);

ACPI_STATUS
acpi_ex_read_field_datum (
	ACPI_OPERAND_OBJECT     *obj_desc,
	u32                     field_byte_offset,
	u32                     *value);

ACPI_STATUS
acpi_ex_common_access_field (
	u32                     mode,
	ACPI_OPERAND_OBJECT     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);


ACPI_STATUS
acpi_ex_access_index_field (
	u32                     mode,
	ACPI_OPERAND_OBJECT     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

ACPI_STATUS
acpi_ex_access_bank_field (
	u32                     mode,
	ACPI_OPERAND_OBJECT     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

ACPI_STATUS
acpi_ex_access_region_field (
	u32                     mode,
	ACPI_OPERAND_OBJECT     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);


ACPI_STATUS
acpi_ex_access_buffer_field (
	u32                     mode,
	ACPI_OPERAND_OBJECT     *obj_desc,
	void                    *buffer,
	u32                     buffer_length);

ACPI_STATUS
acpi_ex_read_data_from_field (
	ACPI_OPERAND_OBJECT     *obj_desc,
	ACPI_OPERAND_OBJECT     **ret_buffer_desc);

ACPI_STATUS
acpi_ex_write_data_to_field (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_OPERAND_OBJECT     *obj_desc);

/*
 * ammisc - ACPI AML (p-code) execution - specific opcodes
 */

ACPI_STATUS
acpi_ex_create_buffer_field (
	u8                      *aml_ptr,
	u32                     aml_length,
	ACPI_NAMESPACE_NODE     *node,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_reconfiguration (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_fatal (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_index (
	ACPI_WALK_STATE         *walk_state,
	ACPI_OPERAND_OBJECT     **return_desc);

ACPI_STATUS
acpi_ex_match (
	ACPI_WALK_STATE         *walk_state,
	ACPI_OPERAND_OBJECT     **return_desc);

ACPI_STATUS
acpi_ex_create_mutex (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_create_processor (
	ACPI_PARSE_OBJECT       *op,
	ACPI_NAMESPACE_NODE     *processor_node);

ACPI_STATUS
acpi_ex_create_power_resource (
	ACPI_PARSE_OBJECT       *op,
	ACPI_NAMESPACE_NODE     *power_node);

ACPI_STATUS
acpi_ex_create_region (
	u8                      *aml_ptr,
	u32                     aml_length,
	u8                      region_space,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_create_event (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_create_alias (
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_create_method (
	u8                      *aml_ptr,
	u32                     aml_length,
	u32                     method_flags,
	ACPI_NAMESPACE_NODE     *method);


/*
 * ammutex - mutex support
 */

ACPI_STATUS
acpi_ex_acquire_mutex (
	ACPI_OPERAND_OBJECT     *time_desc,
	ACPI_OPERAND_OBJECT     *obj_desc,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_release_mutex (
	ACPI_OPERAND_OBJECT     *obj_desc,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_release_all_mutexes (
	ACPI_OPERAND_OBJECT     *mutex_list);

void
acpi_ex_unlink_mutex (
	ACPI_OPERAND_OBJECT     *obj_desc);


/*
 * amprep - ACPI AML (p-code) execution - prep utilities
 */

ACPI_STATUS
acpi_ex_prep_common_field_object (
	ACPI_OPERAND_OBJECT     *obj_desc,
	u8                      field_flags,
	u32                     field_position,
	u32                     field_length);

ACPI_STATUS
acpi_ex_prep_region_field_value (
	ACPI_NAMESPACE_NODE     *node,
	ACPI_HANDLE             region,
	u8                      field_flags,
	u32                     field_position,
	u32                     field_length);

ACPI_STATUS
acpi_ex_prep_bank_field_value (
	ACPI_NAMESPACE_NODE     *node,
	ACPI_NAMESPACE_NODE     *region_node,
	ACPI_NAMESPACE_NODE     *bank_register_node,
	u32                     bank_val,
	u8                      field_flags,
	u32                     field_position,
	u32                     field_length);

ACPI_STATUS
acpi_ex_prep_index_field_value (
	ACPI_NAMESPACE_NODE     *node,
	ACPI_NAMESPACE_NODE     *index_reg,
	ACPI_NAMESPACE_NODE     *data_reg,
	u8                      field_flags,
	u32                     field_position,
	u32                     field_length);


/*
 * amsystem - Interface to OS services
 */

ACPI_STATUS
acpi_ex_system_do_notify_op (
	ACPI_OPERAND_OBJECT     *value,
	ACPI_OPERAND_OBJECT     *obj_desc);

void
acpi_ex_system_do_suspend(
	u32                     time);

void
acpi_ex_system_do_stall (
	u32                     time);

ACPI_STATUS
acpi_ex_system_acquire_mutex(
	ACPI_OPERAND_OBJECT     *time,
	ACPI_OPERAND_OBJECT     *obj_desc);

ACPI_STATUS
acpi_ex_system_release_mutex(
	ACPI_OPERAND_OBJECT     *obj_desc);

ACPI_STATUS
acpi_ex_system_signal_event(
	ACPI_OPERAND_OBJECT     *obj_desc);

ACPI_STATUS
acpi_ex_system_wait_event(
	ACPI_OPERAND_OBJECT     *time,
	ACPI_OPERAND_OBJECT     *obj_desc);

ACPI_STATUS
acpi_ex_system_reset_event(
	ACPI_OPERAND_OBJECT     *obj_desc);

ACPI_STATUS
acpi_ex_system_wait_semaphore (
	ACPI_HANDLE             semaphore,
	u32                     timeout);


/*
 * ammonadic - ACPI AML (p-code) execution, monadic operators
 */

ACPI_STATUS
acpi_ex_monadic1 (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_monadic2 (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OPERAND_OBJECT     **return_desc);

ACPI_STATUS
acpi_ex_monadic2_r (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OPERAND_OBJECT     **return_desc);


/*
 * amdyadic - ACPI AML (p-code) execution, dyadic operators
 */

ACPI_STATUS
acpi_ex_dyadic1 (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_dyadic2 (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OPERAND_OBJECT     **return_desc);

ACPI_STATUS
acpi_ex_dyadic2_r (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OPERAND_OBJECT     **return_desc);

ACPI_STATUS
acpi_ex_dyadic2_s (
	u16                     opcode,
	ACPI_WALK_STATE         *walk_state,
	ACPI_OPERAND_OBJECT     **return_desc);


/*
 * amresolv  - Object resolution and get value functions
 */

ACPI_STATUS
acpi_ex_resolve_to_value (
	ACPI_OPERAND_OBJECT     **stack_ptr,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_resolve_node_to_value (
	ACPI_NAMESPACE_NODE     **stack_ptr,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_resolve_object_to_value (
	ACPI_OPERAND_OBJECT     **stack_ptr,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_get_buffer_field_value (
	ACPI_OPERAND_OBJECT     *field_desc,
	ACPI_OPERAND_OBJECT     *result_desc);


/*
 * amdump - Scanner debug output routines
 */

void
acpi_ex_show_hex_value (
	u32                     byte_count,
	u8                      *aml_ptr,
	u32                     lead_space);


ACPI_STATUS
acpi_ex_dump_operand (
	ACPI_OPERAND_OBJECT     *entry_desc);

void
acpi_ex_dump_operands (
	ACPI_OPERAND_OBJECT     **operands,
	OPERATING_MODE          interpreter_mode,
	NATIVE_CHAR             *ident,
	u32                     num_levels,
	NATIVE_CHAR             *note,
	NATIVE_CHAR             *module_name,
	u32                     line_number);

void
acpi_ex_dump_object_descriptor (
	ACPI_OPERAND_OBJECT     *object,
	u32                     flags);


void
acpi_ex_dump_node (
	ACPI_NAMESPACE_NODE     *node,
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

ACPI_STATUS
acpi_ex_name_segment (
	u8                      **in_aml_address,
	NATIVE_CHAR             *name_string);

ACPI_STATUS
acpi_ex_get_name_string (
	ACPI_OBJECT_TYPE8       data_type,
	u8                      *in_aml_address,
	NATIVE_CHAR             **out_name_string,
	u32                     *out_name_length);

ACPI_STATUS
acpi_ex_do_name (
	ACPI_OBJECT_TYPE        data_type,
	OPERATING_MODE          load_exec_mode);


/*
 * amstore - Object store support
 */

ACPI_STATUS
acpi_ex_store (
	ACPI_OPERAND_OBJECT     *val_desc,
	ACPI_OPERAND_OBJECT     *dest_desc,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_store_object_to_index (
	ACPI_OPERAND_OBJECT     *val_desc,
	ACPI_OPERAND_OBJECT     *dest_desc,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_store_object_to_node (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_NAMESPACE_NODE     *node,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_store_object_to_object (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_OPERAND_OBJECT     *dest_desc,
	ACPI_WALK_STATE         *walk_state);


/*
 *
 */

ACPI_STATUS
acpi_ex_resolve_object (
	ACPI_OPERAND_OBJECT     **source_desc_ptr,
	ACPI_OBJECT_TYPE8       target_type,
	ACPI_WALK_STATE         *walk_state);

ACPI_STATUS
acpi_ex_store_object (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_OBJECT_TYPE8       target_type,
	ACPI_OPERAND_OBJECT     **target_desc_ptr,
	ACPI_WALK_STATE         *walk_state);


/*
 * amcopy - object copy
 */

ACPI_STATUS
acpi_ex_copy_buffer_to_buffer (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_OPERAND_OBJECT     *target_desc);

ACPI_STATUS
acpi_ex_copy_string_to_string (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_OPERAND_OBJECT     *target_desc);

ACPI_STATUS
acpi_ex_copy_integer_to_index_field (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_OPERAND_OBJECT     *target_desc);

ACPI_STATUS
acpi_ex_copy_integer_to_bank_field (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_OPERAND_OBJECT     *target_desc);

ACPI_STATUS
acpi_ex_copy_data_to_named_field (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_NAMESPACE_NODE     *node);

ACPI_STATUS
acpi_ex_copy_integer_to_buffer_field (
	ACPI_OPERAND_OBJECT     *source_desc,
	ACPI_OPERAND_OBJECT     *target_desc);

/*
 * amutils - interpreter/scanner utilities
 */

ACPI_STATUS
acpi_ex_enter_interpreter (
	void);

void
acpi_ex_exit_interpreter (
	void);

void
acpi_ex_truncate_for32bit_table (
	ACPI_OPERAND_OBJECT     *obj_desc,
	ACPI_WALK_STATE         *walk_state);

u8
acpi_ex_validate_object_type (
	ACPI_OBJECT_TYPE        type);

u8
acpi_ex_acquire_global_lock (
	u32                     rule);

ACPI_STATUS
acpi_ex_release_global_lock (
	u8                      locked);

u32
acpi_ex_digits_needed (
	ACPI_INTEGER            value,
	u32                     base);

ACPI_STATUS
acpi_ex_eisa_id_to_string (
	u32                     numeric_id,
	NATIVE_CHAR             *out_string);

ACPI_STATUS
acpi_ex_unsigned_integer_to_string (
	ACPI_INTEGER            value,
	NATIVE_CHAR             *out_string);


/*
 * amregion - default Op_region handlers
 */

ACPI_STATUS
acpi_ex_system_memory_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

ACPI_STATUS
acpi_ex_system_io_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

ACPI_STATUS
acpi_ex_pci_config_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

ACPI_STATUS
acpi_ex_embedded_controller_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);

ACPI_STATUS
acpi_ex_sm_bus_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context);


#endif /* __INTERP_H__ */
