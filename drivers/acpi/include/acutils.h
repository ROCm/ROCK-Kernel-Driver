/******************************************************************************
 *
 * Name: acutils.h -- prototypes for the common (subsystem-wide) procedures
 *       $Revision: 117 $
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
acpi_status (*ACPI_PKG_CALLBACK) (
	u8                      object_type,
	acpi_operand_object     *source_object,
	acpi_generic_state      *state,
	void                    *context);


acpi_status
acpi_ut_walk_package_tree (
	acpi_operand_object     *source_object,
	void                    *target_object,
	ACPI_PKG_CALLBACK       walk_callback,
	void                    *context);


typedef struct acpi_pkg_info
{
	u8                      *free_space;
	u32                     length;
	u32                     object_space;
	u32                     num_packages;
} acpi_pkg_info;

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

acpi_status
acpi_ut_hardware_initialize (
	void);

acpi_status
acpi_ut_subsystem_shutdown (
	void);

acpi_status
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
acpi_ut_hex_to_ascii_char (
	acpi_integer            integer,
	u32                     position);

u8
acpi_ut_valid_object_type (
	u32                     type);

acpi_owner_id
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

acpi_status
acpi_ut_build_simple_object(
	acpi_operand_object     *obj,
	acpi_object             *user_obj,
	u8                      *data_space,
	u32                     *buffer_space_used);

acpi_status
acpi_ut_build_package_object (
	acpi_operand_object     *obj,
	u8                      *buffer,
	u32                     *space_used);

acpi_status
acpi_ut_copy_iobject_to_eobject (
	acpi_operand_object     *obj,
	acpi_buffer             *ret_buffer);

acpi_status
acpi_ut_copy_esimple_to_isimple(
	acpi_object             *user_obj,
	acpi_operand_object     **return_obj);

acpi_status
acpi_ut_copy_eobject_to_iobject (
	acpi_object             *obj,
	acpi_operand_object     **internal_obj);

acpi_status
acpi_ut_copy_isimple_to_isimple (
	acpi_operand_object     *source_obj,
	acpi_operand_object     *dest_obj);

acpi_status
acpi_ut_copy_ipackage_to_ipackage (
	acpi_operand_object     *source_obj,
	acpi_operand_object     *dest_obj,
	acpi_walk_state         *walk_state);


/*
 * Ut_create - Object creation
 */

acpi_status
acpi_ut_update_object_reference (
	acpi_operand_object     *object,
	u16                     action);


/*
 * Ut_debug - Debug interfaces
 */

void
acpi_ut_init_stack_ptr_trace (
	void);

void
acpi_ut_track_stack_ptr (
	void);

void
acpi_ut_trace (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info);

void
acpi_ut_trace_ptr (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	void                    *pointer);

void
acpi_ut_trace_u32 (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	u32                     integer);

void
acpi_ut_trace_str (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	NATIVE_CHAR             *string);

void
acpi_ut_exit (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info);

void
acpi_ut_status_exit (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	acpi_status             status);

void
acpi_ut_value_exit (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	acpi_integer            value);

void
acpi_ut_ptr_exit (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	u8                      *ptr);

void
acpi_ut_report_info (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id);

void
acpi_ut_report_error (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id);

void
acpi_ut_report_warning (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id);

void
acpi_ut_dump_buffer (
	u8                      *buffer,
	u32                     count,
	u32                     display,
	u32                     component_id);

void
acpi_ut_debug_print (
	u32                     requested_debug_level,
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	char                    *format,
	...) ACPI_PRINTF_LIKE_FUNC;

void
acpi_ut_debug_print_raw (
	u32                     requested_debug_level,
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	char                    *format,
	...) ACPI_PRINTF_LIKE_FUNC;


/*
 * Ut_delete - Object deletion
 */

void
acpi_ut_delete_internal_obj (
	acpi_operand_object     *object);

void
acpi_ut_delete_internal_package_object (
	acpi_operand_object     *object);

void
acpi_ut_delete_internal_simple_object (
	acpi_operand_object     *object);

acpi_status
acpi_ut_delete_internal_object_list (
	acpi_operand_object     **obj_list);


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
#define METHOD_NAME__PRT        "_PRT"


acpi_status
acpi_ut_evaluate_numeric_object (
	NATIVE_CHAR             *object_name,
	acpi_namespace_node     *device_node,
	acpi_integer            *address);

acpi_status
acpi_ut_execute_HID (
	acpi_namespace_node     *device_node,
	acpi_device_id          *hid);

acpi_status
acpi_ut_execute_STA (
	acpi_namespace_node     *device_node,
	u32                     *status_flags);

acpi_status
acpi_ut_execute_UID (
	acpi_namespace_node     *device_node,
	acpi_device_id          *uid);


