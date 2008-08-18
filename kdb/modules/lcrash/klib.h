/*
 * $Id: klib.h 1336 2006-10-23 23:27:06Z tjm $
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

/*
 * klib.h -- Interface of the klib library, a library for access to
 *           Linux system memory dumps.
 */

#ifndef __KLIB_H
#define __KLIB_H

/* Include header files
 */
#if 0
 /* cpw: don't include all this: */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <setjmp.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <bfd.h>
#include <assert.h>
#endif

/* cpw: change all the below includes form the < > form to " " */

/* Include libutil header
 */
#include "kl_lib.h"

/* Include libklib header files
 */
#include "kl_types.h"
#include "kl_error.h"
#include "kl_dump.h"
#include "kl_mem.h"
#include "kl_cmp.h"
#include "kl_typeinfo.h"
#include "kl_module.h"
#include "kl_sym.h"
#include "kl_bfd.h"
#include "kl_debug.h"
#include "kl_stabs.h"
#include "kl_dwarfs.h"
#include "kl_task.h"
#include "kl_dump_arch.h"


#ifndef TRUE
# define TRUE	1
#endif
#ifndef FALSE
# define FALSE	0
#endif

#ifndef MIN
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif
#ifndef MAX
#define MAX(x,y) (((x)>(y))?(x):(y))
#endif

#define KL_NR_CPUS 128

/* We have to distinc between HOST_ARCH_* and DUMP_ARCH_*. These two classes of
 * macros are used througout the code for conditional compilation.
 * Additional we have following macros for comparison and switch statements.
 */
#define KL_ARCH_UNKNOWN          0
#define KL_ARCH_ALPHA            1
#define KL_ARCH_ARM              2
#define KL_ARCH_I386             3
#define KL_ARCH_IA64             4
#define KL_ARCH_M68K             5
#define KL_ARCH_MIPS             6
#define KL_ARCH_MIPS64           7
#define KL_ARCH_PPC              8
#define KL_ARCH_S390             9
#define KL_ARCH_SH              10
#define KL_ARCH_SPARK           11
#define KL_ARCH_SPARK64         12
#define KL_ARCH_S390X           13
#define KL_ARCH_PPC64           14
#define KL_ARCH_X86_64          15
#define KL_ARCH_IA64_SN2        16
#define KL_ARCH_IA64_DIG        17
#define KL_ARCH_IA64_HPSIM      18
#define KL_ARCH_IA64_HPZX1      19
#define KL_ARCH_S390SA          20

#define KL_LIVE_SYSTEM        1000

#define ARCH_IS_IA64(A) \
	((A==KL_ARCH_IA64)|| \
	 (A==KL_ARCH_IA64_SN2)|| \
	 (A==KL_ARCH_IA64_DIG)|| \
	 (A==KL_ARCH_IA64_HPSIM)|| \
	 (A==KL_ARCH_IA64_HPZX1))

#ifdef HOST_ARCH_ALPHA
# define KL_HOST_ARCH KL_ARCH_ALPHA
#endif
#ifdef HOST_ARCH_ARM
# define KL_HOST_ARCH KL_ARCH_ARM
#endif
#ifdef HOST_ARCH_I386
# define KL_HOST_ARCH KL_ARCH_I386
#endif
#ifdef HOST_ARCH_IA64
# define KL_HOST_ARCH KL_ARCH_IA64
#endif
#ifdef HOST_ARCH_S390
# define KL_HOST_ARCH KL_ARCH_S390
#endif
#ifdef HOST_ARCH_S390X
# define KL_HOST_ARCH KL_ARCH_S390X
#endif
#ifdef HOST_ARCH_PPC64
#define KL_HOST_ARCH KL_ARCH_PPC64
#endif
#ifdef HOST_ARCH_X86_64
#define KL_HOST_ARCH KL_ARCH_X86_64
#endif

