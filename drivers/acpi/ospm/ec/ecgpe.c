/*****************************************************************************
 *
 * Module Name: ecgpe.c
 *   $Revision: 28 $
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
#include "ec.h"

#define _COMPONENT		ACPI_EC
	MODULE_NAME		("ecgpe")


/****************************************************************************
 *
 * FUNCTION:    ec_query_handler
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
ec_query_handler (
	void                    *context)
{
	EC_CONTEXT		*ec = (EC_CONTEXT*)context;
	static char		object_name[5] = {'_','Q','0','0','\0'};
	const char		hex[] = {'0','1','2','3','4','5','6','7','8',
					'9','A','B','C','D','E','F'};

	FUNCTION_TRACE("ec_query_handler");

	if (!ec) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid (NULL) context.\n"));
		return_VOID;
	}

	/*
	 * Evaluate _Qxx:
	 * --------------
	 * Evaluate corresponding _Qxx method.  Note that a zero query value
	 * indicates a spurious EC_SCI (no such thing as _Q00).
	 */
	object_name[2] = hex[((ec->query_data >> 4) & 0x0F)];
	object_name[3] = hex[(ec->query_data & 0x0F)];

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Evaluating [%s] for ec [%02x].\n", object_name, ec->device_handle));

	bm_evaluate_object(ec->acpi_handle, object_name, NULL, NULL);

	return_VOID;
}


/****************************************************************************
 *
 * FUNCTION:    ec_gpe_handler
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
ec_gpe_handler (
	void                    *context)
{
	acpi_status             status = AE_OK;
	EC_CONTEXT              *ec = (EC_CONTEXT*)context;
	EC_STATUS               ec_status = 0;

	FUNCTION_TRACE("ec_gpe_handler");

	if (!ec) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid (NULL) context.\n"));
		return_VOID;
	}

	/* TBD: synchronize w/ transaction (ectransx). */

	/*
	 * EC_SCI?
	 * -------
	 * Check the EC_SCI bit to see if this is an EC_SCI event.  If not (e.g.
	 * OBF/IBE) just return, as we already poll to detect these events.
	 */
	acpi_os_read_port(ec->status_port, &ec_status, 8);
	if (!(ec_status & EC_FLAG_SCI)) {
		return_VOID;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "EC_SCI event detected on ec [%02x] - running query.\n", ec->device_handle));

	/*
	 * Run Query:
	 * ----------
	 * Query the EC to find out which _Qxx method we need to evaluate.
	 * Note that successful completion of the query causes the EC_SCI
	 * bit to be cleared (and thus clearing the interrupt source).
	 */
	status = ec_io_write(ec, ec->command_port, EC_COMMAND_QUERY,
		EC_EVENT_OUTPUT_BUFFER_FULL);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Unable to send 'query command' to EC.\n"));
		return_VOID;
	}

	status = ec_io_read(ec, ec->data_port, &(ec->query_data),
		EC_EVENT_NONE);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Error reading query data.\n"));
		return_VOID;
	}

	/* TBD: un-synchronize w/ transaction (ectransx). */

	/*
	 * Spurious EC_SCI?
	 * ----------------
	 */
	if (!ec->query_data) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Spurious EC SCI detected.\n"));
		return_VOID;
	}

	/*
	 * Defer _Qxx Execution:
	 * ---------------------
	 * Can't evaluate this method now 'cause we're at interrupt-level.
	 */
	status = acpi_os_queue_for_execution(OSD_PRIORITY_GPE,
		ec_query_handler, ec);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Unable to defer _Qxx method evaluation.\n"));
		return_VOID;
	}

	return_VOID;
}


/****************************************************************************
 *
 * FUNCTION:    ec_install_gpe_handler
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
ec_install_gpe_handler (
	EC_CONTEXT              *ec)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("ec_install_gpe_handler");

	if (!ec) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Evaluate _GPE:
	 * --------------
	 * Evaluate the "_GPE" object (required) to find out which GPE bit
	 * is used by this EC to signal events (SCIs).
	 */
	status = bm_evaluate_simple_integer(ec->acpi_handle,
		"_GPE", &(ec->gpe_bit));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Install GPE Handler:
	 * --------------------
	 * Install a handler for this EC's GPE bit.
	 */
	status = acpi_install_gpe_handler(ec->gpe_bit, ACPI_EVENT_EDGE_TRIGGERED,
		&ec_gpe_handler, ec);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "acpi_install_gpe_handler() failed for GPE bit [%02x] with status [%08x].\n", ec->gpe_bit, status));
		ec->gpe_bit = EC_GPE_UNKNOWN;
		return_ACPI_STATUS(status);
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_remove_gpe_handler
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
ec_remove_gpe_handler (
	EC_CONTEXT              *ec)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("ec_remove_gpe_handler");

	if (!ec) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_remove_gpe_handler(ec->gpe_bit, &ec_gpe_handler);

	return_ACPI_STATUS(status);
}
