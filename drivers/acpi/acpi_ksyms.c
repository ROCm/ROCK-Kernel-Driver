/*
 *  ksyms.c - ACPI exported symbols
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include "acpi.h"
#include "acdebug.h"

extern int acpi_in_debugger;

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("symbols")

#ifdef ENABLE_DEBUGGER
EXPORT_SYMBOL(acpi_in_debugger);
EXPORT_SYMBOL(acpi_db_user_commands);
#endif

EXPORT_SYMBOL(acpi_os_free);
EXPORT_SYMBOL(acpi_os_breakpoint);
EXPORT_SYMBOL(acpi_os_printf);
EXPORT_SYMBOL(acpi_os_callocate);
EXPORT_SYMBOL(acpi_os_sleep);
EXPORT_SYMBOL(acpi_os_sleep_usec);
EXPORT_SYMBOL(acpi_os_in8);
EXPORT_SYMBOL(acpi_os_out8);
EXPORT_SYMBOL(acpi_os_queue_for_execution);

EXPORT_SYMBOL(acpi_dbg_layer);
EXPORT_SYMBOL(acpi_dbg_level);
EXPORT_SYMBOL(function_exit);
EXPORT_SYMBOL(function_trace);
EXPORT_SYMBOL(function_status_exit);
EXPORT_SYMBOL(function_value_exit);
EXPORT_SYMBOL(debug_print_raw);
EXPORT_SYMBOL(debug_print_prefix);

EXPORT_SYMBOL(acpi_cm_strncmp);
EXPORT_SYMBOL(acpi_cm_memcpy);
EXPORT_SYMBOL(acpi_cm_memset);

EXPORT_SYMBOL(acpi_get_handle);
EXPORT_SYMBOL(acpi_get_parent);
EXPORT_SYMBOL(acpi_get_type);
EXPORT_SYMBOL(acpi_get_name);
EXPORT_SYMBOL(acpi_get_object_info);
EXPORT_SYMBOL(acpi_get_next_object);
EXPORT_SYMBOL(acpi_evaluate_object);

EXPORT_SYMBOL(acpi_install_notify_handler);
EXPORT_SYMBOL(acpi_remove_notify_handler);
EXPORT_SYMBOL(acpi_install_gpe_handler);
EXPORT_SYMBOL(acpi_remove_gpe_handler);
EXPORT_SYMBOL(acpi_install_address_space_handler);
EXPORT_SYMBOL(acpi_remove_address_space_handler);

EXPORT_SYMBOL(acpi_get_current_resources);
EXPORT_SYMBOL(acpi_get_possible_resources);
EXPORT_SYMBOL(acpi_set_current_resources);

EXPORT_SYMBOL(acpi_enable_event);
EXPORT_SYMBOL(acpi_disable_event);
EXPORT_SYMBOL(acpi_clear_event);

EXPORT_SYMBOL(acpi_get_processor_throttling_info);
EXPORT_SYMBOL(acpi_get_processor_throttling_state);
EXPORT_SYMBOL(acpi_set_processor_throttling_state);

EXPORT_SYMBOL(acpi_get_processor_cx_info);
EXPORT_SYMBOL(acpi_set_processor_sleep_state);
EXPORT_SYMBOL(acpi_processor_sleep);
