
/******************************************************************************
 *
 * Module Name: exprep - ACPI AML (p-code) execution - field prep utilities
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2003, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */


#include <acpi/acpi.h>
#include <acpi/acinterp.h>
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exprep")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_decode_field_access
 *
 * PARAMETERS:  Access          - Encoded field access bits
 *              Length          - Field length.
 *
 * RETURN:      Field granularity (8, 16, 32 or 64) and
 *              byte_alignment (1, 2, 3, or 4)
 *
 * DESCRIPTION: Decode the access_type bits of a field definition.
 *
 ******************************************************************************/

static u32
acpi_ex_decode_field_access (
	union acpi_operand_object       *obj_desc,
	u8                              field_flags,
	u32                             *return_byte_alignment)
{
	u32                             access;
	u8                              byte_alignment;
	u8                              bit_length;
/*    u32                             Length; */


	ACPI_FUNCTION_NAME ("ex_decode_field_access");


	access = (field_flags & AML_FIELD_ACCESS_TYPE_MASK);

	switch (access) {
	case AML_FIELD_ACCESS_ANY:

		byte_alignment = 1;
		bit_length = 8;

#if 0
		/*
		 * TBD: optimize
		 *
		 * Any attempt to optimize the access size to the size of the field
		 * must take into consideration the length of the region and take
		 * care that an access to the field will not attempt to access
		 * beyond the end of the region.
		 */

		/* Use the length to set the access type */

		length = obj_desc->common_field.bit_length;

		if (length <= 8) {
			bit_length = 8;
		}
		else if (length <= 16) {
			bit_length = 16;
		}
		else if (length <= 32) {
			bit_length = 32;
		}
		else if (length <= 64) {
			bit_length = 64;
		}
		else {
			/* Larger than Qword - just use byte-size chunks */

			bit_length = 8;
		}
#endif
		break;

	case AML_FIELD_ACCESS_BYTE:
	case AML_FIELD_ACCESS_BUFFER:   /* ACPI 2.0 (SMBus Buffer) */
		byte_alignment = 1;
		bit_length    = 8;
		break;

	case AML_FIELD_ACCESS_WORD:
		byte_alignment = 2;
		bit_length    = 16;
		break;

	case AML_FIELD_ACCESS_DWORD:
		byte_alignment = 4;
		bit_length    = 32;
		break;

	case AML_FIELD_ACCESS_QWORD:    /* ACPI 2.0 */
		byte_alignment = 8;
		bit_length    = 64;
		break;

	default:
		/* Invalid field access type */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Unknown field access type %X\n",
			access));
		return (0);
	}

	if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_BUFFER_FIELD) {
		/*
		 * buffer_field access can be on any byte boundary, so the
		 * byte_alignment is always 1 byte -- regardless of any byte_alignment
		 * implied by the field access type.
		 */
		byte_alignment = 1;
	}

	*return_byte_alignment = byte_alignment;
	return (bit_length);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_prep_common_field_object
 *
 * PARAMETERS:  obj_desc            - The field object
 *              field_flags         - Access, lock_rule, and update_rule.
 *                                    The format of a field_flag is described
 *                                    in the ACPI specification
 *              field_bit_position  - Field start position
 *              field_bit_length    - Field length in number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the areas of the field object that are common
 *              to the various types of fields.  Note: This is very "sensitive"
 *              code because we are solving the general case for field
 *              alignment.
 *
 ******************************************************************************/

