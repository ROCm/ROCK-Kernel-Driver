/*
 *  ec.c - Embedded controller support
 *
 *  Copyright (C) 2000 Andrew Henroid
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

#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include "acpi.h"
#include "driver.h"
#include "ec.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("ec")

#define ACPI_EC_HID	"PNP0C09"

enum
{
	ACPI_EC_SMI = 0x40,
	ACPI_EC_SCI = 0x20,
	ACPI_EC_BURST = 0x10,
	ACPI_EC_CMD = 0x08,
	ACPI_EC_IBF = 0x02,
	ACPI_EC_OBF = 0x01
};

enum
{
	ACPI_EC_READ = 0x80,
	ACPI_EC_WRITE = 0x81,
	ACPI_EC_BURST_ENABLE = 0x82,
	ACPI_EC_BURST_DISABLE = 0x83,
	ACPI_EC_QUERY = 0x84,
};

typedef struct
{
	ACPI_HANDLE		acpi_handle;
	u32			gpe_bit;
	ACPI_IO_ADDRESS 	status_port;
	ACPI_IO_ADDRESS 	data_port;
	u32			need_global_lock;
} ec_context_t;


typedef struct 
{
    ec_context_t	*ec;
    u8			data;

} EC_QUERY_DATA;

static char object_name[] = {'_', 'Q', '0', '0', '\0'};

static char hex[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};


static ACPI_STATUS
ec_io_wait (
    ec_context_t              *ec,
    EC_EVENT                wait_event)
{
    EC_STATUS               ec_status = 0;
    UINT32                  i = 100;

    if (!ec || ((wait_event != EC_EVENT_OUTPUT_BUFFER_FULL) 
        && (wait_event != EC_EVENT_INPUT_BUFFER_EMPTY)))
        return(AE_BAD_PARAMETER);

    /* 
     * Wait for Event:
     * ---------------
     * Poll the EC status register waiting for the event to occur.
     * Note that we'll wait a maximum of 1ms in 10us chunks.
     */
    switch (wait_event) {
    case EC_EVENT_OUTPUT_BUFFER_FULL:
        do {
            ec_status = acpi_os_in8(ec->status_port);
            if (ec_status & EC_FLAG_OUTPUT_BUFFER)
                return(AE_OK);
            acpi_os_sleep_usec(10);
        } while (--i>0);
        break;
    case EC_EVENT_INPUT_BUFFER_EMPTY:
        do {
            ec_status = acpi_os_in8(ec->status_port);
            if (!(ec_status & EC_FLAG_INPUT_BUFFER))
                return(AE_OK);
            acpi_os_sleep_usec(10);
        } while (--i>0);
        break;
    }

    return(AE_TIME);
}

static ACPI_STATUS
ec_io_read (
    ec_context_t              *ec,
    ACPI_IO_ADDRESS         io_port,
    UINT8                   *data,
    EC_EVENT                wait_event)
{
    ACPI_STATUS             status = AE_OK;

    if (!ec || !data)
        return(AE_BAD_PARAMETER);

    *data = acpi_os_in8(io_port);

    if (wait_event)
        status = ec_io_wait(ec, wait_event);

    return(status);
}

static ACPI_STATUS
ec_io_write (
    ec_context_t              *ec,
    ACPI_IO_ADDRESS         io_port,
    UINT8                   data,
    EC_EVENT                wait_event)
{
    ACPI_STATUS             status = AE_OK;

    if (!ec)
        return(AE_BAD_PARAMETER);

    acpi_os_out8(io_port, data);

    if (wait_event)
        status = ec_io_wait(ec, wait_event);

    return(status);
}

static ACPI_STATUS
ec_read (
    ec_context_t              *ec,
    UINT8                   address,
    UINT8                   *data)
{
    ACPI_STATUS             status = AE_OK;

    FUNCTION_TRACE("ec_read");

    if (!ec || !data)
        return_ACPI_STATUS(AE_BAD_PARAMETER);

    status = ec_io_write(ec, ec->status_port, EC_COMMAND_READ, EC_EVENT_INPUT_BUFFER_EMPTY);
    if (ACPI_FAILURE(status)) {
        DEBUG_PRINT(ACPI_WARN, ("Unable to send 'read command' to EC.\n"));
        return_ACPI_STATUS(status);
    }

    status = ec_io_write(ec, ec->data_port, address, EC_EVENT_OUTPUT_BUFFER_FULL);
    if (ACPI_FAILURE(status)) {
        DEBUG_PRINT(ACPI_WARN, ("Unable to send 'read address' to EC.\n"));
        return_ACPI_STATUS(status);
    }

    status = ec_io_read(ec, ec->data_port, data, EC_EVENT_NONE);

    DEBUG_PRINT(ACPI_INFO, ("Read data[0x%02x] from address[0x%02x] on ec.\n", (*data), address));

    return_ACPI_STATUS(status);
}

