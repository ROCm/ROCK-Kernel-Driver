/******************************************************************************
 *
 * Name: acnamesp.h - Namespace subcomponent prototypes and defines
 *       $Revision: 110 $
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

#ifndef __ACNAMESP_H__
#define __ACNAMESP_H__


/* To search the entire name space, pass this as Search_base */

#define NS_ALL                  ((acpi_handle)0)

/*
 * Elements of Acpi_ns_properties are bit significant
 * and should be one-to-one with values of acpi_object_type
 */
#define NSP_NORMAL              0
#define NSP_NEWSCOPE            1   /* a definition of this type opens a name scope */
#define NSP_LOCAL               2   /* suppress search of enclosing scopes */


/* Definitions of the predefined namespace names  */

#define ACPI_UNKNOWN_NAME       (u32) 0x3F3F3F3F     /* Unknown name is  "????" */
#define ACPI_ROOT_NAME          (u32) 0x2F202020     /* Root name is     "/   " */
#define ACPI_SYS_BUS_NAME       (u32) 0x5F53425F     /* Sys bus name is  "_SB_" */

#define NS_ROOT_PATH            "/"
#define NS_SYSTEM_BUS           "_SB_"


/* Flags for Acpi_ns_lookup, Acpi_ns_search_and_enter */

#define NS_NO_UPSEARCH          0
#define NS_SEARCH_PARENT        0x01
#define NS_DONT_OPEN_SCOPE      0x02
#define NS_NO_PEER_SEARCH       0x04
#define NS_ERROR_IF_FOUND       0x08

#define NS_WALK_UNLOCK          TRUE
#define NS_WALK_NO_UNLOCK       FALSE


acpi_status
acpi_ns_load_namespace (
	void);

acpi_status
acpi_ns_initialize_objects (
	void);

acpi_status
acpi_ns_initialize_devices (
	void);


/* Namespace init - nsxfinit */

acpi_status
acpi_ns_init_one_device (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value);

acpi_status
acpi_ns_init_one_object (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value);


acpi_status
acpi_ns_walk_namespace (
	acpi_object_type8       type,
	acpi_handle             start_object,
	u32                     max_depth,
	u8                      unlock_before_callback,
	acpi_walk_callback      user_function,
	void                    *context,
	void                    **return_value);

acpi_namespace_node *
acpi_ns_get_next_node (
	acpi_object_type8       type,
	acpi_namespace_node     *parent,
	acpi_namespace_node     *child);

acpi_status
acpi_ns_delete_namespace_by_owner (
	u16                     table_id);


/* Namespace loading - nsload */

acpi_status
acpi_ns_one_complete_parse (
	u32                     pass_number,
	acpi_table_desc         *table_desc);

acpi_status
acpi_ns_parse_table (
	acpi_table_desc         *table_desc,
	acpi_namespace_node     *scope);

acpi_status
acpi_ns_load_table (
	acpi_table_desc         *table_desc,
	acpi_namespace_node     *node);

acpi_status
acpi_ns_load_table_by_type (
	acpi_table_type         table_type);


/*
 * Top-level namespace access - nsaccess
 */


acpi_status
acpi_ns_root_initialize (
	void);

acpi_status
acpi_ns_lookup (
	acpi_generic_state      *scope_info,
	NATIVE_CHAR             *name,
	acpi_object_type8       type,
	operating_mode          interpreter_mode,
	u32                     flags,
	acpi_walk_state         *walk_state,
	acpi_namespace_node     **ret_node);


/*
 * Named object allocation/deallocation - nsalloc
 */


acpi_namespace_node *
acpi_ns_create_node (
	u32                     name);

void
acpi_ns_delete_node (
	acpi_namespace_node     *node);

acpi_status
acpi_ns_delete_namespace_subtree (
	acpi_namespace_node     *parent_handle);

void
acpi_ns_detach_object (
	acpi_namespace_node     *node);

void
acpi_ns_delete_children (
	acpi_namespace_node     *parent);


/*
 * Namespace modification - nsmodify
 */

acpi_status
acpi_ns_unload_namespace (
	acpi_handle             handle);

acpi_status
acpi_ns_delete_subtree (
	acpi_handle             start_handle);


/*
 * Namespace dump/print utilities - nsdump
 */

void
acpi_ns_dump_tables (
	acpi_handle             search_base,
	u32                     max_depth);

void
acpi_ns_dump_entry (
	acpi_handle             handle,
	u32                     debug_level);

acpi_status
acpi_ns_dump_pathname (
	acpi_handle             handle,
	NATIVE_CHAR             *msg,
	u32                     level,
	u32                     component);

void
acpi_ns_dump_root_devices (
	void);

