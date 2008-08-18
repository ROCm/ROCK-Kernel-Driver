/*
 * $Id: kl_mem.h 1157 2005-02-25 22:04:05Z tjm $
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

#ifndef __KL_MEM_H
#define __KL_MEM_H

#if 0
cpw: skip:
extern kaddr_t VMALLOC_START;
extern kaddr_t VMALLOC_END;
#endif

/*
 * Function prototypes
 */

int kl_linux_release(void);

k_error_t kl_readmem(
	kaddr_t 	/* physical address to start reading from */,
	unsigned 	/* number of bytes to read */,
	void *	 	/* pointer to buffer */);

k_error_t kl_readkmem(
	kaddr_t		/* virtual address to start reading from */,
	unsigned	/* number of bytes to read */,
	void *		/* pointer to buffer */);

int kl_virtop(
	kaddr_t 	/* virtual address to translate */,
	void *	 	/* pointer to mem_map for address translation */,
	kaddr_t *       /* pointer to physical address to return */);

k_error_t kl_get_block(
	kaddr_t 	/* virtual address */,
	unsigned 	/* size of block to read in */,
	void *		/* pointer to buffer */,
	void * 		/* pointer to mmap */);

/* Wrapper that eliminates the mmap parameter
 */
#define GET_BLOCK(a, s, b) kl_get_block(a, s, (void *)b, (void *)0)

uint64_t kl_uint(
	void *	 	/* pointer to buffer containing struct */,
	char *		/* name of struct */,
	char *		/* name of member */,
	unsigned 	/* offset */);

int64_t kl_int(
	void *	 	/* pointer to buffer containing struct */,
	char *		/* name of struct */,
	char *		/* name of member */,
	unsigned 	/* offset */);

kaddr_t kl_kaddr(
	void *	 	/* pointer to buffer containing struct */,
	char *		/* name of struct */,
	char *		/* name of member */);

/* XXX deprecated use KL_READ_PTR() instead */
kaddr_t kl_kaddr_to_ptr(
	kaddr_t 		/* Address to dereference */);

int     kl_is_valid_kaddr(
	kaddr_t 		/* Address to test */,
	void *			/* pointer to mmap */,
	int 			/* flags */);

/* REMIND:
 *    Likely not right for ia64
 */
#define KL_KADDR_IS_PHYSICAL(vaddr) ((vaddr >= KL_PAGE_OFFSET) && \
                                     (vaddr <= KL_HIGH_MEMORY))

#define PGNO_TO_PADDR(pgno) (pgno << KL_PAGE_SHIFT)

/*
 * declaration of some defaults that are used in kl_set_dumparch()
 */
int 	kl_valid_physaddr(kaddr_t);
int     kl_valid_physmem(kaddr_t, int);
kaddr_t kl_next_valid_physaddr(kaddr_t);
kaddr_t kl_fix_vaddr(kaddr_t, size_t);
int     kl_init_virtop(void);

#endif /* __KL_MEM_H */
