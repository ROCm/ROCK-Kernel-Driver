/*****************************************************************************
 *
 * Module Name: pr.c
 *   $Revision: 34 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
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


#include <acpi.h>
#include <bm.h>
#include "pr.h"


#define _COMPONENT		ACPI_PROCESSOR
	MODULE_NAME		("pr")


/****************************************************************************
 *                                  Globals
 ****************************************************************************/

extern fadt_descriptor_rev2	acpi_fadt;


/****************************************************************************
 *                             Internal Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    pr_print
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Prints out information on a specific thermal zone.
 *
 ****************************************************************************/

void
pr_print (
	PR_CONTEXT              *processor)
{
#ifdef ACPI_DEBUG
	acpi_buffer             buffer;

	FUNCTION_TRACE("pr_print");

	buffer.length = 256;
	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer) {
		return;
	}

	/*
	 * Get the full pathname for this ACPI object.
	 */
	acpi_get_name(processor->acpi_handle, ACPI_FULL_PATHNAME, &buffer);

	/*
	 * Print out basic processor information.
	 */
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| Processor[%02x]:[%p] uid[%02x] %s\n", processor->device_handle, processor->acpi_handle, processor->uid, (char*)buffer.pointer));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   power: %cC0 %cC1 %cC2[%d] %cC3[%d]\n", (processor->power.state[0].is_valid?'+':'-'), (processor->power.state[1].is_valid?'+':'-'), (processor->power.state[2].is_valid?'+':'-'), processor->power.state[2].latency, (processor->power.state[3].is_valid?'+':'-'), processor->power.state[3].latency));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   performance: states[%d]\n", processor->performance.state_count));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));

	acpi_os_free(buffer.pointer);
#endif /* ACPI_DEBUG */

	return;
}


