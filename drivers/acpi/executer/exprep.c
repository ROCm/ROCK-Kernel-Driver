
/******************************************************************************
 *
 * Module Name: exprep - ACPI AML (p-code) execution - field prep utilities
 *              $Revision: 90 $
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


#include "acpi.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acparser.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exprep")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_decode_field_access_type
 *
 * PARAMETERS:  Access          - Encoded field access bits
 *              Length          - Field length.
 *
 * RETURN:      Field granularity (8, 16, or 32)
 *
 * DESCRIPTION: Decode the Access_type bits of a field definition.
 *
 ******************************************************************************/

static u32
acpi_ex_decode_field_access_type (
	u32                     access,
	u16                     length)
{

	switch (access) {
	case ACCESS_ANY_ACC:

		/* Use the length to set the access type */

		if (length <= 8) {
			return (8);
		}
		else if (length <= 16) {
			return (16);
		}
		else if (length <= 32) {
			return (32);
		}
		else if (length <= 64) {
			return (64);
		}

		/* Default is 8 (byte) */

		return (8);
		break;

	case ACCESS_BYTE_ACC:
		return (8);
		break;

	case ACCESS_WORD_ACC:
		return (16);
		break;

	case ACCESS_DWORD_ACC:
		return (32);
		break;

	case ACCESS_QWORD_ACC:  /* ACPI 2.0 */
		return (64);
		break;

	default:
		/* Invalid field access type */

		return (0);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_prep_common_field_object
 *
 * PARAMETERS:  Obj_desc            - The field object
 *              Field_flags         - Access, Lock_rule, and Update_rule.
 *                                    The format of a Field_flag is described
 *                                    in the ACPI specification
 *              Field_bit_position  - Field start position
 *              Field_bit_length    - Field length in number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the areas of the field object that are common
 *              to the various types of fields.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ex_prep_common_field_object (
	ACPI_OPERAND_OBJECT     *obj_desc,
	u8                      field_flags,
	u32                     field_bit_position,
	u32                     field_bit_length)
{
	u32                     access_bit_width;
	u32                     nearest_byte_address;


	/*
	 * Note: the structure being initialized is the
	 * ACPI_COMMON_FIELD_INFO;  No structure fields outside of the common area
	 * are initialized by this procedure.
	 */

	/* Demultiplex the Field_flags byte */

	obj_desc->common_field.lock_rule = (u8) ((field_flags & LOCK_RULE_MASK)
			 >> LOCK_RULE_SHIFT);
	obj_desc->common_field.update_rule = (u8) ((field_flags & UPDATE_RULE_MASK)
			 >> UPDATE_RULE_SHIFT);
	/* Other misc fields */

	obj_desc->common_field.bit_length = (u16) field_bit_length;

	/* Decode the access type so we can compute offsets */

	access_bit_width = acpi_ex_decode_field_access_type (
			   ((field_flags & ACCESS_TYPE_MASK) >> ACCESS_TYPE_SHIFT),
			   obj_desc->field.bit_length);
	if (!access_bit_width) {
		return (AE_AML_OPERAND_VALUE);
	}

	/* Setup width (access granularity) fields */

	obj_desc->common_field.access_bit_width = (u8) access_bit_width;         /* 8, 16, 32, 64 */
	obj_desc->common_field.access_byte_width = (u8) DIV_8 (access_bit_width); /* 1, 2,  4,  8 */

	if (obj_desc->common.type == ACPI_TYPE_BUFFER_FIELD) {
		/*
		 * Buffer_field access can be on any byte boundary, so the
		 * granularity is always 8
		 */
		access_bit_width = 8;
	}


	/*
	 * Base_byte_offset is the address of the start of the field within the region. It is
	 * the byte address of the first *datum* (field-width data unit) of the field.
	 * (i.e., the first datum that contains at least the first *bit* of the field.)
	 */
	nearest_byte_address                      = ROUND_BITS_DOWN_TO_BYTES (field_bit_position);
	obj_desc->common_field.base_byte_offset   = ROUND_DOWN (nearest_byte_address,
			   DIV_8 (access_bit_width));

	/*
	 * Start_field_bit_offset is the offset of the first bit of the field within a field datum.
	 * This is calculated as the number of bits from the Base_byte_offset. In other words,
	 * the start of the field is relative to a byte address, regardless of the access type
	 * of the field.
	 */
	obj_desc->common_field.start_field_bit_offset = (u8) (MOD_8 (field_bit_position));

	/*
	 * Datum_valid_bits is the number of valid field bits in the first field datum.
	 */
	obj_desc->common_field.datum_valid_bits   = (u8) (access_bit_width -
			   obj_desc->common_field.start_field_bit_offset);

	/*
	 * Valid bits -- the number of bits that compose a partial datum,
	 * 1) At the end of the field within the region (arbitrary starting bit offset)
	 * 2) At the end of a buffer used to contain the field (starting offset always zero)
	 */
	obj_desc->common_field.end_field_valid_bits = (u8) ((obj_desc->common_field.start_field_bit_offset +
			   field_bit_length) % access_bit_width);
	obj_desc->common_field.end_buffer_valid_bits = (u8) (field_bit_length % access_bit_width); /* Start_buffer_bit_offset always = 0 */


	/*
	 * Does the entire field fit within a single field access element
	 * (datum)?  (without crossing a datum boundary)
	 */
	if ((obj_desc->common_field.start_field_bit_offset + obj_desc->common_field.bit_length) <=
		(u16) obj_desc->common_field.access_bit_width) {
		obj_desc->common_field.access_flags |= AFIELD_SINGLE_DATUM;
	}


	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_prep_region_field_value
 *
 * PARAMETERS:  Node                - Owning Node
 *              Region_node         - Region in which field is being defined
 *              Field_flags         - Access, Lock_rule, and Update_rule.
 *              Field_bit_position  - Field start position
 *              Field_bit_length    - Field length in number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Construct an ACPI_OPERAND_OBJECT  of type Def_field and
 *              connect it to the parent Node.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ex_prep_region_field_value (
	ACPI_NAMESPACE_NODE     *node,
	ACPI_HANDLE             region_node,
	u8                      field_flags,
	u32                     field_bit_position,
	u32                     field_bit_length)
{
	ACPI_OPERAND_OBJECT     *obj_desc;
	u32                     type;
	ACPI_STATUS             status;


	/* Parameter validation */

	if (!region_node) {
		return (AE_AML_NO_OPERAND);
	}

	type = acpi_ns_get_type (region_node);
	if (type != ACPI_TYPE_REGION) {
		return (AE_AML_OPERAND_TYPE);
	}

	/* Allocate a new object */

	obj_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_REGION_FIELD);
	if (!obj_desc) {
		return (AE_NO_MEMORY);
	}


	/* Obj_desc and Region valid */

	/* Initialize areas of the object that are common to all fields */

	status = acpi_ex_prep_common_field_object (obj_desc, field_flags,
			  field_bit_position, field_bit_length);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Initialize areas of the object that are specific to this field type */

	obj_desc->field.region_obj = acpi_ns_get_attached_object (region_node);

	/* An additional reference for the container */

	acpi_ut_add_reference (obj_desc->field.region_obj);


	/* Debug info */


	/*
	 * Store the constructed descriptor (Obj_desc) into the parent Node,
	 * preserving the current type of that Named_obj.
	 */
	status = acpi_ns_attach_object (node, obj_desc, (u8) acpi_ns_get_type (node));
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_prep_bank_field_value
 *
 * PARAMETERS:  Node                - Owning Node
 *              Region_node         - Region in which field is being defined
 *              Bank_register_node  - Bank selection register node
 *              Bank_val            - Value to store in selection register
 *              Field_flags         - Access, Lock_rule, and Update_rule
 *              Field_bit_position  - Field start position
 *              Field_bit_length    - Field length in number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Construct an object of type Bank_field and attach it to the
 *              parent Node.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ex_prep_bank_field_value (
	ACPI_NAMESPACE_NODE     *node,
	ACPI_NAMESPACE_NODE     *region_node,
	ACPI_NAMESPACE_NODE     *bank_register_node,
	u32                     bank_val,
	u8                      field_flags,
	u32                     field_bit_position,
	u32                     field_bit_length)
{
	ACPI_OPERAND_OBJECT     *obj_desc;
	u32                     type;
	ACPI_STATUS             status;


	/* Parameter validation */

	if (!region_node) {
		return (AE_AML_NO_OPERAND);
	}

	type = acpi_ns_get_type (region_node);
	if (type != ACPI_TYPE_REGION) {
		return (AE_AML_OPERAND_TYPE);
	}

	/* Allocate a new object */

	obj_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_BANK_FIELD);
	if (!obj_desc) {
		return (AE_NO_MEMORY);
	}

	/*  Obj_desc and Region valid   */

	/* Initialize areas of the object that are common to all fields */

	status = acpi_ex_prep_common_field_object (obj_desc, field_flags,
			  field_bit_position, field_bit_length);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Initialize areas of the object that are specific to this field type */

	obj_desc->bank_field.value         = bank_val;
	obj_desc->bank_field.region_obj    = acpi_ns_get_attached_object (region_node);
	obj_desc->bank_field.bank_register_obj = acpi_ns_get_attached_object (bank_register_node);

	/* An additional reference for the attached objects */

	acpi_ut_add_reference (obj_desc->bank_field.region_obj);
	acpi_ut_add_reference (obj_desc->bank_field.bank_register_obj);

	/* Debug info */


	/*
	 * Store the constructed descriptor (Obj_desc) into the parent Node,
	 * preserving the current type of that Named_obj.
	 */
	status = acpi_ns_attach_object (node, obj_desc, (u8) acpi_ns_get_type (node));
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_prep_index_field_value
 *
 * PARAMETERS:  Node                - Owning Node
 *              Index_reg           - Index register
 *              Data_reg            - Data register
 *              Field_flags         - Access, Lock_rule, and Update_rule
 *              Field_bit_position  - Field start position
 *              Field_bit_length    - Field length in number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Construct an ACPI_OPERAND_OBJECT  of type Index_field and
 *              connect it to the parent Node.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_ex_prep_index_field_value (
	ACPI_NAMESPACE_NODE     *node,
	ACPI_NAMESPACE_NODE     *index_reg,
	ACPI_NAMESPACE_NODE     *data_reg,
	u8                      field_flags,
	u32                     field_bit_position,
	u32                     field_bit_length)
{
	ACPI_OPERAND_OBJECT     *obj_desc;
	ACPI_STATUS             status;


	/* Parameter validation */

	if (!index_reg || !data_reg) {
		return (AE_AML_NO_OPERAND);
	}

	/* Allocate a new object descriptor */

	obj_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_INDEX_FIELD);
	if (!obj_desc) {
		return (AE_NO_MEMORY);
	}

	/* Initialize areas of the object that are common to all fields */

	status = acpi_ex_prep_common_field_object (obj_desc, field_flags,
			  field_bit_position, field_bit_length);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Initialize areas of the object that are specific to this field type */

	obj_desc->index_field.data_obj = acpi_ns_get_attached_object (data_reg);
	obj_desc->index_field.index_obj = acpi_ns_get_attached_object (index_reg);
	obj_desc->index_field.value  = (u32) (field_bit_position /
			  obj_desc->field.access_bit_width);

	/* An additional reference for the attached objects */

	acpi_ut_add_reference (obj_desc->index_field.data_obj);
	acpi_ut_add_reference (obj_desc->index_field.index_obj);

	/* Debug info */


	/*
	 * Store the constructed descriptor (Obj_desc) into the parent Node,
	 * preserving the current type of that Named_obj.
	 */
	status = acpi_ns_attach_object (node, obj_desc, (u8) acpi_ns_get_type (node));
	return (status);
}

