/*
 *  asm-ia64/acpi.h
 *
 *  Copyright (C) 1999 VA Linux Systems
 *  Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *  Copyright (C) 2000,2001 J.I. Lee <jung-ik.lee@intel.com>
 *  Copyright (C) 2001,2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
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

#define __acpi_map_table(phys_addr, size) __va(phys_addr)

const char *acpi_get_sysname (void);
int acpi_boot_init (char *cdline);
int acpi_find_rsdp (unsigned long *phys_addr);
int acpi_request_vector (u32 int_type);
int acpi_get_prt (struct pci_vector_struct **vectors, int *count);
int acpi_get_interrupt_model(int *type);

#ifdef CONFIG_DISCONTIGMEM
#define NODE_ARRAY_INDEX(x)	((x) / 8)	/* 8 bits/char */
#define NODE_ARRAY_OFFSET(x)	((x) % 8)	/* 8 bits/char */
#define MAX_PXM_DOMAINS		(256)
#endif /* CONFIG_DISCONTIGMEM */

#endif /*__KERNEL__*/

#endif /*_ASM_ACPI_H*/