#define KL_ARCH_STR_ALPHA   	"alpha"
#define KL_ARCH_STR_ARM	    	"arm"
#define KL_ARCH_STR_I386    	"i386"
#define KL_ARCH_STR_IA64    	"ia64"
#define KL_ARCH_STR_S390    	"s390"
#define KL_ARCH_STR_S390X   	"s390x"
#define KL_ARCH_STR_PPC64   	"ppc64"
#define KL_ARCH_STR_X86_64  	"x86_64"
#define KL_ARCH_STR_IA64_SN2  	"sn2"
#define KL_ARCH_STR_UNKNOWN 	"unknown"

/* for endianess of dump and host arch
 */
#define KL_UNKNOWN_ENDIAN  0x00
#define KL_LITTLE_ENDIAN   0x01
#define KL_BIG_ENDIAN      0x02

/* macros for handling of different Kernel versions
 */
#define LINUX_2_2_X(R) (((R) & 0xffff00) == 0x020200)
#define LINUX_2_2_16    0x020210
#define LINUX_2_2_17    0x020211
#define LINUX_2_4_X(R) (((R) & 0xffff00) == 0x020400)
#define LINUX_2_4_0 	0x020400
#define LINUX_2_4_4     0x020404
#define LINUX_2_4_15    0x02040f
#define LINUX_2_6_X(R) (((R) & 0xffff00) == 0x020600)
#define LINUX_2_6_0     0x020600

/* libklib flags
 */
#define KL_FAILSAFE_FLG     0x0001
#define KL_NOVERIFY_FLG     0x0002
#define KL_SILENT_FLG	    0x0004
#define KL_SAVETYPES_FLG    0x0008
#define KL_USETYPES_FLG	    0x0010

/* macros for backward compatibility
 */
#define NUM_PHYSPAGES  KLP->dump->mem.num_physpages
#define MEM_MAP        KLP->dump->mem.mem_map
#define KL_HIGH_MEMORY KLP->dump->mem.high_memory
#define KL_INIT_MM     KLP->dump->mem.init_mm
#define KL_NUM_CPUS    KLP->dump->mem.num_cpus
#define KL_PGDAT_LIST  KLP->dump->mem.pgdat_list

/* macros for better use of dump architecture dependent functions
 */

/* read integer value from buffer */
#define KL_GET_PTR(ptr)    (*KLP->dump->func.get_ptr)(ptr)
#define KL_GET_LONG(ptr)   ((int64_t) KL_GET_PTR(ptr))
#define KL_GET_ULONG(ptr)  KL_GET_PTR(ptr)
#define KL_GET_UINT8(ptr)  (*KLP->dump->func.get_uint8)(ptr)
#define KL_GET_UINT16(ptr) (*KLP->dump->func.get_uint16)(ptr)
#define KL_GET_UINT32(ptr) (*KLP->dump->func.get_uint32)(ptr)
#define KL_GET_UINT64(ptr) (*KLP->dump->func.get_uint64)(ptr)
#define KL_GET_INT8(ptr)   ((int8_t) KL_GET_UINT8(ptr))
#define KL_GET_INT16(ptr)  ((int16_t) KL_GET_UINT16(ptr))
#define KL_GET_INT32(ptr)  ((int32_t) KL_GET_UINT32(ptr))
#define KL_GET_INT64(ptr)  ((int64_t) KL_GET_UINT64(ptr))

/* read integer value from dump (without address mapping)
 * Use these functions sparsely, e.g. before address translation
 * is properly set up.
 */
#define KL_READ_PTR(addr)    (*KLP->dump->func.read_ptr)(addr)
#define KL_READ_LONG(addr)   ((int64_t) KL_READ_PTR(addr))
#define KL_READ_ULONG(addr)  KL_READ_PTR(addr)
#define KL_READ_UINT8(addr)  (*KLP->dump->func.read_uint8)(addr)
#define KL_READ_UINT16(addr) (*KLP->dump->func.read_uint16)(addr)
#define KL_READ_UINT32(addr) (*KLP->dump->func.read_uint32)(addr)
#define KL_READ_UINT64(addr) (*KLP->dump->func.read_uint64)(addr)
#define KL_READ_INT8(addr)   ((int8_t) KL_READ_UINT8(addr))
#define KL_READ_INT16(addr)  ((int16_t) KL_READ_UINT16(addr))
#define KL_READ_INT32(addr)  ((int32_t) KL_READ_UINT32(addr))
#define KL_READ_INT64(addr)  ((int64_t) KL_READ_UINT64(addr))

