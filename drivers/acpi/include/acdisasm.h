/******************************************************************************
 *
 * Name: acdisasm.h - AML disassembler
 *       $Revision: 2 $
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

#ifndef __ACDISASM_H__
#define __ACDISASM_H__

#include "amlresrc.h"


#define BLOCK_NONE              0
#define BLOCK_PAREN             1
#define BLOCK_BRACE             2
#define BLOCK_COMMA_LIST        4

extern const char               *acpi_gbl_io_decode[2];
extern const char               *acpi_gbl_word_decode[4];
extern const char               *acpi_gbl_consume_decode[2];
extern const char               *acpi_gbl_min_decode[2];
extern const char               *acpi_gbl_max_decode[2];
extern const char               *acpi_gbl_DECdecode[2];
extern const char               *acpi_gbl_RNGdecode[4];
extern const char               *acpi_gbl_MEMdecode[4];
extern const char               *acpi_gbl_RWdecode[2];
extern const char               *acpi_gbl_irq_decode[2];
extern const char               *acpi_gbl_HEdecode[2];
extern const char               *acpi_gbl_LLdecode[2];
extern const char               *acpi_gbl_SHRdecode[2];
extern const char               *acpi_gbl_TYPdecode[4];
extern const char               *acpi_gbl_BMdecode[2];
extern const char               *acpi_gbl_SIZdecode[4];
extern const NATIVE_CHAR        *acpi_gbl_lock_rule[NUM_LOCK_RULES];
extern const NATIVE_CHAR        *acpi_gbl_access_types[NUM_ACCESS_TYPES];
extern const NATIVE_CHAR        *acpi_gbl_update_rules[NUM_UPDATE_RULES];
extern const NATIVE_CHAR        *acpi_gbl_match_ops[NUM_MATCH_OPS];


typedef struct acpi_op_walk_info
{
	u32                     level;
	u32                     bit_offset;

} ACPI_OP_WALK_INFO;

typedef
acpi_status (*ASL_WALK_CALLBACK) (
	acpi_parse_object           *op,
	u32                         level,
	void                        *context);


/*
 * dmwalk
 */

void
acpi_dm_walk_parse_tree (
	acpi_parse_object       *op,
	ASL_WALK_CALLBACK       descending_callback,
	ASL_WALK_CALLBACK       ascending_callback,
	void                    *context);

acpi_status
acpi_dm_descending_op (
	acpi_parse_object       *op,
	u32                     level,
	void                    *context);

acpi_status
acpi_dm_ascending_op (
	acpi_parse_object       *op,
	u32                     level,
	void                    *context);


/*
 * dmopcode
 */

void
acpi_dm_validate_name (
	char                    *name,
	acpi_parse_object       *op);

u32
acpi_dm_dump_name (
	char                    *name);

void
acpi_dm_string (
	char                    *string);

void
acpi_dm_unicode (
	acpi_parse_object       *op);

void
acpi_dm_disassemble (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *origin,
	u32                     num_opcodes);

void
acpi_dm_namestring (
	NATIVE_CHAR             *name);

void
acpi_dm_display_path (
	acpi_parse_object       *op);

void
acpi_dm_disassemble_one_op (
	acpi_walk_state         *walk_state,
	ACPI_OP_WALK_INFO       *info,
	acpi_parse_object       *op);

void
acpi_dm_decode_internal_object (
	acpi_operand_object     *obj_desc);

void
acpi_dm_decode_node (
	acpi_namespace_node     *node);

u32
acpi_dm_block_type (
	acpi_parse_object       *op);

u32
acpi_dm_list_type (
	acpi_parse_object       *op);

acpi_status
acpi_ps_display_object_pathname (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op);

void
acpi_dm_method_flags (
	acpi_parse_object       *op);

void
acpi_dm_field_flags (
	acpi_parse_object       *op);

void
acpi_dm_address_space (
	u8                      space_id);

void
acpi_dm_region_flags (
	acpi_parse_object       *op);

void
acpi_dm_match_op (
	acpi_parse_object       *op);

void
acpi_dm_match_keyword (
	acpi_parse_object       *op);

u8
acpi_dm_comma_if_list_member (
	acpi_parse_object       *op);

void
acpi_dm_comma_if_field_member (
	acpi_parse_object       *op);


/*
 * dmbuffer
 */

void
acpi_is_eisa_id (
	acpi_parse_object       *op);

void
acpi_dm_eisa_id (
	u32                     encoded_id);

u8
acpi_dm_is_unicode_buffer (
	acpi_parse_object       *op);

u8
acpi_dm_is_string_buffer (
	acpi_parse_object       *op);


/*
 * dmresrc
 */

void
acpi_dm_disasm_byte_list (
	u32                     level,
	u8                      *byte_data,
	u32                     byte_count);

void
acpi_dm_byte_list (
	ACPI_OP_WALK_INFO       *info,
	acpi_parse_object       *op);

void
acpi_dm_resource_descriptor (
	ACPI_OP_WALK_INFO       *info,
	u8                      *byte_data,
	u32                     byte_count);

u8
acpi_dm_is_resource_descriptor (
	acpi_parse_object       *op);

void
acpi_dm_indent (
	u32                     level);

void
acpi_dm_bit_list (
	u16                     mask);


/*
 * dmresrcl
 */

void
acpi_dm_io_flags (
		u8                  flags);

void
acpi_dm_memory_flags (
	u8                      flags,
	u8                      specific_flags);

void
acpi_dm_word_descriptor (
	ASL_WORD_ADDRESS_DESC   *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_dword_descriptor (
	ASL_DWORD_ADDRESS_DESC  *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_qword_descriptor (
	ASL_QWORD_ADDRESS_DESC  *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_memory24_descriptor (
	ASL_MEMORY_24_DESC      *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_memory32_descriptor (
	ASL_MEMORY_32_DESC      *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_fixed_mem32_descriptor (
	ASL_FIXED_MEMORY_32_DESC *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_generic_register_descriptor (
	ASL_GENERAL_REGISTER_DESC *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_interrupt_descriptor (
	ASL_EXTENDED_XRUPT_DESC *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_vendor_large_descriptor (
	ASL_LARGE_VENDOR_DESC   *resource,
	u32                     length,
	u32                     level);


/*
 * dmresrcs
 */

void
acpi_dm_irq_descriptor (
	ASL_IRQ_FORMAT_DESC     *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_dma_descriptor (
	ASL_DMA_FORMAT_DESC     *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_io_descriptor (
	ASL_IO_PORT_DESC        *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_fixed_io_descriptor (
	ASL_FIXED_IO_PORT_DESC  *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_start_dependent_descriptor (
	ASL_START_DEPENDENT_DESC *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_end_dependent_descriptor (
	ASL_START_DEPENDENT_DESC *resource,
	u32                     length,
	u32                     level);

void
acpi_dm_vendor_small_descriptor (
	ASL_SMALL_VENDOR_DESC   *resource,
	u32                     length,
	u32                     level);


#endif  /* __ACDISASM_H__ */
