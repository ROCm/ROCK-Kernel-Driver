
/******************************************************************************
 *
 * Module Name: exprep - ACPI AML (p-code) execution - field prep utilities
 *              $Revision: 99 $
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
 * RETURN:      Field granularity (8, 16, 32 or 64)
 *
 * DESCRIPTION: Decode the Access_type bits of a field definition.
 *
 ******************************************************************************/

static u32
acpi_ex_decode_field_access_type (
	u32                     access,
	u16                     length,
	u32                     *alignment)
{
	PROC_NAME ("Ex_decode_field_access_type");


	switch (access) {
	case ACCESS_ANY_ACC:

		*alignment = 8;

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
		*alignment = 8;
		return (8);
		break;

	case ACCESS_WORD_ACC:
		*alignment = 16;
		return (16);
		break;

	case ACCESS_DWORD_ACC:
		*alignment = 32;
		return (32);
		break;

	case ACCESS_QWORD_ACC:  /* ACPI 2.0 */
		*alignment = 64;
		return (64);
		break;

	default:
		/* Invalid field access type */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Unknown field access type %x\n",
			access));
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

acpi_status
acpi_ex_prep_common_field_object (
	acpi_operand_object     *obj_desc,
	u8                      field_flags,
	u32                     field_bit_position,
	u32                     field_bit_length)
{
	u32                     access_bit_width;
	u32                     alignment;
	u32                     nearest_byte_address;


	FUNCTION_TRACE ("Ex_prep_common_field_object");


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

	/*
	 * Decode the access type so we can compute offsets.  The access type gives
	 * two pieces of information - the width of each field access and the
	 * necessary alignment of the access.  For Any_acc, the width used is the
	 * largest necessary/possible in an attempt to access the whole field in one
	 * I/O operation.  However, for Any_acc, the alignment is 8. For all other
	 * access types (Byte, Word, Dword, Qword), the width is the same as the
	 * alignment.
	 */
	access_bit_width = acpi_ex_decode_field_access_type (
			   ((field_flags & ACCESS_TYPE_MASK) >> ACCESS_TYPE_SHIFT),
			   obj_desc->field.bit_length, &alignment);
	if (!access_bit_width) {
		return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
	}

	/* Setup width (access granularity) fields */

	obj_desc->common_field.access_bit_width = (u8) access_bit_width;         /* 8, 16, 32, 64 */
	obj_desc->common_field.access_byte_width = (u8) DIV_8 (access_bit_width); /* 1, 2,  4,  8 */

	if (obj_desc->common.type == ACPI_TYPE_BUFFER_FIELD) {
		/*
		 * Buffer_field access can be on any byte boundary, so the
		 * alignment is always 8 (regardless of any alignment implied by the
		 * field access type.)
		 */
		alignment = 8;
	}


	/*
	 * Base_byte_offset is the address of the start of the field within the region. It is
	 * the byte address of the first *datum* (field-width data unit) of the field.
	 * (i.e., the first datum that contains at least the first *bit* of the field.)
	 */
	nearest_byte_address                      = ROUND_BITS_DOWN_TO_BYTES (field_bit_position);
	obj_desc->common_field.base_byte_offset   = ROUND_DOWN (nearest_byte_address,
			   DIV_8 (alignment));

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


	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_prep_field_value
 *
 * PARAMETERS:  Node                - Owning Node
 *              Region_node         - Region in which field is being defined
 *              Field_flags         - Access, Lock_rule, and Update_rule.
 *              Field_bit_position  - Field start position
 *              Field_bit_length    - Field length in number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Construct an acpi_operand_object of type Def_field and
 *              connect it to the parent Node.
 *
 ******************************************************************************/

acpi_status
acpi_ex_prep_field_value (
	ACPI_CREATE_FIELD_INFO  *info)
{
	acpi_operand_object     *obj_desc;
	u32                     type;
	acpi_status             status;


	FUNCTION_TRACE ("Ex_prep_field_value");


	/* Parameter validation */

	if (info->field_type != INTERNAL_TYPE_INDEX_FIELD) {
		if (!info->region_node) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null Region_node\n"));
			return_ACPI_STATUS (AE_AML_NO_OPERAND);
		}

		type = acpi_ns_get_type (info->region_node);
		if (type != ACPI_TYPE_REGION) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Needed Region, found type %X %s\n",
				type, acpi_ut_get_type_name (type)));

			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}
	}

	/* Allocate a new region object */

	obj_desc = acpi_ut_create_internal_object (info->field_type);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Initialize areas of the object that are common to all fields */

	status = acpi_ex_prep_common_field_object (obj_desc, info->field_flags,
			  info->field_bit_position, info->field_bit_length);
	if (ACPI_FAILURE (status)) {
		acpi_ut_delete_object_desc (obj_desc);
		return_ACPI_STATUS (status);
	}

	/* Initialize areas of the object that are specific to the field type */

	switch (info->field_type) {
	case INTERNAL_TYPE_REGION_FIELD:

		obj_desc->field.region_obj  = acpi_ns_get_attached_object (info->region_node);

		/* An additional reference for the container */

		acpi_ut_add_reference (obj_desc->field.region_obj);

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Region_field: Bitoff=%X Off=%X Gran=%X Region %p\n",
			obj_desc->field.start_field_bit_offset, obj_desc->field.base_byte_offset,
			obj_desc->field.access_bit_width, obj_desc->field.region_obj));
		break;


	case INTERNAL_TYPE_BANK_FIELD:

		obj_desc->bank_field.value         = info->bank_value;
		obj_desc->bank_field.region_obj    = acpi_ns_get_attached_object (info->region_node);
		obj_desc->bank_field.bank_register_obj = acpi_ns_get_attached_object (info->register_node);

		/* An additional reference for the attached objects */

		acpi_ut_add_reference (obj_desc->bank_field.region_obj);
		acpi_ut_add_reference (obj_desc->bank_field.bank_register_obj);

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Bank Field: Bit_off=%X Off=%X Gran=%X Region %p Bank_reg %p\n",
			obj_desc->bank_field.start_field_bit_offset, obj_desc->bank_field.base_byte_offset,
			obj_desc->field.access_bit_width, obj_desc->bank_field.region_obj,
			obj_desc->bank_field.bank_register_obj));
		break;


	case INTERNAL_TYPE_INDEX_FIELD:

		obj_desc->index_field.index_obj = acpi_ns_get_attached_object (info->register_node);
		obj_desc->index_field.data_obj = acpi_ns_get_attached_object (info->data_register_node);
		obj_desc->index_field.value  = (u32) (info->field_bit_position /
				  obj_desc->field.access_bit_width);

		if (!obj_desc->index_field.data_obj || !obj_desc->index_field.index_obj) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null Index Object\n"));
			return_ACPI_STATUS (AE_AML_INTERNAL);
		}

		/* An additional reference for the attached objects */

		acpi_ut_add_reference (obj_desc->index_field.data_obj);
		acpi_ut_add_reference (obj_desc->index_field.index_obj);

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Index_field: bitoff=%X off=%X gran=%X Index %p Data %p\n",
			obj_desc->index_field.start_field_bit_offset, obj_desc->index_field.base_byte_offset,
			obj_desc->field.access_bit_width, obj_desc->index_field.index_obj,
			obj_desc->index_field.data_obj));
		break;
	}

	/*
	 * Store the constructed descriptor (Obj_desc) into the parent Node,
	 * preserving the current type of that Named_obj.
	 */
	status = acpi_ns_attach_object (info->field_node, obj_desc,
			  (u8) acpi_ns_get_type (info->field_node));

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "set Named_obj %p (%4.4s) val = %p\n",
		info->field_node, (char*)&(info->field_node->name), obj_desc));

	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}

