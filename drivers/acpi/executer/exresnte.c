
/******************************************************************************
 *
 * Module Name: exresnte - AML Interpreter object resolution
 *              $Revision: 41 $
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
#include "acparser.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "actables.h"
#include "acevents.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exresnte")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_node_to_value
 *
 * PARAMETERS:  Stack_ptr       - Pointer to a location on a stack that contains
 *                                a pointer to a Node
 *              Walk_state      - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Resolve a Namespace node (AKA a "direct name pointer") to
 *              a valued object
 *
 * Note: for some of the data types, the pointer attached to the Node
 * can be either a pointer to an actual internal object or a pointer into the
 * AML stream itself.  These types are currently:
 *
 *      ACPI_TYPE_INTEGER
 *      ACPI_TYPE_STRING
 *      ACPI_TYPE_BUFFER
 *      ACPI_TYPE_MUTEX
 *      ACPI_TYPE_PACKAGE
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_node_to_value (
	acpi_namespace_node     **stack_ptr,
	acpi_walk_state         *walk_state)

{
	acpi_status             status = AE_OK;
	acpi_operand_object     *val_desc;
	acpi_operand_object     *obj_desc = NULL;
	acpi_namespace_node     *node;
	acpi_object_type8       entry_type;
	acpi_integer            temp_val;


	FUNCTION_TRACE ("Ex_resolve_node_to_value");


	/*
	 * The stack pointer points to a acpi_namespace_node (Node).  Get the
	 * object that is attached to the Node.
	 */
	node      = *stack_ptr;
	val_desc  = acpi_ns_get_attached_object (node);
	entry_type = acpi_ns_get_type ((acpi_handle) node);

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Entry=%p Val_desc=%p Type=%X\n",
		 node, val_desc, entry_type));


	/*
	 * Several object types require no further processing:
	 * 1) Devices rarely have an attached object, return the Node
	 * 2) Method locals and arguments have a pseudo-Node
	 */
	if (entry_type == ACPI_TYPE_DEVICE ||
		(node->flags & (ANOBJ_METHOD_ARG | ANOBJ_METHOD_LOCAL))) {
		return_ACPI_STATUS (AE_OK);
	}

	if (!val_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No object attached to node %p\n",
			node));
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	/*
	 * Action is based on the type of the Node, which indicates the type
	 * of the attached object or pointer
	 */
	switch (entry_type) {

	case ACPI_TYPE_PACKAGE:

		if (ACPI_TYPE_PACKAGE != val_desc->common.type) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Object not a Package, type %s\n",
				acpi_ut_get_type_name (val_desc->common.type)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = val_desc;
		acpi_ut_add_reference (obj_desc);
		break;


	case ACPI_TYPE_BUFFER:

		if (ACPI_TYPE_BUFFER != val_desc->common.type) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Object not a Buffer, type %s\n",
				acpi_ut_get_type_name (val_desc->common.type)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = val_desc;
		acpi_ut_add_reference (obj_desc);
		break;


	case ACPI_TYPE_STRING:

		if (ACPI_TYPE_STRING != val_desc->common.type) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Object not a String, type %s\n",
				acpi_ut_get_type_name (val_desc->common.type)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = val_desc;
		acpi_ut_add_reference (obj_desc);
		break;


	case ACPI_TYPE_INTEGER:

		if (ACPI_TYPE_INTEGER != val_desc->common.type) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Object not a Integer, type %s\n",
				acpi_ut_get_type_name (val_desc->common.type)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = val_desc;
		acpi_ut_add_reference (obj_desc);
		break;


	case ACPI_TYPE_BUFFER_FIELD:
	case INTERNAL_TYPE_REGION_FIELD:
	case INTERNAL_TYPE_BANK_FIELD:
	case INTERNAL_TYPE_INDEX_FIELD:

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Field_read Node=%p Val_desc=%p Type=%X\n",
			node, val_desc, entry_type));

		status = acpi_ex_read_data_from_field (val_desc, &obj_desc);
		break;


	/*
	 * For these objects, just return the object attached to the Node
	 */
	case ACPI_TYPE_MUTEX:
	case ACPI_TYPE_METHOD:
	case ACPI_TYPE_POWER:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_THERMAL:
	case ACPI_TYPE_EVENT:
	case ACPI_TYPE_REGION:

		/* Return an additional reference to the object */

		obj_desc = val_desc;
		acpi_ut_add_reference (obj_desc);
		break;


	/* TYPE_Any is untyped, and thus there is no object associated with it */

	case ACPI_TYPE_ANY:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Untyped entry %p, no attached object!\n",
			node));

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);  /* Cannot be AE_TYPE */
		break;


	/*
	 * The only named references allowed are named constants
	 *   e.g. -- Name (\OSFL, Ones)
	 */
	case INTERNAL_TYPE_REFERENCE:

		switch (val_desc->reference.opcode) {

		case AML_ZERO_OP:

			temp_val = 0;
			break;

		case AML_ONE_OP:

			temp_val = 1;
			break;

		case AML_ONES_OP:

			temp_val = ACPI_INTEGER_MAX;
			break;

		case AML_REVISION_OP:

			temp_val = ACPI_CA_VERSION;
			break;

		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unsupported reference opcode %X\n",
				val_desc->reference.opcode));

			return_ACPI_STATUS (AE_AML_BAD_OPCODE);
		}

		/* Create object for result */

		obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!obj_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		obj_desc->integer.value = temp_val;

		/* Truncate value if we are executing from a 32-bit ACPI table */

		acpi_ex_truncate_for32bit_table (obj_desc, walk_state);
		break;


	/* Default case is for unknown types */

	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Node %p - Unknown object type %X\n",
			node, entry_type));

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);

	} /* switch (Entry_type) */


	/* Put the object descriptor on the stack */

	*stack_ptr = (void *) obj_desc;
	return_ACPI_STATUS (status);
}


