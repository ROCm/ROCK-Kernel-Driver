/******************************************************************************
 *
 * Name: acexcep.h - Exception codes returned by the ACPI subsystem
 *       $Revision: 50 $
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

#ifndef __ACEXCEP_H__
#define __ACEXCEP_H__


/*
 * Exceptions returned by external ACPI interfaces
 */

#define AE_CODE_ENVIRONMENTAL           0x0000
#define AE_CODE_PROGRAMMER              0x1000
#define AE_CODE_ACPI_TABLES             0x2000
#define AE_CODE_AML                     0x3000
#define AE_CODE_CONTROL                 0x4000
#define AE_CODE_MASK                    0xF000


#define ACPI_SUCCESS(a)                 (!(a))
#define ACPI_FAILURE(a)                 (a)


#define AE_OK                           (acpi_status) 0x0000

/*
 * Environmental exceptions
 */
#define AE_ERROR                        (acpi_status) (0x0001 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_ACPI_TABLES               (acpi_status) (0x0002 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_NAMESPACE                 (acpi_status) (0x0003 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_MEMORY                    (acpi_status) (0x0004 | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_FOUND                    (acpi_status) (0x0005 | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_EXIST                    (acpi_status) (0x0006 | AE_CODE_ENVIRONMENTAL)
#define AE_EXIST                        (acpi_status) (0x0007 | AE_CODE_ENVIRONMENTAL)
#define AE_TYPE                         (acpi_status) (0x0008 | AE_CODE_ENVIRONMENTAL)
#define AE_NULL_OBJECT                  (acpi_status) (0x0009 | AE_CODE_ENVIRONMENTAL)
#define AE_NULL_ENTRY                   (acpi_status) (0x000A | AE_CODE_ENVIRONMENTAL)
#define AE_BUFFER_OVERFLOW              (acpi_status) (0x000B | AE_CODE_ENVIRONMENTAL)
#define AE_STACK_OVERFLOW               (acpi_status) (0x000C | AE_CODE_ENVIRONMENTAL)
#define AE_STACK_UNDERFLOW              (acpi_status) (0x000D | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_IMPLEMENTED              (acpi_status) (0x000E | AE_CODE_ENVIRONMENTAL)
#define AE_VERSION_MISMATCH             (acpi_status) (0x000F | AE_CODE_ENVIRONMENTAL)
#define AE_SUPPORT                      (acpi_status) (0x0010 | AE_CODE_ENVIRONMENTAL)
#define AE_SHARE                        (acpi_status) (0x0011 | AE_CODE_ENVIRONMENTAL)
#define AE_LIMIT                        (acpi_status) (0x0012 | AE_CODE_ENVIRONMENTAL)
#define AE_TIME                         (acpi_status) (0x0013 | AE_CODE_ENVIRONMENTAL)
#define AE_UNKNOWN_STATUS               (acpi_status) (0x0014 | AE_CODE_ENVIRONMENTAL)
#define AE_ACQUIRE_DEADLOCK             (acpi_status) (0x0015 | AE_CODE_ENVIRONMENTAL)
#define AE_RELEASE_DEADLOCK             (acpi_status) (0x0016 | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_ACQUIRED                 (acpi_status) (0x0017 | AE_CODE_ENVIRONMENTAL)
#define AE_ALREADY_ACQUIRED             (acpi_status) (0x0018 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_HARDWARE_RESPONSE         (acpi_status) (0x0019 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_GLOBAL_LOCK               (acpi_status) (0x001A | AE_CODE_ENVIRONMENTAL)

#define AE_CODE_ENV_MAX                 0x001A

/*
 * Programmer exceptions
 */
#define AE_BAD_PARAMETER                (acpi_status) (0x0001 | AE_CODE_PROGRAMMER)
#define AE_BAD_CHARACTER                (acpi_status) (0x0002 | AE_CODE_PROGRAMMER)
#define AE_BAD_PATHNAME                 (acpi_status) (0x0003 | AE_CODE_PROGRAMMER)
#define AE_BAD_DATA                     (acpi_status) (0x0004 | AE_CODE_PROGRAMMER)
#define AE_BAD_ADDRESS                  (acpi_status) (0x0005 | AE_CODE_PROGRAMMER)