void
acpi_ns_dump_objects (
	acpi_object_type8       type,
	u8                      display_type,
	u32                     max_depth,
	u32                     ownder_id,
	acpi_handle             start_handle);


/*
 * Namespace evaluation functions - nseval
 */

acpi_status
acpi_ns_evaluate_by_handle (
	acpi_namespace_node     *prefix_node,
	acpi_operand_object     **params,
	acpi_operand_object     **return_object);

acpi_status
acpi_ns_evaluate_by_name (
	NATIVE_CHAR             *pathname,
	acpi_operand_object     **params,
	acpi_operand_object     **return_object);

acpi_status
acpi_ns_evaluate_relative (
	acpi_namespace_node     *prefix_node,
	NATIVE_CHAR             *pathname,
	acpi_operand_object     **params,
	acpi_operand_object     **return_object);

acpi_status
acpi_ns_execute_control_method (
	acpi_namespace_node     *method_node,
	acpi_operand_object     **params,
	acpi_operand_object     **return_obj_desc);

acpi_status
acpi_ns_get_object_value (
	acpi_namespace_node     *object_node,
	acpi_operand_object     **return_obj_desc);


/*
 * Parent/Child/Peer utility functions - nsfamily
 */

acpi_name
acpi_ns_find_parent_name (
	acpi_namespace_node     *node_to_search);

u8
acpi_ns_exist_downstream_sibling (
	acpi_namespace_node     *this_node);


/*
 * Scope manipulation - nsscope
 */

u32
acpi_ns_opens_scope (
	acpi_object_type8       type);

NATIVE_CHAR *
acpi_ns_get_table_pathname (
	acpi_namespace_node     *node);

NATIVE_CHAR *
acpi_ns_name_of_current_scope (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ns_handle_to_pathname (
	acpi_handle             obj_handle,
	u32                     *buf_size,
	NATIVE_CHAR             *user_buffer);

u8
acpi_ns_pattern_match (
	acpi_namespace_node     *obj_node,
	NATIVE_CHAR             *search_for);

acpi_status
acpi_ns_get_node (
	NATIVE_CHAR             *pathname,
	acpi_namespace_node     *in_prefix_node,
	acpi_namespace_node     **out_node);

u32
acpi_ns_get_pathname_length (
	acpi_namespace_node     *node);


/*
 * Object management for NTEs - nsobject
 */

acpi_status
acpi_ns_attach_object (
	acpi_namespace_node     *node,
	acpi_operand_object     *object,
	acpi_object_type8       type);


/*
 * Namespace searching and entry - nssearch
 */

acpi_status
acpi_ns_search_and_enter (
	u32                     entry_name,
	acpi_walk_state         *walk_state,
	acpi_namespace_node     *node,
	operating_mode          interpreter_mode,
	acpi_object_type8       type,
	u32                     flags,
	acpi_namespace_node     **ret_node);

acpi_status
acpi_ns_search_node (
	u32                     entry_name,
	acpi_namespace_node     *node,
	acpi_object_type8       type,
	acpi_namespace_node     **ret_node);

void
acpi_ns_install_node (
	acpi_walk_state         *walk_state,
	acpi_namespace_node     *parent_node,   /* Parent */
	acpi_namespace_node     *node,      /* New Child*/
	acpi_object_type8       type);


/*
 * Utility functions - nsutils
 */

u8
acpi_ns_valid_root_prefix (
	NATIVE_CHAR             prefix);

u8
acpi_ns_valid_path_separator (
	NATIVE_CHAR             sep);

acpi_object_type8
acpi_ns_get_type (
	acpi_namespace_node     *node);

void *
acpi_ns_get_attached_object (
	acpi_namespace_node     *node);

u32
acpi_ns_local (
	acpi_object_type8       type);

acpi_status
acpi_ns_build_internal_name (
	acpi_namestring_info    *info);

acpi_status
acpi_ns_get_internal_name_length (
	acpi_namestring_info    *info);

acpi_status
acpi_ns_internalize_name (
	NATIVE_CHAR             *dotted_name,
	NATIVE_CHAR             **converted_name);

acpi_status
acpi_ns_externalize_name (
	u32                     internal_name_length,
	NATIVE_CHAR             *internal_name,
	u32                     *converted_name_length,
	NATIVE_CHAR             **converted_name);

acpi_namespace_node *
acpi_ns_map_handle_to_node (
	acpi_handle             handle);

acpi_handle
acpi_ns_convert_entry_to_handle(
	acpi_namespace_node     *node);

void
acpi_ns_terminate (
	void);

acpi_namespace_node *
acpi_ns_get_parent_object (
	acpi_namespace_node     *node);


acpi_namespace_node *
acpi_ns_get_next_valid_node (
	acpi_namespace_node     *node);


#endif /* __ACNAMESP_H__ */
