/******************************************************************************
 *
 * Name: acmacros.h - C macros for the entire subsystem.
 *       $Revision: 97 $
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

#ifndef __ACMACROS_H__
#define __ACMACROS_H__


/*
 * Data manipulation macros
 */

#ifndef LOWORD
#define LOWORD(l)                       ((u16)(NATIVE_UINT)(l))
#endif

#ifndef HIWORD
#define HIWORD(l)                       ((u16)((((NATIVE_UINT)(l)) >> 16) & 0xFFFF))
#endif

#ifndef LOBYTE
#define LOBYTE(l)                       ((u8)(u16)(l))
#endif

#ifndef HIBYTE
#define HIBYTE(l)                       ((u8)((((u16)(l)) >> 8) & 0xFF))
#endif

#define BIT0(x)                         ((((x) & 0x01) > 0) ? 1 : 0)
#define BIT1(x)                         ((((x) & 0x02) > 0) ? 1 : 0)
#define BIT2(x)                         ((((x) & 0x04) > 0) ? 1 : 0)

#define BIT3(x)                         ((((x) & 0x08) > 0) ? 1 : 0)
#define BIT4(x)                         ((((x) & 0x10) > 0) ? 1 : 0)
#define BIT5(x)                         ((((x) & 0x20) > 0) ? 1 : 0)
#define BIT6(x)                         ((((x) & 0x40) > 0) ? 1 : 0)
#define BIT7(x)                         ((((x) & 0x80) > 0) ? 1 : 0)

#define LOW_BASE(w)                     ((u16) ((w) & 0x0000FFFF))
#define MID_BASE(b)                     ((u8) (((b) & 0x00FF0000) >> 16))
#define HI_BASE(b)                      ((u8) (((b) & 0xFF000000) >> 24))
#define LOW_LIMIT(w)                    ((u16) ((w) & 0x0000FFFF))
#define HI_LIMIT(b)                     ((u8) (((b) & 0x00FF0000) >> 16))


#ifdef _IA16
/*
 * For 16-bit addresses, we have to assume that the upper 32 bits
 * are zero.
 */
#ifndef LODWORD
#define LODWORD(l)                      (l)
#endif

#ifndef HIDWORD
#define HIDWORD(l)                      (0)
#endif

#define ACPI_GET_ADDRESS(a)             ((a).lo)
#define ACPI_STORE_ADDRESS(a,b)         {(a).hi=0;(a).lo=(b);}
#define ACPI_VALID_ADDRESS(a)           ((a).hi | (a).lo)

#else
#ifdef ACPI_NO_INTEGER64_SUPPORT
/*
 * acpi_integer is 32-bits, no 64-bit support on this platform
 */
#ifndef LODWORD
#define LODWORD(l)                      ((u32)(l))
#endif

#ifndef HIDWORD
#define HIDWORD(l)                      (0)
#endif

#define ACPI_GET_ADDRESS(a)             (a)
#define ACPI_STORE_ADDRESS(a,b)         ((a)=(b))
#define ACPI_VALID_ADDRESS(a)           (a)

#else

/*
 * Full 64-bit address/integer on both 32-bit and 64-bit platforms
 */
#ifndef LODWORD
#define LODWORD(l)                      ((u32)(u64)(l))
#endif

#ifndef HIDWORD
#define HIDWORD(l)                      ((u32)(((*(uint64_struct *)(&l))).hi))
#endif

#define ACPI_GET_ADDRESS(a)             (a)
#define ACPI_STORE_ADDRESS(a,b)         ((a)=(b))
#define ACPI_VALID_ADDRESS(a)           (a)
#endif
#endif

 /*
  * Extract a byte of data using a pointer.  Any more than a byte and we
  * get into potential aligment issues -- see the STORE macros below
  */
#define GET8(addr)                      (*(u8*)(addr))

/* Pointer arithmetic */


#define POINTER_ADD(t,a,b)              (t *) ((NATIVE_UINT)(a) + (NATIVE_UINT)(b))
#define POINTER_DIFF(a,b)               ((u32) ((NATIVE_UINT)(a) - (NATIVE_UINT)(b)))

/*
 * Macros for moving data around to/from buffers that are possibly unaligned.
 * If the hardware supports the transfer of unaligned data, just do the store.
 * Otherwise, we have to move one byte at a time.
 */

#ifdef _HW_ALIGNMENT_SUPPORT

