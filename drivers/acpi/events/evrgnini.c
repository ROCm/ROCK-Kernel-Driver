/******************************************************************************
 *
 * Module Name: evrgnini- ACPI Address_space (Op_region) init
 *              $Revision: 63 $
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
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evrgnini")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_system_memory_region_setup
 *
 * PARAMETERS:  Region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              Handler_context     - Address space handler context
 *              Region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling, a nop for now
 *
 ******************************************************************************/

acpi_status
acpi_ev_system_memory_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context)
{
	acpi_operand_object     *region_desc = (acpi_operand_object *) handle;
	acpi_mem_space_context  *local_region_context;


	ACPI_FUNCTION_TRACE ("Ev_system_memory_region_setup");


	if (function == ACPI_REGION_DEACTIVATE) {
		if (*region_context) {
			ACPI_MEM_FREE (*region_context);
			*region_context = NULL;
		}
		return_ACPI_STATUS (AE_OK);
	}

	/* Create a new context */

	local_region_context = ACPI_MEM_CALLOCATE (sizeof (acpi_mem_space_context));
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
 * FUNCTION:    Acpi_ev_io_space_region_setup
 *
 * PARAMETERS:  Region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              Handler_context     - Address space handler context
 *              Region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 ******************************************************************************/

acpi_status
acpi_ev_io_space_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context)
{
	ACPI_FUNCTION_TRACE ("Ev_io_space_region_setup");


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
 * FUNCTION:    Acpi_ev_pci_config_region_setup
 *
 * PARAMETERS:  Region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              Handler_context     - Address space handler context
 *              Region_context      - Region specific context
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
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context)
{
	acpi_status             status = AE_OK;
	acpi_integer            temp;
	acpi_pci_id             *pci_id = *region_context;
	acpi_operand_object     *handler_obj;
	acpi_namespace_node     *node;
	acpi_operand_object     *region_obj = (acpi_operand_object *) handle;
	acpi_device_id          object_hID;


	ACPI_FUNCTION_TRACE ("Ev_pci_config_region_setup");


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

	pci_id = ACPI_MEM_CALLOCATE (sizeof (acpi_pci_id));
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
	 * If the Addr_handler.Node is still pointing to the root, we need
	 * to scan upward for a PCI Root bridge and re-associate the Op_region
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
						ACPI_REPORT_ERROR (("Could not install Pci_config handler for %4.4s, %s\n",
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

	*region_context = pci_id;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_pci_bar_region_setup
 *
 * PARAMETERS:  Region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              Handler_context     - Address space handler context
 *              Region_context      - Region specific context
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
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context)
{
	ACPI_FUNCTION_TRACE ("Ev_pci_bar_region_setup");


	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_cmos_region_setup
 *
 * PARAMETERS:  Region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              Handler_context     - Address space handler context
 *              Region_context      - Region specific context
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
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context)
{
	ACPI_FUNCTION_TRACE ("Ev_cmos_region_setup");


	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_default_region_setup
 *
 * PARAMETERS:  Region_obj          - Region we are interested in
 *              Function            - Start or stop
 *              Handler_context     - Address space handler context
 *              Region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Do any prep work for region handling
 *
 ******************************************************************************/

acpi_status
acpi_ev_default_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context)
{
	ACPI_FUNCTION_TRACE ("Ev_default_region_setup");


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
 * FUNCTION:    Acpi_ev_initialize_region
 *
 * PARAMETERS:  Region_obj      - Region we are initializing
 *              Acpi_ns_locked  - Is namespace locked?
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes the region, finds any _REG methods and saves them
 *              for execution at a later time
 *
 *              Get the appropriate address space handler for a newly
 *              created region.
 *
 *              This also performs address space specific intialization.  For
 *              example, PCI regions must have an _ADR object that contains
 *              a PCI address in the scope of the definition.  This address is
 *              required to perform an access to PCI config space.
 *
 ******************************************************************************/

acpi_status
acpi_ev_initialize_region (
	acpi_operand_object     *region_obj,
	u8                      acpi_ns_locked)
{
	acpi_operand_object     *handler_obj;
	acpi_operand_object     *obj_desc;
	ACPI_ADR_SPACE_TYPE     space_id;
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_namespace_node     *method_node;
	acpi_name               *reg_name_ptr = (acpi_name *) METHOD_NAME__REG;
	acpi_operand_object     *region_obj2;


	ACPI_FUNCTION_TRACE_U32 ("Ev_initialize_region", acpi_ns_locked);


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
	 * ie: Acpi_gbl_Root_node->Parent_entry being set to NULL
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
		"No handler for Region_type %s(%X) (Region_obj %p)\n",
		acpi_ut_get_region_name (space_id), space_id, region_obj));

	return_ACPI_STATUS (AE_NOT_EXIST);
}