static ACPI_STATUS
ec_write (
    ec_context_t              *ec,
    UINT8                   address,
    UINT8                   data)
{
    ACPI_STATUS             status = AE_OK;

    FUNCTION_TRACE("ec_write");

    if (!ec)
        return_ACPI_STATUS(AE_BAD_PARAMETER);

    status = ec_io_write(ec, ec->status_port, EC_COMMAND_WRITE, EC_EVENT_INPUT_BUFFER_EMPTY);
    if (ACPI_FAILURE(status)) {
        DEBUG_PRINT(ACPI_WARN, ("Unable to send 'write command' to EC.\n"));
        return_ACPI_STATUS(status);
    }

    status = ec_io_write(ec, ec->data_port, address, EC_EVENT_INPUT_BUFFER_EMPTY);
    if (ACPI_FAILURE(status)) {
        DEBUG_PRINT(ACPI_WARN, ("Unable to send 'write address' to EC.\n"));
        return_ACPI_STATUS(status);
    }

    status = ec_io_write(ec, ec->data_port, data, EC_EVENT_INPUT_BUFFER_EMPTY);
    if (ACPI_FAILURE(status)) {
        DEBUG_PRINT(ACPI_WARN, ("Unable to send 'write data' to EC.\n"));
        return_ACPI_STATUS(status);
    }

    DEBUG_PRINT(ACPI_INFO, ("Wrote data[0x%02x] to address[0x%02x] on ec.\n", data, address));

    return_ACPI_STATUS(status);
}

static ACPI_STATUS
ec_transaction (
    ec_context_t              *ec,
    EC_REQUEST              *request)
{
    ACPI_STATUS             status = AE_OK;

    FUNCTION_TRACE("ec_transaction");

    if (!ec || !request)
        return_ACPI_STATUS(AE_BAD_PARAMETER);

    /*
     * Obtaining semaphore (mutex) to serialize all EC transactions.
     */
    /*
    DEBUG_PRINT(ACPI_INFO, ("Calling acpi_os_wait_semaphore(%p, 1, %d)\n", ec->mutex, EC_DEFAULT_TIMEOUT));
    status = acpi_os_wait_semaphore(ec->mutex, 1, EC_DEFAULT_TIMEOUT);
    if (ACPI_FAILURE(status))
        return_ACPI_STATUS(status);
    */

    /*
     * Perform the transaction.
     */
    switch (request->command) {

    case EC_COMMAND_READ:
        status = ec_read(ec, request->address, &(request->data));
        break;

    case EC_COMMAND_WRITE:
        status = ec_write(ec, request->address, request->data);
        break;

    default:
        status = AE_SUPPORT;
        break;
    }

    /*
     * Signal the semaphore (mutex) to indicate transaction completion.
     */
    /*
    DEBUG_PRINT(ACPI_INFO, ("Calling acpi_os_signal_semaphore(%p, 1)\n", ec->mutex));
    acpi_os_signal_semaphore(ec->mutex, 1);
    */

    return_ACPI_STATUS(status);
}

static ACPI_STATUS 
ec_space_setup (
    ACPI_HANDLE                 region_handle,
    UINT32                      function,
    void                        *handler_context,
    void                        **return_context)
{
	// TODO: What is this function for?
	/* 
	 * The ec object is in the handler context and is needed
	 * when calling the ec_space_handler.
	 */
	*return_context = handler_context;

    return AE_OK;
}




static void
ec_query_handler (
    void                    *context)
{
    ACPI_STATUS             status = AE_OK;
    EC_QUERY_DATA           *ec_q = (EC_QUERY_DATA*)context;

    FUNCTION_TRACE("ec_query_handler");

    if (!ec_q || !ec_q->ec) {
        DEBUG_PRINT(ACPI_ERROR, ("Invalid (NULL) context.\n"));
        return_VOID;
    }

    /*
     * Evaluate _Qxx:
     * --------------
     * Evaluate corresponding _Qxx method.  Note that a zero query
     * value indicates a spurious EC_SCI (no such thing as _Q00).
     */
    object_name[2] = hex[((ec_q->data >> 4) & 0x0F)];
    object_name[3] = hex[(ec_q->data & 0x0F)];

    DEBUG_PRINT(ACPI_INFO, ("Read query data[0x%02x] from ec - evaluating [%s].\n", ec_q->data, object_name));

    status = acpi_evaluate_object(ec_q->ec->acpi_handle, object_name, NULL, NULL);

    kfree(ec_q);

    return_VOID;
}

/*
 * handle GPE
 */
