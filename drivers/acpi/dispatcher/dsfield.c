/******************************************************************************
 *
 * Module Name: dsfield - Dispatcher field routines
 *              $Revision: 68 $
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


#include "acpi.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "acparser.h"


#define _COMPONENT          ACPI_DISPATCHER
	 ACPI_MODULE_NAME    ("dsfield")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_buffer_field
 *
 * PARAMETERS:  Opcode              - The opcode to be executed
 *              Operands            - List of operands for the opcode
 *              Walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute the Create_field operators:
 *              Create_bit_field_op,
 *              Create_byte_field_op,
 *              Create_word_field_op,
 *              Create_dWord_field_op,
 *              Create_qWord_field_op,
 *              Create_field_op     (all of which define fields in buffers)
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_buffer_field (
	acpi_parse_object       *op,
	acpi_walk_state         *walk_state)
{
	acpi_parse_object       *arg;
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *second_desc = NULL;
	u32                     flags;


	ACPI_FUNCTION_TRACE ("Ds_create_buffer_field");


	/* Get the Name_string argument */

	if (op->common.aml_opcode == AML_CREATE_FIELD_OP) {
		arg = acpi_ps_get_arg (op, 3);
	}
	else {
		/* Create Bit/Byte/Word/Dword field */

		arg = acpi_ps_get_arg (op, 2);
	}

	if (!arg) {
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	/*
	 * During the load phase, we want to enter the name of the field into
	 * the namespace.  During the execute phase (when we evaluate the size
	 * operand), we want to lookup the name
	 */
	if (walk_state->parse_flags & ACPI_PARSE_EXECUTE) {
		flags = ACPI_NS_NO_UPSEARCH | ACPI_NS_DONT_OPEN_SCOPE;
	}
	else {
		flags = ACPI_NS_NO_UPSEARCH | ACPI_NS_DONT_OPEN_SCOPE | ACPI_NS_ERROR_IF_FOUND;
	}

	/*
	 * Enter the Name_string into the namespace
	 */
	status = acpi_ns_lookup (walk_state->scope_info, arg->common.value.string,
			 INTERNAL_TYPE_DEF_ANY, ACPI_IMODE_LOAD_PASS1,
			 flags, walk_state, &(node));
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_NSERROR (arg->common.value.string, status);
		return_ACPI_STATUS (status);
	}

	/* We could put the returned object (Node) on the object stack for later, but
	 * for now, we will put it in the "op" object that the parser uses, so we
	 * can get it again at the end of this scope
	 */
	op->common.node = node;

	/*
	 * If there is no object attached to the node, this node was just created and
	 * we need to create the field object.  Otherwise, this was a lookup of an
	 * existing node and we don't want to create the field object again.
	 */
	obj_desc = acpi_ns_get_attached_object (node);
	if (obj_desc) {
		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * The Field definition is not fully parsed at this time.
	 * (We must save the address of the AML for the buffer and index operands)
	 */

	/* Create the buffer field object */

	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER_FIELD);
	if (!obj_desc) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/*
	 * Remember location in AML stream of the field unit
	 * opcode and operands -- since the buffer and index
	 * operands must be evaluated.
	 */
	second_desc                 = obj_desc->common.next_object;
	second_desc->extra.aml_start = op->named.data;
	second_desc->extra.aml_length = op->named.length;
	obj_desc->buffer_field.node = node;

	/* Attach constructed field descriptors to parent node */

	status = acpi_ns_attach_object (node, obj_desc, ACPI_TYPE_BUFFER_FIELD);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}


