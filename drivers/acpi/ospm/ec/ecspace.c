/*****************************************************************************
 *
 * Module Name: ecspace.c
 *   $Revision: 23 $
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
	MODULE_NAME		("ecspace")


/****************************************************************************
 *
 * FUNCTION:    ec_space_setup
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
ec_space_setup (
	acpi_handle		region_handle,
	u32			function,
	void			*handler_context,
	void			**return_context)
{
	/*
	 * The EC object is in the handler context and is needed
	 * when calling the ec_space_handler.
	 */
	*return_context = handler_context;

	return AE_OK;
}


/****************************************************************************
 *
 * FUNCTION:    ec_space_handler
 *
 * PARAMETERS:  function            - Read or Write operation
 *              address             - Where in the space to read or write
 *              bit_width           - Field width in bits (should be 8)
 *              value               - Pointer to in or out value
 *              context             - context pointer
 *
 * RETURN:
 *
 * DESCRIPTION: Handler for the Embedded Controller (EC) address space
 *              (Op Region)
 *
 ****************************************************************************/

acpi_status
ec_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;
	EC_CONTEXT              *ec = NULL;
	EC_REQUEST              ec_request;

	FUNCTION_TRACE("ec_space_handler");

	if (address > 0xFF || bit_width != 8 || !value || !handler_context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	ec = (EC_CONTEXT*)handler_context;

	switch (function) {

	case ACPI_READ_ADR_SPACE:
		ec_request.command = EC_COMMAND_READ;
		ec_request.address = address;
		ec_request.data = 0;
		break;

	case ACPI_WRITE_ADR_SPACE:
		ec_request.command = EC_COMMAND_WRITE;
		ec_request.address = address;
		ec_request.data = (u8)(*value);
		break;

	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Received request with invalid function [%X].\n", function));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
		break;
	}

	/*
	 * Perform the Transaction.
	 */
	status = ec_transaction(ec, &ec_request);
	if (ACPI_SUCCESS(status)) {
		(*value) = (u32)ec_request.data;
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_install_space_handler
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
ec_install_space_handler (
	EC_CONTEXT              *ec)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("ec_install_space_handler");

	if (!ec) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_install_address_space_handler (ec->acpi_handle,
		ACPI_ADR_SPACE_EC, &ec_space_handler, &ec_space_setup, ec);
	
	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_remove_space_handler
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
ec_remove_space_handler (
	EC_CONTEXT              *ec)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("ec_remove_space_handler");

	if (!ec) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_remove_address_space_handler(ec->acpi_handle,
		ACPI_ADR_SPACE_EC, &ec_space_handler);

	return_ACPI_STATUS(status);
}
