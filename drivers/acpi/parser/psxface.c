/******************************************************************************
 *
 * Module Name: psxface - Parser external interfaces
 *              $Revision: 47 $
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
#include "acparser.h"
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"


#define _COMPONENT          ACPI_PARSER
	 MODULE_NAME         ("psxface")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_psx_execute
 *
 * PARAMETERS:  Method_node         - A method object containing both the AML
 *                                    address and length.
 *              **Params            - List of parameters to pass to method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *              **Return_obj_desc   - Return object from execution of the
 *                                    method.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method
 *
 ******************************************************************************/

acpi_status
acpi_psx_execute (
	acpi_namespace_node     *method_node,
	acpi_operand_object     **params,
	acpi_operand_object     **return_obj_desc)
{
	acpi_status             status;
	acpi_operand_object     *obj_desc;
	u32                     i;
	acpi_parse_object       *op;


	FUNCTION_TRACE ("Psx_execute");


	/* Validate the Node and get the attached object */

	if (!method_node) {
		return_ACPI_STATUS (AE_NULL_ENTRY);
	}

	obj_desc = acpi_ns_get_attached_object (method_node);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NULL_OBJECT);
	}

	/* Init for new method, wait on concurrency semaphore */

	status = acpi_ds_begin_method_execution (method_node, obj_desc, NULL);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	if (params) {
		/*
		 * The caller "owns" the parameters, so give each one an extra
		 * reference
		 */
		for (i = 0; params[i]; i++) {
			acpi_ut_add_reference (params[i]);
		}
	}

	/*
	 * Perform the first pass parse of the method to enter any
	 * named objects that it creates into the namespace
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"**** Begin Method Execution **** Entry=%p obj=%p\n",
		method_node, obj_desc));

	/* Create and init a Root Node */

	op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!op) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ps_parse_aml (op, obj_desc->method.pcode,
			  obj_desc->method.pcode_length,
			  ACPI_PARSE_LOAD_PASS1 | ACPI_PARSE_DELETE_TREE,
			  method_node, params, return_obj_desc,
			  acpi_ds_load1_begin_op, acpi_ds_load1_end_op);
	acpi_ps_delete_parse_tree (op);

	/* Create and init a Root Node */

	op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!op) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}


	/* Init new op with the method name and pointer back to the NS node */

	acpi_ps_set_name (op, method_node->name);
	op->node = method_node;

	/*
	 * The walk of the parse tree is where we actually execute the method
	 */
	status = acpi_ps_parse_aml (op, obj_desc->method.pcode,
			  obj_desc->method.pcode_length,
			  ACPI_PARSE_EXECUTE | ACPI_PARSE_DELETE_TREE,
			  method_node, params, return_obj_desc,
			  acpi_ds_exec_begin_op, acpi_ds_exec_end_op);
	acpi_ps_delete_parse_tree (op);

	if (params) {
		/* Take away the extra reference that we gave the parameters above */

		for (i = 0; params[i]; i++) {
			acpi_ut_update_object_reference (params[i], REF_DECREMENT);
		}
	}


	/*
	 * If the method has returned an object, signal this to the caller with
	 * a control exception code
	 */
	if (*return_obj_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Method returned Obj_desc=%X\n",
			*return_obj_desc));
		DUMP_STACK_ENTRY (*return_obj_desc);

		status = AE_CTRL_RETURN_VALUE;
	}


	return_ACPI_STATUS (status);
}


