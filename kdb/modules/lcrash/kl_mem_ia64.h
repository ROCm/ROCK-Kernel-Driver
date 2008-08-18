/*
 * $Id: kl_mem_ia64.h 1250 2006-04-18 18:23:44Z cliffpwickman $
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

#ifndef __KL_MEM_IA64_H
#define __KL_MEM_IA64_H

/* XXX - the following macros are used by functions in kl_page.c and in */
/*       function kl_virtop, they still have to be defined properly, */
/*       all the following macros have first to be set with correct values. */
/*       I don't have a clue what values to use for ia64 architecture!!! */

/* KSTACK_SIZE depends on page size (see kernel headers ptrace.h and page.h)
 * #define IA64_STK_OFFSET   ((1 << IA64_TASK_STRUCT_LOG_NUM_PAGES)*PAGE_SIZE)
 * and
 * #define PAGE_SIZE 1UL<<PAGE_SHIFT
 * and
 * #if defined(CONFIG_IA64_PAGE_SIZE_4KB)
 * # define PAGE_SHIFT     12
 * #elif defined(CONFIG_IA64_PAGE_SIZE_8KB)
 * # define PAGE_SHIFT     13
 * #elif defined(CONFIG_IA64_PAGE_SIZE_16KB)
 * # define PAGE_SHIFT     14
 * #elif defined(CONFIG_IA64_PAGE_SIZE_64KB)
 * # define PAGE_SHIFT     16
 * #else
 * # error Unsupported page size!
 * #endif
 * and
 * #if defined(CONFIG_IA64_PAGE_SIZE_4KB)
 * # define IA64_TASK_STRUCT_LOG_NUM_PAGES         3
 * #elif defined(CONFIG_IA64_PAGE_SIZE_8KB)
 * # define IA64_TASK_STRUCT_LOG_NUM_PAGES         2
 * #elif defined(CONFIG_IA64_PAGE_SIZE_16KB)
 * # define IA64_TASK_STRUCT_LOG_NUM_PAGES         1
 * #else
 * # define IA64_TASK_STRUCT_LOG_NUM_PAGES         0
 * #endif
 * Finally we have for page sizes 4KB, 8K, 16K IA64_STK_OFFSET=32K, and
 * for page size 64K IA64_STK_OFFSET=64K.
 * FIXME: !!!Don't know how to handle 64K page size case!!!
 */
#define KL_KSTACK_SIZE_IA64        0x8000ULL
/* 64KB page size case:
 * #define KL_KSTACK_SIZE_IA64        0x10000ULL
 */

#define KL_PAGE_OFFSET_IA64        0xe000000000000000

#define KL_PAGE_SHIFT_IA64         KL_PAGE_SHIFT
#define KL_PAGE_SIZE_IA64          (1ULL << KL_PAGE_SHIFT_IA64)
#define KL_PAGE_MASK_IA64          (~(KL_PAGE_SIZE_IA64-1))

/* for 3-level page tables: */
#define KL_PGDIR_SHIFT_IA64      (KL_PAGE_SHIFT_IA64+(KL_PAGE_SHIFT_IA64-3)*2)
#define KL_PGDIR_SIZE_IA64       (1ULL<<KL_PGDIR_SHIFT_IA64)
#define KL_PGDIR_MASK_IA64       (~(KL_PGDIR_SIZE_IA64-1))

/* for 4-level page tables: */
#define KL_PGDIR4_SHIFT_IA64      (KL_PAGE_SHIFT_IA64+(KL_PAGE_SHIFT_IA64-3)*3)
#define KL_PGDIR4_SIZE_IA64       (1ULL<<KL_PGDIR4_SHIFT_IA64)
#define KL_PGDIR4_MASK_IA64       (~(KL_PGDIR_SIZE4_IA64-1))
#define KL_PUD_SHIFT_IA64        (KL_PAGE_SHIFT_IA64+(KL_PAGE_SHIFT_IA64-3)*2)
#define KL_PUD_SIZE_IA64         (1ULL<<KL_PUD_SHIFT_IA64)
#define KL_PUD_MASK_IA64         (~(KL_PUD_SIZE_IA64-1))

