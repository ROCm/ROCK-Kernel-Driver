/*
 *  kdbm_acpi.c - kdb debugger module interface for ACPI debugger
 *
 *  Copyright (C) 2000 Andrew Grover
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

#include <linux/types.h>
#include <linux/kdb.h>
#include <linux/module.h>

#include "acpi.h"
#include "acdebug.h"

extern int acpi_in_debugger;

static int
kdbm_acpi(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	acpi_in_debugger = 1;

	acpi_db_user_commands(DB_COMMAND_PROMPT, NULL);

	acpi_in_debugger = 0;

	return 0;
}

int
init_module(void)
{
	kdb_register("acpi", kdbm_acpi, "", "Enter ACPI debugger", 0);

	return 0;
}

void
cleanup_module(void)
{
	kdb_unregister("acpi");
}