static void
ec_gpe_handler(void *context)
{
    ACPI_STATUS             status = AE_OK;
    ec_context_t            *ec = (ec_context_t *) context;
    EC_QUERY_DATA           *ec_q = NULL;
    EC_STATUS               ec_status = 0;

    FUNCTION_TRACE("ec_gpe_handler");

    if (!ec) {
        DEBUG_PRINT(ACPI_INFO, ("Invalid (NULL) context.\n"));
        return_VOID;
    }

    // GET SPINLOCK!

    /*
     * EC_SCI?
     * -------
     * Check the EC_SCI bit to see if this is an EC_SCI event.  If not (e.g.
     * OBF/IBE) just return, as we already poll to detect these events.
     */
    ec_status = acpi_os_in8(ec->status_port);
    DEBUG_PRINT(ACPI_INFO, ("EC Status Register: [0x%02x]\n", ec_status));
    if (!(ec_status & EC_FLAG_SCI))
        return_VOID;

    DEBUG_PRINT(ACPI_INFO, ("EC_SCI detected - running QUERY.\n"));

    // TODO: Need GFP_ATOMIC 'switch' for OSL interface...
    ec_q = kmalloc(sizeof(EC_QUERY_DATA), GFP_ATOMIC);
    if (!ec_q) {
        DEBUG_PRINT(ACPI_INFO, ("Memory allocation failure.\n"));
        return_VOID;
    }

    ec_q->ec = ec;
    ec_q->data = 0;

    /*
     * Run Query:
     * ----------
     * Query the EC to find out which _Qxx method we need to evaluate.
     * Note that successful completion of the query causes the EC_SCI
     * bit to be cleared (and thus clearing the interrupt source).
     */
    status = ec_io_write(ec, ec->status_port, EC_COMMAND_QUERY, EC_EVENT_OUTPUT_BUFFER_FULL);
    if (ACPI_FAILURE(status)) {
        DEBUG_PRINT(ACPI_WARN, ("Unable to send 'query command' to EC.\n"));
        goto End;
    }

    status = ec_io_read(ec, ec->data_port, &(ec_q->data), EC_EVENT_NONE);
    if (ACPI_FAILURE(status)) {
        DEBUG_PRINT(ACPI_WARN, ("Error reading query data.\n"));
        goto End;
    }

    // RELEASE SPINLOCK!

    if (!ec_q->data) {
        DEBUG_PRINT(ACPI_WARN, ("Spurious EC SCI detected.\n"));
        status = AE_ERROR;
        goto End;
    }

    /*
     * Defer _Qxx Execution:
     * ---------------------
     * Can't evaluate this method now 'cause we're at interrupt-level.
     */
    status = acpi_os_queue_for_execution(OSD_PRIORITY_GPE, ec_query_handler, ec_q);
    if (ACPI_FAILURE(status)) {
        DEBUG_PRINT(ACPI_ERROR, ("Unable to defer _Qxx method evaluation.\n"));
        goto End;
    }

End:
    if (ACPI_FAILURE(status))
        kfree(ec_q);
    
    return_VOID;
}

static ACPI_STATUS
ec_region_setup (
    ACPI_HANDLE handle,
    u32 function,
    void *handler_context,
    void **region_context)
{
	FUNCTION_TRACE("acpi_ec_region_setup");

	printk("acpi_ec_region_setup\n");

	if (function == ACPI_REGION_DEACTIVATE)
	{
		if (*region_context)
		{
			acpi_cm_free (*region_context);
			*region_context = NULL;
		}

		return_ACPI_STATUS (AE_OK);
	}

	*region_context = NULL;

	return_ACPI_STATUS (AE_OK);
}

/*****************************************************************************
 * 
 * FUNCTION:    ec_region_handler
 *
 * PARAMETERS:  function            - Read or Write operation
 *              address             - Where in the space to read or write
 *              bit_width            - Field width in bits (8, 16, or 32)
 *              value               - Pointer to in or out value
 *              context             - context pointer
 *
 * RETURN:      <TBD>
 *
 * DESCRIPTION: Handler for the Embedded Controller (EC) address space 
 *              (Op Region)
 *
 ****************************************************************************/

