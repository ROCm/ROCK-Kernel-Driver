/******************************************************************************
 *
 * Module Name: acparser.h - AML Parser subcomponent prototypes and defines
 *       $Revision: 54 $
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


#ifndef __ACPARSER_H__
#define __ACPARSER_H__


#define OP_HAS_RETURN_VALUE         1

/* variable # arguments */

#define ACPI_VAR_ARGS               ACPI_UINT32_MAX

/* maximum virtual address */

#define ACPI_MAX_AML                ((u8 *)(~0UL))


#define ACPI_PARSE_DELETE_TREE          0x0001
#define ACPI_PARSE_NO_TREE_DELETE       0x0000
#define ACPI_PARSE_TREE_MASK            0x0001

#define ACPI_PARSE_LOAD_PASS1           0x0010
#define ACPI_PARSE_LOAD_PASS2           0x0020
#define ACPI_PARSE_EXECUTE              0x0030
#define ACPI_PARSE_MODE_MASK            0x0030

/* psapi - Parser external interfaces */

acpi_status
acpi_psx_load_table (
	u8                      *pcode_addr,
	u32                     pcode_length);

acpi_status
acpi_psx_execute (
	acpi_namespace_node     *method_node,
	acpi_operand_object     **params,
	acpi_operand_object     **return_obj_desc);

/******************************************************************************
 *
 * Parser interfaces
 *
 *****************************************************************************/


/* psargs - Parse AML opcode arguments */

u8 *
acpi_ps_get_next_package_end (
	acpi_parse_state        *parser_state);

u32
acpi_ps_get_next_package_length (
	acpi_parse_state        *parser_state);

NATIVE_CHAR *
acpi_ps_get_next_namestring (
	acpi_parse_state        *parser_state);

void
acpi_ps_get_next_simple_arg (
	acpi_parse_state        *parser_state,
	u32                     arg_type,       /* type of argument */
	acpi_parse_object       *arg);           /* (OUT) argument data */

void
acpi_ps_get_next_namepath (
	acpi_parse_state        *parser_state,
	acpi_parse_object       *arg,
	u32                     *arg_count,
	u8                      method_call);

acpi_parse_object *
acpi_ps_get_next_field (
	acpi_parse_state        *parser_state);

acpi_parse_object *
acpi_ps_get_next_arg (
	acpi_parse_state        *parser_state,
	u32                     arg_type,
	u32                     *arg_count);


/* psopcode - AML Opcode information */

const acpi_opcode_info *
acpi_ps_get_opcode_info (
	u16                     opcode);

NATIVE_CHAR *
acpi_ps_get_opcode_name (
	u16                     opcode);


/* psparse - top level parsing routines */

acpi_status
acpi_ps_find_object (
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op);

void
acpi_ps_delete_parse_tree (
	acpi_parse_object       *root);

acpi_status
acpi_ps_parse_loop (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ps_parse_aml (
	acpi_walk_state         *walk_state);

acpi_status
acpi_ps_parse_table (
	u8                      *aml,
	u32                     aml_size,
	acpi_parse_downwards    descending_callback,
	acpi_parse_upwards      ascending_callback,
	acpi_parse_object       **root_object);

u16
acpi_ps_peek_opcode (
	acpi_parse_state        *state);


/* psscope - Scope stack management routines */


acpi_status
acpi_ps_init_scope (
	acpi_parse_state        *parser_state,
	acpi_parse_object       *root);

acpi_parse_object *
acpi_ps_get_parent_scope (
	acpi_parse_state        *state);

u8
acpi_ps_has_completed_scope (
	acpi_parse_state        *parser_state);

void
acpi_ps_pop_scope (
	acpi_parse_state        *parser_state,
	acpi_parse_object       **op,
	u32                     *arg_list,
	u32                     *arg_count);

acpi_status
acpi_ps_push_scope (
	acpi_parse_state        *parser_state,
	acpi_parse_object       *op,
	u32                     remaining_args,
	u32                     arg_count);

void
acpi_ps_cleanup_scope (
	acpi_parse_state        *state);


/* pstree - parse tree manipulation routines */

void
acpi_ps_append_arg(
	acpi_parse_object       *op,
	acpi_parse_object       *arg);

acpi_parse_object*
acpi_ps_find (
	acpi_parse_object       *scope,
	NATIVE_CHAR             *path,
	u16                     opcode,
	u32                     create);

acpi_parse_object *
acpi_ps_get_arg(
	acpi_parse_object       *op,
	u32                      argn);

acpi_parse_object *
acpi_ps_get_child (
	acpi_parse_object       *op);

acpi_parse_object *
acpi_ps_get_depth_next (
	acpi_parse_object       *origin,
	acpi_parse_object       *op);


/* pswalk - parse tree walk routines */

acpi_status
acpi_ps_walk_parsed_aml (
	acpi_parse_object       *start_op,
	acpi_parse_object       *end_op,
	acpi_operand_object     *mth_desc,
	acpi_namespace_node     *start_node,
	acpi_operand_object     **params,
	acpi_operand_object     **caller_return_desc,
	acpi_owner_id           owner_id,
	acpi_parse_downwards    descending_callback,
	acpi_parse_upwards      ascending_callback);

acpi_status
acpi_ps_get_next_walk_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_parse_upwards      ascending_callback);


/* psutils - parser utilities */

void
acpi_ps_init_op (
	acpi_parse_object       *op,
	u16                     opcode);

acpi_parse_object *
acpi_ps_alloc_op (
	u16                     opcode);

void
acpi_ps_free_op (
	acpi_parse_object       *op);

void
acpi_ps_delete_parse_cache (
	void);

u8
acpi_ps_is_leading_char (
	u32                     c);

u8
acpi_ps_is_prefix_char (
	u32                     c);

u32
acpi_ps_get_name(
	acpi_parse_object       *op);

void
acpi_ps_set_name(
	acpi_parse_object       *op,
	u32                     name);


/* psdump - display parser tree */

u32
acpi_ps_sprint_path (
	NATIVE_CHAR             *buffer_start,
	u32                     buffer_size,
	acpi_parse_object       *op);

u32
acpi_ps_sprint_op (
	NATIVE_CHAR             *buffer_start,
	u32                     buffer_size,
	acpi_parse_object       *op);

void
acpi_ps_show (
	acpi_parse_object       *op);


#endif /* __ACPARSER_H__ */
