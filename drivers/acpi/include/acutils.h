/******************************************************************************
 *
 * Name: acutils.h -- prototypes for the common (subsystem-wide) procedures
 *       $Revision: 100 $
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

#ifndef _ACUTILS_H
#define _ACUTILS_H


typedef
ACPI_STATUS (*ACPI_PKG_CALLBACK) (
	u8                      object_type,
	ACPI_OPERAND_OBJECT     *source_object,
	ACPI_GENERIC_STATE      *state,
	void                    *context);


ACPI_STATUS
acpi_ut_walk_package_tree (
	ACPI_OPERAND_OBJECT     *source_object,
	void                    *target_object,
	ACPI_PKG_CALLBACK       walk_callback,
	void                    *context);


typedef struct acpi_pkg_info
{
	u8                      *free_space;
	u32                     length;
	u32                     object_space;
	u32                     num_packages;
} ACPI_PKG_INFO;

#define REF_INCREMENT       (u16) 0
#define REF_DECREMENT       (u16) 1
#define REF_FORCE_DELETE    (u16) 2

/* Acpi_ut_dump_buffer */

#define DB_BYTE_DISPLAY     1
#define DB_WORD_DISPLAY     2
#define DB_DWORD_DISPLAY    4
#define DB_QWORD_DISPLAY    8


/* Global initialization interfaces */

void
acpi_ut_init_globals (
	void);

void
acpi_ut_terminate (
	void);


/*
 * Ut_init - miscellaneous initialization and shutdown
 */

ACPI_STATUS
acpi_ut_hardware_initialize (
	void);

ACPI_STATUS
acpi_ut_subsystem_shutdown (
	void);

ACPI_STATUS
acpi_ut_validate_fadt (
	void);

/*
 * Ut_global - Global data structures and procedures
 */

#ifdef ACPI_DEBUG

NATIVE_CHAR *
acpi_ut_get_mutex_name (
	u32                     mutex_id);

NATIVE_CHAR *
acpi_ut_get_type_name (
	u32                     type);

NATIVE_CHAR *
acpi_ut_get_region_name (
	u8                      space_id);

#endif


u8
acpi_ut_valid_object_type (
	u32                     type);

ACPI_OWNER_ID
acpi_ut_allocate_owner_id (
	u32                     id_type);


/*
 * Ut_clib - Local implementations of C library functions
 */

#ifndef ACPI_USE_SYSTEM_CLIBRARY

u32
acpi_ut_strlen (
	const NATIVE_CHAR       *string);

NATIVE_CHAR *
acpi_ut_strcpy (
	NATIVE_CHAR             *dst_string,
	const NATIVE_CHAR       *src_string);

NATIVE_CHAR *
acpi_ut_strncpy (
	NATIVE_CHAR             *dst_string,
	const NATIVE_CHAR       *src_string,
	NATIVE_UINT             count);

u32
acpi_ut_strncmp (
	const NATIVE_CHAR       *string1,
	const NATIVE_CHAR       *string2,
	NATIVE_UINT             count);

u32
acpi_ut_strcmp (
	const NATIVE_CHAR       *string1,
	const NATIVE_CHAR       *string2);

NATIVE_CHAR *
acpi_ut_strcat (
	NATIVE_CHAR             *dst_string,
	const NATIVE_CHAR       *src_string);

NATIVE_CHAR *
acpi_ut_strncat (
	NATIVE_CHAR             *dst_string,
	const NATIVE_CHAR       *src_string,
	NATIVE_UINT             count);

u32
acpi_ut_strtoul (
	const NATIVE_CHAR       *string,
	NATIVE_CHAR             **terminator,
	u32                     base);

NATIVE_CHAR *
acpi_ut_strstr (
	NATIVE_CHAR             *string1,
	NATIVE_CHAR             *string2);

void *
acpi_ut_memcpy (
	void                    *dest,
	const void              *src,
	NATIVE_UINT             count);

void *
acpi_ut_memset (
	void                    *dest,
	NATIVE_UINT             value,
	NATIVE_UINT             count);

u32
acpi_ut_to_upper (
	u32                     c);

u32
acpi_ut_to_lower (
	u32                     c);

#endif /* ACPI_USE_SYSTEM_CLIBRARY */

/*
 * Ut_copy - Object construction and conversion interfaces
 */

ACPI_STATUS
acpi_ut_build_simple_object(
	ACPI_OPERAND_OBJECT     *obj,
	ACPI_OBJECT             *user_obj,
	u8                      *data_space,
	u32                     *buffer_space_used);

ACPI_STATUS
acpi_ut_build_package_object (
	ACPI_OPERAND_OBJECT     *obj,
	u8                      *buffer,
	u32                     *space_used);