/* read integer value from dump (from virtual address) doing address mapping */
#define KL_VREAD_PTR(addr)    (*KLP->dump->func.vread_ptr)(addr)
#define KL_VREAD_LONG(addr)   ((int64_t) KL_VREAD_PTR(addr))
#define KL_VREAD_ULONG(addr)  KL_VREAD_PTR(addr)
#define KL_VREAD_UINT8(addr)  (*KLP->dump->func.vread_uint8)(addr)
#define KL_VREAD_UINT16(addr) (*KLP->dump->func.vread_uint16)(addr)
#define KL_VREAD_UINT32(addr) (*KLP->dump->func.vread_uint32)(addr)
#define KL_VREAD_UINT64(addr) (*KLP->dump->func.vread_uint64)(addr)
#define KL_VREAD_INT8(addr)   ((int8_t) KL_VREAD_UINT8(addr))
#define KL_VREAD_INT16(addr)  ((int16_t) KL_VREAD_UINT16(addr))
#define KL_VREAD_INT32(addr)  ((int32_t) KL_VREAD_UINT32(addr))
#define KL_VREAD_INT64(addr)  ((int64_t) KL_VREAD_UINT64(addr))

/* determine start of stack */
#define KL_KERNELSTACK_UINT64 (*KLP->dump->arch.kernelstack)
/* map virtual adress to physical one */
#define KL_VIRTOP (*KLP->dump->arch.virtop)
/* travers page table */
#define KL_MMAP_VIRTOP (*KLP->dump->arch.mmap_virtop)
/* check whether address points to valid physical memory */
#define KL_VALID_PHYSMEM (*KLP->dump->arch.valid_physmem)
/* determine next valid physical address */
#define KL_NEXT_VALID_PHYSADDR (*KLP->dump->arch.next_valid_physaddr)
/* XXX */
#define KL_FIX_VADDR (*KLP->dump->arch.fix_vaddr)
/* write dump_header_asm_t */
#define KL_WRITE_DHA (*KLP->dump->arch.write_dha)
/* size of dump_header_asm_t */
#define KL_DHA_SIZE (KLP->dump->arch.dha_size)
/* init virtual to physical address mapping */
#define KL_INIT_VIRTOP (KLP->dump->arch.init_virtop)


/* macros for easier access to dump specific values */
#define KL_CORE_TYPE            KLP->dump->core_type
#define KL_CORE_FD              KLP->dump->core_fd
#define KL_ARCH                 KLP->dump->arch.arch
#define KL_PTRSZ                KLP->dump->arch.ptrsz
#define KL_NBPW                 (KL_PTRSZ/8)
#define KL_BYTE_ORDER           KLP->dump->arch.byteorder
#define KL_PAGE_SHIFT           KLP->dump->arch.pageshift
#define KL_PAGE_SIZE            KLP->dump->arch.pagesize
#define KL_PAGE_MASK            KLP->dump->arch.pagemask
#define KL_PAGE_OFFSET          KLP->dump->arch.pageoffset
#define KL_STACK_OFFSET         KLP->dump->arch.kstacksize
#define IS_BIG_ENDIAN()         (KL_BYTE_ORDER == KL_BIG_ENDIAN)
#define IS_LITTLE_ENDIAN()      (KL_BYTE_ORDER == KL_LITTLE_ENDIAN)
#define KL_LINUX_RELEASE        KLP->dump->mem.linux_release
#define KL_KERNEL_FLAGS         KLP->dump->mem.kernel_flags

#if 0
/* cpw: don't need all this dump file stuff: */
/* macros to access input files */
#define KL_MAP_FILE       KLP->dump->map
#define KL_DUMP_FILE      KLP->dump->dump
#define KL_KERNTYPES_FILE KLP->kerntypes