#define KL_PMD_SHIFT_IA64        (KL_PAGE_SHIFT_IA64+(KL_PAGE_SHIFT_IA64-3)*1)
#define KL_PMD_SIZE_IA64         (1ULL<<KL_PMD_SHIFT_IA64)
#define KL_PMD_MASK_IA64         (~(KL_PMD_SIZE_IA64-1))

#define KL_PTRS_PER_PGD_IA64     (1ULL<<(KL_PAGE_SHIFT_IA64-3))
#define KL_PTRS_PER_PUD_IA64     (1ULL<<(KL_PAGE_SHIFT_IA64-3))
#define KL_PTRS_PER_PMD_IA64     (1ULL<<(KL_PAGE_SHIFT_IA64-3))
#define KL_PTRS_PER_PTE_IA64     (1ULL<<(KL_PAGE_SHIFT_IA64-3))

/* These values describe the bits of pgd/pmd/pte entries that are
 * status bits and therefor have to be masked in order to get valid
 * addresses
 */
#define KL_PMD_BASE_MASK_IA64    (((1ULL<<50)-1)&(~0xfffULL))
#define KL_PT_BASE_MASK_IA64     KL_PMD_BASE_MASK_IA64
#define KL_PAGE_BASE_MASK_IA64   KL_PMD_BASE_MASK_IA64

#define KL_KADDR_IS_HIGHMEM(vaddr) ((KL_HIGH_MEMORY && (vaddr >= KL_HIGH_MEMORY)))

uint32_t dha_num_cpus_ia64(void);
kaddr_t dha_current_task_ia64(int);
int dha_cpuid_ia64(kaddr_t);
kaddr_t dha_stack_ia64(int);
kaddr_t dha_stack_ptr_ia64(int);
kaddr_t kl_kernelstack_ia64(kaddr_t);
kaddr_t kl_mmap_virtop_ia64(kaddr_t, void*);
int     kl_init_virtop_ia64(void);
int     kl_virtop_ia64(kaddr_t, void*, kaddr_t*);
int     kl_vtop_ia64(kaddr_t, kaddr_t*);
int 	kl_valid_physmem_ia64(kaddr_t, int);
kaddr_t kl_next_valid_physaddr_ia64(kaddr_t);
kaddr_t kl_fix_vaddr_ia64(kaddr_t, size_t);

/* Structure containing key data for ia64 virtual memory mapping.
 * Note that a number of fields are SN system specific.
 */
typedef struct ia64_vminfo_s {
        int             flags;
        kaddr_t         vpernode_base;
        kaddr_t         vglobal_base;
        kaddr_t         to_phys_mask;
        kaddr_t         kernphysbase;
        int             nasid_shift;    /* SN specific */
        int             nasid_mask;     /* SN specific */
} ia64_vminfo_t;

extern ia64_vminfo_t ia64_vminfo;

/* Some vminfo flags
 */
#define MAPPED_KERN_FLAG        0x1
#define SN2_FLAG                0x2

/* Some vminfo macros
 */
#define IS_MAPPED_KERN (ia64_vminfo.flags & MAPPED_KERN_FLAG)
#define IS_SN2 (ia64_vminfo.flags & SN2_FLAG)
#define KL_VPERNODE_BASE ia64_vminfo.vpernode_base
#define KL_VGLOBAL_BASE ia64_vminfo.vglobal_base
#define KL_TO_PHYS_MASK ia64_vminfo.to_phys_mask
#define KL_KERNPHYSBASE ia64_vminfo.kernphysbase
#define KL_NASID_SHIFT ia64_vminfo.nasid_shift
#define KL_NASID_MASK ia64_vminfo.nasid_mask

#define ADDR_TO_NASID(A) (((A) >> (long)(KL_NASID_SHIFT)) & KL_NASID_MASK)

#endif /* __KL_MEM_IA64_H */
