/*******************************************************************************
 *
 * Module Name: dbexec - debugger control method execution
 *              $Revision: 34 $
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
#include "acparser.h"
#include "acdispat.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acparser.h"
#include "acevents.h"
#include "acinterp.h"
#include "acdebug.h"
#include "actables.h"

#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 MODULE_NAME         ("dbexec")


db_method_info              acpi_gbl_db_method_info;


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_execute_method
 *
 * PARAMETERS:  Info            - Valid info segment
 *              Return_obj      - Where to put return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method.
 *
 ******************************************************************************/

acpi_status
acpi_db_execute_method (
	db_method_info          *info,
	acpi_buffer             *return_obj)
{
	acpi_status             status;
	acpi_object_list        param_objects;
	acpi_object             params[MTH_NUM_ARGS];
	u32                     i;


	if (acpi_gbl_db_output_to_file && !acpi_dbg_level) {
		acpi_os_printf ("Warning: debug output is not enabled!\n");
	}

	/* Are there arguments to the method? */

	if (info->args && info->args[0]) {
		for (i = 0; info->args[i] && i < MTH_NUM_ARGS; i++) {
			params[i].type              = ACPI_TYPE_INTEGER;
			params[i].integer.value     = STRTOUL (info->args[i], NULL, 16);
		}

		param_objects.pointer       = params;
		param_objects.count         = i;
	}

	else {
		/* Setup default parameters */

		params[0].type              = ACPI_TYPE_INTEGER;
		params[0].integer.value     = 0x01020304;

		params[1].type              = ACPI_TYPE_STRING;
		params[1].string.length     = 12;
		params[1].string.pointer    = "AML Debugger";

		param_objects.pointer       = params;
		param_objects.count         = 2;
	}

	/* Prepare for a return object of arbitrary size */

	return_obj->pointer          = acpi_gbl_db_buffer;
	return_obj->length           = ACPI_DEBUG_BUFFER_SIZE;


	/* Do the actual method execution */

	status = acpi_evaluate_object (NULL, info->pathname, &param_objects, return_obj);

	acpi_gbl_cm_single_step = FALSE;
	acpi_gbl_method_executing = FALSE;

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_execute_setup
 *
 * PARAMETERS:  Info            - Valid method info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup info segment prior to method execution
 *
 ******************************************************************************/

void
acpi_db_execute_setup (
	db_method_info          *info)
{

	/* Catenate the current scope to the supplied name */

	info->pathname[0] = 0;
	if ((info->name[0] != '\\') &&
		(info->name[0] != '/')) {
		STRCAT (info->pathname, acpi_gbl_db_scope_buf);
	}

	STRCAT (info->pathname, info->name);
	acpi_db_prep_namestring (info->pathname);

	acpi_db_set_output_destination (DB_DUPLICATE_OUTPUT);
	acpi_os_printf ("Executing %s\n", info->pathname);

	if (info->flags & EX_SINGLE_STEP) {
		acpi_gbl_cm_single_step = TRUE;
		acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);
	}

	else {
		/* No single step, allow redirection to a file */

		acpi_db_set_output_destination (DB_REDIRECTABLE_OUTPUT);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_get_outstanding_allocations
 *
 * PARAMETERS:  None
 *
 * RETURN:      Current global allocation count minus cache entries
 *
 * DESCRIPTION: Determine the current number of "outstanding" allocations --
 *              those allocations that have not been freed and also are not
 *              in one of the various object caches.
 *
 ******************************************************************************/

u32
acpi_db_get_outstanding_allocations (void)
{
	u32                     i;
	u32                     outstanding = 0;


#ifdef ACPI_DBG_TRACK_ALLOCATIONS

	for (i = ACPI_MEM_LIST_FIRST_CACHE_LIST; i < ACPI_NUM_MEM_LISTS; i++) {
		outstanding += (acpi_gbl_memory_lists[i].total_allocated -
				  acpi_gbl_memory_lists[i].total_freed -
				  acpi_gbl_memory_lists[i].cache_depth);
	}
#endif

	return (outstanding);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_execute
 *
 * PARAMETERS:  Name                - Name of method to execute
 *              Args                - Parameters to the method
 *              Flags               - single step/no single step
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method.  Name is relative to the current
 *              scope.
 *
 ******************************************************************************/

void
acpi_db_execute (
	NATIVE_CHAR             *name,
	NATIVE_CHAR             **args,
	u32                     flags)
{
	acpi_status             status;
	acpi_buffer             return_obj;


#ifdef ACPI_DEBUG
	u32                     previous_allocations;
	u32                     allocations;


	/* Memory allocation tracking */

	previous_allocations = acpi_db_get_outstanding_allocations ();
#endif

	acpi_gbl_db_method_info.name = name;
	acpi_gbl_db_method_info.args = args;
	acpi_gbl_db_method_info.flags = flags;

	acpi_db_execute_setup (&acpi_gbl_db_method_info);
	status = acpi_db_execute_method (&acpi_gbl_db_method_info, &return_obj);

	/*
	 * Allow any handlers in separate threads to complete.
	 * (Such as Notify handlers invoked from AML executed above).
	 */
	acpi_os_sleep (0, 10);


#ifdef ACPI_DEBUG

	/* Memory allocation tracking */

	allocations = acpi_db_get_outstanding_allocations () - previous_allocations;

	acpi_db_set_output_destination (DB_DUPLICATE_OUTPUT);

	if (allocations > 0) {
		acpi_os_printf ("Outstanding: %ld allocations after execution\n",
				  allocations);
	}
#endif

	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Execution of %s failed with status %s\n",
			acpi_gbl_db_method_info.pathname, acpi_format_exception (status));
	}

	else {
		/* Display a return object, if any */

		if (return_obj.length) {
			acpi_os_printf ("Execution of %s returned object %p Buflen %X\n",
				acpi_gbl_db_method_info.pathname, return_obj.pointer, return_obj.length);
			acpi_db_dump_object (return_obj.pointer, 1);
		}
	}

	acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_method_thread
 *
 * PARAMETERS:  Context             - Execution info segment
 *
 * RETURN:      None
 *
 * DESCRIPTION: Debugger execute thread.  Waits for a command line, then
 *              simply dispatches it.
 *
 ******************************************************************************/

void
acpi_db_method_thread (
	void                    *context)
{
	acpi_status             status;
	db_method_info          *info = context;
	u32                     i;
	acpi_buffer             return_obj;


	for (i = 0; i < info->num_loops; i++) {
		status = acpi_db_execute_method (info, &return_obj);
		if (ACPI_SUCCESS (status)) {
			if (return_obj.length) {
				acpi_os_printf ("Execution of %s returned object %p Buflen %X\n",
					info->pathname, return_obj.pointer, return_obj.length);
				acpi_db_dump_object (return_obj.pointer, 1);
			}
		}
	}

	/* Signal our completion */

	acpi_os_signal_semaphore (info->thread_gate, 1);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_create_execution_threads
 *
 * PARAMETERS:  Num_threads_arg         - Number of threads to create
 *              Num_loops_arg           - Loop count for the thread(s)
 *              Method_name_arg         - Control method to execute
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create threads to execute method(s)
 *
 ******************************************************************************/

void
acpi_db_create_execution_threads (
	NATIVE_CHAR             *num_threads_arg,
	NATIVE_CHAR             *num_loops_arg,
	NATIVE_CHAR             *method_name_arg)
{
	acpi_status             status;
	u32                     num_threads;
	u32                     num_loops;
	u32                     i;
	acpi_handle             thread_gate;


	/* Get the arguments */

	num_threads = STRTOUL (num_threads_arg, NULL, 0);
	num_loops = STRTOUL (num_loops_arg, NULL, 0);

	if (!num_threads || !num_loops) {
		acpi_os_printf ("Bad argument: Threads %X, Loops %X\n", num_threads, num_loops);
		return;
	}


	/* Create the synchronization semaphore */

	status = acpi_os_create_semaphore (1, 0, &thread_gate);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Could not create semaphore, %s\n", acpi_format_exception (status));
		return;
	}

	/* Setup the context to be passed to each thread */

	acpi_gbl_db_method_info.name = method_name_arg;
	acpi_gbl_db_method_info.args = NULL;
	acpi_gbl_db_method_info.flags = 0;
	acpi_gbl_db_method_info.num_loops = num_loops;
	acpi_gbl_db_method_info.thread_gate = thread_gate;

	acpi_db_execute_setup (&acpi_gbl_db_method_info);


	/* Create the threads */

	acpi_os_printf ("Creating %X threads to execute %X times each\n", num_threads, num_loops);

	for (i = 0; i < (num_threads); i++) {
		acpi_os_queue_for_execution (OSD_PRIORITY_MED, acpi_db_method_thread, &acpi_gbl_db_method_info);
	}


	/* Wait for all threads to complete */

	i = num_threads;
	while (i)   /* Brain damage for OSD implementations that only support wait of 1 unit */ {
		status = acpi_os_wait_semaphore (thread_gate, 1, WAIT_FOREVER);
		i--;
	}

	/* Cleanup and exit */

	acpi_os_delete_semaphore (thread_gate);

	acpi_db_set_output_destination (DB_DUPLICATE_OUTPUT);
	acpi_os_printf ("All threads (%X) have completed\n", num_threads);
	acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);
}


#endif /* ENABLE_DEBUGGER */


