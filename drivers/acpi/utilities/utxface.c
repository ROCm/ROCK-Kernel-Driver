/******************************************************************************
 *
 * Module Name: utxface - External interfaces for "global" ACPI functions
 *              $Revision: 100 $
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
#include "acparser.h"
#include "acdispat.h"
#include "acdebug.h"

#define _COMPONENT          ACPI_UTILITIES
	 ACPI_MODULE_NAME    ("utxface")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_initialize_subsystem
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes all global variables.  This is the first function
 *              called, so any early initialization belongs here.
 *
 ******************************************************************************/

acpi_status
acpi_initialize_subsystem (
	void)
{
	acpi_status             status;

	ACPI_FUNCTION_TRACE ("Acpi_initialize_subsystem");


	ACPI_DEBUG_EXEC (acpi_ut_init_stack_ptr_trace ());


	/* Initialize all globals used by the subsystem */

	acpi_ut_init_globals ();

	/* Initialize the OS-Dependent layer */

	status = acpi_os_initialize ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("OSD failed to initialize, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/* Create the default mutex objects */

	status = acpi_ut_mutex_initialize ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Global mutex creation failure, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/*
	 * Initialize the namespace manager and
	 * the root of the namespace tree
	 */

	status = acpi_ns_root_initialize ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Namespace initialization failure, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}


	/* If configured, initialize the AML debugger */

	ACPI_DEBUGGER_EXEC (status = acpi_db_initialize ());

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_enable_subsystem
 *
 * PARAMETERS:  Flags           - Init/enable Options
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Completes the subsystem initialization including hardware.
 *              Puts system into ACPI mode if it isn't already.
 *
 ******************************************************************************/

acpi_status
acpi_enable_subsystem (
	u32                     flags)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_enable_subsystem");


	/*
	 * Install the default Op_region handlers. These are installed unless
	 * other handlers have already been installed via the
	 * Install_address_space_handler interface
	 */
	if (!(flags & ACPI_NO_ADDRESS_SPACE_INIT)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Init] Installing default address space handlers\n"));

		status = acpi_ev_init_address_spaces ();
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * We must initialize the hardware before we can enable ACPI.
	 * FADT values are validated here.
	 */
	if (!(flags & ACPI_NO_HARDWARE_INIT)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Init] Initializing ACPI hardware\n"));

		status = acpi_hw_initialize ();
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * Enable ACPI on this platform
	 */
	if (!(flags & ACPI_NO_ACPI_ENABLE)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Init] Going into ACPI mode\n"));

		acpi_gbl_original_mode = acpi_hw_get_mode();

		status = acpi_enable ();
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Acpi_enable failed.\n"));
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * Note:
	 * We must have the hardware AND events initialized before we can execute
	 * ANY control methods SAFELY.  Any control method can require ACPI hardware
	 * support, so the hardware MUST be initialized before execution!
	 */
	if (!(flags & ACPI_NO_EVENT_INIT)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Init] Initializing ACPI events\n"));

		status = acpi_ev_initialize ();
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Install SCI handler, Global Lock handler, GPE handlers */

	if (!(flags & ACPI_NO_HANDLER_INIT)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Init] Installing SCI/GL/GPE handlers\n"));

		status = acpi_ev_handler_initialize ();
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	return_ACPI_STATUS (status);
}

/*******************************************************************************
 *
 * FUNCTION:    Acpi_initialize_objects
 *
 * PARAMETERS:  Flags           - Init/enable Options
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Completes namespace initialization by initializing device
 *              objects and executing AML code for Regions, buffers, etc.
 *
 ******************************************************************************/

acpi_status
acpi_initialize_objects (
	u32                     flags)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_initialize_objects");

	/*
	 * Initialize all device objects in the namespace
	 * This runs the _STA and _INI methods.
	 */
	if (!(flags & ACPI_NO_DEVICE_INIT)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Init] Initializing ACPI Devices\n"));

		status = acpi_ns_initialize_devices ();
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * Initialize the objects that remain uninitialized.  This
	 * runs the executable AML that is part of the declaration of Op_regions
	 * and Fields.
	 */
	if (!(flags & ACPI_NO_OBJECT_INIT)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Init] Initializing ACPI Objects\n"));

		status = acpi_ns_initialize_objects ();
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * Empty the caches (delete the cached objects) on the assumption that
	 * the table load filled them up more than they will be at runtime --
	 * thus wasting non-paged memory.
	 */
	status = acpi_purge_cached_objects ();

	acpi_gbl_startup_flags |= ACPI_INITIALIZED_OK;
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_terminate
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Shutdown the ACPI subsystem.  Release all resources.
 *
 ******************************************************************************/

