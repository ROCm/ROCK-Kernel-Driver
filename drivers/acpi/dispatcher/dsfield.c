/******************************************************************************
 *
 * Module Name: dsfield - Dispatcher field routines
 *              $Revision: 44 $
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


#define _COMPONENT          ACPI_DISPATCHER
	 MODULE_NAME         ("dsfield")


/*
 * Field flags: Bits 00 - 03 : Access_type (Any_acc, Byte_acc, etc.)
 *                   04      : Lock_rule (1 == Lock)
 *                   05 - 06 : Update_rule
 */

#define FIELD_ACCESS_TYPE_MASK      0x0F
#define FIELD_LOCK_RULE_MASK        0x10
#define FIELD_UPDATE_RULE_MASK      0x60


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
	acpi_namespace_node     *node;
	u8                      field_flags;
	u32                     field_bit_position = 0;


	FUNCTION_TRACE_PTR ("Ds_create_field", op);


	/* First arg is the name of the parent Op_region */

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
	field_flags = (u8) arg->value.integer;

	/* Each remaining arg is a Named Field */

	arg = arg->next;
	while (arg) {
		switch (arg->opcode) {
		case AML_INT_RESERVEDFIELD_OP:

			field_bit_position += arg->value.size;
			break;


		case AML_INT_ACCESSFIELD_OP:

			/*
			 * Get a new Access_type and Access_attribute for all
			 * entries (until end or another Access_as keyword)
			 */
			field_flags = (u8) ((field_flags & FIELD_ACCESS_TYPE_MASK) ||
					   ((u8) (arg->value.integer >> 8)));
			break;


		case AML_INT_NAMEDFIELD_OP:

			status = acpi_ns_lookup (walk_state->scope_info,
					  (NATIVE_CHAR *) &((acpi_parse2_object *)arg)->name,
					  INTERNAL_TYPE_REGION_FIELD, IMODE_LOAD_PASS1,
					  NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
					  NULL, &node);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/*
			 * Initialize an object for the new Node that is on
			 * the object stack
			 */
			status = acpi_ex_prep_region_field_value (node, region_node, field_flags,
					  field_bit_position, arg->value.size);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/* Keep track of bit position for *next* field */

			field_bit_position += arg->value.size;
			break;
		}

		arg = arg->next;
	}

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
	acpi_namespace_node     *register_node;
	acpi_namespace_node     *node;
	u32                     bank_value;
	u8                      field_flags;
	u32                     field_bit_position = 0;


	FUNCTION_TRACE_PTR ("Ds_create_bank_field", op);


	/* First arg is the name of the parent Op_region */

	arg = op->value.arg;
	if (!region_node) {
		status = acpi_ns_lookup (walk_state->scope_info, arg->value.name,
				  ACPI_TYPE_REGION, IMODE_EXECUTE,
				  NS_SEARCH_PARENT, walk_state, &region_node);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Second arg is the Bank Register */

	arg = arg->next;

	status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
			  INTERNAL_TYPE_BANK_FIELD_DEFN, IMODE_LOAD_PASS1,
			  NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
			  NULL, &register_node);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Third arg is the Bank_value */

	arg = arg->next;
	bank_value = arg->value.integer32;


	/* Next arg is the field flags */

	arg = arg->next;
	field_flags = arg->value.integer8;

	/* Each remaining arg is a Named Field */

	arg = arg->next;
	while (arg) {
		switch (arg->opcode) {
		case AML_INT_RESERVEDFIELD_OP:

			field_bit_position += arg->value.size;
			break;


		case AML_INT_ACCESSFIELD_OP:

			/*
			 * Get a new Access_type and Access_attribute for
			 * all entries (until end or another Access_as keyword)
			 */
			field_flags = (u8) ((field_flags & FIELD_ACCESS_TYPE_MASK) ||
					 ((u8) (arg->value.integer >> 8)));
			break;


		case AML_INT_NAMEDFIELD_OP:

			status = acpi_ns_lookup (walk_state->scope_info,
					  (NATIVE_CHAR *) &((acpi_parse2_object *)arg)->name,
					  INTERNAL_TYPE_REGION_FIELD, IMODE_LOAD_PASS1,
					  NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
					  NULL, &node);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/*
			 * Initialize an object for the new Node that is on
			 * the object stack
			 */
			status = acpi_ex_prep_bank_field_value (node, region_node, register_node,
					  bank_value, field_flags, field_bit_position,
					  arg->value.size);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/* Keep track of bit position for the *next* field */

			field_bit_position += arg->value.size;
			break;

		}

		arg = arg->next;
	}

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
	acpi_namespace_node     *node;
	acpi_namespace_node     *index_register_node;
	acpi_namespace_node     *data_register_node;
	u8                      field_flags;
	u32                     field_bit_position = 0;


	FUNCTION_TRACE_PTR ("Ds_create_index_field", op);


	arg = op->value.arg;

	/* First arg is the name of the Index register */

	status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
			  ACPI_TYPE_ANY, IMODE_LOAD_PASS1,
			  NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
			  NULL, &index_register_node);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Second arg is the data register */

	arg = arg->next;

	status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
			  INTERNAL_TYPE_INDEX_FIELD_DEFN, IMODE_LOAD_PASS1,
			  NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
			  NULL, &data_register_node);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}


	/* Next arg is the field flags */

	arg = arg->next;
	field_flags = (u8) arg->value.integer;


	/* Each remaining arg is a Named Field */

	arg = arg->next;
	while (arg) {
		switch (arg->opcode) {
		case AML_INT_RESERVEDFIELD_OP:

			field_bit_position += arg->value.size;
			break;


		case AML_INT_ACCESSFIELD_OP:

			/*
			 * Get a new Access_type and Access_attribute for all
			 * entries (until end or another Access_as keyword)
			 */
			field_flags = (u8) ((field_flags & FIELD_ACCESS_TYPE_MASK) ||
					 ((u8) (arg->value.integer >> 8)));
			break;


		case AML_INT_NAMEDFIELD_OP:

			status = acpi_ns_lookup (walk_state->scope_info,
					  (NATIVE_CHAR *) &((acpi_parse2_object *)arg)->name,
					  INTERNAL_TYPE_INDEX_FIELD, IMODE_LOAD_PASS1,
					  NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
					  NULL, &node);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/*
			 * Initialize an object for the new Node that is on
			 * the object stack
			 */
			status = acpi_ex_prep_index_field_value (node, index_register_node,
					  data_register_node, field_flags,
					  field_bit_position, arg->value.size);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/* Keep track of bit position for the *next* field */

			field_bit_position += arg->value.size;
			break;


		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid opcode in field list: %X\n",
				arg->opcode));
			status = AE_AML_ERROR;
			break;
		}

		arg = arg->next;
	}

	return_ACPI_STATUS (status);
}


