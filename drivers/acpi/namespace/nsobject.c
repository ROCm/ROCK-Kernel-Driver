/*******************************************************************************
 *
 * Module Name: nsobject - Utilities for objects attached to namespace
 *                         table entries
 *              $Revision: 67 $
 *
 ******************************************************************************/

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
#include "acnamesp.h"
#include "acinterp.h"
#include "actables.h"


#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nsobject")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_attach_object
 *
 * PARAMETERS:  Node                - Parent Node
 *              Object              - Object to be attached
 *              Type                - Type of object, or ACPI_TYPE_ANY if not
 *                                    known
 *
 * DESCRIPTION: Record the given object as the value associated with the
 *              name whose acpi_handle is passed.  If Object is NULL
 *              and Type is ACPI_TYPE_ANY, set the name as having no value.
 *
 * MUTEX:       Assumes namespace is locked
 *
 ******************************************************************************/

acpi_status
acpi_ns_attach_object (
	acpi_namespace_node     *node,
	acpi_operand_object     *object,
	acpi_object_type8       type)
{
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *previous_obj_desc;
	acpi_object_type8       obj_type = ACPI_TYPE_ANY;
	u8                      flags;


	FUNCTION_TRACE ("Ns_attach_object");


	/*
	 * Parameter validation
	 */
	if (!acpi_gbl_root_node) {
		/* Name space not initialized  */

		REPORT_ERROR (("Ns_attach_object: Namespace not initialized\n"));
		return_ACPI_STATUS (AE_NO_NAMESPACE);
	}

	if (!node) {
		/* Invalid handle */

		REPORT_ERROR (("Ns_attach_object: Null Named_obj handle\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (!object && (ACPI_TYPE_ANY != type)) {
		/* Null object */

		REPORT_ERROR (("Ns_attach_object: Null object, but type not ACPI_TYPE_ANY\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (!VALID_DESCRIPTOR_TYPE (node, ACPI_DESC_TYPE_NAMED)) {
		/* Not a name handle */

		REPORT_ERROR (("Ns_attach_object: Invalid handle\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Check if this object is already attached */

	if (node->object == object) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj %p already installed in Name_obj %p\n",
			object, node));

		return_ACPI_STATUS (AE_OK);
	}


	/* Get the current flags field of the Node */

	flags = node->flags;
	flags &= ~ANOBJ_AML_ATTACHMENT;


	/* If null object, we will just install it */

	if (!object) {
		obj_desc = NULL;
		obj_type = ACPI_TYPE_ANY;
	}

	/*
	 * If the source object is a namespace Node with an attached object,
	 * we will use that (attached) object
	 */
	else if (VALID_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_NAMED) &&
			((acpi_namespace_node *) object)->object) {
		/*
		 * Value passed is a name handle and that name has a
		 * non-null value.  Use that name's value and type.
		 */
		obj_desc = ((acpi_namespace_node *) object)->object;
		obj_type = ((acpi_namespace_node *) object)->type;

		/*
		 * Copy appropriate flags
		 */
		if (((acpi_namespace_node *) object)->flags & ANOBJ_AML_ATTACHMENT) {
			flags |= ANOBJ_AML_ATTACHMENT;
		}
	}


	/*
	 * Otherwise, we will use the parameter object, but we must type
	 * it first
	 */
	else {
		obj_desc = (acpi_operand_object *) object;

		/* If a valid type (non-ANY) was given, just use it */

		if (ACPI_TYPE_ANY != type) {
			obj_type = type;
		}

		else {
			/*
			 * Cannot figure out the type -- set to Def_any which
			 * will print as an error in the name table dump
			 */
			if (acpi_dbg_level > 0) {
				DUMP_PATHNAME (node,
					"Ns_attach_object confused: setting bogus type for ",
					ACPI_LV_INFO, _COMPONENT);

				if (VALID_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_NAMED)) {
					DUMP_PATHNAME (object, "name ", ACPI_LV_INFO, _COMPONENT);
				}

				else {
					DUMP_PATHNAME (object, "object ", ACPI_LV_INFO, _COMPONENT);
					DUMP_STACK_ENTRY (object);
				}
			}

			obj_type = INTERNAL_TYPE_DEF_ANY;
		}
	}


	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Installing %p into Node %p [%4.4s]\n",
		obj_desc, node, (char*)&node->name));


	/*
	 * Must increment the new value's reference count
	 * (if it is an internal object)
	 */
	acpi_ut_add_reference (obj_desc);

	/* Save the existing object (if any) for deletion later */

	previous_obj_desc = node->object;

	/* Install the object and set the type, flags */

	node->object   = obj_desc;
	node->type     = (u8) obj_type;
	node->flags    |= flags;


	/*
	 * Delete an existing attached object.
	 */
	if (previous_obj_desc) {
		/* One for the attach to the Node */

		acpi_ut_remove_reference (previous_obj_desc);

		/* Now delete */

		acpi_ut_remove_reference (previous_obj_desc);
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_detach_object
 *
 * PARAMETERS:  Node           - An object whose Value will be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete the Value associated with a namespace object.  If the
 *              Value is an allocated object, it is freed.  Otherwise, the
 *              field is simply cleared.
 *
 ******************************************************************************/

void
acpi_ns_detach_object (
	acpi_namespace_node     *node)
{
	acpi_operand_object     *obj_desc;


	FUNCTION_TRACE ("Ns_detach_object");


	obj_desc = node->object;
	if (!obj_desc) {
		return_VOID;
	}

	/* Clear the entry in all cases */

	node->object = NULL;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Object=%p Value=%p Name %4.4s\n",
		node, obj_desc, (char*)&node->name));

	/* Remove one reference on the object (and all subobjects) */

	acpi_ut_remove_reference (obj_desc);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_attached_object
 *
 * PARAMETERS:  Node             - Parent Node to be examined
 *
 * RETURN:      Current value of the object field from the Node whose
 *              handle is passed
 *
 ******************************************************************************/

void *
acpi_ns_get_attached_object (
	acpi_namespace_node     *node)
{
	FUNCTION_TRACE_PTR ("Ns_get_attached_object", node);


	if (!node) {
		/* handle invalid */

		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Null Node ptr\n"));
		return_PTR (NULL);
	}

	return_PTR (node->object);
}