#define AE_CODE_PGM_MAX                 0x0005


/*
 * Acpi table exceptions
 */
#define AE_BAD_SIGNATURE                (acpi_status) (0x0001 | AE_CODE_ACPI_TABLES)
#define AE_BAD_HEADER                   (acpi_status) (0x0002 | AE_CODE_ACPI_TABLES)
#define AE_BAD_CHECKSUM                 (acpi_status) (0x0003 | AE_CODE_ACPI_TABLES)
#define AE_BAD_VALUE                    (acpi_status) (0x0004 | AE_CODE_ACPI_TABLES)

#define AE_CODE_TBL_MAX                 0x0003


/*
 * AML exceptions.  These are caused by problems with
 * the actual AML byte stream
 */
#define AE_AML_ERROR                    (acpi_status) (0x0001 | AE_CODE_AML)
#define AE_AML_PARSE                    (acpi_status) (0x0002 | AE_CODE_AML)
#define AE_AML_BAD_OPCODE               (acpi_status) (0x0003 | AE_CODE_AML)
#define AE_AML_NO_OPERAND               (acpi_status) (0x0004 | AE_CODE_AML)
#define AE_AML_OPERAND_TYPE             (acpi_status) (0x0005 | AE_CODE_AML)
#define AE_AML_OPERAND_VALUE            (acpi_status) (0x0006 | AE_CODE_AML)
#define AE_AML_UNINITIALIZED_LOCAL      (acpi_status) (0x0007 | AE_CODE_AML)
#define AE_AML_UNINITIALIZED_ARG        (acpi_status) (0x0008 | AE_CODE_AML)
#define AE_AML_UNINITIALIZED_ELEMENT    (acpi_status) (0x0009 | AE_CODE_AML)
#define AE_AML_NUMERIC_OVERFLOW         (acpi_status) (0x000A | AE_CODE_AML)
#define AE_AML_REGION_LIMIT             (acpi_status) (0x000B | AE_CODE_AML)
#define AE_AML_BUFFER_LIMIT             (acpi_status) (0x000C | AE_CODE_AML)
#define AE_AML_PACKAGE_LIMIT            (acpi_status) (0x000D | AE_CODE_AML)
#define AE_AML_DIVIDE_BY_ZERO           (acpi_status) (0x000E | AE_CODE_AML)
#define AE_AML_BAD_NAME                 (acpi_status) (0x000F | AE_CODE_AML)
#define AE_AML_NAME_NOT_FOUND           (acpi_status) (0x0010 | AE_CODE_AML)
#define AE_AML_INTERNAL                 (acpi_status) (0x0011 | AE_CODE_AML)
#define AE_AML_INVALID_SPACE_ID         (acpi_status) (0x0012 | AE_CODE_AML)
#define AE_AML_STRING_LIMIT             (acpi_status) (0x0013 | AE_CODE_AML)
#define AE_AML_NO_RETURN_VALUE          (acpi_status) (0x0014 | AE_CODE_AML)
#define AE_AML_METHOD_LIMIT             (acpi_status) (0x0015 | AE_CODE_AML)
#define AE_AML_NOT_OWNER                (acpi_status) (0x0016 | AE_CODE_AML)
#define AE_AML_MUTEX_ORDER              (acpi_status) (0x0017 | AE_CODE_AML)
#define AE_AML_MUTEX_NOT_ACQUIRED       (acpi_status) (0x0018 | AE_CODE_AML)
#define AE_AML_INVALID_RESOURCE_TYPE    (acpi_status) (0x0019 | AE_CODE_AML)

#define AE_CODE_AML_MAX                 0x0019

/*
 * Internal exceptions used for control
 */
#define AE_CTRL_RETURN_VALUE            (acpi_status) (0x0001 | AE_CODE_CONTROL)
#define AE_CTRL_PENDING                 (acpi_status) (0x0002 | AE_CODE_CONTROL)
#define AE_CTRL_TERMINATE               (acpi_status) (0x0003 | AE_CODE_CONTROL)
#define AE_CTRL_TRUE                    (acpi_status) (0x0004 | AE_CODE_CONTROL)
#define AE_CTRL_FALSE                   (acpi_status) (0x0005 | AE_CODE_CONTROL)
#define AE_CTRL_DEPTH                   (acpi_status) (0x0006 | AE_CODE_CONTROL)
#define AE_CTRL_END                     (acpi_status) (0x0007 | AE_CODE_CONTROL)
#define AE_CTRL_TRANSFER                (acpi_status) (0x0008 | AE_CODE_CONTROL)