#define CORE_IS_KMEM (KL_CORE_TYPE == dev_kmem)
#define CORE_IS_DUMP ((KL_CORE_TYPE > dev_kmem) && (KL_CORE_TYPE <= unk_core))


/* Generic dump header structure (the first three members of
 * dump_header and dump_header_asm are the same).
 */
typedef struct generic_dump_header_s {
        uint64_t        magic_number;
        uint32_t        version;
        uint32_t        header_size;
} generic_dump_header_t;

/* Some macros for making it easier to access the generic header
 * information in a dump_header or dump_header_asm stuct.
 */
#define DHP(dh)                 ((generic_dump_header_t*)(dh))
#define DH_MAGIC(dh)            DHP(dh)->magic_number
#define DH_VERSION(dh)          DHP(dh)->version
#define DH_HEADER_SIZE(dh)      DHP(dh)->header_size

extern kl_dump_header_t *DUMP_HEADER;
extern void *DUMP_HEADER_ASM;
#endif

/* Struct to store some host architecture specific values
 */
typedef struct kl_hostarch_s {
	int             arch;          /* KL_ARCH_ */
	int             ptrsz;         /* 32 or 64 bit */
	int             byteorder;     /* KL_LITTLE_ENDIAN or KL_BIG_ENDIAN */
} kl_hostarch_t;

/* Struct klib_s, contains all the information necessary for accessing
 * information in the kernel. A pointer to a klib_t struct will be
 * returned from libkern_init() if core dump analysis (or live system
 * analysis) is possible.
 *
 */
typedef struct klib_s {
	int		k_flags;     /* Flags pertaining to klib_s struct */
	kl_hostarch_t  *host;        /* host arch info */
	kl_dumpinfo_t  *dump;        /* dump information */
	maplist_t      *k_symmap;    /* symbol information */
	kltype_t       *k_typeinfo;  /* type information */
	char            *kerntypes;     /* pathname for kerntypes file */
} klib_t;

/* Structure to accomodate all debug formats */
struct namelist_format_opns {
	/* to open/setup the namelist file */
	int (*open_namelist) (char *filename , int flags);
	int (*setup_typeinfo)(void);
};

/*
 * global variables
 */

/* Here we store almost everything, we need to know about a dump. */
extern klib_t *KLP;

/* macros to make live easier */
#define MIP               KLP->dump
#define STP               KLP->k_symmap
#define TASK_STRUCT_SZ    (KLP->dump->mem.struct_sizes.task_struct_sz)
#define MM_STRUCT_SZ      (KLP->dump->mem.struct_sizes.mm_struct_sz)
#define PAGE_SZ           (KLP->dump->mem.struct_sizes.page_sz)
#define MODULE_SZ         (KLP->dump->mem.struct_sizes.module_sz)
#define NEW_UTSNAME_SZ    (KLP->dump->mem.struct_sizes.new_utsname_sz)
#define SWITCH_STACK_SZ   (KLP->dump->mem.struct_sizes.switch_stack_sz)
#define PT_REGS_SZ   	  (KLP->dump->mem.struct_sizes.pt_regs_sz)
#define PGLIST_DATA_SZ    (KLP->dump->mem.struct_sizes.pglist_data_sz)
#define RUNQUEUE_SZ       (KLP->dump->mem.struct_sizes.runqueue_sz)

#if 0
cpw: used for sial?
/* klib_jbuf has to be defined outside libklib.
 * Make sure to call setjmp(klib_jbuf) BEFORE kl_sig_setup() is called! */
extern jmp_buf klib_jbuf;
#endif

/* Macros that eliminate the offset paramaters to the kl_uint() and kl_int()
 * functions (just makes things cleaner looking)
 */
#define KL_UINT(p, s, m) kl_uint(p, s, m, 0)
#define KL_INT(p, s, m) kl_int(p, s, m, 0)

/* Macros for translating strings into long numeric values depending
 * on the base of 's'.
 */