/* The hardware supports unaligned transfers, just do the move */

#define MOVE_UNALIGNED16_TO_16(d,s)     *(u16*)(d) = *(u16*)(s)
#define MOVE_UNALIGNED32_TO_32(d,s)     *(u32*)(d) = *(u32*)(s)
#define MOVE_UNALIGNED16_TO_32(d,s)     *(u32*)(d) = *(u16*)(s)
#define MOVE_UNALIGNED64_TO_64(d,s)     *(u64*)(d) = *(u64*)(s)

#else
/*
 * The hardware does not support unaligned transfers.  We must move the
 * data one byte at a time.  These macros work whether the source or
 * the destination (or both) is/are unaligned.
 */

#define MOVE_UNALIGNED16_TO_16(d,s)     {((u8 *)(d))[0] = ((u8 *)(s))[0];\
	 ((u8 *)(d))[1] = ((u8 *)(s))[1];}

#define MOVE_UNALIGNED32_TO_32(d,s)     {((u8 *)(d))[0] = ((u8 *)(s))[0];\
			  ((u8 *)(d))[1] = ((u8 *)(s))[1];\
			  ((u8 *)(d))[2] = ((u8 *)(s))[2];\
			  ((u8 *)(d))[3] = ((u8 *)(s))[3];}

#define MOVE_UNALIGNED16_TO_32(d,s)     {(*(u32*)(d)) = 0; MOVE_UNALIGNED16_TO_16(d,s);}

#define MOVE_UNALIGNED64_TO_64(d,s)     {((u8 *)(d))[0] = ((u8 *)(s))[0];\
					   ((u8 *)(d))[1] = ((u8 *)(s))[1];\
					   ((u8 *)(d))[2] = ((u8 *)(s))[2];\
					   ((u8 *)(d))[3] = ((u8 *)(s))[3];\
					   ((u8 *)(d))[4] = ((u8 *)(s))[4];\
					   ((u8 *)(d))[5] = ((u8 *)(s))[5];\
					   ((u8 *)(d))[6] = ((u8 *)(s))[6];\
					   ((u8 *)(d))[7] = ((u8 *)(s))[7];}

#endif


/*
 * Fast power-of-two math macros for non-optimized compilers
 */

#define _DIV(value,power_of2)           ((u32) ((value) >> (power_of2)))
#define _MUL(value,power_of2)           ((u32) ((value) << (power_of2)))
#define _MOD(value,divisor)             ((u32) ((value) & ((divisor) -1)))

#define DIV_2(a)                        _DIV(a,1)
#define MUL_2(a)                        _MUL(a,1)
#define MOD_2(a)                        _MOD(a,2)

#define DIV_4(a)                        _DIV(a,2)
#define MUL_4(a)                        _MUL(a,2)
#define MOD_4(a)                        _MOD(a,4)

#define DIV_8(a)                        _DIV(a,3)
#define MUL_8(a)                        _MUL(a,3)
#define MOD_8(a)                        _MOD(a,8)

#define DIV_16(a)                       _DIV(a,4)
#define MUL_16(a)                       _MUL(a,4)
#define MOD_16(a)                       _MOD(a,16)


/*
 * Rounding macros (Power of two boundaries only)
 */
#define ROUND_DOWN(value,boundary)      ((value) & (~((boundary)-1)))
#define ROUND_UP(value,boundary)        (((value) + ((boundary)-1)) & (~((boundary)-1)))

#define ROUND_DOWN_TO_32_BITS(a)        ROUND_DOWN(a,4)
#define ROUND_DOWN_TO_64_BITS(a)        ROUND_DOWN(a,8)
#define ROUND_DOWN_TO_NATIVE_WORD(a)    ROUND_DOWN(a,ALIGNED_ADDRESS_BOUNDARY)

#define ROUND_UP_TO_32_bITS(a)          ROUND_UP(a,4)
#define ROUND_UP_TO_64_bITS(a)          ROUND_UP(a,8)
#define ROUND_UP_TO_NATIVE_WORD(a)      ROUND_UP(a,ALIGNED_ADDRESS_BOUNDARY)

#define ROUND_PTR_UP_TO_4(a,b)          ((b *)(((NATIVE_UINT)(a) + 3) & ~3))
#define ROUND_PTR_UP_TO_8(a,b)          ((b *)(((NATIVE_UINT)(a) + 7) & ~7))

