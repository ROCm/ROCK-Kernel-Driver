/*
 * $Id: kl_dump_ia64.h 1151 2005-02-23 01:09:12Z tjm $
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

/* This header file holds the architecture specific crash dump header */
#ifndef __KL_DUMP_IA64_H
#define __KL_DUMP_IA64_H

/* definitions */
#ifndef KL_NR_CPUS
# define KL_NR_CPUS  128    /* max number CPUs */
#endif

#define KL_DUMP_MAGIC_NUMBER_IA64   0xdeaddeadULL  /* magic number            */
#define KL_DUMP_VERSION_NUMBER_IA64 0x4            /* version number          */


/*
 * mkswap.c calls getpagesize() to get the system page size,
 * which is not  necessarily the same as the hardware page size.
 *
 * For ia64 the kernel PAGE_SIZE can be configured from 4KB ... 16KB.
 *
 * The physical memory is layed out out in the hardware/minimal pages.
 * This is the size we need to use for dumping physical pages.
 *
 * Note ths hardware/minimal page size being use in;
 *      arch/ia64/kernel/efi.c`efi_memmap_walk():
 *	    curr.end   = curr.start + (md->num_pages << 12);
 *
 * Since the system page size could change between the kernel we boot
 * on the the kernel that cause the core dume we may want to have something
 * more constant like the maximum system page size (See include/asm-ia64/page.h).
 */
#define DUMP_MIN_PAGE_SHIFT                 	12
#define DUMP_MIN_PAGE_SIZE                  	(1UL << DUMP_MIN_PAGE_SHIFT)
#define DUMP_MIN_PAGE_MASK                  	(~(DUMP_MIN_PAGE_SIZE - 1))
#define DUMP_MIN_PAGE_ALIGN(addr)           	(((addr) + DUMP_MIN_PAGE_SIZE - 1) & DUMP_MIN_PAGE_MASK)

#define DUMP_MAX_PAGE_SHIFT                 	16
#define DUMP_MAX_PAGE_SIZE                  	(1UL << DUMP_MAX_PAGE_SHIFT)
#define DUMP_MAX_PAGE_MASK                  	(~(DUMP_MAX_PAGE_SIZE - 1))
#define DUMP_MAX_PAGE_ALIGN(addr)           	(((addr) + DUMP_MAX_PAGE_SIZE - 1) & DUMP_MAX_PAGE_MASK)

#define DUMP_HEADER_OFFSET              	DUMP_MAX_PAGE_SIZE

#define DUMP_EF_PAGE_SHIFT			DUMP_MIN_PAGE_SHIFT

#define DUMP_PAGE_SHIFT				DUMP_MIN_PAGE_SHIFT
#define DUMP_PAGE_SIZE				DUMP_MIN_PAGE_SIZE
#define DUMP_PAGE_MASK				DUMP_MIN_PAGE_MASK
#define DUMP_PAGE_ALIGN(addr)			DUMP_MIN_PAGE_ALIGN(addr)

struct kl_ia64_fpreg {
	union {
		unsigned long bits[2];
		long double __dummy;    /* force 16-byte alignment */
	} u;
};

struct kl_pt_regs_ia64 {
	/* for 2.6 kernels only. This structure was totally different in 2.4 kernels */
	unsigned long b6;               /* scratch */
	unsigned long b7;               /* scratch */

	unsigned long ar_csd;           /* used by cmp8xchg16 (scratch) */
	unsigned long ar_ssd;           /* reserved for future use (scratch) */

	unsigned long r8;               /* scratch (return value register 0) */
	unsigned long r9;               /* scratch (return value register 1) */
	unsigned long r10;              /* scratch (return value register 2) */
	unsigned long r11;              /* scratch (return value register 3) */

	unsigned long cr_ipsr;          /* interrupted task's psr */
	unsigned long cr_iip;           /* interrupted task's instruction pointer */
	unsigned long cr_ifs;           /* interrupted task's function state */

