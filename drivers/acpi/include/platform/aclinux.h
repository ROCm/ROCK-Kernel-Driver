/******************************************************************************
 *
 * Name: aclinux.h - OS specific defines, etc.
 *       $Revision: 27 $
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

#ifndef __ACLINUX_H__
#define __ACLINUX_H__

#define ACPI_OS_NAME                "Linux"

#define ACPI_USE_SYSTEM_CLIBRARY

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/div64.h>
#include <asm/acpi.h>

#define strtoul simple_strtoul

#define ACPI_MACHINE_WIDTH	BITS_PER_LONG

#else /* !__KERNEL__ */

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

#if defined(__ia64__) || defined(__x86_64__)
#define ACPI_MACHINE_WIDTH		64
#define COMPILER_DEPENDENT_INT64	long
#define COMPILER_DEPENDENT_UINT64	unsigned long
#else
#define ACPI_MACHINE_WIDTH		32
#define COMPILER_DEPENDENT_INT64	long long
#define COMPILER_DEPENDENT_UINT64	unsigned long long
#define ACPI_USE_NATIVE_DIVIDE
#endif

#endif /* __KERNEL__ */

/* Linux uses GCC */

#include "acgcc.h"

#endif /* __ACLINUX_H__ */
