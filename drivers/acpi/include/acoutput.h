/******************************************************************************
 *
 * Name: acoutput.h -- debug output
 *       $Revision: 84 $
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

#ifndef __ACOUTPUT_H__
#define __ACOUTPUT_H__

/*
 * Debug levels and component IDs.  These are used to control the
 * granularity of the output of the DEBUG_PRINT macro -- on a per-
 * component basis and a per-exception-type basis.
 */

/* Component IDs are used in the global "Debug_layer" */

#define ACPI_UTILITIES              0x00000001
#define ACPI_HARDWARE               0x00000002
#define ACPI_EVENTS                 0x00000004
#define ACPI_TABLES                 0x00000008
#define ACPI_NAMESPACE              0x00000010
#define ACPI_PARSER                 0x00000020
#define ACPI_DISPATCHER             0x00000040
#define ACPI_EXECUTER               0x00000080
#define ACPI_RESOURCES              0x00000100
#define ACPI_DEBUGGER               0x00000200
#define ACPI_OS_SERVICES            0x00000400

#define ACPI_BUS                    0x00010000
#define ACPI_SYSTEM                 0x00020000
#define ACPI_POWER                  0x00040000
#define ACPI_EC                     0x00080000
#define ACPI_AC_ADAPTER             0x00100000
#define ACPI_BATTERY                0x00200000
#define ACPI_BUTTON                 0x00400000
#define ACPI_PROCESSOR              0x00800000
#define ACPI_THERMAL                0x01000000
#define ACPI_FAN                    0x02000000

#define ACPI_ALL_COMPONENTS         0x0FFFFFFF

#define ACPI_COMPONENT_DEFAULT      (ACPI_ALL_COMPONENTS)


#define ACPI_COMPILER               0x10000000
#define ACPI_TOOLS                  0x20000000


/*
 * Raw debug output levels, do not use these in the DEBUG_PRINT macros
 */

#define ACPI_LV_OK                  0x00000001
#define ACPI_LV_INFO                0x00000002
#define ACPI_LV_WARN                0x00000004
#define ACPI_LV_ERROR               0x00000008
#define ACPI_LV_FATAL               0x00000010
#define ACPI_LV_DEBUG_OBJECT        0x00000020
#define ACPI_LV_ALL_EXCEPTIONS      0x0000003F


/* Trace verbosity level 1 [Standard Trace Level] */

#define ACPI_LV_PARSE               0x00000040
#define ACPI_LV_LOAD                0x00000080
#define ACPI_LV_DISPATCH            0x00000100
#define ACPI_LV_EXEC                0x00000200
#define ACPI_LV_NAMES               0x00000400
#define ACPI_LV_OPREGION            0x00000800
#define ACPI_LV_BFIELD              0x00001000
#define ACPI_LV_TABLES              0x00002000
#define ACPI_LV_VALUES              0x00004000
#define ACPI_LV_OBJECTS             0x00008000
#define ACPI_LV_RESOURCES           0x00010000
#define ACPI_LV_USER_REQUESTS       0x00020000
#define ACPI_LV_PACKAGE             0x00040000
#define ACPI_LV_INIT                0x00080000
#define ACPI_LV_VERBOSITY1          0x000FFF40 | ACPI_LV_ALL_EXCEPTIONS

/* Trace verbosity level 2 [Function tracing and memory allocation] */

#define ACPI_LV_ALLOCATIONS         0x00100000
#define ACPI_LV_FUNCTIONS           0x00200000
#define ACPI_LV_VERBOSITY2          0x00300000 | ACPI_LV_VERBOSITY1
#define ACPI_LV_ALL                 ACPI_LV_VERBOSITY2

/* Trace verbosity level 3 [Threading, I/O, and Interrupts] */

#define ACPI_LV_MUTEX               0x01000000
#define ACPI_LV_THREADS             0x02000000
#define ACPI_LV_IO                  0x04000000
#define ACPI_LV_INTERRUPTS          0x08000000
#define ACPI_LV_VERBOSITY3          0x0F000000 | ACPI_LV_VERBOSITY2

/*
 * Debug level macros that are used in the DEBUG_PRINT macros
 */

#define ACPI_DEBUG_LEVEL(dl)       dl,__LINE__,&_dbg

/* Exception level -- used in the global "Debug_level" */

