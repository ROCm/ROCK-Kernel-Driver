/*
 *  vtypes.h
 *
 *  Copyright (C) 2002-2004 Intel Corporation
 *  Maintainer - Juan Villacis <juan.villacis@intel.com>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
/*
 * ===========================================================================
 *
 *	File: vtypes.h
 *
 *	Description: translation of types and macros from Windows* to Linux*
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#ifndef _VTYPES_H
#define _VTYPES_H

#include <linux/types.h>

#define TEXT(txt) #txt

/*
typedef __u8 __u8;	// unsigned  8-bit integer (1 byte)
typedef __u16 __u16;	// unsigned 16-bit integer (2 bytes)
typedef __u32 __u32;	// unsigned 32-bit integer (4 bytes)
typedef __s32 __s32;	//   signed 32-bit integer (4 bytes)
typedef __u64 __u64;	// unsigned 64-bit integer (8 bytes)

typedef __u16 __u16;
typedef __u32 __u32;
typedef char char;
typedef void void;
*/
typedef char *char_ptr;
typedef void *void_ptr;

#ifdef linux32
#define __u32_PTR __u32
#endif

#if defined(linux64) || defined(linux32_64)
#define __u32_PTR __u64
#endif

typedef union _ULARGE_INTEGER {	// unsigned 64-bit integer (8 bytes)
  struct {
    __u32 low_part;
    __u32 high_part;
  };
  __u64 quad_part;
} ULARGE_INTEGER;

typedef ULARGE_INTEGER *PULARGE_INTEGER;

#define TRUE      1
#define FALSE     0

typedef enum boolean { B_FALSE, B_TRUE } BOOLEAN;

#endif	// _VTYPES_H
