/******************************************************************************
 *
 * Name: acglobal.h - Declarations for global variables
 *       $Revision: 130 $
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

#ifndef __ACGLOBAL_H__
#define __ACGLOBAL_H__


/*
 * Ensure that the globals are actually defined only once.
 *
 * The use of these defines allows a single list of globals (here) in order
 * to simplify maintenance of the code.
 */
#ifdef DEFINE_ACPI_GLOBALS
#define ACPI_EXTERN
#else
#define ACPI_EXTERN extern
#endif


/*****************************************************************************
 *
 * Debug support
 *
 ****************************************************************************/

/* Runtime configuration of debug print levels */

extern      u32                         acpi_dbg_level;
extern      u32                         acpi_dbg_layer;

/* Procedure nesting level for debug output */

extern      u32                         acpi_gbl_nesting_level;


/*****************************************************************************
 *
 * ACPI Table globals
 *
 ****************************************************************************/

/*
 * Table pointers.
 * Although these pointers are somewhat redundant with the global Acpi_table,
 * they are convenient because they are typed pointers.
 *
 * These tables are single-table only; meaning that there can be at most one
 * of each in the system.  Each global points to the actual table.
 *
 */
ACPI_EXTERN u32                         acpi_gbl_table_flags;
ACPI_EXTERN u32                         acpi_gbl_rsdt_table_count;
ACPI_EXTERN RSDP_DESCRIPTOR            *acpi_gbl_RSDP;
ACPI_EXTERN xsdt_descriptor            *acpi_gbl_XSDT;
ACPI_EXTERN FADT_DESCRIPTOR            *acpi_gbl_FADT;
ACPI_EXTERN acpi_table_header          *acpi_gbl_DSDT;
ACPI_EXTERN FACS_DESCRIPTOR            *acpi_gbl_FACS;
ACPI_EXTERN acpi_common_facs            acpi_gbl_common_fACS;

/*
 * Handle both ACPI 1.0 and ACPI 2.0 Integer widths
 * If we are running a method that exists in a 32-bit ACPI table.
 * Use only 32 bits of the Integer for conversion.
 */
ACPI_EXTERN u8                          acpi_gbl_integer_bit_width;
ACPI_EXTERN u8                          acpi_gbl_integer_byte_width;

/*
 * Since there may be multiple SSDTs and PSDTS, a single pointer is not
 * sufficient; Therefore, there isn't one!
 */


/*
 * ACPI Table info arrays
 */
extern      acpi_table_desc             acpi_gbl_acpi_tables[NUM_ACPI_TABLES];
extern      ACPI_TABLE_SUPPORT          acpi_gbl_acpi_table_data[NUM_ACPI_TABLES];

/*
 * Predefined mutex objects.  This array contains the
 * actual OS mutex handles, indexed by the local ACPI_MUTEX_HANDLEs.
 * (The table maps local handles to the real OS handles)
 */
ACPI_EXTERN acpi_mutex_info             acpi_gbl_acpi_mutex_info [NUM_MTX];


/*****************************************************************************
 *
 * Miscellaneous globals
 *
 ****************************************************************************/


ACPI_EXTERN ACPI_MEMORY_LIST            acpi_gbl_memory_lists[ACPI_NUM_MEM_LISTS];
ACPI_EXTERN ACPI_OBJECT_NOTIFY_HANDLER  acpi_gbl_drv_notify;
ACPI_EXTERN ACPI_OBJECT_NOTIFY_HANDLER  acpi_gbl_sys_notify;
ACPI_EXTERN ACPI_INIT_HANDLER           acpi_gbl_init_handler;
ACPI_EXTERN acpi_walk_state            *acpi_gbl_breakpoint_walk;
ACPI_EXTERN acpi_handle                 acpi_gbl_global_lock_semaphore;