#define ACPI_DB_OK                  ACPI_DEBUG_LEVEL (ACPI_LV_OK)
#define ACPI_DB_INFO                ACPI_DEBUG_LEVEL (ACPI_LV_INFO)
#define ACPI_DB_WARN                ACPI_DEBUG_LEVEL (ACPI_LV_WARN)
#define ACPI_DB_ERROR               ACPI_DEBUG_LEVEL (ACPI_LV_ERROR)
#define ACPI_DB_FATAL               ACPI_DEBUG_LEVEL (ACPI_LV_FATAL)
#define ACPI_DB_DEBUG_OBJECT        ACPI_DEBUG_LEVEL (ACPI_LV_DEBUG_OBJECT)
#define ACPI_DB_ALL_EXCEPTIONS      ACPI_DEBUG_LEVEL (ACPI_LV_ALL_EXCEPTIONS)


/* Trace level -- also used in the global "Debug_level" */

#define ACPI_DB_THREADS             ACPI_DEBUG_LEVEL (ACPI_LV_THREADS)
#define ACPI_DB_PARSE               ACPI_DEBUG_LEVEL (ACPI_LV_PARSE)
#define ACPI_DB_DISPATCH            ACPI_DEBUG_LEVEL (ACPI_LV_DISPATCH)
#define ACPI_DB_LOAD                ACPI_DEBUG_LEVEL (ACPI_LV_LOAD)
#define ACPI_DB_EXEC                ACPI_DEBUG_LEVEL (ACPI_LV_EXEC)
#define ACPI_DB_NAMES               ACPI_DEBUG_LEVEL (ACPI_LV_NAMES)
#define ACPI_DB_OPREGION            ACPI_DEBUG_LEVEL (ACPI_LV_OPREGION)
#define ACPI_DB_BFIELD              ACPI_DEBUG_LEVEL (ACPI_LV_BFIELD)
#define ACPI_DB_TABLES              ACPI_DEBUG_LEVEL (ACPI_LV_TABLES)
#define ACPI_DB_FUNCTIONS           ACPI_DEBUG_LEVEL (ACPI_LV_FUNCTIONS)
#define ACPI_DB_VALUES              ACPI_DEBUG_LEVEL (ACPI_LV_VALUES)
#define ACPI_DB_OBJECTS             ACPI_DEBUG_LEVEL (ACPI_LV_OBJECTS)
#define ACPI_DB_ALLOCATIONS         ACPI_DEBUG_LEVEL (ACPI_LV_ALLOCATIONS)
#define ACPI_DB_RESOURCES           ACPI_DEBUG_LEVEL (ACPI_LV_RESOURCES)
#define ACPI_DB_IO                  ACPI_DEBUG_LEVEL (ACPI_LV_IO)
#define ACPI_DB_INTERRUPTS          ACPI_DEBUG_LEVEL (ACPI_LV_INTERRUPTS)
#define ACPI_DB_USER_REQUESTS       ACPI_DEBUG_LEVEL (ACPI_LV_USER_REQUESTS)
#define ACPI_DB_PACKAGE             ACPI_DEBUG_LEVEL (ACPI_LV_PACKAGE)
#define ACPI_DB_MUTEX               ACPI_DEBUG_LEVEL (ACPI_LV_MUTEX)
#define ACPI_DB_INIT                ACPI_DEBUG_LEVEL (ACPI_LV_INIT)

#define ACPI_DB_ALL                 ACPI_DEBUG_LEVEL (0x0FFFFF80)


/* Exceptionally verbose output -- also used in the global "Debug_level" */

#define ACPI_DB_AML_DISASSEMBLE     0x10000000
#define ACPI_DB_VERBOSE_INFO        0x20000000
#define ACPI_DB_FULL_TABLES         0x40000000
#define ACPI_DB_EVENTS              0x80000000

#define ACPI_DB_VERBOSE             0xF0000000


/* Defaults for Debug_level, debug and normal */

#define DEBUG_DEFAULT               (ACPI_LV_OK | ACPI_LV_WARN | ACPI_LV_ERROR | ACPI_LV_DEBUG_OBJECT)
#define NORMAL_DEFAULT              (ACPI_LV_OK | ACPI_LV_WARN | ACPI_LV_ERROR | ACPI_LV_DEBUG_OBJECT)
#define DEBUG_ALL                   (ACPI_LV_AML_DISASSEMBLE | ACPI_LV_ALL_EXCEPTIONS | ACPI_LV_ALL)

/* Misc defines */

#define HEX                         0x01
#define ASCII                       0x02
#define FULL_ADDRESS                0x04
#define CHARS_PER_LINE              16          /* used in Dump_buf function */


#endif /* __ACOUTPUT_H__ */
