/******************************************************************************
 *
 * Module Name: evrgnini- ACPI address_space (op_region) init
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
#include <acpi/acevents.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evrgnini")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_system_memory_region_setup
 *
 * PARAMETERS:  region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling, a nop for now
 *
 ******************************************************************************/

acpi_status
acpi_ev_system_memory_region_setup (
	acpi_handle                     handle,
	u32                             function,
	void                            *handler_context,
	void                            **region_context)
{
	union acpi_operand_object       *region_desc = (union acpi_operand_object *) handle;
	struct acpi_mem_space_context   *local_region_context;


	ACPI_FUNCTION_TRACE ("ev_system_memory_region_setup");


	if (function == ACPI_REGION_DEACTIVATE) {
		if (*region_context) {
			ACPI_MEM_FREE (*region_context);
			*region_context = NULL;
		}
		return_ACPI_STATUS (AE_OK);
	}

	/* Create a new context */

	local_region_context = ACPI_MEM_CALLOCATE (sizeof (struct acpi_mem_space_context));
	if (!(local_region_context)) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Save the region length and address for use in the handler */

	local_region_context->length = region_desc->region.length;
	local_region_context->address = region_desc->region.address;

	*region_context = local_region_context;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_io_space_region_setup
 *
 * PARAMETERS:  region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 ******************************************************************************/

acpi_status
acpi_ev_io_space_region_setup (
	acpi_handle                     handle,
	u32                             function,
	void                            *handler_context,
	void                            **region_context)
{
	ACPI_FUNCTION_TRACE ("ev_io_space_region_setup");


	if (function == ACPI_REGION_DEACTIVATE) {
		*region_context = NULL;
	}
	else {
		*region_context = handler_context;
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_pci_config_region_setup
 *
 * PARAMETERS:  region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

acpi_status
acpi_ev_pci_config_region_setup (
	acpi_handle                     handle,
	u32                             function,
	void                            *handler_context,
	void                            **region_context)
{
	acpi_status                     status = AE_OK;
	acpi_integer                    temp;
	struct acpi_pci_id              *pci_id = *region_context;
	union acpi_operand_object       *handler_obj;
	struct acpi_namespace_node      *node;
	union acpi_operand_object       *region_obj = (union acpi_operand_object   *) handle;
	struct acpi_device_id           object_hID;


	ACPI_FUNCTION_TRACE ("ev_pci_config_region_setup");


	handler_obj = region_obj->region.addr_handler;
	if (!handler_obj) {
		/*
		 * No installed handler. This shouldn't happen because the dispatch
		 * routine checks before we get here, but we check again just in case.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
			"Attempting to init a region %p, with no handler\n", region_obj));
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	if (function == ACPI_REGION_DEACTIVATE) {
		if (pci_id) {
			ACPI_MEM_FREE (pci_id);
			*region_context = NULL;
		}

		return_ACPI_STATUS (status);
	}

	/* Create a new context */

	pci_id = ACPI_MEM_CALLOCATE (sizeof (struct acpi_pci_id));
	if (!pci_id) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/*
	 * For PCI Config space access, we have to pass the segment, bus,
	 * device and function numbers.  This routine must acquire those.
	 */

	/*
	 * First get device and function numbers from the _ADR object
	 * in the parent's scope.
	 */
	node = acpi_ns_get_parent_node (region_obj->region.node);

	/* Evaluate the _ADR object */

	status = acpi_ut_evaluate_numeric_object (METHOD_NAME__ADR, node, &temp);

	/*
	 * The default is zero, and since the allocation above zeroed
	 * the data, just do nothing on failure.
	 */
	if (ACPI_SUCCESS (status)) {
		pci_id->device  = ACPI_HIWORD (ACPI_LODWORD (temp));
		pci_id->function = ACPI_LOWORD (ACPI_LODWORD (temp));
	}

	/*
	 * Get the _SEG and _BBN values from the device upon which the handler
	 * is installed.
	 *
	 * We need to get the _SEG and _BBN objects relative to the PCI BUS device.
	 * This is the device the handler has been registered to handle.
	 */

	/*
	 * If the addr_handler.Node is still pointing to the root, we need
	 * to scan upward for a PCI Root bridge and re-associate the op_region
	 * handlers with that device.
	 */
	if (handler_obj->addr_handler.node == acpi_gbl_root_node) {
		/*
		 * Node is currently the parent object
		 */
		while (node != acpi_gbl_root_node) {
			status = acpi_ut_execute_HID (node, &object_hID);
			if (ACPI_SUCCESS (status)) {
				/* Got a valid _HID, check if this is a PCI root */

				if (!(ACPI_STRNCMP (object_hID.buffer, PCI_ROOT_HID_STRING,
						   sizeof (PCI_ROOT_HID_STRING)))) {
					/* Install a handler for this PCI root bridge */

					status = acpi_install_address_space_handler ((acpi_handle) node,
							   ACPI_ADR_SPACE_PCI_CONFIG,
							   ACPI_DEFAULT_HANDLER, NULL, NULL);
					if (ACPI_FAILURE (status)) {
						ACPI_REPORT_ERROR (("Could not install pci_config handler for %4.4s, %s\n",
							node->name.ascii, acpi_format_exception (status)));
					}
					break;
				}
			}

			node = acpi_ns_get_parent_node (node);
		}
	}
	else {
		node = handler_obj->addr_handler.node;
	}

	/*
	 * The PCI segment number comes from the _SEG method
	 */
	status = acpi_ut_evaluate_numeric_object (METHOD_NAME__SEG, node, &temp);
	if (ACPI_SUCCESS (status)) {
		pci_id->segment = ACPI_LOWORD (temp);
	}

	/*
	 * The PCI bus number comes from the _BBN method
	 */
	status = acpi_ut_evaluate_numeric_object (METHOD_NAME__BBN, node, &temp);
	if (ACPI_SUCCESS (status)) {
		pci_id->bus = ACPI_LOWORD (temp);
	}

	/*
	 * Complete this device's pci_id
	 */
	acpi_os_derive_pci_id (node, region_obj->region.node, &pci_id);

	*region_context = pci_id;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_pci_bar_region_setup
 *
 * PARAMETERS:  region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

acpi_status
acpi_ev_pci_bar_region_setup (
	acpi_handle                     handle,
	u32                             function,
	void                            *handler_context,
	void                            **region_context)
{
	ACPI_FUNCTION_TRACE ("ev_pci_bar_region_setup");


	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_cmos_region_setup
 *
 * PARAMETERS:  region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

acpi_status
acpi_ev_cmos_region_setup (
	acpi_handle                     handle,
	u32                             function,
	void                            *handler_context,
	void                            **region_context)
{
	ACPI_FUNCTION_TRACE ("ev_cmos_region_setup");


	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_default_region_setup
 *
 * PARAMETERS:  region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 ******************************************************************************/

acpi_status
acpi_ev_default_region_setup (
	acpi_handle                     handle,
	u32                             function,
	void                            *handler_context,
	void                            **region_context)
{
	ACPI_FUNCTION_TRACE ("ev_default_region_setup");


	if (function == ACPI_REGION_DEACTIVATE) {
		*region_context = NULL;
	}
	else {
		*region_context = handler_context;
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_initialize_region
 *
 * PARAMETERS:  region_obj      - Region we are initializing
 *              acpi_ns_locked  - Is namespace locked?
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes the region, finds any _REG methods and saves them
 *              for execution at a later time
 *
 *              Get the appropriate address space handler for a newly
 *              created region.
 *
 *              This also performs address space specific initialization.  For
 *              example, PCI regions must have an _ADR object that contains
 *              a PCI address in the scope of the definition.  This address is
 *              required to perform an access to PCI config space.
 *
 ******************************************************************************/

acpi_status
acpi_ev_initialize_region (
	union acpi_operand_object       *region_obj,
	u8                              acpi_ns_locked)
{
	union acpi_operand_object       *handler_obj;
	union acpi_operand_object       *obj_desc;
	acpi_adr_space_type             space_id;
	struct acpi_namespace_node      *node;
	acpi_status                     status;
	struct acpi_namespace_node      *method_node;
	acpi_name                       *reg_name_ptr = (acpi_name *) METHOD_NAME__REG;
	union acpi_operand_object       *region_obj2;


	ACPI_FUNCTION_TRACE_U32 ("ev_initialize_region", acpi_ns_locked);


	if (!region_obj) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (region_obj->common.flags & AOPOBJ_OBJECT_INITIALIZED) {
		return_ACPI_STATUS (AE_OK);
	}

	region_obj2 = acpi_ns_get_secondary_object (region_obj);
	if (!region_obj2) {
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	node = acpi_ns_get_parent_node (region_obj->region.node);
	space_id = region_obj->region.space_id;

	region_obj->region.addr_handler = NULL;
	region_obj2->extra.method_REG = NULL;
	region_obj->common.flags &= ~(AOPOBJ_SETUP_COMPLETE);
	region_obj->common.flags |= AOPOBJ_OBJECT_INITIALIZED;

	/*
	 * Find any "_REG" method associated with this region definition
	 */
	status = acpi_ns_search_node (*reg_name_ptr, node,
			  ACPI_TYPE_METHOD, &method_node);
	if (ACPI_SUCCESS (status)) {
		/*
		 * The _REG method is optional and there can be only one per region
		 * definition.  This will be executed when the handler is attached
		 * or removed
		 */
		region_obj2->extra.method_REG = method_node;
	}

	/*
	 * The following loop depends upon the root Node having no parent
	 * ie: acpi_gbl_root_node->parent_entry being set to NULL
	 */
	while (node) {
		/*
		 * Check to see if a handler exists
		 */
		handler_obj = NULL;
		obj_desc = acpi_ns_get_attached_object (node);
		if (obj_desc) {
			/*
			 * Can only be a handler if the object exists
			 */
			switch (node->type) {
			case ACPI_TYPE_DEVICE:

				handler_obj = obj_desc->device.addr_handler;
				break;

			case ACPI_TYPE_PROCESSOR:

				handler_obj = obj_desc->processor.addr_handler;
				break;

			case ACPI_TYPE_THERMAL:

				handler_obj = obj_desc->thermal_zone.addr_handler;
				break;

			default:
				/* Ignore other objects */
				break;
			}

			while (handler_obj) {
				/* Is this handler of the correct type? */

				if (handler_obj->addr_handler.space_id == space_id) {
					/* Found correct handler */

					ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
						"Found handler %p for region %p in obj %p\n",
						handler_obj, region_obj, obj_desc));

					status = acpi_ev_attach_region (handler_obj, region_obj,
							 acpi_ns_locked);

					return_ACPI_STATUS (AE_OK);
				}

				/* Try next handler in the list */

				handler_obj = handler_obj->addr_handler.next;
			}
		}

		/*
		 * This node does not have the handler we need;
		 * Pop up one level
		 */
		node = acpi_ns_get_parent_node (node);
	}

	/*
	 * If we get here, there is no handler for this region
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
		"No handler for region_type %s(%X) (region_obj %p)\n",
		acpi_ut_get_region_name (space_id), space_id, region_obj));

	return_ACPI_STATUS (AE_NOT_EXIST);
}