acpi_status
acpi_terminate (void)
{
	acpi_status         status;


	ACPI_FUNCTION_TRACE ("Acpi_terminate");


	/* Terminate the AML Debugger if present */

	ACPI_DEBUGGER_EXEC(acpi_gbl_db_terminate_threads = TRUE);

	/* Shutdown and free all resources */

	acpi_ut_subsystem_shutdown ();


	/* Free the mutex objects */

	acpi_ut_mutex_terminate ();


#ifdef ACPI_DEBUGGER

	/* Shut down the debugger */

	acpi_db_terminate ();
#endif

	/* Now we can shutdown the OS-dependent layer */

	status = acpi_os_terminate ();
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_subsystem_status
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status of the ACPI subsystem
 *
 * DESCRIPTION: Other drivers that use the ACPI subsystem should call this
 *              before making any other calls, to ensure the subsystem initial-
 *              ized successfully.
 *
 ****************************************************************************/

acpi_status
acpi_subsystem_status (void)
{
	if (acpi_gbl_startup_flags & ACPI_INITIALIZED_OK) {
		return (AE_OK);
	}
	else {
		return (AE_ERROR);
	}
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_get_system_info
 *
 * PARAMETERS:  Out_buffer      - a pointer to a buffer to receive the
 *                                resources for the device
 *              Buffer_length   - the number of bytes available in the buffer
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to get information about the current
 *              state of the ACPI subsystem.  It will return system information
 *              in the Out_buffer.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of Out_buffer is undefined.
 *
 ******************************************************************************/

acpi_status
acpi_get_system_info (
	acpi_buffer             *out_buffer)
{
	acpi_system_info        *info_ptr;
	u32                     i;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Acpi_get_system_info");


	/* Parameter validation */

	status = acpi_ut_validate_buffer (out_buffer);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer (out_buffer, sizeof (acpi_system_info));
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Populate the return buffer
	 */
	info_ptr = (acpi_system_info *) out_buffer->pointer;

	info_ptr->acpi_ca_version   = ACPI_CA_VERSION;

	/* System flags (ACPI capabilities) */

	info_ptr->flags             = ACPI_SYS_MODE_ACPI;

	/* Timer resolution - 24 or 32 bits  */

	if (!acpi_gbl_FADT) {
		info_ptr->timer_resolution = 0;
	}
	else if (acpi_gbl_FADT->tmr_val_ext == 0) {
		info_ptr->timer_resolution = 24;
	}
	else {
		info_ptr->timer_resolution = 32;
	}

	/* Clear the reserved fields */

	info_ptr->reserved1         = 0;
	info_ptr->reserved2         = 0;

	/* Current debug levels */

	info_ptr->debug_layer       = acpi_dbg_layer;
	info_ptr->debug_level       = acpi_dbg_level;

	/* Current status of the ACPI tables, per table type */

	info_ptr->num_table_types = NUM_ACPI_TABLES;
	for (i = 0; i < NUM_ACPI_TABLES; i++) {
		info_ptr->table_info[i].count = acpi_gbl_acpi_tables[i].count;
	}

	return_ACPI_STATUS (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_install_initialization_handler
 *
 * PARAMETERS:  Handler             - Callback procedure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install an initialization handler
 *
 * TBD: When a second function is added, must save the Function also.
 *
 ****************************************************************************/

acpi_status
acpi_install_initialization_handler (
	ACPI_INIT_HANDLER       handler,
	u32                     function)
{

	if (!handler) {
		return (AE_BAD_PARAMETER);
	}

	if (acpi_gbl_init_handler) {
		return (AE_ALREADY_EXISTS);
	}

	acpi_gbl_init_handler = handler;
	return AE_OK;
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_purge_cached_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Empty all caches (delete the cached objects)
 *
 ****************************************************************************/

acpi_status
acpi_purge_cached_objects (void)
{
	ACPI_FUNCTION_TRACE ("Acpi_purge_cached_objects");


	acpi_ut_delete_generic_state_cache ();
	acpi_ut_delete_object_cache ();
	acpi_ds_delete_walk_state_cache ();
	acpi_ps_delete_parse_cache ();

	return_ACPI_STATUS (AE_OK);
}