#define ROUND_BITS_UP_TO_BYTES(a)       DIV_8((a) + 7)
#define ROUND_BITS_DOWN_TO_BYTES(a)     DIV_8((a))

#define ROUND_UP_TO_1K(a)               (((a) + 1023) >> 10)

/* Generic (non-power-of-two) rounding */

#define ROUND_UP_TO(value,boundary)     (((value) + ((boundary)-1)) / (boundary))

/*
 * Bitmask creation
 * Bit positions start at zero.
 * MASK_BITS_ABOVE creates a mask starting AT the position and above
 * MASK_BITS_BELOW creates a mask starting one bit BELOW the position
 */
#define MASK_BITS_ABOVE(position)       (~(((u32)(-1)) << ((u32) (position))))
#define MASK_BITS_BELOW(position)       (((u32)(-1)) << ((u32) (position)))


/* Macros for GAS addressing */

#ifndef _IA16

#define ACPI_PCI_DEVICE_MASK            (u64) 0x0000FFFF00000000
#define ACPI_PCI_FUNCTION_MASK          (u64) 0x00000000FFFF0000
#define ACPI_PCI_REGISTER_MASK          (u64) 0x000000000000FFFF

#define ACPI_PCI_FUNCTION(a)            (u16) ((((a) & ACPI_PCI_FUNCTION_MASK) >> 16))
#define ACPI_PCI_DEVICE(a)              (u16) ((((a) & ACPI_PCI_DEVICE_MASK) >> 32))
#define ACPI_PCI_REGISTER(a)            (u16) (((a) & ACPI_PCI_REGISTER_MASK))

#else

/* No support for GAS and PCI IDs in 16-bit mode  */

#define ACPI_PCI_FUNCTION(a)            (u16) ((a) & 0xFFFF0000)
#define ACPI_PCI_DEVICE(a)              (u16) ((a) & 0x0000FFFF)
#define ACPI_PCI_REGISTER(a)            (u16) ((a) & 0x0000FFFF)

#endif

/*
 * An acpi_handle (which is actually an acpi_namespace_node *) can appear in some contexts,
 * such as on ap_obj_stack, where a pointer to an acpi_operand_object can also
 * appear.  This macro is used to distinguish them.
 *
 * The Data_type field is the first field in both structures.
 */
#define VALID_DESCRIPTOR_TYPE(d,t)      (((acpi_namespace_node *)d)->data_type == t)


/* Macro to test the object type */

#define IS_THIS_OBJECT_TYPE(d,t)        (((acpi_operand_object  *)d)->common.type == (u8)t)

/* Macro to check the table flags for SINGLE or MULTIPLE tables are allowed */

#define IS_SINGLE_TABLE(x)              (((x) & 0x01) == ACPI_TABLE_SINGLE ? 1 : 0)

/*
 * Macro to check if a pointer is within an ACPI table.
 * Parameter (a) is the pointer to check.  Parameter (b) must be defined
 * as a pointer to an acpi_table_header.  (b+1) then points past the header,
 * and ((u8 *)b+b->Length) points one byte past the end of the table.
 */
#ifndef _IA16
#define IS_IN_ACPI_TABLE(a,b)           (((u8 *)(a) >= (u8 *)(b + 1)) &&\
							   ((u8 *)(a) < ((u8 *)b + b->length)))

#else
#define IS_IN_ACPI_TABLE(a,b)           (_segment)(a) == (_segment)(b) &&\
									 (((u8 *)(a) >= (u8 *)(b + 1)) &&\
									 ((u8 *)(a) < ((u8 *)b + b->length)))
#endif

/*
 * Macros for the master AML opcode table
 */
#ifdef ACPI_DEBUG
#define ACPI_OP(name,Pargs,Iargs,class,type,flags)     {Pargs,Iargs,flags,class,type,name}
#else
#define ACPI_OP(name,Pargs,Iargs,class,type,flags)     {Pargs,Iargs,flags,class,type}
#endif

#define ARG_TYPE_WIDTH                  5
#define ARG_1(x)                        ((u32)(x))
#define ARG_2(x)                        ((u32)(x) << (1 * ARG_TYPE_WIDTH))
#define ARG_3(x)                        ((u32)(x) << (2 * ARG_TYPE_WIDTH))
#define ARG_4(x)                        ((u32)(x) << (3 * ARG_TYPE_WIDTH))
#define ARG_5(x)                        ((u32)(x) << (4 * ARG_TYPE_WIDTH))
#define ARG_6(x)                        ((u32)(x) << (5 * ARG_TYPE_WIDTH))