acpi_status
acpi_ex_prep_common_field_object (
	union acpi_operand_object       *obj_desc,
	u8                              field_flags,
	u8                              field_attribute,
	u32                             field_bit_position,
	u32                             field_bit_length)
{
	u32                             access_bit_width;
	u32                             byte_alignment;
	u32                             nearest_byte_address;


	ACPI_FUNCTION_TRACE ("ex_prep_common_field_object");


	/*
	 * Note: the structure being initialized is the
	 * ACPI_COMMON_FIELD_INFO;  No structure fields outside of the common
	 * area are initialized by this procedure.
	 */
	obj_desc->common_field.field_flags = field_flags;
	obj_desc->common_field.attribute = field_attribute;
	obj_desc->common_field.bit_length = field_bit_length;

	/*
	 * Decode the access type so we can compute offsets.  The access type gives
	 * two pieces of information - the width of each field access and the
	 * necessary byte_alignment (address granularity) of the access.
	 *
	 * For any_acc, the access_bit_width is the largest width that is both
	 * necessary and possible in an attempt to access the whole field in one
	 * I/O operation.  However, for any_acc, the byte_alignment is always one
	 * byte.
	 *
	 * For all Buffer Fields, the byte_alignment is always one byte.
	 *
	 * For all other access types (Byte, Word, Dword, Qword), the Bitwidth is
	 * the same (equivalent) as the byte_alignment.
	 */
	access_bit_width = acpi_ex_decode_field_access (obj_desc, field_flags,
			  &byte_alignment);
	if (!access_bit_width) {
		return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
	}

	/* Setup width (access granularity) fields */

	obj_desc->common_field.access_byte_width = (u8)
			ACPI_DIV_8 (access_bit_width); /* 1, 2, 4,  8 */

	/*
	 * base_byte_offset is the address of the start of the field within the
	 * region.  It is the byte address of the first *datum* (field-width data
	 * unit) of the field. (i.e., the first datum that contains at least the
	 * first *bit* of the field.)
	 *
	 * Note: byte_alignment is always either equal to the access_bit_width or 8
	 * (Byte access), and it defines the addressing granularity of the parent
	 * region or buffer.
	 */
	nearest_byte_address =
			ACPI_ROUND_BITS_DOWN_TO_BYTES (field_bit_position);
	obj_desc->common_field.base_byte_offset =
			ACPI_ROUND_DOWN (nearest_byte_address, byte_alignment);

	/*
	 * start_field_bit_offset is the offset of the first bit of the field within
	 * a field datum.
	 */
	obj_desc->common_field.start_field_bit_offset = (u8)
		(field_bit_position - ACPI_MUL_8 (obj_desc->common_field.base_byte_offset));

	/*
	 * Valid bits -- the number of bits that compose a partial datum,
	 * 1) At the end of the field within the region (arbitrary starting bit
	 *    offset)
	 * 2) At the end of a buffer used to contain the field (starting offset
	 *    always zero)
	 */
	obj_desc->common_field.end_field_valid_bits = (u8)
		((obj_desc->common_field.start_field_bit_offset + field_bit_length) %
				  access_bit_width);
	/* start_buffer_bit_offset always = 0 */

	obj_desc->common_field.end_buffer_valid_bits = (u8)
		(field_bit_length % access_bit_width);

	/*
	 * datum_valid_bits is the number of valid field bits in the first
	 * field datum.
	 */
	obj_desc->common_field.datum_valid_bits  = (u8)
		(access_bit_width - obj_desc->common_field.start_field_bit_offset);

	/*
	 * Does the entire field fit within a single field access element? (datum)
	 * (i.e., without crossing a datum boundary)
	 */
	if ((obj_desc->common_field.start_field_bit_offset + field_bit_length) <=
			(u16) access_bit_width) {
		obj_desc->common.flags |= AOPOBJ_SINGLE_DATUM;
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_prep_field_value
 *
 * PARAMETERS:  Node                - Owning Node
 *              region_node         - Region in which field is being defined
 *              field_flags         - Access, lock_rule, and update_rule.
 *              field_bit_position  - Field start position
 *              field_bit_length    - Field length in number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Construct an union acpi_operand_object of type def_field and
 *              connect it to the parent Node.
 *
 ******************************************************************************/

acpi_status
acpi_ex_prep_field_value (
	struct acpi_create_field_info   *info)
{
	union acpi_operand_object       *obj_desc;
	u32                             type;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ex_prep_field_value");


	/* Parameter validation */

	if (info->field_type != ACPI_TYPE_LOCAL_INDEX_FIELD) {
		if (!info->region_node) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null region_node\n"));
			return_ACPI_STATUS (AE_AML_NO_OPERAND);
		}

		type = acpi_ns_get_type (info->region_node);
		if (type != ACPI_TYPE_REGION) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Needed Region, found type %X %s\n",
				type, acpi_ut_get_type_name (type)));

			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}
	}

	/* Allocate a new field object */

	obj_desc = acpi_ut_create_internal_object (info->field_type);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Initialize areas of the object that are common to all fields */

	obj_desc->common_field.node = info->field_node;
	status = acpi_ex_prep_common_field_object (obj_desc, info->field_flags,
			 info->attribute, info->field_bit_position, info->field_bit_length);
	if (ACPI_FAILURE (status)) {
		acpi_ut_delete_object_desc (obj_desc);
		return_ACPI_STATUS (status);
	}

	/* Initialize areas of the object that are specific to the field type */

	switch (info->field_type) {
	case ACPI_TYPE_LOCAL_REGION_FIELD:

		obj_desc->field.region_obj   = acpi_ns_get_attached_object (info->region_node);

		/* An additional reference for the container */

		acpi_ut_add_reference (obj_desc->field.region_obj);

		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
			"region_field: Bitoff=%X Off=%X Gran=%X Region %p\n",
			obj_desc->field.start_field_bit_offset, obj_desc->field.base_byte_offset,
			obj_desc->field.access_byte_width, obj_desc->field.region_obj));
		break;


	case ACPI_TYPE_LOCAL_BANK_FIELD:

		obj_desc->bank_field.value   = info->bank_value;
		obj_desc->bank_field.region_obj = acpi_ns_get_attached_object (info->region_node);
		obj_desc->bank_field.bank_obj = acpi_ns_get_attached_object (info->register_node);

		/* An additional reference for the attached objects */

		acpi_ut_add_reference (obj_desc->bank_field.region_obj);
		acpi_ut_add_reference (obj_desc->bank_field.bank_obj);

		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
			"Bank Field: bit_off=%X Off=%X Gran=%X Region %p bank_reg %p\n",
			obj_desc->bank_field.start_field_bit_offset,
			obj_desc->bank_field.base_byte_offset,
			obj_desc->field.access_byte_width,
			obj_desc->bank_field.region_obj,
			obj_desc->bank_field.bank_obj));
		break;


	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		obj_desc->index_field.index_obj = acpi_ns_get_attached_object (info->register_node);
		obj_desc->index_field.data_obj = acpi_ns_get_attached_object (info->data_register_node);
		obj_desc->index_field.value  = (u32)
			(info->field_bit_position / ACPI_MUL_8 (obj_desc->field.access_byte_width));

		if (!obj_desc->index_field.data_obj || !obj_desc->index_field.index_obj) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null Index Object\n"));
			return_ACPI_STATUS (AE_AML_INTERNAL);
		}

		/* An additional reference for the attached objects */

		acpi_ut_add_reference (obj_desc->index_field.data_obj);
		acpi_ut_add_reference (obj_desc->index_field.index_obj);

		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
			"index_field: bitoff=%X off=%X gran=%X Index %p Data %p\n",
			obj_desc->index_field.start_field_bit_offset,
			obj_desc->index_field.base_byte_offset,
			obj_desc->field.access_byte_width,
			obj_desc->index_field.index_obj,
			obj_desc->index_field.data_obj));
		break;

	default:
		/* No other types should get here */
		break;
	}

	/*
	 * Store the constructed descriptor (obj_desc) into the parent Node,
	 * preserving the current type of that named_obj.
	 */
	status = acpi_ns_attach_object (info->field_node, obj_desc,
			  acpi_ns_get_type (info->field_node));

	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "set named_obj %p (%4.4s) val = %p\n",
			info->field_node, info->field_node->name.ascii, obj_desc));

	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}