	unsigned long ar_unat;          /* interrupted task's NaT register (preserved) */
	unsigned long ar_pfs;           /* prev function state  */
	unsigned long ar_rsc;           /* RSE configuration */
	/* The following two are valid only if cr_ipsr.cpl > 0: */
	unsigned long ar_rnat;          /* RSE NaT */
	unsigned long ar_bspstore;      /* RSE bspstore */

	unsigned long pr;               /* 64 predicate registers (1 bit each) */
	unsigned long b0;               /* return pointer (bp) */
	unsigned long loadrs;           /* size of dirty partition << 16 */

	unsigned long r1;               /* the gp pointer */
	unsigned long r12;              /* interrupted task's memory stack pointer */
	unsigned long r13;              /* thread pointer */

	unsigned long ar_fpsr;          /* floating point status (preserved) */
	unsigned long r15;              /* scratch */

	/* The remaining registers are NOT saved for system calls.  */

	unsigned long r14;              /* scratch */
	unsigned long r2;               /* scratch */
	unsigned long r3;               /* scratch */

	/* The following registers are saved by SAVE_REST: */
	unsigned long r16;              /* scratch */
	unsigned long r17;              /* scratch */
	unsigned long r18;              /* scratch */
	unsigned long r19;              /* scratch */
	unsigned long r20;              /* scratch */
	unsigned long r21;              /* scratch */
	unsigned long r22;              /* scratch */
	unsigned long r23;              /* scratch */
	unsigned long r24;              /* scratch */
	unsigned long r25;              /* scratch */
	unsigned long r26;              /* scratch */
	unsigned long r27;              /* scratch */
	unsigned long r28;              /* scratch */
	unsigned long r29;              /* scratch */
	unsigned long r30;              /* scratch */
	unsigned long r31;              /* scratch */

	unsigned long ar_ccv;           /* compare/exchange value (scratch) */

	/*
	 *          * Floating point registers that the kernel considers scratch:
	 *                   */
	struct kl_ia64_fpreg f6;           /* scratch */
	struct kl_ia64_fpreg f7;           /* scratch */
	struct kl_ia64_fpreg f8;           /* scratch */
	struct kl_ia64_fpreg f9;           /* scratch */
	struct kl_ia64_fpreg f10;          /* scratch */
	struct kl_ia64_fpreg f11;          /* scratch */
} __attribute__((packed));

/*
 * Structure: dump_header_asm_t
 *  Function: This is the header for architecture-specific stuff.  It
 *            follows right after the dump header.
 */
typedef struct kl_dump_header_ia64_s {
        /* the dump magic number -- unique to verify dump is valid */
        uint64_t             magic_number;
        /* the version number of this dump */
        uint32_t             version;
        /* the size of this header (in case we can't read it) */
        uint32_t             header_size;
        /* pointer to pt_regs */
	uint64_t             pt_regs;
	/* the dump registers */
	struct kl_pt_regs_ia64 regs;
        /* the rnat register saved after flushrs */
        uint64_t             rnat;
	/* the pfs register saved after flushrs */
	uint64_t             pfs;
	/* the bspstore register saved after flushrs */
	uint64_t             bspstore;

	/* smp specific */
	uint32_t	       smp_num_cpus;
	uint32_t	       dumping_cpu;
	struct kl_pt_regs_ia64 smp_regs[KL_NR_CPUS];
	uint64_t               smp_current_task[KL_NR_CPUS];
	uint64_t               stack[KL_NR_CPUS];
} __attribute__((packed)) kl_dump_header_ia64_t;

/* The following struct is used just to calculate the size needed
 * to store per CPU info. (Make sure it is sync with the above struct)
 */
struct kl_dump_CPU_info_ia64 {
       struct kl_pt_regs_ia64          smp_regs;
       uint64_t                        smp_current_task;
       uint64_t                        stack;
} __attribute__((packed));

/* function declarations
 */
int kl_set_dumparch_ia64(void);
uint32_t dha_num_cpus_ia64(void);
kaddr_t dha_current_task_ia64(int cpuid);
int dha_cpuid_ia64(kaddr_t);
kaddr_t dha_stack_ia64(int);
kaddr_t dha_stack_ptr_ia64(int);
int kl_read_dump_header_ia64(void);

#endif /* __KL_DUMP_IA64_H */