#define ARGI_LIST1(a)                   (ARG_1(a))
#define ARGI_LIST2(a,b)                 (ARG_1(b)|ARG_2(a))
#define ARGI_LIST3(a,b,c)               (ARG_1(c)|ARG_2(b)|ARG_3(a))
#define ARGI_LIST4(a,b,c,d)             (ARG_1(d)|ARG_2(c)|ARG_3(b)|ARG_4(a))
#define ARGI_LIST5(a,b,c,d,e)           (ARG_1(e)|ARG_2(d)|ARG_3(c)|ARG_4(b)|ARG_5(a))
#define ARGI_LIST6(a,b,c,d,e,f)         (ARG_1(f)|ARG_2(e)|ARG_3(d)|ARG_4(c)|ARG_5(b)|ARG_6(a))

#define ARGP_LIST1(a)                   (ARG_1(a))
#define ARGP_LIST2(a,b)                 (ARG_1(a)|ARG_2(b))
#define ARGP_LIST3(a,b,c)               (ARG_1(a)|ARG_2(b)|ARG_3(c))
#define ARGP_LIST4(a,b,c,d)             (ARG_1(a)|ARG_2(b)|ARG_3(c)|ARG_4(d))
#define ARGP_LIST5(a,b,c,d,e)           (ARG_1(a)|ARG_2(b)|ARG_3(c)|ARG_4(d)|ARG_5(e))
#define ARGP_LIST6(a,b,c,d,e,f)         (ARG_1(a)|ARG_2(b)|ARG_3(c)|ARG_4(d)|ARG_5(e)|ARG_6(f))

#define GET_CURRENT_ARG_TYPE(list)      (list & ((u32) 0x1F))
#define INCREMENT_ARG_LIST(list)        (list >>= ((u32) ARG_TYPE_WIDTH))


/*
 * Build a GAS structure from earlier ACPI table entries (V1.0 and 0.71 extensions)
 *
 * 1) Address space
 * 2) Length in bytes -- convert to length in bits
 * 3) Bit offset is zero
 * 4) Reserved field is zero
 * 5) Expand address to 64 bits
 */
#define ASL_BUILD_GAS_FROM_ENTRY(a,b,c,d)   {a.address_space_id = (u8) d;\
											 a.register_bit_width = (u8) MUL_8 (b);\
											 a.register_bit_offset = 0;\
											 a.reserved = 0;\
											 ACPI_STORE_ADDRESS (a.address,c);}

/* ACPI V1.0 entries -- address space is always I/O */

#define ASL_BUILD_GAS_FROM_V1_ENTRY(a,b,c)  ASL_BUILD_GAS_FROM_ENTRY(a,b,c,ACPI_ADR_SPACE_SYSTEM_IO)


/*
 * Reporting macros that are never compiled out
 */

#define PARAM_LIST(pl)                  pl

/*
 * Error reporting.  These versions add callers module and line#.  Since
 * _THIS_MODULE gets compiled out when ACPI_DEBUG isn't defined, only
 * use it in debug mode.
 */

#ifdef ACPI_DEBUG

#define REPORT_INFO(fp)                 {acpi_ut_report_info(_THIS_MODULE,__LINE__,_COMPONENT); \
											acpi_os_printf PARAM_LIST(fp);}
#define REPORT_ERROR(fp)                {acpi_ut_report_error(_THIS_MODULE,__LINE__,_COMPONENT); \
											acpi_os_printf PARAM_LIST(fp);}
#define REPORT_WARNING(fp)              {acpi_ut_report_warning(_THIS_MODULE,__LINE__,_COMPONENT); \
											acpi_os_printf PARAM_LIST(fp);}

#else

#define REPORT_INFO(fp)                 {acpi_ut_report_info("ACPI",__LINE__,_COMPONENT); \
											acpi_os_printf PARAM_LIST(fp);}
#define REPORT_ERROR(fp)                {acpi_ut_report_error("ACPI",__LINE__,_COMPONENT); \
											acpi_os_printf PARAM_LIST(fp);}
