/******************************************************************************
 *
 * Name: acdispat.h - dispatcher (parser to interpreter interface)
 *       $Revision: 45 $
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


#ifndef _ACDISPAT_H_
#define _ACDISPAT_H_


#define NAMEOF_LOCAL_NTE    "__L0"
#define NAMEOF_ARG_NTE      "__A0"


/* Common interfaces */

acpi_status
acpi_ds_obj_stack_push (
	void                    *object,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_obj_stack_pop (
	u32                     pop_count,
	acpi_walk_state         *walk_state);

void *
acpi_ds_obj_stack_get_value (
	u32                     index,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_obj_stack_pop_object (
	acpi_operand_object     **object,
	acpi_walk_state         *walk_state);


/* dsopcode - support for late evaluation */

acpi_status
acpi_ds_get_buffer_field_arguments (
	acpi_operand_object     *obj_desc);

acpi_status
acpi_ds_get_region_arguments (
	acpi_operand_object     *rgn_desc);


/* dsctrl - Parser/Interpreter interface, control stack routines */


acpi_status
acpi_ds_exec_begin_control_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op);

acpi_status
acpi_ds_exec_end_control_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op);


/* dsexec - Parser/Interpreter interface, method execution callbacks */


acpi_status
acpi_ds_get_predicate_value (
	acpi_walk_state         *walk_state,
	u32                     has_result_obj);

acpi_status
acpi_ds_exec_begin_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op);

acpi_status
acpi_ds_exec_end_op (
	acpi_walk_state         *state);


/* dsfield - Parser/Interpreter interface for AML fields */

