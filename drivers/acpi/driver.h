/*
 *  driver.h - ACPI driver
 *
 *  Copyright (C) 2000 Andrew Henroid
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

#ifndef __DRIVER_H
#define __DRIVER_H

#include <linux/tqueue.h>
#include <linux/wait.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include <asm/io.h>

#define ACPI_MAX_THROTTLE 10
#define ACPI_INVALID ~0UL
#define ACPI_INFINITE ~0UL

/*
 * cpu.c
 */
int acpi_cpu_init(void);
u32 acpi_read_pm_timer(void);

extern unsigned long acpi_c2_exit_latency;
extern unsigned long acpi_c3_exit_latency;
extern unsigned long acpi_c2_enter_latency;
extern unsigned long acpi_c3_enter_latency;

/*
 * driver.c
 */
int acpi_run(void (*callback)(void*), void *context);

/*
 * ec.c
 */
int acpi_ec_init(void);

/*
 * power.c
 */
int acpi_power_init(void);

/*
 * sys.c
 */
int acpi_sys_init(void);
int acpi_enter_sx(acpi_sstate_t state);

extern volatile acpi_sstate_t acpi_sleep_state;

/*
 * table.c
 */
extern FADT_DESCRIPTOR acpi_fadt;

int acpi_find_and_load_tables(u64 rsdp);

#endif /* __DRIVER_H */
