/*******************************************************************************
 *
 * Module Name: dbfileio - Debugger file I/O commands.  These can't usually
 *              be used when running the debugger in Ring 0 (Kernel mode)
 *              $Revision: 53 $
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
#include "acdebug.h"
#include "acnamesp.h"
#include "acparser.h"
#include "acevents.h"
#include "actables.h"

#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 MODULE_NAME         ("dbfileio")


/*
 * NOTE: this is here for lack of a better place.  It is used in all
 * flavors of the debugger, need LCD file
 */
#ifdef ACPI_APPLICATION
#include <stdio.h>
FILE                        *acpi_gbl_debug_file = NULL;
#endif


acpi_table_header           *acpi_gbl_db_table_ptr = NULL;


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_match_argument
 *
 * PARAMETERS:  User_argument           - User command line
 *              Arguments               - Array of commands to match against
 *
 * RETURN:      Index into command array or ACPI_TYPE_NOT_FOUND if not found
 *
 * DESCRIPTION: Search command array for a command match
 *
 ******************************************************************************/

acpi_object_type8
acpi_db_match_argument (
	NATIVE_CHAR             *user_argument,
	ARGUMENT_INFO           *arguments)
{
	u32                     i;


	if (!user_argument || user_argument[0] == 0) {
		return (ACPI_TYPE_NOT_FOUND);
	}

	for (i = 0; arguments[i].name; i++) {
		if (STRSTR (arguments[i].name, user_argument) == arguments[i].name) {
			return ((acpi_object_type8) i);
		}
	}

	/* Argument not recognized */

	return (ACPI_TYPE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_close_debug_file
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: If open, close the current debug output file
 *
 ******************************************************************************/

void
acpi_db_close_debug_file (
	void)
{

#ifdef ACPI_APPLICATION

	if (acpi_gbl_debug_file) {
	   fclose (acpi_gbl_debug_file);
	   acpi_gbl_debug_file = NULL;
	   acpi_gbl_db_output_to_file = FALSE;
	   acpi_os_printf ("Debug output file %s closed\n", acpi_gbl_db_debug_filename);
	}
#endif

}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_open_debug_file
 *
 * PARAMETERS:  Name                - Filename to open
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Open a file where debug output will be directed.
 *
 ******************************************************************************/

void
acpi_db_open_debug_file (
	NATIVE_CHAR             *name)
{

#ifdef ACPI_APPLICATION

	acpi_db_close_debug_file ();
	acpi_gbl_debug_file = fopen (name, "w+");
	if (acpi_gbl_debug_file) {
		acpi_os_printf ("Debug output file %s opened\n", name);
		STRCPY (acpi_gbl_db_debug_filename, name);
		acpi_gbl_db_output_to_file = TRUE;
	}
	else {
		acpi_os_printf ("Could not open debug file %s\n", name);
	}

#endif
}


#ifdef ACPI_APPLICATION
/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_load_table
 *
 * PARAMETERS:  fp              - File that contains table
 *              Table_ptr       - Return value, buffer with table
 *              Table_lenght    - Return value, length of table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load the DSDT from the file pointer
 *
 ******************************************************************************/

acpi_status
acpi_db_load_table(
	FILE                    *fp,
	acpi_table_header       **table_ptr,
	u32                     *table_length)
{
	acpi_table_header       table_header;
	u8                      *aml_start;
	u32                     aml_length;
	u32                     actual;
	acpi_status             status;


	/* Read the table header */

	if (fread (&table_header, 1, sizeof (table_header), fp) != sizeof (acpi_table_header)) {
		acpi_os_printf ("Couldn't read the table header\n");
		return (AE_BAD_SIGNATURE);
	}


	/* Validate the table header/length */

	status = acpi_tb_validate_table_header (&table_header);
	if ((ACPI_FAILURE (status)) ||
		(table_header.length > 524288)) /* 1/2 Mbyte should be enough */ {
		acpi_os_printf ("Table header is invalid!\n");
		return (AE_ERROR);
	}


	/* We only support a limited number of table types */

	if (STRNCMP ((char *) table_header.signature, DSDT_SIG, 4) &&
		STRNCMP ((char *) table_header.signature, PSDT_SIG, 4) &&
		STRNCMP ((char *) table_header.signature, SSDT_SIG, 4)) {
		acpi_os_printf ("Table signature is invalid\n");
		DUMP_BUFFER (&table_header, sizeof (acpi_table_header));
		return (AE_ERROR);
	}

	/* Allocate a buffer for the table */

	*table_length = table_header.length;
	*table_ptr = acpi_os_allocate ((size_t) *table_length);
	if (!*table_ptr) {
		acpi_os_printf ("Could not allocate memory for ACPI table %4.4s (size=%X)\n",
				 table_header.signature, table_header.length);
		return (AE_NO_MEMORY);
	}


	aml_start = (u8 *) *table_ptr + sizeof (table_header);
	aml_length = *table_length - sizeof (table_header);

	/* Copy the header to the buffer */

	MEMCPY (*table_ptr, &table_header, sizeof (table_header));

	/* Get the rest of the table */

	actual = fread (aml_start, 1, (size_t) aml_length, fp);
	if (actual == aml_length) {
		return (AE_OK);
	}

	if (actual > 0) {
		acpi_os_printf ("Warning - reading table, asked for %X got %X\n", aml_length, actual);
		return (AE_OK);
	}


	acpi_os_printf ("Error - could not read the table file\n");
	acpi_os_free (*table_ptr);
	*table_ptr = NULL;
	*table_length = 0;

	return (AE_ERROR);
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    Ae_local_load_table
 *
 * PARAMETERS:  Table_ptr       - pointer to a buffer containing the entire
 *                                table to be loaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to load a table from the caller's
 *              buffer.  The buffer must contain an entire ACPI Table including
 *              a valid header.  The header fields will be verified, and if it
 *              is determined that the table is invalid, the call will fail.
 *
 *              If the call fails an appropriate status will be returned.
 *
 ******************************************************************************/

acpi_status
ae_local_load_table (
	acpi_table_header       *table_ptr)
{
	acpi_status             status;
	acpi_table_desc         table_info;


	FUNCTION_TRACE ("Ae_local_load_table");

	if (!table_ptr) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Install the new table into the local data structures */

	table_info.pointer = table_ptr;

	status = acpi_tb_install_table (NULL, &table_info);
	if (ACPI_FAILURE (status)) {
		/* Free table allocated by Acpi_tb_get_table */

		acpi_tb_delete_single_table (&table_info);
		return_ACPI_STATUS (status);
	}


#ifndef PARSER_ONLY
	status = acpi_ns_load_table (table_info.installed_desc, acpi_gbl_root_node);
	if (ACPI_FAILURE (status)) {
		/* Uninstall table and free the buffer */

		acpi_tb_delete_acpi_table (ACPI_TABLE_DSDT);
		return_ACPI_STATUS (status);
	}
#endif

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_load_acpi_table
 *
 * PARAMETERS:  Filname         - File where table is located
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table from a file
 *
 ******************************************************************************/

acpi_status
acpi_db_load_acpi_table (
	NATIVE_CHAR             *filename)
{
#ifdef ACPI_APPLICATION
	FILE                    *fp;
	acpi_status             status;
	u32                     table_length;


	/* Open the file */

	fp = fopen (filename, "rb");
	if (!fp) {
		acpi_os_printf ("Could not open file %s\n", filename);
		return (AE_ERROR);
	}


	/* Get the entire file */

	acpi_os_printf ("Loading Acpi table from file %s\n", filename);
	status = acpi_db_load_table (fp, &acpi_gbl_db_table_ptr, &table_length);
	fclose(fp);

	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Couldn't get table from the file\n");
		return (status);
	}

	/* Attempt to recognize and install the table */

	status = ae_local_load_table (acpi_gbl_db_table_ptr);
	if (ACPI_FAILURE (status)) {
		if (status == AE_EXIST) {
			acpi_os_printf ("Table %4.4s is already installed\n",
					  &acpi_gbl_db_table_ptr->signature);
		}
		else {
			acpi_os_printf ("Could not install table, %s\n",
					  acpi_format_exception (status));
		}

		acpi_os_free (acpi_gbl_db_table_ptr);
		return (status);
	}

	acpi_os_printf ("%4.4s at %p successfully installed and loaded\n",
			  &acpi_gbl_db_table_ptr->signature, acpi_gbl_db_table_ptr);

	acpi_gbl_acpi_hardware_present = FALSE;

#endif  /* ACPI_APPLICATION */
	return (AE_OK);
}


#endif  /* ENABLE_DEBUGGER */

