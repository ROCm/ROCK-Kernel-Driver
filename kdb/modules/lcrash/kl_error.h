/*
 * $Id: kl_error.h 1169 2005-03-02 21:38:01Z tjm $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2005 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_ERROR_H
#define __KL_ERROR_H

extern uint64_t klib_error;
extern FILE *kl_stdout;
extern FILE *kl_stderr;

/* Error Classes
 */
#define KLEC_APP        0
#define KLEC_KLIB       1
#define KLEC_MEM	2
#define KLEC_SYM	3
#define KLEC_KERN	4

#define KLEC_CLASS_MASK 0x00000000ff000000ULL
#define KLEC_CLASS_SHIFT 24
#define KLEC_ECODE_MASK 0x0000000000ffffffULL
#define KLEC_TYPE_MASK  0xffffffff00000000ULL
#define KLEC_TYPE_SHIFT 32
#define KLEC_CLASS(e) ((e & KLEC_CLASS_MASK) >> KLEC_CLASS_SHIFT)
#define KLEC_ECODE(e) (e & KLEC_ECODE_MASK)
#define KLEC_TYPE(e) ((e & KLEC_TYPE_MASK) >> KLEC_TYPE_SHIFT)

void kl_reset_error(void);  /* reset klib_error */
void kl_print_error(void);  /* print warning/error messages */
void kl_check_error(char*); /* check for/handle errors, generate messages */

/* FIXME: not used yet -- for changes in future, improve error handling
 */
typedef struct klib_error_s{
	uint32_t code;      /* error code */
	uint16_t class;     /* error class */
	uint16_t severity;  /* severity of error: e.g. warning or fatal error */
	uint32_t datadesc;  /* description of data which caused the error */
	FILE     *fp;       /* fp where to place warning and error messages */
} klib_error_t;

/*
 * Some macros for accessing data in klib_error
 */
#define KL_ERROR 		klib_error
#define KL_ERRORFP 		kl_stderr

/* Error codes
 *
 * There are basically two types of error codes -- with each type
 * residing in a single word in a two word error code value. The lower
 * 32-bits contains an error class and code that represents exactly
 * WHAT error occurred (e.g., non-numeric text in a numeric value
 * entered by a user, bad virtual address, etc.).
 *
 * The upper 32-bits represents what type of data was being referenced
 * when the error occurred (e.g., bad proc struct). Having two tiers of
 * error codes makes it easier to generate useful and specific error
 * messages. Note that is possible to have situations where one or the
 * other type of error codes is not set. This is OK as long as at least
 * one type s set.
 */

/* General klib error codes
 */
#define KLE_KLIB (KLEC_KLIB << KLEC_CLASS_SHIFT)
#define KLE_NO_MEMORY				(KLE_KLIB|1)
#define KLE_OPEN_ERROR				(KLE_KLIB|2)
#define KLE_ZERO_BLOCK 				(KLE_KLIB|3)
#define KLE_INVALID_VALUE 			(KLE_KLIB|4)
#define KLE_NULL_BUFF 				(KLE_KLIB|5)
#define KLE_ZERO_SIZE 				(KLE_KLIB|6)
#define KLE_ACTIVE 				(KLE_KLIB|7)
#define KLE_NULL_POINTER 			(KLE_KLIB|8)
#define KLE_UNSUPPORTED_ARCH 			(KLE_KLIB|9)

#define KLE_MISC_ERROR 				(KLE_KLIB|97)
#define KLE_NOT_SUPPORTED 			(KLE_KLIB|98)
#define KLE_UNKNOWN_ERROR 			(KLE_KLIB|99)

/* memory error codes
 */