acpi_status
acpi_ds_create_field (
	acpi_parse_object       *op,
	acpi_namespace_node     *region_node,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_create_bank_field (
	acpi_parse_object       *op,
	acpi_namespace_node     *region_node,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_create_index_field (
	acpi_parse_object       *op,
	acpi_namespace_node     *region_node,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_create_buffer_field (
	acpi_parse_object       *op,
	acpi_walk_state         *walk_state);


/* dsload - Parser/Interpreter interface, namespace load callbacks */

acpi_status
acpi_ds_load1_begin_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op);

acpi_status
acpi_ds_load1_end_op (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_load2_begin_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op);

acpi_status
acpi_ds_load2_end_op (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_init_callbacks (
	acpi_walk_state         *walk_state,
	u32                     pass_number);


/* dsmthdat - method data (locals/args) */


acpi_status
acpi_ds_store_object_to_local (
	u16                     opcode,
	u32                     index,
	acpi_operand_object     *src_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_method_data_get_entry (
	u16                     opcode,
	u32                     index,
	acpi_walk_state         *walk_state,
	acpi_operand_object     ***node);

acpi_status
acpi_ds_method_data_delete_all (
	acpi_walk_state         *walk_state);

u8
acpi_ds_is_method_value (
	acpi_operand_object     *obj_desc);

acpi_object_type8
acpi_ds_method_data_get_type (
	u16                     opcode,
	u32                     index,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_method_data_get_value (
	u16                     opcode,
	u32                     index,
	acpi_walk_state         *walk_state,
	acpi_operand_object     **dest_desc);

acpi_status
acpi_ds_method_data_delete_value (
	u16                     opcode,
	u32                     index,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_method_data_init_args (
	acpi_operand_object     **params,
	u32                     max_param_count,
	acpi_walk_state         *walk_state);

acpi_namespace_node *
acpi_ds_method_data_get_node (
	u16                     opcode,
	u32                     index,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_method_data_init (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_method_data_set_entry (
	u16                     opcode,
	u32                     index,
	acpi_operand_object     *object,
	acpi_walk_state         *walk_state);


/* dsmethod - Parser/Interpreter interface - control method parsing */

acpi_status
acpi_ds_parse_method (
	acpi_handle             obj_handle);

acpi_status
acpi_ds_call_control_method (
	acpi_walk_list          *walk_list,
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op);

acpi_status
acpi_ds_restart_control_method (
	acpi_walk_state         *walk_state,
	acpi_operand_object     *return_desc);

acpi_status
acpi_ds_terminate_control_method (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_begin_method_execution (
	acpi_namespace_node     *method_node,
	acpi_operand_object     *obj_desc,
	acpi_namespace_node     *calling_method_node);


/* dsobj - Parser/Interpreter interface - object initialization and conversion */

acpi_status
acpi_ds_init_one_object (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value);

acpi_status
acpi_ds_initialize_objects (
	acpi_table_desc         *table_desc,
	acpi_namespace_node     *start_node);

acpi_status
acpi_ds_build_internal_package_obj (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_operand_object     **obj_desc);

acpi_status
acpi_ds_build_internal_object (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_operand_object     **obj_desc_ptr);

acpi_status
acpi_ds_init_object_from_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	u16                     opcode,
	acpi_operand_object     **obj_desc);

acpi_status
acpi_ds_create_node (
	acpi_walk_state         *walk_state,
	acpi_namespace_node     *node,
	acpi_parse_object       *op);


/* dsregn - Parser/Interpreter interface - Op Region parsing */

acpi_status
acpi_ds_eval_buffer_field_operands (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op);

acpi_status
acpi_ds_eval_region_operands (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op);

acpi_status
acpi_ds_initialize_region (
	acpi_handle             obj_handle);


/* dsutils - Parser/Interpreter interface utility routines */

u8
acpi_ds_is_result_used (
	acpi_parse_object       *op,
	acpi_walk_state         *walk_state);

void
acpi_ds_delete_result_if_not_used (
	acpi_parse_object       *op,
	acpi_operand_object     *result_obj,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_create_operand (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *arg,
	u32                     args_remaining);

acpi_status
acpi_ds_create_operands (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *first_arg);

acpi_status
acpi_ds_resolve_operands (
	acpi_walk_state         *walk_state);

acpi_object_type8
acpi_ds_map_opcode_to_data_type (
	u16                     opcode,
	u32                     *out_flags);

acpi_object_type8
acpi_ds_map_named_opcode_to_data_type (
	u16                     opcode);


/*
 * dswscope - Scope Stack manipulation
 */

acpi_status
acpi_ds_scope_stack_push (
	acpi_namespace_node     *node,
	acpi_object_type8       type,
	acpi_walk_state         *walk_state);


acpi_status
acpi_ds_scope_stack_pop (
	acpi_walk_state         *walk_state);

void
acpi_ds_scope_stack_clear (
	acpi_walk_state         *walk_state);


/* dswstate - parser WALK_STATE management routines */

acpi_walk_state *
acpi_ds_create_walk_state (
	acpi_owner_id           owner_id,
	acpi_parse_object       *origin,
	acpi_operand_object     *mth_desc,
	acpi_walk_list          *walk_list);

acpi_status
acpi_ds_init_aml_walk (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_namespace_node     *method_node,
	u8                      *aml_start,
	u32                     aml_length,
	acpi_operand_object     **params,
	acpi_operand_object     **return_obj_desc,
	u32                     pass_number);

acpi_status
acpi_ds_obj_stack_delete_all (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_obj_stack_pop_and_delete (
	u32                     pop_count,
	acpi_walk_state         *walk_state);

void
acpi_ds_delete_walk_state (
	acpi_walk_state         *walk_state);

acpi_walk_state *
acpi_ds_pop_walk_state (
	acpi_walk_list          *walk_list);

void
acpi_ds_push_walk_state (
	acpi_walk_state         *walk_state,
	acpi_walk_list          *walk_list);

acpi_status
acpi_ds_result_stack_pop (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_result_stack_push (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_result_stack_clear (
	acpi_walk_state         *walk_state);

acpi_walk_state *
acpi_ds_get_current_walk_state (
	acpi_walk_list          *walk_list);

void
acpi_ds_delete_walk_state_cache (
	void);

acpi_status
acpi_ds_result_insert (
	void                    *object,
	u32                     index,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_result_remove (
	acpi_operand_object     **object,
	u32                     index,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_result_pop (
	acpi_operand_object     **object,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_result_push (
	acpi_operand_object     *object,
	acpi_walk_state         *walk_state);

acpi_status
acpi_ds_result_pop_from_bottom (
	acpi_operand_object     **object,
	acpi_walk_state         *walk_state);

#endif /* _ACDISPAT_H_ */
