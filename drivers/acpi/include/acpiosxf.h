
/******************************************************************************
 *
 * Name: acpiosxf.h - All interfaces to the OS Services Layer (OSL).  These
 *                    interfaces must be implemented by OSL to interface the
 *                    ACPI components to the host operating system.
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

#ifndef __ACPIOSXF_H__
#define __ACPIOSXF_H__

#include "platform/acenv.h"
#include "actypes.h"


/* Priorities for Acpi_os_queue_for_execution */

#define OSD_PRIORITY_GPE            1
#define OSD_PRIORITY_HIGH           2
#define OSD_PRIORITY_MED            3
#define OSD_PRIORITY_LO             4

#define ACPI_NO_UNIT_LIMIT          ((u32) -1)
#define ACPI_MUTEX_SEM              1


/* Functions for Acpi_os_signal */

#define ACPI_SIGNAL_FATAL           0
#define ACPI_SIGNAL_BREAKPOINT      1

typedef struct acpi_fatal_info
{
	u32                     type;
	u32                     code;
	u32                     argument;

} ACPI_SIGNAL_FATAL_INFO;


/*
 * Types specific to the OS service interfaces
 */

typedef
u32 (*OSD_HANDLER) (
	void                    *context);

typedef
void (*OSD_EXECUTION_CALLBACK) (
	void                    *context);


/*
 * OSL Initialization and shutdown primitives
 */

acpi_status
acpi_os_initialize (
	void);

acpi_status
acpi_os_terminate (
	void);

acpi_status
acpi_os_get_root_pointer (
	u32                     flags,
	ACPI_PHYSICAL_ADDRESS   *rsdp_physical_address);


/*
 * Synchronization primitives
 */

acpi_status
acpi_os_create_semaphore (
	u32                     max_units,
	u32                     initial_units,
	acpi_handle             *out_handle);

acpi_status
acpi_os_delete_semaphore (
	acpi_handle             handle);

acpi_status
acpi_os_wait_semaphore (
	acpi_handle             handle,
	u32                     units,
	u32                     timeout);

acpi_status
acpi_os_signal_semaphore (
	acpi_handle             handle,
	u32                     units);


/*
 * Memory allocation and mapping
 */

void *
acpi_os_allocate (
	u32                     size);

void *
acpi_os_callocate (
	u32                     size);

void
acpi_os_free (
	void *                  memory);

acpi_status
acpi_os_map_memory (
	ACPI_PHYSICAL_ADDRESS   physical_address,
	u32                     length,
	void                    **logical_address);

void
acpi_os_unmap_memory (
	void                    *logical_address,
	u32                     length);

acpi_status
acpi_os_get_physical_address (
	void                    *logical_address,
	ACPI_PHYSICAL_ADDRESS   *physical_address);


/*
 * Interrupt handlers
 */

acpi_status
acpi_os_install_interrupt_handler (
	u32                     interrupt_number,
	OSD_HANDLER             service_routine,
	void                    *context);

acpi_status
acpi_os_remove_interrupt_handler (
	u32                     interrupt_number,
	OSD_HANDLER             service_routine);


/*
 * Threads and Scheduling
 */

u32
acpi_os_get_thread_id (
	void);

acpi_status
acpi_os_queue_for_execution (
	u32                     priority,
	OSD_EXECUTION_CALLBACK  function,
	void                    *context);

void
acpi_os_sleep (
	u32                     seconds,
	u32                     milliseconds);

void
acpi_os_stall (
	u32                     microseconds);


/*
 * Platform and hardware-independent I/O interfaces
 */

acpi_status
acpi_os_read_port (
	ACPI_IO_ADDRESS         address,
	void                    *value,
	u32                     width);


acpi_status
acpi_os_write_port (
	ACPI_IO_ADDRESS         address,
	NATIVE_UINT             value,
	u32                     width);


/*
 * Platform and hardware-independent physical memory interfaces
 */

acpi_status
acpi_os_read_memory (
	ACPI_PHYSICAL_ADDRESS   address,
	void                    *value,
	u32                     width);


acpi_status
acpi_os_write_memory (
	ACPI_PHYSICAL_ADDRESS   address,
	NATIVE_UINT             value,
	u32                     width);


/*
 * Platform and hardware-independent PCI configuration space access
 */

acpi_status
acpi_os_read_pci_configuration (
	acpi_pci_id             *pci_id,
	u32                     register,
	void                    *value,
	u32                     width);


acpi_status
acpi_os_write_pci_configuration (
	acpi_pci_id             *pci_id,
	u32                     register,
	NATIVE_UINT             value,
	u32                     width);


/*
 * Miscellaneous
 */

u8
acpi_os_readable (
	void                    *pointer,
	u32                     length);


u8
acpi_os_writable (
	void                    *pointer,
	u32                     length);

u32
acpi_os_get_timer (
	void);

acpi_status
acpi_os_signal (
	u32                     function,
	void                    *info);

/*
 * Debug print routines
 */

s32
acpi_os_printf (
	const NATIVE_CHAR       *format,
	...);

s32
acpi_os_vprintf (
	const NATIVE_CHAR       *format,
	va_list                 args);


/*
 * Debug input
 */

u32
acpi_os_get_line (
	NATIVE_CHAR             *buffer);


/*
 * Debug
 */

void
acpi_os_dbg_assert(
	void                    *failed_assertion,
	void                    *file_name,
	u32                     line_number,
	NATIVE_CHAR             *message);


#endif /* __ACPIOSXF_H__ */