/*
 * Ut_mutex - mutual exclusion interfaces
 */

acpi_status
acpi_ut_mutex_initialize (
	void);

void
acpi_ut_mutex_terminate (
	void);

acpi_status
acpi_ut_create_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

acpi_status
acpi_ut_delete_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

acpi_status
acpi_ut_acquire_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);

acpi_status
acpi_ut_release_mutex (
	ACPI_MUTEX_HANDLE       mutex_id);


/*
 * Ut_object - internal object create/delete/cache routines
 */

acpi_operand_object  *
acpi_ut_create_internal_object_dbg (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	acpi_object_type8       type);

void *
acpi_ut_allocate_object_desc_dbg (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id);

#define acpi_ut_create_internal_object(t) acpi_ut_create_internal_object_dbg (_THIS_MODULE,__LINE__,_COMPONENT,t)
#define acpi_ut_allocate_object_desc()  acpi_ut_allocate_object_desc_dbg (_THIS_MODULE,__LINE__,_COMPONENT)

void
acpi_ut_delete_object_desc (
	acpi_operand_object     *object);

u8
acpi_ut_valid_internal_object (
	void                    *object);


/*
 * Ut_ref_cnt - Object reference count management
 */

void
acpi_ut_add_reference (
	acpi_operand_object     *object);

void
acpi_ut_remove_reference (
	acpi_operand_object     *object);

/*
 * Ut_size - Object size routines
 */

acpi_status
acpi_ut_get_simple_object_size (
	acpi_operand_object     *obj,
	u32                     *obj_length);

acpi_status
acpi_ut_get_package_object_size (
	acpi_operand_object     *obj,
	u32                     *obj_length);

acpi_status
acpi_ut_get_object_size(
	acpi_operand_object     *obj,
	u32                     *obj_length);


/*
 * Ut_state - Generic state creation/cache routines
 */

void
acpi_ut_push_generic_state (
	acpi_generic_state      **list_head,
	acpi_generic_state      *state);

acpi_generic_state *
acpi_ut_pop_generic_state (
	acpi_generic_state      **list_head);


acpi_generic_state *
acpi_ut_create_generic_state (
	void);

acpi_generic_state *
acpi_ut_create_update_state (
	acpi_operand_object     *object,
	u16                     action);

acpi_generic_state *
acpi_ut_create_pkg_state (
	void                    *internal_object,
	void                    *external_object,
	u16                     index);

acpi_status
acpi_ut_create_update_state_and_push (
	acpi_operand_object     *object,
	u16                     action,
	acpi_generic_state      **state_list);

acpi_status
acpi_ut_create_pkg_state_and_push (
	void                    *internal_object,
	void                    *external_object,
	u16                     index,
	acpi_generic_state      **state_list);

acpi_generic_state *
acpi_ut_create_control_state (
	void);

void
acpi_ut_delete_generic_state (
	acpi_generic_state      *state);

void
acpi_ut_delete_generic_state_cache (
	void);

void
acpi_ut_delete_object_cache (
	void);

/*
 * utmisc
 */

acpi_status
acpi_ut_divide (
	acpi_integer            *in_dividend,
	acpi_integer            *in_divisor,
	acpi_integer            *out_quotient,
	acpi_integer            *out_remainder);

acpi_status
acpi_ut_short_divide (
	acpi_integer            *in_dividend,
	u32                     divisor,
	acpi_integer            *out_quotient,
	u32                     *out_remainder);

u8
acpi_ut_valid_acpi_name (
	u32                     name);

u8
acpi_ut_valid_acpi_character (
	NATIVE_CHAR             character);

NATIVE_CHAR *
acpi_ut_strupr (
	NATIVE_CHAR             *src_string);

acpi_status
acpi_ut_resolve_package_references (
	acpi_operand_object     *obj_desc);


#ifdef ACPI_DEBUG
void
acpi_ut_display_init_pathname (
	acpi_handle             obj_handle,
	char                    *path);

#endif


/*
 * Utalloc - memory allocation and object caching
 */

void *
acpi_ut_acquire_from_cache (
	u32                     list_id);

void
acpi_ut_release_to_cache (
	u32                     list_id,
	void                    *object);

void
acpi_ut_delete_generic_cache (
	u32                     list_id);


/* Debug Memory allocation functions */

void *
acpi_ut_allocate (
	u32                     size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line);

void *
acpi_ut_callocate (
	u32                     size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line);

void
acpi_ut_free (
	void                    *address,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line);

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
void
acpi_ut_dump_allocation_info (
	void);

void
acpi_ut_dump_allocations (
	u32                     component,
	NATIVE_CHAR             *module);
#endif


#endif /* _ACUTILS_H */
