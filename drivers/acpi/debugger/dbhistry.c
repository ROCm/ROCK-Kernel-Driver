/******************************************************************************
 *
 * Module Name: dbhistry - debugger HISTORY command
 *              $Revision: 19 $
 *
 *****************************************************************************/

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
	 MODULE_NAME         ("dbhistry")


#define HI_NO_HISTORY       0
#define HI_RECORD_HISTORY   1
#define HISTORY_SIZE        20


typedef struct history_info
{
	NATIVE_CHAR             command[80];
	u32                     cmd_num;

} HISTORY_INFO;


HISTORY_INFO                acpi_gbl_history_buffer[HISTORY_SIZE];
u16                         acpi_gbl_lo_history = 0;
u16                         acpi_gbl_num_history = 0;
u16                         acpi_gbl_next_history_index = 0;
u32                         acpi_gbl_next_cmd_num = 1;


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_add_to_history
 *
 * PARAMETERS:  Command_line    - Command to add
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add a command line to the history buffer.
 *
 ******************************************************************************/

void
acpi_db_add_to_history (
	NATIVE_CHAR             *command_line)
{


	/* Put command into the next available slot */

	STRCPY (acpi_gbl_history_buffer[acpi_gbl_next_history_index].command, command_line);

	acpi_gbl_history_buffer[acpi_gbl_next_history_index].cmd_num = acpi_gbl_next_cmd_num;

	/* Adjust indexes */

	if ((acpi_gbl_num_history == HISTORY_SIZE) &&
		(acpi_gbl_next_history_index == acpi_gbl_lo_history)) {
		acpi_gbl_lo_history++;
		if (acpi_gbl_lo_history >= HISTORY_SIZE) {
			acpi_gbl_lo_history = 0;
		}
	}

	acpi_gbl_next_history_index++;
	if (acpi_gbl_next_history_index >= HISTORY_SIZE) {
		acpi_gbl_next_history_index = 0;
	}


	acpi_gbl_next_cmd_num++;
	if (acpi_gbl_num_history < HISTORY_SIZE) {
		acpi_gbl_num_history++;
	}

}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_history
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the contents of the history buffer
 *
 ******************************************************************************/

void
acpi_db_display_history (void)
{
	NATIVE_UINT             i;
	u16                     history_index;


	history_index = acpi_gbl_lo_history;

	/* Dump entire history buffer */

	for (i = 0; i < acpi_gbl_num_history; i++) {
		acpi_os_printf ("%ld %s\n", acpi_gbl_history_buffer[history_index].cmd_num,
				 acpi_gbl_history_buffer[history_index].command);

		history_index++;
		if (history_index >= HISTORY_SIZE) {
			history_index = 0;
		}
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_get_from_history
 *
 * PARAMETERS:  Command_num_arg         - String containing the number of the
 *                                        command to be retrieved
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get a command from the history buffer
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_db_get_from_history (
	NATIVE_CHAR             *command_num_arg)
{
	NATIVE_UINT             i;
	u16                     history_index;
	u32                     cmd_num;


	if (command_num_arg == NULL) {
		cmd_num = acpi_gbl_next_cmd_num - 1;
	}

	else {
		cmd_num = STRTOUL (command_num_arg, NULL, 0);
	}


	/* Search history buffer */

	history_index = acpi_gbl_lo_history;
	for (i = 0; i < acpi_gbl_num_history; i++) {
		if (acpi_gbl_history_buffer[history_index].cmd_num == cmd_num) {
			/* Found the commnad, return it */

			return (acpi_gbl_history_buffer[history_index].command);
		}


		history_index++;
		if (history_index >= HISTORY_SIZE) {
			history_index = 0;
		}
	}

	acpi_os_printf ("Invalid history number: %d\n", history_index);
	return (NULL);
}


#endif /* ENABLE_DEBUGGER */