ACPI_STATUS
acpi_ut_copy_iobject_to_eobject (
	ACPI_OPERAND_OBJECT     *obj,
	ACPI_BUFFER             *ret_buffer);

ACPI_STATUS
acpi_ut_copy_esimple_to_isimple(
	ACPI_OBJECT             *user_obj,
	ACPI_OPERAND_OBJECT     *obj);

ACPI_STATUS
acpi_ut_copy_eobject_to_iobject (
	ACPI_OBJECT             *obj,
	ACPI_OPERAND_OBJECT     *internal_obj);

ACPI_STATUS
acpi_ut_copy_isimple_to_isimple (
	ACPI_OPERAND_OBJECT     *source_obj,
	ACPI_OPERAND_OBJECT     *dest_obj);

ACPI_STATUS
acpi_ut_copy_ipackage_to_ipackage (
	ACPI_OPERAND_OBJECT     *source_obj,
	ACPI_OPERAND_OBJECT     *dest_obj,
	ACPI_WALK_STATE         *walk_state);


/*
 * Ut_create - Object creation
 */

ACPI_STATUS
acpi_ut_update_object_reference (
	ACPI_OPERAND_OBJECT     *object,
	u16                     action);

ACPI_OPERAND_OBJECT  *
_ut_create_internal_object (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	ACPI_OBJECT_TYPE8       type);


/*
 * Ut_debug - Debug interfaces
 */

u32
get_debug_level (
	void);

void
set_debug_level (
	u32                     level);

void
function_trace (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	NATIVE_CHAR             *function_name);

void
function_trace_ptr (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	NATIVE_CHAR             *function_name,
	void                    *pointer);

void
function_trace_u32 (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	NATIVE_CHAR             *function_name,
	u32                     integer);

void
function_trace_str (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	NATIVE_CHAR             *function_name,
	NATIVE_CHAR             *string);

void
function_exit (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	NATIVE_CHAR             *function_name);

void
function_status_exit (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	NATIVE_CHAR             *function_name,
	ACPI_STATUS             status);

void
function_value_exit (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	NATIVE_CHAR             *function_name,
	ACPI_INTEGER            value);

void
function_ptr_exit (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	NATIVE_CHAR             *function_name,
	u8                      *ptr);

void
debug_print_prefix (
	NATIVE_CHAR             *module_name,
	u32                     line_number);

void
debug_print (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	u32                     print_level,
	NATIVE_CHAR             *format, ...);

void
debug_print_raw (
	NATIVE_CHAR             *format, ...);

void
_report_info (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id);

void
_report_error (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id);

void
_report_warning (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id);

void
acpi_ut_dump_buffer (
	u8                      *buffer,
	u32                     count,
	u32                     display,
	u32                     component_id);


/*
 * Ut_delete - Object deletion
 */

void
acpi_ut_delete_internal_obj (
	ACPI_OPERAND_OBJECT     *object);

void
acpi_ut_delete_internal_package_object (
	ACPI_OPERAND_OBJECT     *object);

void
acpi_ut_delete_internal_simple_object (
	ACPI_OPERAND_OBJECT     *object);

ACPI_STATUS
acpi_ut_delete_internal_object_list (
	ACPI_OPERAND_OBJECT     **obj_list);


/*
 * Ut_eval - object evaluation
 */

/* Method name strings */

#define METHOD_NAME__HID        "_HID"
#define METHOD_NAME__UID        "_UID"
#define METHOD_NAME__ADR        "_ADR"
#define METHOD_NAME__STA        "_STA"
#define METHOD_NAME__REG        "_REG"
#define METHOD_NAME__SEG        "_SEG"
#define METHOD_NAME__BBN        "_BBN"


ACPI_STATUS
acpi_ut_evaluate_numeric_object (
	NATIVE_CHAR             *object_name,
	ACPI_NAMESPACE_NODE     *device_node,
	ACPI_INTEGER            *address);

ACPI_STATUS
acpi_ut_execute_HID (
	ACPI_NAMESPACE_NODE     *device_node,
	ACPI_DEVICE_ID          *hid);

ACPI_STATUS
acpi_ut_execute_STA (
	ACPI_NAMESPACE_NODE     *device_node,
	u32                     *status_flags);

ACPI_STATUS
acpi_ut_execute_UID (
	ACPI_NAMESPACE_NODE     *device_node,
	ACPI_DEVICE_ID          *uid);


/*
 * Ut_error - exception interfaces
 */

NATIVE_CHAR *
acpi_ut_format_exception (
	ACPI_STATUS             status);


/*
 * Ut_mutex - mutual exclusion interfaces
 */

ACPI_STATUS
acpi_ut_mutex_initialize (
	void);