#define REPORT_WARNING(fp)              {acpi_ut_report_warning("ACPI",__LINE__,_COMPONENT); \
											acpi_os_printf PARAM_LIST(fp);}

#endif

/* Error reporting.  These versions pass thru the module and line# */

#define _REPORT_INFO(a,b,c,fp)          {acpi_ut_report_info(a,b,c); \
											acpi_os_printf PARAM_LIST(fp);}
#define _REPORT_ERROR(a,b,c,fp)         {acpi_ut_report_error(a,b,c); \
											acpi_os_printf PARAM_LIST(fp);}
#define _REPORT_WARNING(a,b,c,fp)       {acpi_ut_report_warning(a,b,c); \
											acpi_os_printf PARAM_LIST(fp);}

/*
 * Debug macros that are conditionally compiled
 */

#ifdef ACPI_DEBUG

#define MODULE_NAME(name)               static char *_THIS_MODULE = name;

/*
 * Function entry tracing.
 * The first parameter should be the procedure name as a quoted string.  This is declared
 * as a local string ("_Proc_name) so that it can be also used by the function exit macros below.
 */

#define PROC_NAME(a)                    acpi_debug_print_info _dbg;     \
										_dbg.component_id = _COMPONENT; \
										_dbg.proc_name   = a;           \
										_dbg.module_name = _THIS_MODULE;

#define FUNCTION_TRACE(a)               PROC_NAME(a)\
										acpi_ut_trace(__LINE__,&_dbg)
#define FUNCTION_TRACE_PTR(a,b)         PROC_NAME(a)\
										acpi_ut_trace_ptr(__LINE__,&_dbg,(void *)b)
#define FUNCTION_TRACE_U32(a,b)         PROC_NAME(a)\
										acpi_ut_trace_u32(__LINE__,&_dbg,(u32)b)
#define FUNCTION_TRACE_STR(a,b)         PROC_NAME(a)\
										acpi_ut_trace_str(__LINE__,&_dbg,(NATIVE_CHAR *)b)

#define FUNCTION_ENTRY()                acpi_ut_track_stack_ptr()

/*
 * Function exit tracing.
 * WARNING: These macros include a return statement.  This is usually considered
 * bad form, but having a separate exit macro is very ugly and difficult to maintain.
 * One of the FUNCTION_TRACE macros above must be used in conjunction with these macros
 * so that "_Proc_name" is defined.
 */
#define return_VOID                     {acpi_ut_exit(__LINE__,&_dbg);return;}
#define return_ACPI_STATUS(s)           {acpi_ut_status_exit(__LINE__,&_dbg,s);return(s);}
#define return_VALUE(s)                 {acpi_ut_value_exit(__LINE__,&_dbg,s);return(s);}
#define return_PTR(s)                   {acpi_ut_ptr_exit(__LINE__,&_dbg,(u8 *)s);return(s);}


/* Conditional execution */

#define DEBUG_EXEC(a)                   a
#define NORMAL_EXEC(a)

#define DEBUG_DEFINE(a)                 a;
#define DEBUG_ONLY_MEMBERS(a)           a;
#define _OPCODE_NAMES
#define _VERBOSE_STRUCTURES


/* Stack and buffer dumping */

#define DUMP_STACK_ENTRY(a)             acpi_ex_dump_operand(a)
#define DUMP_OPERANDS(a,b,c,d,e)        acpi_ex_dump_operands(a,b,c,d,e,_THIS_MODULE,__LINE__)


#define DUMP_ENTRY(a,b)                 acpi_ns_dump_entry (a,b)
#define DUMP_TABLES(a,b)                acpi_ns_dump_tables(a,b)
#define DUMP_PATHNAME(a,b,c,d)          acpi_ns_dump_pathname(a,b,c,d)
#define DUMP_RESOURCE_LIST(a)           acpi_rs_dump_resource_list(a)
#define DUMP_BUFFER(a,b)                acpi_ut_dump_buffer((u8 *)a,b,DB_BYTE_DISPLAY,_COMPONENT)
#define BREAK_MSG(a)                    acpi_os_signal (ACPI_SIGNAL_BREAKPOINT,(a))


/*
 * Generate INT3 on ACPI_ERROR (Debug only!)
 */

#define ERROR_BREAK
#ifdef  ERROR_BREAK
#define BREAK_ON_ERROR(lvl)              if ((lvl)&ACPI_ERROR) acpi_os_signal(ACPI_SIGNAL_BREAKPOINT,"Fatal error encountered\n")
#else
#define BREAK_ON_ERROR(lvl)
#endif