ACPI_EXTERN u32                         acpi_gbl_global_lock_thread_count;
ACPI_EXTERN u32                         acpi_gbl_original_mode;
ACPI_EXTERN u32                         acpi_gbl_rsdp_original_location;
ACPI_EXTERN u32                         acpi_gbl_ns_lookup_count;
ACPI_EXTERN u32                         acpi_gbl_ps_find_count;
ACPI_EXTERN u16                         acpi_gbl_pm1_enable_register_save;
ACPI_EXTERN u16                         acpi_gbl_next_table_owner_id;
ACPI_EXTERN u16                         acpi_gbl_next_method_owner_id;
ACPI_EXTERN u16                         acpi_gbl_global_lock_handle;
ACPI_EXTERN u8                          acpi_gbl_debugger_configuration;
ACPI_EXTERN u8                          acpi_gbl_global_lock_acquired;
ACPI_EXTERN u8                          acpi_gbl_step_to_next_call;
ACPI_EXTERN u8                          acpi_gbl_acpi_hardware_present;
ACPI_EXTERN u8                          acpi_gbl_global_lock_present;

extern u8                               acpi_gbl_shutdown;
extern u32                              acpi_gbl_startup_flags;
extern const u8                         acpi_gbl_decode_to8bit[8];
extern const NATIVE_CHAR               *acpi_gbl_db_sleep_states[ACPI_NUM_SLEEP_STATES];
extern const acpi_opcode_info           acpi_gbl_aml_op_info[AML_NUM_OPCODES];
extern const NATIVE_CHAR               *acpi_gbl_region_types[ACPI_NUM_PREDEFINED_REGIONS];


/*****************************************************************************
 *
 * Namespace globals
 *
 ****************************************************************************/

#define NUM_NS_TYPES                    INTERNAL_TYPE_INVALID+1

#if defined (ACPI_NO_METHOD_EXECUTION) || defined (ACPI_CONSTANT_EVAL_ONLY)
#define NUM_PREDEFINED_NAMES            10
#else
#define NUM_PREDEFINED_NAMES            9
#endif

ACPI_EXTERN acpi_namespace_node         acpi_gbl_root_node_struct;
ACPI_EXTERN acpi_namespace_node        *acpi_gbl_root_node;

extern const u8                         acpi_gbl_ns_properties[NUM_NS_TYPES];
extern const acpi_predefined_names      acpi_gbl_pre_defined_names [NUM_PREDEFINED_NAMES];

#ifdef ACPI_DEBUG_OUTPUT
ACPI_EXTERN u32                         acpi_gbl_current_node_count;
ACPI_EXTERN u32                         acpi_gbl_current_node_size;
ACPI_EXTERN u32                         acpi_gbl_max_concurrent_node_count;
ACPI_EXTERN ACPI_SIZE                   acpi_gbl_entry_stack_pointer;
ACPI_EXTERN ACPI_SIZE                   acpi_gbl_lowest_stack_pointer;
ACPI_EXTERN u32                         acpi_gbl_deepest_nesting;
#endif

/*****************************************************************************
 *
 * Interpreter globals
 *
 ****************************************************************************/


ACPI_EXTERN ACPI_THREAD_STATE           *acpi_gbl_current_walk_list;

/* Control method single step flag */

ACPI_EXTERN u8                          acpi_gbl_cm_single_step;


/*****************************************************************************
 *
 * Parser globals
 *
 ****************************************************************************/

ACPI_EXTERN acpi_parse_object           *acpi_gbl_parsed_namespace_root;

/*****************************************************************************
 *
 * Hardware globals
 *
 ****************************************************************************/

extern      ACPI_BIT_REGISTER_INFO      acpi_gbl_bit_register_info[ACPI_NUM_BITREG];
ACPI_EXTERN u8                          acpi_gbl_sleep_type_a;
ACPI_EXTERN u8                          acpi_gbl_sleep_type_b;


/*****************************************************************************
 *
 * Event and GPE globals
 *
 ****************************************************************************/

extern      acpi_fixed_event_info       acpi_gbl_fixed_event_info[ACPI_NUM_FIXED_EVENTS];
ACPI_EXTERN ACPI_FIXED_EVENT_HANDLER    acpi_gbl_fixed_event_handlers[ACPI_NUM_FIXED_EVENTS];

