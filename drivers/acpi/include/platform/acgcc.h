/******************************************************************************
 *
 * Name: acgcc.h - GCC specific defines, etc.
 *       $Revision: 24 $
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

#ifndef __ACGCC_H__
#define __ACGCC_H__

/* This macro is used to tag functions as "printf-like" because
 * some compilers (like GCC) can catch printf format string problems.
 */
#define ACPI_PRINTF_LIKE_FUNC __attribute__ ((__format__ (__printf__, 4, 5)))

/* Some compilers complain about unused variables. Sometimes we don't want to
 * use all the variables (most specifically for _THIS_MODULE). This allow us
 * to to tell the compiler warning in a per-variable manner that a variable
 * is unused.
 */
#define ACPI_UNUSED_VAR __attribute__ ((unused))

#endif /* __ACGCC_H__ */
