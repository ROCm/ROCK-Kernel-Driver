
/******************************************************************************
 *
 * Module Name: exresnte - AML Interpreter object resolution
 *              $Revision: 57 $
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


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exresnte")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_node_to_value
 *
 * PARAMETERS:  Object_ptr      - Pointer to a location that contains
 *                                a pointer to a NS node, and will receive a
 *                                pointer to the resolved object.
 *              Walk_state      - Current state.  Valid only if executing AML
 *                                code.  NULL if simply resolving an object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Resolve a Namespace node to a valued object
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
	acpi_namespace_node     **object_ptr,
	acpi_walk_state         *walk_state)

{
	acpi_status             status = AE_OK;
	acpi_operand_object     *source_desc;
	acpi_operand_object     *obj_desc = NULL;
	acpi_namespace_node     *node;
	acpi_object_type        entry_type;


	ACPI_FUNCTION_TRACE ("Ex_resolve_node_to_value");


	/*
	 * The stack pointer points to a acpi_namespace_node (Node).  Get the
	 * object that is attached to the Node.
	 */
	node       = *object_ptr;
	source_desc = acpi_ns_get_attached_object (node);
	entry_type = acpi_ns_get_type ((acpi_handle) node);

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Entry=%p Source_desc=%p Type=%X\n",
		 node, source_desc, entry_type));

	/*
	 * Several object types require no further processing:
	 * 1) Devices rarely have an attached object, return the Node
	 * 2) Method locals and arguments have a pseudo-Node
	 */
	if (entry_type == ACPI_TYPE_DEVICE ||
		(node->flags & (ANOBJ_METHOD_ARG | ANOBJ_METHOD_LOCAL))) {
		return_ACPI_STATUS (AE_OK);
	}

	if (!source_desc) {
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

		if (ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_TYPE_PACKAGE) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Object not a Package, type %s\n",
				acpi_ut_get_object_type_name (source_desc)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		status = acpi_ds_get_package_arguments (source_desc);
		if (ACPI_SUCCESS (status)) {
			/* Return an additional reference to the object */

			obj_desc = source_desc;
			acpi_ut_add_reference (obj_desc);
		}
		break;


	case ACPI_TYPE_BUFFER:

		if (ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_TYPE_BUFFER) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Object not a Buffer, type %s\n",
				acpi_ut_get_object_type_name (source_desc)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		status = acpi_ds_get_buffer_arguments (source_desc);
		if (ACPI_SUCCESS (status)) {
			/* Return an additional reference to the object */

			obj_desc = source_desc;
			acpi_ut_add_reference (obj_desc);
		}
		break;


	case ACPI_TYPE_STRING:

		if (ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_TYPE_STRING) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Object not a String, type %s\n",
				acpi_ut_get_object_type_name (source_desc)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = source_desc;
		acpi_ut_add_reference (obj_desc);
		break;


	case ACPI_TYPE_INTEGER:

		if (ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_TYPE_INTEGER) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Object not a Integer, type %s\n",
				acpi_ut_get_object_type_name (source_desc)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = source_desc;
		acpi_ut_add_reference (obj_desc);
		break;


	case ACPI_TYPE_BUFFER_FIELD:
	case INTERNAL_TYPE_REGION_FIELD:
	case INTERNAL_TYPE_BANK_FIELD:
	case INTERNAL_TYPE_INDEX_FIELD:

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Field_read Node=%p Source_desc=%p Type=%X\n",
			node, source_desc, entry_type));

		status = acpi_ex_read_data_from_field (walk_state, source_desc, &obj_desc);
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

		obj_desc = source_desc;
		acpi_ut_add_reference (obj_desc);
		break;


	/* TYPE_Any is untyped, and thus there is no object associated with it */

	case ACPI_TYPE_ANY:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Untyped entry %p, no attached object!\n",
			node));

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);  /* Cannot be AE_TYPE */


	case INTERNAL_TYPE_REFERENCE:

		/* No named references are allowed here */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unsupported Reference opcode %X\n",
			source_desc->reference.opcode));

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);


	/* Default case is for unknown types */

	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Node %p - Unknown object type %X\n",
			node, entry_type));

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);

	} /* switch (Entry_type) */


	/* Put the object descriptor on the stack */

	*object_ptr = (void *) obj_desc;
	return_ACPI_STATUS (status);
}