ACPI_EXTERN acpi_handle                 acpi_gbl_gpe_obj_handle;
ACPI_EXTERN u32                         acpi_gbl_gpe_register_count;
ACPI_EXTERN u32                         acpi_gbl_gpe_number_max;
ACPI_EXTERN ACPI_GPE_REGISTER_INFO     *acpi_gbl_gpe_register_info;
ACPI_EXTERN ACPI_GPE_NUMBER_INFO       *acpi_gbl_gpe_number_info;
ACPI_EXTERN ACPI_GPE_BLOCK_INFO         acpi_gbl_gpe_block_info[ACPI_MAX_GPE_BLOCKS];

/*
 * GPE translation table
 * Indexed by the GPE number, returns a valid index into the global GPE tables.
 *
 * This table is needed because the GPE numbers supported by block 1 do not
 * have to be contiguous with the GPE numbers supported by block 0.
 */
ACPI_EXTERN ACPI_GPE_INDEX_INFO        *acpi_gbl_gpe_number_to_index;


/*****************************************************************************
 *
 * Debugger globals
 *
 ****************************************************************************/


ACPI_EXTERN u8                          acpi_gbl_db_output_flags;

#ifdef ACPI_DISASSEMBLER

ACPI_EXTERN u8                          acpi_gbl_db_opt_disasm;
ACPI_EXTERN u8                          acpi_gbl_db_opt_verbose;
#endif


#ifdef ACPI_DEBUGGER

extern      u8                          acpi_gbl_method_executing;
extern      u8                          acpi_gbl_db_terminate_threads;

ACPI_EXTERN int                         optind;
ACPI_EXTERN NATIVE_CHAR                *optarg;

ACPI_EXTERN u8                          acpi_gbl_db_opt_tables;
ACPI_EXTERN u8                          acpi_gbl_db_opt_stats;
ACPI_EXTERN u8                          acpi_gbl_db_opt_ini_methods;


ACPI_EXTERN NATIVE_CHAR                *acpi_gbl_db_args[ACPI_DEBUGGER_MAX_ARGS];
ACPI_EXTERN NATIVE_CHAR                 acpi_gbl_db_line_buf[80];
ACPI_EXTERN NATIVE_CHAR                 acpi_gbl_db_parsed_buf[80];
ACPI_EXTERN NATIVE_CHAR                 acpi_gbl_db_scope_buf[40];
ACPI_EXTERN NATIVE_CHAR                 acpi_gbl_db_debug_filename[40];
ACPI_EXTERN u8                          acpi_gbl_db_output_to_file;
ACPI_EXTERN NATIVE_CHAR                *acpi_gbl_db_buffer;
ACPI_EXTERN NATIVE_CHAR                *acpi_gbl_db_filename;
ACPI_EXTERN u32                         acpi_gbl_db_debug_level;
ACPI_EXTERN u32                         acpi_gbl_db_console_debug_level;
ACPI_EXTERN acpi_table_header          *acpi_gbl_db_table_ptr;
ACPI_EXTERN acpi_namespace_node        *acpi_gbl_db_scope_node;

/*
 * Statistic globals
 */
ACPI_EXTERN u16                         acpi_gbl_obj_type_count[INTERNAL_TYPE_NODE_MAX+1];
ACPI_EXTERN u16                         acpi_gbl_node_type_count[INTERNAL_TYPE_NODE_MAX+1];
ACPI_EXTERN u16                         acpi_gbl_obj_type_count_misc;
ACPI_EXTERN u16                         acpi_gbl_node_type_count_misc;
ACPI_EXTERN u32                         acpi_gbl_num_nodes;
ACPI_EXTERN u32                         acpi_gbl_num_objects;


ACPI_EXTERN u32                         acpi_gbl_size_of_parse_tree;
ACPI_EXTERN u32                         acpi_gbl_size_of_method_trees;
ACPI_EXTERN u32                         acpi_gbl_size_of_node_entries;
ACPI_EXTERN u32                         acpi_gbl_size_of_acpi_objects;

#endif /* ACPI_DEBUGGER */


#endif /* __ACGLOBAL_H__ */