/****************************************************************************
 *
 * FUNCTION:    pr_add_device
 *
 * PARAMETERS:  <none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_add_device(
	BM_HANDLE		device_handle,
	void			**context)
{
	acpi_status		status = AE_OK;
	PR_CONTEXT		*processor = NULL;
	BM_DEVICE		*device = NULL;
	acpi_buffer		buffer;
	acpi_object		acpi_object;
	static u32		processor_count = 0;


	FUNCTION_TRACE("pr_add_device");

	if (!context || *context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = bm_get_device_info(device_handle, &device);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	processor = acpi_os_callocate(sizeof(PR_CONTEXT));
	if (!processor) {
		return AE_NO_MEMORY;
	}

	processor->device_handle = device->handle;
	processor->acpi_handle = device->acpi_handle;

	/*
	 * Processor Block:
	 * ----------------
	 */
	memset(&acpi_object, 0, sizeof(acpi_object));

	buffer.length = sizeof(acpi_object);
	buffer.pointer = &acpi_object;

	status = acpi_evaluate_object(processor->acpi_handle, NULL, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	/*
	 * Processor ID:
	 * -------------
	 * TBD:  We need to synchronize the processor ID values in ACPI
	 *       with those of the APIC.  For example, an IBM T20 has a
	 *       proc_id value of '1', where the Linux value for the
	 *       first CPU on this system is '0'.  Since x86 CPUs are
	 *       mapped 1:1 we can simply use a zero-based counter.  Note
	 *       that this assumes that processor objects are enumerated
	 *       in the proper order.
	 */
	/* processor->uid = acpi_object.processor.proc_id; */
	processor->uid = processor_count++;

	processor->pblk.length = acpi_object.processor.pblk_length;
	processor->pblk.address = acpi_object.processor.pblk_address;

	status = pr_power_add_device(processor);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	status = pr_perf_add_device(processor);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	status = pr_osl_add_device(processor);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	*context = processor;

	pr_print(processor);

end:
	if (ACPI_FAILURE(status)) {
		acpi_os_free(processor);
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    pr_remove_device
 *
 * PARAMETERS:  <none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_remove_device (
	void			**context)
{
	acpi_status		status = AE_OK;
	PR_CONTEXT		*processor= NULL;

	FUNCTION_TRACE("pr_remove_device");

	if (!context || !*context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	processor = (PR_CONTEXT*)(*context);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing processor device [%02x].\n", processor->device_handle));

	pr_osl_remove_device(processor);

	pr_perf_remove_device(processor);

	pr_power_remove_device(processor);

	acpi_os_free(processor);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *                            External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    pr_initialize
 *
 * PARAMETERS:  <none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_initialize (void)
{
	acpi_status		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("pr_initialize");

	memset(&criteria, 0, sizeof(BM_DEVICE_ID));
	memset(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Initialize power (Cx state) policy.
	 */
	status = pr_power_initialize();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Register driver for processor devices.
	 */
	criteria.type = BM_TYPE_PROCESSOR;

	driver.notify = &pr_notify;
	driver.request = &pr_request;

	status = bm_register_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    pr_terminate
 *
 * PARAMETERS:  <none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_terminate (void)
{
	acpi_status             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("pr_terminate");

	memset(&criteria, 0, sizeof(BM_DEVICE_ID));
	memset(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Terminate power (Cx state) policy.
	 */
	status = pr_power_terminate();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Unegister driver for processor devices.
	 */
	criteria.type = BM_TYPE_PROCESSOR;

	driver.notify = &pr_notify;
	driver.request = &pr_request;

	status = bm_unregister_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    pr_notify
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_notify (
	BM_NOTIFY		notify_type,
	BM_HANDLE		device_handle,
	void			**context)
{
	acpi_status		status = AE_OK;
	PR_CONTEXT		*processor = NULL;

	FUNCTION_TRACE("pr_notify");

	processor = (PR_CONTEXT*)*context;

	switch (notify_type) {

	case BM_NOTIFY_DEVICE_ADDED:
		status = pr_add_device(device_handle, context);
		break;

	case BM_NOTIFY_DEVICE_REMOVED:
		status = pr_remove_device(context);
		break;

	case PR_NOTIFY_PERF_STATES:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Performance states change event detected on processor [%02x].\n", device_handle));
		/* TBD: Streamline (this is simple but overkill). */
		status = pr_perf_remove_device(processor);
		if (ACPI_SUCCESS(status)) {
			status = pr_perf_add_device(processor);
		}
		if (ACPI_SUCCESS(status)) {
			status = pr_osl_generate_event(notify_type,
				(processor));
		}
		break;

	case PR_NOTIFY_POWER_STATES:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Power states change event detected on processor [%02x].\n", device_handle));
		/* TBD: Streamline (this is simple but overkill). */
		status = pr_power_remove_device(processor);
		if (ACPI_SUCCESS(status)) {
			status = pr_power_add_device(processor);
		}
		if (ACPI_SUCCESS(status)) {
			status = pr_osl_generate_event(notify_type,
				(processor));
		}
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    pr_request
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_request (
	BM_REQUEST		*request,
	void			*context)
{
	acpi_status		status = AE_OK;
	PR_CONTEXT		*processor = NULL;

	FUNCTION_TRACE("pr_request");

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	processor = (PR_CONTEXT*)context;

	/*
	 * Handle request:
	 * ---------------
	 */
	switch (request->command) {

	case PR_COMMAND_GET_POWER_INFO:
		status = bm_copy_to_buffer(&(request->buffer),
			&(processor->power), sizeof(PR_POWER));
		break;

	case PR_COMMAND_SET_POWER_INFO:
	 {
		PR_POWER *power_info = NULL;
		u32 i = 0;

		status = bm_cast_buffer(&(request->buffer),
			(void**)&power_info, sizeof(PR_POWER));
		if (ACPI_SUCCESS(status)) {
			for (i=0; i<processor->power.state_count; i++) {
				MEMCPY(&(processor->power.state[i].promotion),
					&(power_info->state[i].promotion),
					sizeof(PR_CX_POLICY_VALUES));
				MEMCPY(&(processor->power.state[i].demotion),
					&(power_info->state[i].demotion),
					sizeof(PR_CX_POLICY_VALUES));
			}
		}
	}
		break;

	case PR_COMMAND_GET_PERF_INFO:
		status = bm_copy_to_buffer(&(request->buffer),
			&(processor->performance), sizeof(PR_PERFORMANCE));
		break;

	case PR_COMMAND_GET_PERF_STATE:
		status = bm_copy_to_buffer(&(request->buffer),
			&(processor->performance.active_state), sizeof(u32));
		break;

	case PR_COMMAND_SET_PERF_LIMIT:
	 {
		u32 *limit = NULL;

		status = bm_cast_buffer(&(request->buffer),
			(void**)&limit, sizeof(u32));
		if (ACPI_SUCCESS(status)) {
			status = pr_perf_set_limit(processor, *limit);
		}
	}
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	request->status = status;

	return_ACPI_STATUS(status);
}
