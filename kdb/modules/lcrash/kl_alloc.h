/*
 * $Id: kl_alloc.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libutil.
 * A library which provides auxiliary functions.
 * libutil is part of lkcdutils -- utilities for Linux kernel crash dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_ALLOC_H
#define __KL_ALLOC_H

/**
 ** Header file for kl_alloc.c module
 **
 **/

#define K_TEMP	1
#define K_PERM	2

/** function prototypes for register functions
 **/

/* Memory block allocator. Returns a pointer to an allocated block
 * of size bytes. In case of error, a NULL pointer will be returned
 * and errno will be set to indicate exactly what error occurred.
 * Note that the flag value will determine if the block allocated is
 * temporary (can be freed via a call to kl_free_temp_blks()) or
 * permenant (must be freed with a call to kl_free_block())..
 */
typedef void * (*klib_block_alloc_func) (
	int		/* size of block required */,
	int		/* flag value */,
	void *		/* return address */);

/* Memory block reallocator. Returns a pointer to a block of new_size
 * bytes. In case of error, a NULL pointer will be returned and
 * errno will be set to indicate exactly what error occurred.
 * Note that the flag value will determine if the block allocated is
 * temporary (can be free via a call to kl_free_temp_blks()) or
 * permenant.
 */
typedef void * (*klib_block_realloc_func) (
	void *		/* pointer to block to realloc */,
	int		/* size of new block required */,
	int		/* flag value */,
	void *       	/* return address */);

/* Memory block duplicator. Returns a pointer to a block that is
 * a copy of the block passed in via pointer. In case of error, a
 * NULL pointer will be returned and errno will be set to indicate
 * exactly what error occurred. Note that the flag value will
 * determine if the block allocated is temporary (will be freed
 * via a call to kl_free_temp_blks()) or permenant. Note that this
 * function is only supported when liballoc is used (there is no
 * way to tell the size of a malloced block.
 */
typedef void * (*klib_block_dup_func) (
	void *		/* pointer to block to dup */,
	int		/* flag value */,
	void *       	/* return address */);

/* Allocates a block large enough to hold a string (plus the terminating
 * NULL character).
 */
typedef void * (*klib_str_to_block_func) (
	char *		/* pointer to character string */,
	int		/* flag value */,
	void *       	/* return address */);

/* Frees blocks that were previously allocated.
 */
typedef void (*klib_block_free_func) (
	void *     /* pointer to block */);

/* alloc block wrapper function table structure
 */
typedef struct alloc_functions_s {
	int			flag;          /* Functions initialized? */
	klib_block_alloc_func	block_alloc;   /* Returns ptr to block   */
	klib_block_realloc_func	block_realloc; /* Returns ptr to new blk */
	klib_block_dup_func	block_dup;     /* Returns ptr to new blk */
	klib_str_to_block_func	str_to_block;  /* Returns ptr to new blk */
	klib_block_free_func	block_free;    /* Frees memory block     */
} alloc_functions_t;

extern alloc_functions_t alloc_functions;

/* Macros for accessing functions in alloc_functions table
 */
#define KL_BLOCK_ALLOC()	(alloc_functions.block_alloc)
#define KL_BLOCK_REALLOC()	(alloc_functions.block_realloc)
#define KL_BLOCK_DUP()		(alloc_functions.block_dup)
#define KL_STR_TO_BLOCK()	(alloc_functions.str_to_block)
#define KL_BLOCK_FREE()		(alloc_functions.block_free)

void *_kl_alloc_block(int, int, void *);
void *_kl_realloc_block(void *, int, int, void *);
void *_kl_dup_block(void *, int, void *);
void *_kl_str_to_block(char *, int, void *);
#if 0
cpw: we create a new wrappers for these:
void kl_free_block(void *);

#define kl_alloc_block(size, flags) _kl_alloc_block(size, flags, kl_get_ra())
#endif
#define kl_realloc_block(b, new_size, flags) \
	_kl_realloc_block(b, new_size, flags, kl_get_ra())
#define kl_dup_block(b, flags) _kl_dup_block(b, flags, kl_get_ra())
#define kl_str_to_block(s, flags) _kl_str_to_block(s, flags, kl_get_ra())

#endif /* __KL_ALLOC_H */