#define KLE_MEM (KLEC_MEM << KLEC_CLASS_SHIFT)
#define KLE_BAD_MAP_FILE			(KLE_MEM|1)
#define KLE_BAD_DUMP	  			(KLE_MEM|2)
#define KLE_BAD_DUMPTYPE			(KLE_MEM|3)
#define KLE_INVALID_LSEEK 			(KLE_MEM|4)
#define KLE_INVALID_READ 			(KLE_MEM|5)
#define KLE_BAD_KERNINFO 			(KLE_MEM|6)
#define KLE_INVALID_PADDR 			(KLE_MEM|7)
#define KLE_INVALID_VADDR 			(KLE_MEM|8)
#define KLE_INVALID_VADDR_ALIGN 		(KLE_MEM|9)
#define KLE_INVALID_MAPPING 		        (KLE_MEM|10)
#define KLE_CMP_ERROR 		        	(KLE_MEM|11)
#define KLE_INVALID_DUMP_MAGIC 		        (KLE_MEM|12)
#define KLE_KERNEL_MAGIC_MISMATCH               (KLE_MEM|13)
#define KLE_NO_END_SYMBOL                       (KLE_MEM|14)
#define KLE_INVALID_DUMP_HEADER			(KLE_MEM|15)
#define KLE_DUMP_INDEX_CREATION			(KLE_MEM|16)
#define KLE_DUMP_HEADER_ONLY			(KLE_MEM|17)
#define KLE_PAGE_NOT_PRESENT 		        (KLE_MEM|18)
#define KLE_BAD_ELF_FILE			(KLE_MEM|19)
#define KLE_ARCHIVE_FILE			(KLE_MEM|20)
#define KLE_MAP_FILE_PRESENT			(KLE_MEM|21)
#define KLE_BAD_MAP_FILENAME			(KLE_MEM|22)
#define KLE_BAD_DUMP_FILENAME			(KLE_MEM|23)
#define KLE_BAD_NAMELIST_FILE			(KLE_MEM|24)
#define KLE_BAD_NAMELIST_FILENAME		(KLE_MEM|25)
#define KLE_LIVE_SYSTEM				(KLE_MEM|26)
#define KLE_NOT_INITIALIZED			(KLE_MEM|27)

/* symbol error codes
 */
#define KLE_SYM (KLEC_SYM << KLEC_CLASS_SHIFT)
#define KLE_NO_SYMTAB                     	(KLE_SYM|1)
#define KLE_NO_SYMBOLS                     	(KLE_SYM|2)
#define KLE_INVALID_TYPE                        (KLE_SYM|3)
#define KLE_NO_MODULE_LIST                      (KLE_SYM|4)

/* kernel data error codes
 */
#define KLE_KERN (KLEC_KERN << KLEC_CLASS_SHIFT)
#define KLE_INVALID_KERNELSTACK 		(KLE_KERN|1)
#define KLE_INVALID_STRUCT_SIZE 		(KLE_KERN|2)
#define KLE_BEFORE_RAM_OFFSET	 		(KLE_KERN|3)
#define KLE_AFTER_MAXPFN 			(KLE_KERN|4)
#define KLE_AFTER_PHYSMEM  			(KLE_KERN|5)
#define KLE_AFTER_MAXMEM 			(KLE_KERN|6)
#define KLE_PHYSMEM_NOT_INSTALLED 		(KLE_KERN|7)
#define KLE_NO_DEFTASK	 			(KLE_KERN|8)
#define KLE_PID_NOT_FOUND 			(KLE_KERN|9)
#define KLE_DEFTASK_NOT_ON_CPU 			(KLE_KERN|10)
#define KLE_NO_CURCPU 				(KLE_KERN|11)
#define KLE_NO_CPU 				(KLE_KERN|12)
#define KLE_SIG_ERROR 				(KLE_KERN|13)
#define KLE_TASK_RUNNING                        (KLE_KERN|14)
#define KLE_NO_SWITCH_STACK                     (KLE_KERN|15)

/* Error codes that indicate what type of data was bad. These are
 * placed in the upper 32-bits of klib_error.
 */
#define KLE_BAD_TASK_STRUCT    	(((uint64_t)1)<<32)
#define KLE_BAD_SYMNAME         (((uint64_t)2)<<32)
#define KLE_BAD_SYMADDR         (((uint64_t)3)<<32)
#define KLE_BAD_FUNCADDR        (((uint64_t)4)<<32)
#define KLE_BAD_STRUCT          (((uint64_t)5)<<32)
#define KLE_BAD_FIELD           (((uint64_t)6)<<32)
#define KLE_BAD_PC              (((uint64_t)7)<<32)
#define KLE_BAD_RA              (((uint64_t)8)<<32)
#define KLE_BAD_SP              (((uint64_t)9)<<32)
#define KLE_BAD_EP              (((uint64_t)10)<<32)
#define KLE_BAD_SADDR           (((uint64_t)11)<<32)
#define KLE_BAD_KERNELSTACK     (((uint64_t)12)<<32)
#define KLE_BAD_LINENO          (((uint64_t)13)<<32)
#define KLE_MAP_FILE          	(((uint64_t)14)<<32)
#define KLE_DUMP          	(((uint64_t)15)<<32)
#define KLE_BAD_STRING          (((uint64_t)16)<<32)
#define KLE_ELF_FILE          	(((uint64_t)17)<<32)

/* flags for function kl_msg()
 * First 3 bits define trace levels. Minimum trace threshold is trace level 1.
 * So maximal 7 trace levels are possible. We are using only KLE_TRACELEVEL_MAX.
 * If no trace level bits are set, it is normal output.
 */
