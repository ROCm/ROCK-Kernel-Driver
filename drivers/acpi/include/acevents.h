/******************************************************************************
 *
 * Name: acevents.h - Event subcomponent prototypes and defines
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

#ifndef __ACEVENTS_H__
#define __ACEVENTS_H__


acpi_status
acpi_ev_initialize (
	void);

acpi_status
acpi_ev_handler_initialize (
	void);


/*
 * Evfixed - Fixed event handling
 */

acpi_status
acpi_ev_fixed_event_initialize (
	void);

u32
acpi_ev_fixed_event_detect (
	void);

u32
acpi_ev_fixed_event_dispatch (
	u32                     event);


/*
 * Evmisc
 */

u8
acpi_ev_is_notify_object (
	acpi_namespace_node     *node);

acpi_status
acpi_ev_acquire_global_lock(
	u16                     timeout);

acpi_status
acpi_ev_release_global_lock(
	void);

acpi_status
acpi_ev_init_global_lock_handler (
	void);

u32
acpi_ev_get_gpe_register_index (
	u32                     gpe_number);

u32
acpi_ev_get_gpe_number_index (
	u32                     gpe_number);

acpi_status
acpi_ev_queue_notify_request (
	acpi_namespace_node     *node,
	u32                     notify_value);

void ACPI_SYSTEM_XFACE
acpi_ev_notify_dispatch (
	void                    *context);


/*
 * Evgpe - GPE handling and dispatch
 */

acpi_status
acpi_ev_gpe_initialize (
	void);

acpi_status
acpi_ev_init_gpe_control_methods (
	void);

u32
acpi_ev_gpe_dispatch (
	u32                     gpe_number);

u32
acpi_ev_gpe_detect (
	void);

/*
 * Evregion - Address Space handling
 */

acpi_status
acpi_ev_init_address_spaces (
	void);

acpi_status
acpi_ev_address_space_dispatch (
	acpi_operand_object    *region_obj,
	u32                     function,
	acpi_physical_address   address,
	u32                     bit_width,
	void                    *value);

acpi_status
acpi_ev_addr_handler_helper (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value);

acpi_status
acpi_ev_attach_region (
	acpi_operand_object     *handler_obj,
	acpi_operand_object     *region_obj,
	u8                      acpi_ns_is_locked);

void
acpi_ev_detach_region (
	acpi_operand_object    *region_obj,
	u8                      acpi_ns_is_locked);


/*
 * Evregini - Region initialization and setup
 */

acpi_status
acpi_ev_system_memory_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context);

acpi_status
acpi_ev_io_space_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context);

acpi_status
acpi_ev_pci_config_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context);

acpi_status
acpi_ev_cmos_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context);

acpi_status
acpi_ev_pci_bar_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context);

acpi_status
acpi_ev_default_region_setup (
	acpi_handle             handle,
	u32                     function,
	void                    *handler_context,
	void                    **region_context);

acpi_status
acpi_ev_initialize_region (
	acpi_operand_object     *region_obj,
	u8                      acpi_ns_locked);


/*
 * Evsci - SCI (System Control Interrupt) handling/dispatch
 */

u32
acpi_ev_install_sci_handler (
	void);

acpi_status
acpi_ev_remove_sci_handler (
	void);

u32
acpi_ev_initialize_sCI (
	u32                     program_sCI);

void
acpi_ev_terminate (
	void);


#endif  /* __ACEVENTS_H__  */
