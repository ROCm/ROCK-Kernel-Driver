/*
 *  asm-i386/acpi.h
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Patrick Mochel <mochel@osdl.org>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef _ASM_ACPI_H
#define _ASM_ACPI_H

#ifdef __KERNEL__

#ifdef CONFIG_ACPI_BOOT

/* Fixmap pages to reserve for ACPI boot-time tables (see fixmap.h) */
#define FIX_ACPI_PAGES		4

extern int acpi_mp_config;

char * __acpi_map_table (unsigned long phys_addr, unsigned long size);
extern int acpi_find_rsdp (unsigned long *phys_addr);
extern int acpi_parse_madt (unsigned long phys_addr, unsigned long size);
extern int acpi_boot_init (char *cmdline);

#else
#define acpi_mp_config 0
#endif /*CONFIG_ACPI_BOOT*/

#ifdef CONFIG_ACPI_PCI
int acpi_get_interrupt_model (int *type);
#endif /*CONFIG_ACPI_PCI*/


#ifdef CONFIG_ACPI_SLEEP

extern unsigned long saved_eip;
extern unsigned long saved_esp;
extern unsigned long saved_ebp;
extern unsigned long saved_ebx;
extern unsigned long saved_esi;
extern unsigned long saved_edi;

static inline void acpi_save_register_state(unsigned long return_point)
{
	saved_eip = return_point;
	asm volatile ("movl %%esp,(%0)" : "=m" (saved_esp));
	asm volatile ("movl %%ebp,(%0)" : "=m" (saved_ebp));
	asm volatile ("movl %%ebx,(%0)" : "=m" (saved_ebx));
	asm volatile ("movl %%edi,(%0)" : "=m" (saved_edi));
	asm volatile ("movl %%esi,(%0)" : "=m" (saved_esi));
}

#define acpi_restore_register_state()	do {} while (0)

/* routines for saving/restoring kernel state */
extern int acpi_save_state_mem(void);
extern int acpi_save_state_disk(void);
extern void acpi_restore_state_mem(void);

extern unsigned long acpi_wakeup_address;

/* early initialization routine */
extern void acpi_reserve_bootmem(void);

#endif /*CONFIG_ACPI_SLEEP*/


#endif /*__KERNEL__*/

#endif /*_ASM_ACPI_H*/