#define GET_VALUE(s, value) kl_get_value(s, NULL, 0, value)
#define GET_HEX_VALUE(s) (kaddr_t)strtoull(s, (char**)NULL, 16)
#define GET_DEC_VALUE(s) (unsigned)strtoull(s, (char**)NULL, 10)
#define GET_OCT_VALUE(s) (unsigned)strtoull(s, (char**)NULL, 8)

#define KL_SIGFLG_CORE          0x1
#define KL_SIGFLG_SILENT        0x2
#define KL_SIGFLG_LNGJMP        0x4

/* Flag that tells kl_is_valid_kaddr() to perform a word aligned check
 */
#define WORD_ALIGN_FLAG         1

#define ADDR_TO_PGNO(addr) ((addr - KL_PAGE_OFFSET) >> KL_PAGE_SHIFT);

/* Generalized macros for pointing at different data types at particular
 * offsets in kernel structs.
 */
/* #define K_ADDR(p, s, f)   ((uaddr_t)(p) + kl_member_offset(s, f)) */
#define K_ADDR(p, s, f)   ((p) + kl_member_offset(s, f))
#define K_PTR(p, s, f)    (K_ADDR((void*)p, s, f))
#define CHAR(p, s, f)     (K_ADDR((char*)p, s, f))

#define PTRSZ32 ((KL_PTRSZ == 32) ? 1 : 0)
#define PTRSZ64 ((KL_PTRSZ == 64) ? 1 : 0)

/* Function prototypes
 */
/* cpw: remove the last argument   FILE * */
void kl_binary_print(uint64_t);
void kl_print_bit_value(void *, int, int, int, int);
void kl_print_char(void *, int);
void kl_print_uchar(void *, int);
void kl_print_int2(void *, int);
void kl_print_uint2(void *, int);
void kl_print_int4(void *, int);
void kl_print_uint4(void *, int);
void kl_print_float4(void *, int);
void kl_print_int8(void *, int);
void kl_print_uint8(void *, int);
void kl_print_float8(void *, int);
void kl_print_base(void *, int, int, int);
void kl_print_string(char *);

int kl_get_live_filenames(
	char *			/* pointer to buffer for map filename */,
	char *			/* pointer to buffer for dump filename */,
	char *			/* pointer to buffer for namelist filename */);

int kl_init_klib(
        char *                  /* map file name */,
        char *                  /* dump file name */,
        char *                  /* namelist file name */,
        int                   	/* system arch of memory in dump */,
        int                   	/* rwflag flag (/dev/mem only) */,
	int			/* Linux release */);

void kl_free_klib(
	klib_t *		/* Pointer to klib_s struct */);


int kl_dump_retrieve(
	char *			/* dumpdev name */,
	char *			/* dumpdir name */,
	int			/* progress flag (zero or non-zero) */,
	int			/* debug flag (zero or non-zero) */);

int kl_dump_erase(
	char *			/* dumpdev name */);

uint64_t kl_strtoull(
	char *			/* string containing numeric value */,
	char **			/* pointer to pointer to bad char */,
	int 			/* base */);

int kl_get_value(
	char *			/* param */,
	int *			/* mode pointer */,
	int 			/* number of elements */,
	uint64_t *		/* pointer to value */);

/* Functions for working with list_head structs
 */
kaddr_t kl_list_entry(kaddr_t, char *, char *);
kaddr_t kl_list_next(kaddr_t);
kaddr_t kl_list_prev(kaddr_t);

int kl_sig_setup(int);

void kl_set_curnmlist(
	int 			/* index  of namelist */);

int kl_open_namelist(
	char *			/* name of namelist */,
	int 			/* flags */,
	int 			/* kl_flags */);

int   kl_get_structure(kaddr_t,  char*, size_t*, void**);
uint64_t kl_get_bit_value(void*, unsigned int, unsigned int, unsigned int);
void kl_s390tod_to_timeval(uint64_t, struct timeval*);

#endif /* __KLIB_H */
