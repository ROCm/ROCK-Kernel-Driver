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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include "acpi.h"
#include "acdebug.h"

extern int acpi_in_debugger;
extern FADT_DESCRIPTOR acpi_fadt;

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("symbols")

#ifdef ENABLE_DEBUGGER
EXPORT_SYMBOL(acpi_in_debugger);
EXPORT_SYMBOL(acpi_db_user_commands);
#endif

#ifdef ACPI_DEBUG
EXPORT_SYMBOL(acpi_ut_debug_print_raw);
EXPORT_SYMBOL(acpi_ut_debug_print);
EXPORT_SYMBOL(acpi_ut_status_exit);
EXPORT_SYMBOL(acpi_ut_exit);
EXPORT_SYMBOL(acpi_ut_trace);
#endif

EXPORT_SYMBOL(acpi_gbl_FADT);

EXPORT_SYMBOL(acpi_os_free);
EXPORT_SYMBOL(acpi_os_printf);
EXPORT_SYMBOL(acpi_os_callocate);
EXPORT_SYMBOL(acpi_os_sleep);
EXPORT_SYMBOL(acpi_os_stall);
EXPORT_SYMBOL(acpi_os_queue_for_execution);

EXPORT_SYMBOL(acpi_dbg_layer);
EXPORT_SYMBOL(acpi_dbg_level);

EXPORT_SYMBOL(acpi_format_exception);

EXPORT_SYMBOL(acpi_get_handle);
EXPORT_SYMBOL(acpi_get_parent);
EXPORT_SYMBOL(acpi_get_type);
EXPORT_SYMBOL(acpi_get_name);
EXPORT_SYMBOL(acpi_get_object_info);
EXPORT_SYMBOL(acpi_get_next_object);
EXPORT_SYMBOL(acpi_evaluate_object);
EXPORT_SYMBOL(acpi_get_table);

EXPORT_SYMBOL(acpi_install_notify_handler);
EXPORT_SYMBOL(acpi_remove_notify_handler);
EXPORT_SYMBOL(acpi_install_gpe_handler);
EXPORT_SYMBOL(acpi_remove_gpe_handler);
EXPORT_SYMBOL(acpi_install_address_space_handler);
EXPORT_SYMBOL(acpi_remove_address_space_handler);
EXPORT_SYMBOL(acpi_install_fixed_event_handler);
EXPORT_SYMBOL(acpi_remove_fixed_event_handler);

EXPORT_SYMBOL(acpi_acquire_global_lock);
EXPORT_SYMBOL(acpi_release_global_lock);

EXPORT_SYMBOL(acpi_get_current_resources);
EXPORT_SYMBOL(acpi_get_possible_resources);
EXPORT_SYMBOL(acpi_set_current_resources);

EXPORT_SYMBOL(acpi_enable_event);
EXPORT_SYMBOL(acpi_disable_event);
EXPORT_SYMBOL(acpi_clear_event);

EXPORT_SYMBOL(acpi_get_timer_duration);
EXPORT_SYMBOL(acpi_get_timer);

EXPORT_SYMBOL(acpi_os_signal_semaphore);
EXPORT_SYMBOL(acpi_os_create_semaphore);
EXPORT_SYMBOL(acpi_os_delete_semaphore);
EXPORT_SYMBOL(acpi_os_wait_semaphore);

EXPORT_SYMBOL(acpi_os_read_port);
EXPORT_SYMBOL(acpi_os_write_port);

EXPORT_SYMBOL(acpi_fadt);
EXPORT_SYMBOL(acpi_hw_register_bit_access);
EXPORT_SYMBOL(acpi_hw_obtain_sleep_type_register_data);
EXPORT_SYMBOL(acpi_enter_sleep_state);
EXPORT_SYMBOL(acpi_get_system_info);
EXPORT_SYMBOL(acpi_leave_sleep_state);
/*EXPORT_SYMBOL(acpi_save_state_mem);*/
/*EXPORT_SYMBOL(acpi_save_state_disk);*/
EXPORT_SYMBOL(acpi_hw_register_read);
EXPORT_SYMBOL(acpi_set_firmware_waking_vector);
EXPORT_SYMBOL(acpi_subsystem_status);

EXPORT_SYMBOL(acpi_os_signal);