#define AE_CODE_CTRL_MAX                0x0008


#ifdef DEFINE_ACPI_GLOBALS

/*
 * String versions of the exception codes above
 * These strings must match the corresponding defines exactly
 */
NATIVE_CHAR const   *acpi_gbl_exception_names_env[] =
{
	"AE_OK",
	"AE_ERROR",
	"AE_NO_ACPI_TABLES",
	"AE_NO_NAMESPACE",
	"AE_NO_MEMORY",
	"AE_NOT_FOUND",
	"AE_NOT_EXIST",
	"AE_EXIST",
	"AE_TYPE",
	"AE_NULL_OBJECT",
	"AE_NULL_ENTRY",
	"AE_BUFFER_OVERFLOW",
	"AE_STACK_OVERFLOW",
	"AE_STACK_UNDERFLOW",
	"AE_NOT_IMPLEMENTED",
	"AE_VERSION_MISMATCH",
	"AE_SUPPORT",
	"AE_SHARE",
	"AE_LIMIT",
	"AE_TIME",
	"AE_UNKNOWN_STATUS",
	"AE_ACQUIRE_DEADLOCK",
	"AE_RELEASE_DEADLOCK",
	"AE_NOT_ACQUIRED",
	"AE_ALREADY_ACQUIRED",
	"AE_NO_HARDWARE_RESPONSE",
	"AE_NO_GLOBAL_LOCK",
};

NATIVE_CHAR const   *acpi_gbl_exception_names_pgm[] =
{
	"AE_BAD_PARAMETER",
	"AE_BAD_CHARACTER",
	"AE_BAD_PATHNAME",
	"AE_BAD_DATA",
	"AE_BAD_ADDRESS",
};

NATIVE_CHAR const   *acpi_gbl_exception_names_tbl[] =
{
	"AE_BAD_SIGNATURE",
	"AE_BAD_HEADER",
	"AE_BAD_CHECKSUM",
	"AE_BAD_VALUE",
};

NATIVE_CHAR const   *acpi_gbl_exception_names_aml[] =
{
	"AE_AML_ERROR",
	"AE_AML_PARSE",
	"AE_AML_BAD_OPCODE",
	"AE_AML_NO_OPERAND",
	"AE_AML_OPERAND_TYPE",
	"AE_AML_OPERAND_VALUE",
	"AE_AML_UNINITIALIZED_LOCAL",
	"AE_AML_UNINITIALIZED_ARG",
	"AE_AML_UNINITIALIZED_ELEMENT",
	"AE_AML_NUMERIC_OVERFLOW",
	"AE_AML_REGION_LIMIT",
	"AE_AML_BUFFER_LIMIT",
	"AE_AML_PACKAGE_LIMIT",
	"AE_AML_DIVIDE_BY_ZERO",
	"AE_AML_BAD_NAME",
	"AE_AML_NAME_NOT_FOUND",
	"AE_AML_INTERNAL",
	"AE_AML_INVALID_SPACE_ID",
	"AE_AML_STRING_LIMIT",
	"AE_AML_NO_RETURN_VALUE",
	"AE_AML_METHOD_LIMIT",
	"AE_AML_NOT_OWNER",
	"AE_AML_MUTEX_ORDER",
	"AE_AML_MUTEX_NOT_ACQUIRED",
	"AE_AML_INVALID_RESOURCE_TYPE",
};

NATIVE_CHAR const   *acpi_gbl_exception_names_ctrl[] =
{
	"AE_CTRL_RETURN_VALUE",
	"AE_CTRL_PENDING",
	"AE_CTRL_TERMINATE",
	"AE_CTRL_TRUE",
	"AE_CTRL_FALSE",
	"AE_CTRL_DEPTH",
	"AE_CTRL_END",
	"AE_CTRL_TRANSFER",
};

#endif /* ACPI GLOBALS */


#endif /* __ACEXCEP_H__ */