static ACPI_STATUS
ec_region_handler (
    UINT32                  function,
    ACPI_PHYSICAL_ADDRESS   address,
    UINT32                  bit_width,
    UINT32                  *value,
    void                    *handler_context,
    void                    *region_context)
{
    ACPI_STATUS             status = AE_OK;
    ec_context_t              *ec = NULL;
    EC_REQUEST              ec_request;

    FUNCTION_TRACE("ec_space_handler");

    if (address > 0xFF || bit_width != 8 || !value || !handler_context)
        return_ACPI_STATUS(AE_BAD_PARAMETER);

    ec = (ec_context_t*)handler_context;

    switch (function) {

    case ADDRESS_SPACE_READ:
        ec_request.command = EC_COMMAND_READ;
        ec_request.address = address;
        ec_request.data = 0;
        break;

    case ADDRESS_SPACE_WRITE:
        ec_request.command = EC_COMMAND_WRITE;
        ec_request.address = address;
        ec_request.data = (UINT8)(*value);
        break;

    default:
        DEBUG_PRINT(ACPI_WARN, ("Received request with invalid function [0x%08X].\n", function));
        return_ACPI_STATUS(AE_BAD_PARAMETER);
        break;
    }

    DEBUG_PRINT(ACPI_INFO, ("device[ec] command[0x%02X] address[0x%02X] data[0x%02X]\n", ec_request.command, ec_request.address, ec_request.data));

    /*
     * Perform the Transaction.
     */
    status = ec_transaction(ec, &ec_request);
    if (ACPI_SUCCESS(status))
        (*value) = (UINT32)ec_request.data;

    return_ACPI_STATUS(status);
}

/*
 * Get Embedded Controller information
 */
static ACPI_STATUS
found_ec(
	ACPI_HANDLE handle,
	u32 level, 
	void *ctx, 
	void **value)
{
	ACPI_STATUS status;
	ACPI_OBJECT obj;
	ACPI_BUFFER buf;
	RESOURCE *res;
	ec_context_t *ec_cxt;

	buf.length = 0;
	buf.pointer = NULL;
	if (acpi_get_current_resources(handle, &buf) != AE_BUFFER_OVERFLOW)
		return AE_OK;

	buf.pointer = kmalloc(buf.length, GFP_KERNEL);
	if (!buf.pointer)
		return AE_NO_MEMORY;

	if (!ACPI_SUCCESS(acpi_get_current_resources(handle, &buf))) {
		kfree(buf.pointer);
		return AE_OK;
	}

	ec_cxt = kmalloc(sizeof(ec_context_t), GFP_KERNEL);
	if (!ec_cxt) {
		kfree(buf.pointer);
		return AE_NO_MEMORY;
	}

	ec_cxt->acpi_handle = handle;

	res = (RESOURCE*) buf.pointer;
	ec_cxt->data_port = res->data.io.min_base_address;
	res = NEXT_RESOURCE(res);
	ec_cxt->status_port = (int) res->data.io.min_base_address;

	kfree(buf.pointer);

	/* determine GPE bit */
	/* BUG: in acpi 2.0 this could return a package */
	buf.length = sizeof(obj);
	buf.pointer = &obj;
	if (!ACPI_SUCCESS(acpi_evaluate_object(handle, "_GPE", NULL, &buf))
		|| obj.type != ACPI_TYPE_NUMBER)
		return AE_OK;

	ec_cxt->gpe_bit = obj.number.value;

	/* determine if we need the Global Lock when accessing */
	buf.length = sizeof(obj);
	buf.pointer = &obj;

	status = acpi_evaluate_object(handle, "_GLK", NULL, &buf);
	if (status == AE_NOT_FOUND)
		ec_cxt->need_global_lock = 0;
	else if (!ACPI_SUCCESS(status) || obj.type != ACPI_TYPE_NUMBER) {
		DEBUG_PRINT(ACPI_ERROR, ("_GLK failed\n"));
		return AE_OK;
	}

	ec_cxt->need_global_lock = obj.number.value;

	printk(KERN_INFO "ACPI: found EC @ (0x%02x,0x%02x,gpe %d GL %d)\n",
		ec_cxt->data_port, ec_cxt->status_port, ec_cxt->gpe_bit,
		ec_cxt->need_global_lock);

	if (!ACPI_SUCCESS(acpi_install_gpe_handler(
		ec_cxt->gpe_bit,
		ACPI_EVENT_EDGE_TRIGGERED,
		ec_gpe_handler,
		ec_cxt))) {
		
		REPORT_ERROR(("Could not install GPE handler for EC.\n"));
		return AE_OK;
	}

	status = acpi_install_address_space_handler (handle, ADDRESS_SPACE_EC, 
		    ec_region_handler, ec_region_setup, ec_cxt);

	if (!ACPI_SUCCESS(status)) {
		REPORT_ERROR(("Could not install EC address "
			"space handler, error %s\n", acpi_cm_format_exception (status)));
	}
	
	return AE_OK;
}

int
acpi_ec_init(void)
{
	acpi_get_devices(ACPI_EC_HID, 
			found_ec,
			NULL,
			NULL);

	return 0;
}

int
acpi_ec_terminate(void)
{
	/* TODO */	
	/* walk list of EC's */
	/* free their context and release resources */
	return 0;
}
