/******************************************************************************
 *
 * Name: acevents.h - Event subcomponent prototypes and defines
 *       $Revision: 66 $
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

#ifndef __ACEVENTS_H__
#define __ACEVENTS_H__


acpi_status
acpi_ev_initialize (
	void);


/*
 * Acpi_evfixed - Fixed event handling
 */

acpi_status
acpi_ev_fixed_event_initialize (
	void);

u32
acpi_ev_fixed_event_detect (
	void);

u32
acpi_ev_fixed_event_dispatch (
	u32                     acpi_event);


/*
 * Acpi_evglock - Global Lock support
 */

acpi_status
acpi_ev_acquire_global_lock(
	void);

void
acpi_ev_release_global_lock(
	void);

acpi_status
acpi_ev_init_global_lock_handler (
	void);


/*
 * Acpi_evgpe - GPE handling and dispatch
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
 * Acpi_evnotify - Device Notify handling and dispatch
 */

acpi_status
acpi_ev_queue_notify_request (
	acpi_namespace_node     *node,
	u32                     notify_value);

void
acpi_ev_notify_dispatch (
	void                    *context);

/*
 * Acpi_evregion - Address Space handling
 */

acpi_status
acpi_ev_install_default_address_space_handlers (
	void);

acpi_status
acpi_ev_address_space_dispatch (
	acpi_operand_object    *region_obj,
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	u32                     *value);


acpi_status
acpi_ev_addr_handler_helper (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value);

void
acpi_ev_disassociate_region_from_handler(
	acpi_operand_object    *region_obj,
	u8                      acpi_ns_is_locked);


acpi_status
acpi_ev_associate_region_and_handler (
	acpi_operand_object     *handler_obj,
	acpi_operand_object     *region_obj,
	u8                      acpi_ns_is_locked);


/*
 * Acpi_evregini - Region initialization and setup
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
acpi_ev_restore_acpi_state (
	void);

void
acpi_ev_terminate (
	void);


#endif  /* __ACEVENTS_H__  */
