/*****************************************************************************
 *
 * Module Name: pr.c
 *   $Revision: 30 $
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

extern FADT_DESCRIPTOR_REV2	acpi_fadt;


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

ACPI_STATUS
pr_add_device(
	BM_HANDLE		device_handle,
	void			**context)
{
	ACPI_STATUS		status = AE_OK;
	PR_CONTEXT		*processor = NULL;
	BM_DEVICE		*device = NULL;
	ACPI_BUFFER		buffer;
	ACPI_OBJECT		acpi_object;
	static u32		processor_count = 0;


	if (!context || *context) {
		return(AE_BAD_PARAMETER);
	}

	status = bm_get_device_info(device_handle, &device);
	if (ACPI_FAILURE(status)) {
		return(status);
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
	MEMSET(&acpi_object, 0, sizeof(ACPI_OBJECT));

	buffer.length = sizeof(ACPI_OBJECT);
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

	return(status);
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

ACPI_STATUS
pr_remove_device (
	void			**context)
{
	ACPI_STATUS		status = AE_OK;
	PR_CONTEXT		*processor= NULL;

	if (!context || !*context) {
		return(AE_BAD_PARAMETER);
	}

	processor = (PR_CONTEXT*)(*context);

	pr_osl_remove_device(processor);

	pr_perf_remove_device(processor);

	pr_power_remove_device(processor);

	acpi_os_free(processor);

	return(status);
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

ACPI_STATUS
pr_initialize (void)
{
	ACPI_STATUS		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Initialize power (Cx state) policy.
	 */
	status = pr_power_initialize();
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	/*
	 * Register driver for processor devices.
	 */
	criteria.type = BM_TYPE_PROCESSOR;

	driver.notify = &pr_notify;
	driver.request = &pr_request;

	status = bm_register_driver(&criteria, &driver);

	return(status);
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

ACPI_STATUS
pr_terminate (void)
{
	ACPI_STATUS             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Terminate power (Cx state) policy.
	 */
	status = pr_power_terminate();
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	/*
	 * Unegister driver for processor devices.
	 */
	criteria.type = BM_TYPE_PROCESSOR;

	driver.notify = &pr_notify;
	driver.request = &pr_request;

	status = bm_unregister_driver(&criteria, &driver);

	return(status);
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

ACPI_STATUS
pr_notify (
	BM_NOTIFY		notify_type,
	BM_HANDLE		device_handle,
	void			**context)
{
	ACPI_STATUS		status = AE_OK;
	PR_CONTEXT		*processor = NULL;

	processor = (PR_CONTEXT*)*context;

	switch (notify_type) {

	case BM_NOTIFY_DEVICE_ADDED:
		status = pr_add_device(device_handle, context);
		break;

	case BM_NOTIFY_DEVICE_REMOVED:
		status = pr_remove_device(context);
		break;

	case PR_NOTIFY_PERF_STATES:
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

	return(status);
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

ACPI_STATUS
pr_request (
	BM_REQUEST		*request,
	void			*context)
{
	ACPI_STATUS		status = AE_OK;
	PR_CONTEXT		*processor = NULL;

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context) {
		return(AE_BAD_PARAMETER);
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

	return(status);
}
