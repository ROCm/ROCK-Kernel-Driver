/******************************************************************************
 *
 * Module Name: utinit - Common ACPI subsystem initialization
 *              $Revision: 113 $
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
#include "acnamesp.h"
#include "acevents.h"

#define _COMPONENT          ACPI_UTILITIES
	 ACPI_MODULE_NAME    ("utinit")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_fadt_register_error
 *
 * PARAMETERS:  *Register_name          - Pointer to string identifying register
 *              Value                   - Actual register contents value
 *              Acpi_test_spec_section  - TDS section containing assertion
 *              Acpi_assertion          - Assertion number being tested
 *
 * RETURN:      AE_BAD_VALUE
 *
 * DESCRIPTION: Display failure message and link failure to TDS assertion
 *
 ******************************************************************************/

static void
acpi_ut_fadt_register_error (
	NATIVE_CHAR             *register_name,
	u32                     value,
	ACPI_SIZE               offset)
{

	ACPI_REPORT_WARNING (
		("Invalid FADT value %s=%X at offset %X FADT=%p\n",
		register_name, value, offset, acpi_gbl_FADT));
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ut_validate_fadt
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Validate various ACPI registers in the FADT
 *
 ******************************************************************************/

acpi_status
acpi_ut_validate_fadt (
	void)
{

	/*
	 * Verify Fixed ACPI Description Table fields,
	 * but don't abort on any problems, just display error
	 */
	if (acpi_gbl_FADT->pm1_evt_len < 4) {
		acpi_ut_fadt_register_error ("PM1_EVT_LEN",
				  (u32) acpi_gbl_FADT->pm1_evt_len,
				  ACPI_FADT_OFFSET (pm1_evt_len));
	}

	if (!acpi_gbl_FADT->pm1_cnt_len) {
		acpi_ut_fadt_register_error ("PM1_CNT_LEN", 0,
				  ACPI_FADT_OFFSET (pm1_cnt_len));
	}

	if (!ACPI_VALID_ADDRESS (acpi_gbl_FADT->Xpm1a_evt_blk.address)) {
		acpi_ut_fadt_register_error ("X_PM1a_EVT_BLK", 0,
				  ACPI_FADT_OFFSET (Xpm1a_evt_blk.address));
	}

	if (!ACPI_VALID_ADDRESS (acpi_gbl_FADT->Xpm1a_cnt_blk.address)) {
		acpi_ut_fadt_register_error ("X_PM1a_CNT_BLK", 0,
				  ACPI_FADT_OFFSET (Xpm1a_cnt_blk.address));
	}

	if (!ACPI_VALID_ADDRESS (acpi_gbl_FADT->Xpm_tmr_blk.address)) {
		acpi_ut_fadt_register_error ("X_PM_TMR_BLK", 0,
				  ACPI_FADT_OFFSET (Xpm_tmr_blk.address));
	}

	if ((ACPI_VALID_ADDRESS (acpi_gbl_FADT->Xpm2_cnt_blk.address) &&
		!acpi_gbl_FADT->pm2_cnt_len)) {
		acpi_ut_fadt_register_error ("PM2_CNT_LEN",
				  (u32) acpi_gbl_FADT->pm2_cnt_len,
				  ACPI_FADT_OFFSET (pm2_cnt_len));
	}

	if (acpi_gbl_FADT->pm_tm_len < 4) {
		acpi_ut_fadt_register_error ("PM_TM_LEN",
				  (u32) acpi_gbl_FADT->pm_tm_len,
				  ACPI_FADT_OFFSET (pm_tm_len));
	}

	/* Length of GPE blocks must be a multiple of 2 */

	if (ACPI_VALID_ADDRESS (acpi_gbl_FADT->Xgpe0_blk.address) &&
		(acpi_gbl_FADT->gpe0_blk_len & 1)) {
		acpi_ut_fadt_register_error ("(x)GPE0_BLK_LEN",
				  (u32) acpi_gbl_FADT->gpe0_blk_len,
				  ACPI_FADT_OFFSET (gpe0_blk_len));
	}

	if (ACPI_VALID_ADDRESS (acpi_gbl_FADT->Xgpe1_blk.address) &&
		(acpi_gbl_FADT->gpe1_blk_len & 1)) {
		acpi_ut_fadt_register_error ("(x)GPE1_BLK_LEN",
				  (u32) acpi_gbl_FADT->gpe1_blk_len,
				  ACPI_FADT_OFFSET (gpe1_blk_len));
	}

	return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ut_terminate
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: free memory allocated for table storage.
 *
 ******************************************************************************/

void
acpi_ut_terminate (void)
{

	ACPI_FUNCTION_TRACE ("Ut_terminate");


	/* Free global tables, etc. */

	/* Nothing to do at this time */

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_subsystem_shutdown
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Shutdown the various subsystems.  Don't delete the mutex
 *              objects here -- because the AML debugger may be still running.
 *
 ******************************************************************************/

void
acpi_ut_subsystem_shutdown (void)
{

	ACPI_FUNCTION_TRACE ("Ut_subsystem_shutdown");

	/* Just exit if subsystem is already shutdown */

	if (acpi_gbl_shutdown) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "ACPI Subsystem is already terminated\n"));
		return_VOID;
	}

	/* Subsystem appears active, go ahead and shut it down */

	acpi_gbl_shutdown = TRUE;
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Shutting down ACPI Subsystem...\n"));

	/* Close the Acpi_event Handling */

	acpi_ev_terminate ();

	/* Close the Namespace */

	acpi_ns_terminate ();

	/* Close the globals */

	acpi_ut_terminate ();

	/* Purge the local caches */

	(void) acpi_purge_cached_objects ();

	/* Debug only - display leftover memory allocation, if any */

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	acpi_ut_dump_allocations (ACPI_UINT32_MAX, NULL);
#endif

	return_VOID;
}