cleanup:

	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_get_field_names
 *
 * PARAMETERS:  Info            - Create_field info structure
 *  `           Walk_state      - Current method state
 *              Arg             - First parser arg for the field name list
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Process all named fields in a field declaration.  Names are
 *              entered into the namespace.
 *
 ******************************************************************************/

acpi_status
acpi_ds_get_field_names (
	ACPI_CREATE_FIELD_INFO  *info,
	acpi_walk_state         *walk_state,
	acpi_parse_object       *arg)
{
	acpi_status             status;
	acpi_integer            position;


	ACPI_FUNCTION_TRACE_PTR ("Ds_get_field_names", info);


	/* First field starts at bit zero */

	info->field_bit_position = 0;

	/* Process all elements in the field list (of parse nodes) */

	while (arg) {
		/*
		 * Three types of field elements are handled:
		 * 1) Offset - specifies a bit offset
		 * 2) Access_as - changes the access mode
		 * 3) Name - Enters a new named field into the namespace
		 */
		switch (arg->common.aml_opcode) {
		case AML_INT_RESERVEDFIELD_OP:

			position = (acpi_integer) info->field_bit_position
					 + (acpi_integer) arg->common.value.size;

			if (position > ACPI_UINT32_MAX) {
				ACPI_REPORT_ERROR (("Bit offset within field too large (> 0xFFFFFFFF)\n"));
				return_ACPI_STATUS (AE_SUPPORT);
			}

			info->field_bit_position = (u32) position;
			break;


		case AML_INT_ACCESSFIELD_OP:

			/*
			 * Get a new Access_type and Access_attribute -- to be used for all
			 * field units that follow, until field end or another Access_as keyword.
			 *
			 * In Field_flags, preserve the flag bits other than the ACCESS_TYPE bits
			 */
			info->field_flags = (u8) ((info->field_flags & ~(AML_FIELD_ACCESS_TYPE_MASK)) |
					  ((u8) (arg->common.value.integer32 >> 8)));

			info->attribute = (u8) (arg->common.value.integer32);
			break;


		case AML_INT_NAMEDFIELD_OP:

			/* Lookup the name */

			status = acpi_ns_lookup (walk_state->scope_info,
					  (NATIVE_CHAR *) &arg->named.name,
					  info->field_type, ACPI_IMODE_EXECUTE, ACPI_NS_DONT_OPEN_SCOPE,
					  walk_state, &info->field_node);
			if (ACPI_FAILURE (status)) {
				ACPI_REPORT_NSERROR ((char *) &arg->named.name, status);
				if (status != AE_ALREADY_EXISTS) {
					return_ACPI_STATUS (status);
				}

				/* Already exists, ignore error */
			}
			else {
				arg->common.node = info->field_node;
				info->field_bit_length = arg->common.value.size;

				/* Create and initialize an object for the new Field Node */

				status = acpi_ex_prep_field_value (info);
				if (ACPI_FAILURE (status)) {
					return_ACPI_STATUS (status);
				}
			}

			/* Keep track of bit position for the next field */

			position = (acpi_integer) info->field_bit_position
					 + (acpi_integer) arg->common.value.size;

			if (position > ACPI_UINT32_MAX) {
				ACPI_REPORT_ERROR (("Field [%4.4s] bit offset too large (> 0xFFFFFFFF)\n",
						(char *) &info->field_node->name));
				return_ACPI_STATUS (AE_SUPPORT);
			}

			info->field_bit_position += info->field_bit_length;
			break;


		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid opcode in field list: %X\n",
				arg->common.aml_opcode));
			return_ACPI_STATUS (AE_AML_BAD_OPCODE);
		}

		arg = arg->common.next;
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_field
 *
 * PARAMETERS:  Op              - Op containing the Field definition and args
 *              Region_node     - Object for the containing Operation Region
 *  `           Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new field in the specified operation region
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_field (
	acpi_parse_object       *op,
	acpi_namespace_node     *region_node,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;
	acpi_parse_object       *arg;
	ACPI_CREATE_FIELD_INFO  info;


	ACPI_FUNCTION_TRACE_PTR ("Ds_create_field", op);


	/* First arg is the name of the parent Op_region (must already exist) */

	arg = op->common.value.arg;
	if (!region_node) {
		status = acpi_ns_lookup (walk_state->scope_info, arg->common.value.name,
				  ACPI_TYPE_REGION, ACPI_IMODE_EXECUTE,
				  ACPI_NS_SEARCH_PARENT, walk_state, &region_node);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_NSERROR (arg->common.value.name, status);
			return_ACPI_STATUS (status);
		}
	}

	/* Second arg is the field flags */

	arg = arg->common.next;
	info.field_flags = arg->common.value.integer8;
	info.attribute = 0;

	/* Each remaining arg is a Named Field */

	info.field_type = INTERNAL_TYPE_REGION_FIELD;
	info.region_node = region_node;

	status = acpi_ds_get_field_names (&info, walk_state, arg->common.next);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_init_field_objects
 *
 * PARAMETERS:  Op              - Op containing the Field definition and args
 *  `           Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: For each "Field Unit" name in the argument list that is
 *              part of the field declaration, enter the name into the
 *              namespace.
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_field_objects (
	acpi_parse_object       *op,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;
	acpi_parse_object       *arg = NULL;
	acpi_namespace_node     *node;
	u8                      type = 0;


	ACPI_FUNCTION_TRACE_PTR ("Ds_init_field_objects", op);


	switch (walk_state->opcode) {
	case AML_FIELD_OP:
		arg = acpi_ps_get_arg (op, 2);
		type = INTERNAL_TYPE_REGION_FIELD;
		break;

	case AML_BANK_FIELD_OP:
		arg = acpi_ps_get_arg (op, 4);
		type = INTERNAL_TYPE_BANK_FIELD;
		break;

	case AML_INDEX_FIELD_OP:
		arg = acpi_ps_get_arg (op, 3);
		type = INTERNAL_TYPE_INDEX_FIELD;
		break;

	default:
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Walk the list of entries in the Field_list
	 */
	while (arg) {
		/* Ignore OFFSET and ACCESSAS terms here */

		if (arg->common.aml_opcode == AML_INT_NAMEDFIELD_OP) {
			status = acpi_ns_lookup (walk_state->scope_info,
					  (NATIVE_CHAR *) &arg->named.name,
					  type, ACPI_IMODE_LOAD_PASS1,
					  ACPI_NS_NO_UPSEARCH | ACPI_NS_DONT_OPEN_SCOPE | ACPI_NS_ERROR_IF_FOUND,
					  walk_state, &node);
			if (ACPI_FAILURE (status)) {
				ACPI_REPORT_NSERROR ((char *) &arg->named.name, status);
				if (status != AE_ALREADY_EXISTS) {
					return_ACPI_STATUS (status);
				}

				/* Name already exists, just ignore this error */

				status = AE_OK;
			}

			arg->common.node = node;
		}

		/* Move to next field in the list */

		arg = arg->common.next;
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_bank_field
 *
 * PARAMETERS:  Op              - Op containing the Field definition and args
 *              Region_node     - Object for the containing Operation Region
 *  `           Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new bank field in the specified operation region
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_bank_field (
	acpi_parse_object       *op,
	acpi_namespace_node     *region_node,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;
	acpi_parse_object       *arg;
	ACPI_CREATE_FIELD_INFO  info;


	ACPI_FUNCTION_TRACE_PTR ("Ds_create_bank_field", op);


	/* First arg is the name of the parent Op_region (must already exist) */

	arg = op->common.value.arg;
	if (!region_node) {
		status = acpi_ns_lookup (walk_state->scope_info, arg->common.value.name,
				  ACPI_TYPE_REGION, ACPI_IMODE_EXECUTE,
				  ACPI_NS_SEARCH_PARENT, walk_state, &region_node);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_NSERROR (arg->common.value.name, status);
			return_ACPI_STATUS (status);
		}
	}

	/* Second arg is the Bank Register (must already exist) */

	arg = arg->common.next;
	status = acpi_ns_lookup (walk_state->scope_info, arg->common.value.string,
			  INTERNAL_TYPE_BANK_FIELD_DEFN, ACPI_IMODE_EXECUTE,
			  ACPI_NS_SEARCH_PARENT, walk_state, &info.register_node);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_NSERROR (arg->common.value.string, status);
		return_ACPI_STATUS (status);
	}

	/* Third arg is the Bank_value */

	arg = arg->common.next;
	info.bank_value = arg->common.value.integer32;

	/* Fourth arg is the field flags */

	arg = arg->common.next;
	info.field_flags = arg->common.value.integer8;

	/* Each remaining arg is a Named Field */

	info.field_type = INTERNAL_TYPE_BANK_FIELD;
	info.region_node = region_node;

	status = acpi_ds_get_field_names (&info, walk_state, arg->common.next);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_index_field
 *
 * PARAMETERS:  Op              - Op containing the Field definition and args
 *              Region_node     - Object for the containing Operation Region
 *  `           Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new index field in the specified operation region
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_index_field (
	acpi_parse_object       *op,
	acpi_namespace_node     *region_node,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;
	acpi_parse_object       *arg;
	ACPI_CREATE_FIELD_INFO  info;


	ACPI_FUNCTION_TRACE_PTR ("Ds_create_index_field", op);


	/* First arg is the name of the Index register (must already exist) */

	arg = op->common.value.arg;
	status = acpi_ns_lookup (walk_state->scope_info, arg->common.value.string,
			  ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
			  ACPI_NS_SEARCH_PARENT, walk_state, &info.register_node);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_NSERROR (arg->common.value.string, status);
		return_ACPI_STATUS (status);
	}

	/* Second arg is the data register (must already exist) */

	arg = arg->common.next;
	status = acpi_ns_lookup (walk_state->scope_info, arg->common.value.string,
			  INTERNAL_TYPE_INDEX_FIELD_DEFN, ACPI_IMODE_EXECUTE,
			  ACPI_NS_SEARCH_PARENT, walk_state, &info.data_register_node);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_NSERROR (arg->common.value.string, status);
		return_ACPI_STATUS (status);
	}

	/* Next arg is the field flags */

	arg = arg->common.next;
	info.field_flags = arg->common.value.integer8;

	/* Each remaining arg is a Named Field */

	info.field_type = INTERNAL_TYPE_INDEX_FIELD;
	info.region_node = region_node;

	status = acpi_ds_get_field_names (&info, walk_state, arg->common.next);

	return_ACPI_STATUS (status);
}


