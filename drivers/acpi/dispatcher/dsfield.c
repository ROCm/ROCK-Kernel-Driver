/******************************************************************************
 *
 * Module Name: dsfield - Dispatcher field routines
 *              $Revision: 46 $
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
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "acparser.h"


#define _COMPONENT          ACPI_DISPATCHER
	 MODULE_NAME         ("dsfield")


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


	FUNCTION_TRACE ("Ds_create_buffer_field");


	/* Get the Name_string argument */

	if (op->opcode == AML_CREATE_FIELD_OP) {
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
	 * Enter the Name_string into the namespace
	 */
	status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
			 INTERNAL_TYPE_DEF_ANY, IMODE_LOAD_PASS1,
			 NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
			 walk_state, &(node));
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* We could put the returned object (Node) on the object stack for later, but
	 * for now, we will put it in the "op" object that the parser uses, so we
	 * can get it again at the end of this scope
	 */
	op->node = node;

	/*
	 * If there is no object attached to the node, this node was just created and
	 * we need to create the field object.  Otherwise, this was a lookup of an
	 * existing node and we don't want to create the field object again.
	 */
	if (node->object) {
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
	 * Allocate a method object for this field unit
	 */
	obj_desc->buffer_field.extra = acpi_ut_create_internal_object (
			   INTERNAL_TYPE_EXTRA);
	if (!obj_desc->buffer_field.extra) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/*
	 * Remember location in AML stream of the field unit
	 * opcode and operands -- since the buffer and index
	 * operands must be evaluated.
	 */
	obj_desc->buffer_field.extra->extra.aml_start = ((acpi_parse2_object *) op)->data;
	obj_desc->buffer_field.extra->extra.aml_length = ((acpi_parse2_object *) op)->length;
	obj_desc->buffer_field.node = node;

	/* Attach constructed field descriptor to parent node */

	status = acpi_ns_attach_object (node, obj_desc, ACPI_TYPE_BUFFER_FIELD);


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


	FUNCTION_TRACE_U32 ("Ds_get_field_names", info);


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
		switch (arg->opcode) {
		case AML_INT_RESERVEDFIELD_OP:

			info->field_bit_position += arg->value.size;
			break;


		case AML_INT_ACCESSFIELD_OP:

			/*
			 * Get a new Access_type and Access_attribute for all
			 * entries (until end or another Access_as keyword)
			 */
			info->field_flags = (u8) ((info->field_flags & FIELD_ACCESS_TYPE_MASK) ||
					  ((u8) (arg->value.integer >> 8)));
			break;


		case AML_INT_NAMEDFIELD_OP:

			/* Enter a new field name into the namespace */

			status = acpi_ns_lookup (walk_state->scope_info,
					  (NATIVE_CHAR *) &((acpi_parse2_object *)arg)->name,
					  info->field_type, IMODE_LOAD_PASS1,
					  NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
					  NULL, &info->field_node);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/* Create and initialize an object for the new Field Node */

			info->field_bit_length = arg->value.size;

			status = acpi_ex_prep_field_value (info);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/* Keep track of bit position for the next field */

			info->field_bit_position += info->field_bit_length;
			break;


		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid opcode in field list: %X\n",
				arg->opcode));
			return_ACPI_STATUS (AE_AML_ERROR);
			break;
		}

		arg = arg->next;
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
	acpi_status             status = AE_AML_ERROR;
	acpi_parse_object       *arg;
	ACPI_CREATE_FIELD_INFO  info;


	FUNCTION_TRACE_PTR ("Ds_create_field", op);


	/* First arg is the name of the parent Op_region (must already exist) */

	arg = op->value.arg;
	if (!region_node) {
		status = acpi_ns_lookup (walk_state->scope_info, arg->value.name,
				  ACPI_TYPE_REGION, IMODE_EXECUTE,
				  NS_SEARCH_PARENT, walk_state, &region_node);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Second arg is the field flags */

	arg = arg->next;
	info.field_flags = arg->value.integer8;

	/* Each remaining arg is a Named Field */

	info.field_type = INTERNAL_TYPE_REGION_FIELD;
	info.region_node = region_node;

	status = acpi_ds_get_field_names (&info, walk_state, arg->next);

	return_ACPI_STATUS (status);
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
	acpi_status             status = AE_AML_ERROR;
	acpi_parse_object       *arg;
	ACPI_CREATE_FIELD_INFO  info;


	FUNCTION_TRACE_PTR ("Ds_create_bank_field", op);


	/* First arg is the name of the parent Op_region (must already exist) */

	arg = op->value.arg;
	if (!region_node) {
		status = acpi_ns_lookup (walk_state->scope_info, arg->value.name,
				  ACPI_TYPE_REGION, IMODE_EXECUTE,
				  NS_SEARCH_PARENT, walk_state, &region_node);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Second arg is the Bank Register (must already exist) */

	arg = arg->next;
	status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
			  INTERNAL_TYPE_BANK_FIELD_DEFN, IMODE_EXECUTE,
			  NS_SEARCH_PARENT, walk_state, &info.register_node);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Third arg is the Bank_value */

	arg = arg->next;
	info.bank_value = arg->value.integer32;

	/* Fourth arg is the field flags */

	arg = arg->next;
	info.field_flags = arg->value.integer8;

	/* Each remaining arg is a Named Field */

	info.field_type = INTERNAL_TYPE_BANK_FIELD;
	info.region_node = region_node;

	status = acpi_ds_get_field_names (&info, walk_state, arg->next);

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


	FUNCTION_TRACE_PTR ("Ds_create_index_field", op);


	/* First arg is the name of the Index register (must already exist) */

	arg = op->value.arg;
	status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
			  ACPI_TYPE_ANY, IMODE_EXECUTE,
			  NS_SEARCH_PARENT, walk_state, &info.register_node);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Second arg is the data register (must already exist) */

	arg = arg->next;
	status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
			  INTERNAL_TYPE_INDEX_FIELD_DEFN, IMODE_EXECUTE,
			  NS_SEARCH_PARENT, walk_state, &info.data_register_node);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Next arg is the field flags */

	arg = arg->next;
	info.field_flags = arg->value.integer8;


	/* Each remaining arg is a Named Field */

	info.field_type = INTERNAL_TYPE_INDEX_FIELD;
	info.region_node = region_node;

	status = acpi_ds_get_field_names (&info, walk_state, arg->next);

	return_ACPI_STATUS (status);
}