/*
 * Master debug print macros
 * Print iff:
 *    1) Debug print for the current component is enabled
 *    2) Debug error level or trace level for the print statement is enabled
 */

#define ACPI_DEBUG_PRINT(pl)            acpi_ut_debug_print PARAM_LIST(pl)
#define ACPI_DEBUG_PRINT_RAW(pl)        acpi_ut_debug_print_raw PARAM_LIST(pl)


#else
/*
 * This is the non-debug case -- make everything go away,
 * leaving no executable debug code!
 */

#define MODULE_NAME(name)
#define _THIS_MODULE ""

#define DEBUG_EXEC(a)
#define NORMAL_EXEC(a)                  a;

#define DEBUG_DEFINE(a)
#define DEBUG_ONLY_MEMBERS(a)
#define PROC_NAME(a)
#define FUNCTION_TRACE(a)
#define FUNCTION_TRACE_PTR(a,b)
#define FUNCTION_TRACE_U32(a,b)
#define FUNCTION_TRACE_STR(a,b)
#define FUNCTION_EXIT
#define FUNCTION_STATUS_EXIT(s)
#define FUNCTION_VALUE_EXIT(s)
#define FUNCTION_ENTRY()
#define DUMP_STACK_ENTRY(a)
#define DUMP_OPERANDS(a,b,c,d,e)
#define DUMP_ENTRY(a,b)
#define DUMP_TABLES(a,b)
#define DUMP_PATHNAME(a,b,c,d)
#define DUMP_RESOURCE_LIST(a)
#define DUMP_BUFFER(a,b)
#define ACPI_DEBUG_PRINT(pl)
#define ACPI_DEBUG_PRINT_RAW(pl)
#define BREAK_MSG(a)

#define return_VOID                     return
#define return_ACPI_STATUS(s)           return(s)
#define return_VALUE(s)                 return(s)
#define return_PTR(s)                   return(s)

#endif

/*
 * Some code only gets executed when the debugger is built in.
 * Note that this is entirely independent of whether the
 * DEBUG_PRINT stuff (set by ACPI_DEBUG) is on, or not.
 */
#ifdef ENABLE_DEBUGGER
#define DEBUGGER_EXEC(a)                a
#else
#define DEBUGGER_EXEC(a)
#endif


/*
 * For 16-bit code, we want to shrink some things even though
 * we are using ACPI_DEBUG to get the debug output
 */
#ifdef _IA16
#undef DEBUG_ONLY_MEMBERS
#undef _VERBOSE_STRUCTURES
#define DEBUG_ONLY_MEMBERS(a)
#endif


#ifdef ACPI_DEBUG
/*
 * 1) Set name to blanks
 * 2) Copy the object name
 */
#define ADD_OBJECT_NAME(a,b)            MEMSET (a->common.name, ' ', sizeof (a->common.name));\
										STRNCPY (a->common.name, acpi_gbl_ns_type_names[b], sizeof (a->common.name))
#else

#define ADD_OBJECT_NAME(a,b)
#endif


/*
 * Memory allocation tracking (DEBUG ONLY)
 */

#ifndef ACPI_DBG_TRACK_ALLOCATIONS

/* Memory allocation */

#define ACPI_MEM_ALLOCATE(a)            acpi_os_allocate(a)
#define ACPI_MEM_CALLOCATE(a)           acpi_os_callocate(a)
#define ACPI_MEM_FREE(a)                acpi_os_free(a)
#define ACPI_MEM_TRACKING(a)


#else

/* Memory allocation */

#define ACPI_MEM_ALLOCATE(a)            acpi_ut_allocate(a,_COMPONENT,_THIS_MODULE,__LINE__)
#define ACPI_MEM_CALLOCATE(a)           acpi_ut_callocate(a, _COMPONENT,_THIS_MODULE,__LINE__)
#define ACPI_MEM_FREE(a)                acpi_ut_free(a,_COMPONENT,_THIS_MODULE,__LINE__)
#define ACPI_MEM_TRACKING(a)            a

#endif /* ACPI_DBG_TRACK_ALLOCATIONS */


#define ACPI_GET_STACK_POINTER          _asm {mov eax, ebx}

#endif /* ACMACROS_H */
