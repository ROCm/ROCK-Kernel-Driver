/******************************************************************************
 *
 * Name: achware.h -- hardware specific interfaces
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2003, R. Byron Moore
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

#ifndef __ACHWARE_H__
#define __ACHWARE_H__


/* PM Timer ticks per second (HZ) */
#define PM_TIMER_FREQUENCY  3579545


/* Prototypes */


acpi_status
acpi_hw_initialize (
	void);

acpi_status
acpi_hw_shutdown (
	void);

acpi_status
acpi_hw_initialize_system_info (
	void);

acpi_status
acpi_hw_set_mode (
	u32                             mode);

u32
acpi_hw_get_mode (
	void);

u32
acpi_hw_get_mode_capabilities (
	void);

/* Register I/O Prototypes */

struct acpi_bit_register_info *
acpi_hw_get_bit_register_info (
	u32                             register_id);

acpi_status
acpi_hw_register_read (
	u8                              use_lock,
	u32                             register_id,
	u32                             *return_value);

acpi_status
acpi_hw_register_write (
	u8                              use_lock,
	u32                             register_id,
	u32                             value);

acpi_status
acpi_hw_low_level_read (
	u32                             width,
	u32                             *value,
	struct acpi_generic_address     *reg,
	u32                             offset);

acpi_status
acpi_hw_low_level_write (
	u32                             width,
	u32                             value,
	struct acpi_generic_address     *reg,
	u32                             offset);

acpi_status
acpi_hw_clear_acpi_status (
   void);


/* GPE support */

u8
acpi_hw_get_gpe_bit_mask (
	u32                             gpe_number);

acpi_status
acpi_hw_enable_gpe (
	u32                             gpe_number);

void
acpi_hw_enable_gpe_for_wakeup (
	u32                             gpe_number);

acpi_status
acpi_hw_disable_gpe (
	u32                             gpe_number);

void
acpi_hw_disable_gpe_for_wakeup (
	u32                             gpe_number);

acpi_status
acpi_hw_clear_gpe (
	u32                             gpe_number);

acpi_status
acpi_hw_get_gpe_status (
	u32                             gpe_number,
	acpi_event_status               *event_status);

acpi_status
acpi_hw_disable_non_wakeup_gpes (
	void);

acpi_status
acpi_hw_enable_non_wakeup_gpes (
	void);


/* ACPI Timer prototypes */

acpi_status
acpi_get_timer_resolution (
	u32                             *resolution);

acpi_status
acpi_get_timer (
	u32                             *ticks);

acpi_status
acpi_get_timer_duration (
	u32                             start_ticks,
	u32                             end_ticks,
	u32                             *time_elapsed);


#endif /* __ACHWARE_H__ */