#define _KLE_TRACEBIT1       0x00000001  /* trace bit 1 */
#define _KLE_TRACEBIT2       0x00000002  /* trace bit 2 */
#define _KLE_TRACEBIT3       0x00000004  /* trace bit 3 */
#define _KLE_TRACENUM        8           /* used in _KLE_TRACENUM */
#define _KLE_TRACEMASK       (_KLE_TRACENUM-1) /* mask for trace bits */
/* further flags */
#define KLE_F_NOORIGIN       0x00001000 /* do not print origin for this msg */
#define KLE_F_ERRORMSG       0x00002000 /* treat message as error message */
/* trace levels := predefined combinations of trace bits */
#define KLE_F_TRACELEVEL1      (_KLE_TRACEBIT1)
#define KLE_F_TRACELEVEL2      (_KLE_TRACEBIT2)
#define KLE_F_TRACELEVEL3      (_KLE_TRACEBIT1|_KLE_TRACEBIT2)
#define KLE_F_TRACELEVEL4      (_KLE_TRACEBIT3)
#define KLE_TRACELEVELMAX      4
#define KLE_TRACELEVEL(flg)    (flg & _KLE_TRACEMASK)
#define KLE_GETTRACELEVEL(flg) \
 ((KLE_TRACELEVEL(flg) > KLE_TRACELEVELMAX) ? KLE_TRACELEVELMAX : \
  KLE_TRACELEVEL(flg))

/* define debug components of libklib (64 components possible)
 * used by kl_msg()
 */
#define KL_DBGCOMP_ALLOC    0x0000000001  /* liballoc */
#define KL_DBGCOMP_BFD      0x0000000002  /* general bfd support */
#define KL_DBGCOMP_BTREE    0x0000000004  /* btree implementation */
#define KL_DBGCOMP_COMPRESS 0x0000000008  /* gzip/rle (de)compression */
#define KL_DBGCOMP_INIT     0x0000000010  /* klib initialization */
#define KL_DBGCOMP_MEMMAP   0x0000000020  /* memory mapping */
#define KL_DBGCOMP_MODULE   0x0000000040  /* kernel module handling */
#define KL_DBGCOMP_SIGNAL   0x0000000080  /* signal handling */
#define KL_DBGCOMP_STABS    0x0000000100  /* stabs format support */
#define KL_DBGCOMP_SYMBOL   0x0000000200  /* symbol handling */
#define KL_DBGCOMP_TYPE     0x0000000400  /* type information handling */
#define KL_DBGCOMP_ALL      ((uint64_t) -1)  /* all components */

/* central output routine, shouldn't be used directly, but
 * by following macros
 */
void kl_msg(uint64_t, uint32_t, const char*, const char*, int,
	    const char*, ...);

/* vararg macros that should be used instead of kl_msg()
 */
/* used within libklib to print non-error messages (e.g. progress indication)
 */
#define KL_MSG(fmt, args...) \
kl_msg(0, 0, NULL, NULL, 0, fmt, ## args)
/* Can be used by application to print error messages;
 * not used by libklib itself.
 */
#define kl_error(fmt, args...)  \
kl_msg(0, KLE_F_ERRORMSG, __FUNCTION__, __FILE__, __LINE__, fmt, ## args)
/* Generate trace messages. Used for libklib debugging. Might be used
 * by an application, too.
 * A macro _DBG_COMPONENT has to be defined locally in the module where
 * any trace macro is used. See above debug components.
 * Trace messages are only printed iff _DBG_COMPONENT was set before with a
 * call to kl_set_dbg_component().
 */
#define kl_trace1(flg, fmt, args...) \
kl_msg(_DBG_COMPONENT,  KLE_F_TRACELEVEL1|(flg), \
       __FUNCTION__, __FILE__, __LINE__, fmt, ## args)
#define kl_trace2(flg, fmt, args...) \
kl_msg(_DBG_COMPONENT,  KLE_F_TRACELEVEL2|(flg), \
       __FUNCTION__, __FILE__, __LINE__, fmt, ## args)
#define kl_trace3(flg, fmt, args...) \
kl_msg(_DBG_COMPONENT,  KLE_F_TRACELEVEL3|(flg), \
       __FUNCTION__, __FILE__, __LINE__, fmt, ## args)
#define kl_trace4(flg, fmt, args...) \
kl_msg(_DBG_COMPONENT,  KLE_F_TRACELEVEL4|(flg), \
       __FUNCTION__, __FILE__, __LINE__, fmt, ## args)

/* functions to set some global variables for libklib debugging
 */
int  kl_set_trace_threshold(uint32_t);
void kl_set_dbg_component(uint64_t);
void kl_set_stdout(FILE *);
void kl_set_stderr(FILE *);

/* functions to get contents of global variables for libklib debugging
 */
uint32_t kl_get_trace_threshold(void);
uint64_t kl_get_dbg_component(void);

#endif /* __KL_ERROR_H */