void
acpi_ut_mutex_terminate (
	void);

ACPI_STATUS
acpi_ut_create_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

ACPI_STATUS
acpi_ut_delete_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

ACPI_STATUS
acpi_ut_acquire_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

ACPI_STATUS
acpi_ut_release_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);


/*
 * Ut_object - internal object create/delete/cache routines
 */

void *
_ut_allocate_object_desc (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id);

#define acpi_ut_create_internal_object(t) _ut_create_internal_object(_THIS_MODULE,__LINE__,_COMPONENT,t)
#define acpi_ut_allocate_object_desc()  _ut_allocate_object_desc(_THIS_MODULE,__LINE__,_COMPONENT)

void
acpi_ut_delete_object_desc (
	ACPI_OPERAND_OBJECT     *object);

u8
acpi_ut_valid_internal_object (
	void                    *object);


/*
 * Ut_ref_cnt - Object reference count management
 */

void
acpi_ut_add_reference (
	ACPI_OPERAND_OBJECT     *object);

void
acpi_ut_remove_reference (
	ACPI_OPERAND_OBJECT     *object);

/*
 * Ut_size - Object size routines
 */

ACPI_STATUS
acpi_ut_get_simple_object_size (
	ACPI_OPERAND_OBJECT     *obj,
	u32                     *obj_length);

ACPI_STATUS
acpi_ut_get_package_object_size (
	ACPI_OPERAND_OBJECT     *obj,
	u32                     *obj_length);

ACPI_STATUS
acpi_ut_get_object_size(
	ACPI_OPERAND_OBJECT     *obj,
	u32                     *obj_length);


/*
 * Ut_state - Generic state creation/cache routines
 */

void
acpi_ut_push_generic_state (
	ACPI_GENERIC_STATE      **list_head,
	ACPI_GENERIC_STATE      *state);

ACPI_GENERIC_STATE *
acpi_ut_pop_generic_state (
	ACPI_GENERIC_STATE      **list_head);


ACPI_GENERIC_STATE *
acpi_ut_create_generic_state (
	void);

ACPI_GENERIC_STATE *
acpi_ut_create_update_state (
	ACPI_OPERAND_OBJECT     *object,
	u16                     action);

ACPI_GENERIC_STATE *
acpi_ut_create_pkg_state (
	void                    *internal_object,
	void                    *external_object,
	u16                     index);

ACPI_STATUS
acpi_ut_create_update_state_and_push (
	ACPI_OPERAND_OBJECT     *object,
	u16                     action,
	ACPI_GENERIC_STATE      **state_list);

ACPI_STATUS
acpi_ut_create_pkg_state_and_push (
	void                    *internal_object,
	void                    *external_object,
	u16                     index,
	ACPI_GENERIC_STATE      **state_list);

ACPI_GENERIC_STATE *
acpi_ut_create_control_state (
	void);

void
acpi_ut_delete_generic_state (
	ACPI_GENERIC_STATE      *state);

void
acpi_ut_delete_generic_state_cache (
	void);

void
acpi_ut_delete_object_cache (
	void);

/*
 * Ututils
 */

u8
acpi_ut_valid_acpi_name (
	u32                     name);

u8
acpi_ut_valid_acpi_character (
	NATIVE_CHAR             character);

NATIVE_CHAR *
acpi_ut_strupr (
	NATIVE_CHAR             *src_string);

ACPI_STATUS
acpi_ut_resolve_package_references (
	ACPI_OPERAND_OBJECT     *obj_desc);


#ifdef ACPI_DEBUG
void
acpi_ut_display_init_pathname (
	ACPI_HANDLE             obj_handle,
	char                    *path);

#endif


/*
 * Memory allocation functions and related macros.
 * Macros that expand to include filename and line number
 */

void *
_ut_allocate (
	u32                     size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line);

void *
_ut_callocate (
	u32                     size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line);

void
_ut_free (
	void                    *address,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line);

void
acpi_ut_init_static_object (
	ACPI_OPERAND_OBJECT     *obj_desc);


#ifdef ACPI_DEBUG_TRACK_ALLOCATIONS
void
acpi_ut_dump_allocation_info (
	void);

void
acpi_ut_dump_current_allocations (
	u32                     component,
	NATIVE_CHAR             *module);
#endif


#define acpi_ut_allocate(a) _ut_allocate(a,_COMPONENT,_THIS_MODULE,__LINE__)
#define acpi_ut_callocate(a) _ut_callocate(a, _COMPONENT,_THIS_MODULE,__LINE__)
#define acpi_ut_free(a)     _ut_free(a,_COMPONENT,_THIS_MODULE,__LINE__)


#endif /* _ACUTILS_H */
